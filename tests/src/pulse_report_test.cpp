#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <libmcu/metrics.h>

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
#include "metricfs_stub.h"
}

static uint8_t transmitted_payload[1024];
static size_t transmitted_payload_len;
static const struct pulse_report_ctx *last_transmit_ctx;

#define TEST_TOKEN "test-token"
#define TEST_SERIAL "device-serial"
#define TEST_VERSION "1.0.0"

static uint64_t fake_timestamp;
static unsigned int transport_cancel_calls;
static unsigned int prepare_handler_calls;
static unsigned int prepare_order;
static unsigned int prepare_handler_order;
static void *last_prepare_handler_ctx;

extern "C" void pulse_transport_cancel(void);

extern "C" uint64_t metrics_get_unix_timestamp(void)
{
	return fake_timestamp;
}

extern "C" int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	last_transmit_ctx = ctx;

	if (datasize <= sizeof(transmitted_payload)) {
		memcpy(transmitted_payload, data, datasize);
		transmitted_payload_len = datasize;
	}

	return mock().actualCall("pulse_transport_transmit")
		.withParameter("datasize", datasize)
		.returnIntValue();
}

extern "C" void pulse_transport_cancel(void)
{
	transport_cancel_calls++;
}

static void response_handler(const void *data, size_t datasize, void *ctx)
{
	(void)data;
	(void)datasize;
	(void)ctx;
}

static void prepare_handler(void *ctx)
{
	prepare_handler_calls++;
	last_prepare_handler_ctx = ctx;
	prepare_handler_order = ++prepare_order;
}

static bool payload_contains(const char *needle)
{
	const size_t needle_len = strlen(needle);

	if (needle_len == 0u || needle_len > transmitted_payload_len) {
		return false;
	}

	for (size_t i = 0; i + needle_len <= transmitted_payload_len; ++i) {
		if (memcmp(&transmitted_payload[i], needle, needle_len) == 0) {
			return true;
		}
	}

	return false;
}

struct cbor_cursor {
	const uint8_t *buf;
	size_t len;
	size_t pos;
};

static uint8_t cbor_read_byte(struct cbor_cursor *cursor)
{
	CHECK_TRUE(cursor->pos < cursor->len);
	return cursor->buf[cursor->pos++];
}

static uint64_t cbor_read_arg(struct cbor_cursor *cursor, uint8_t additional)
{
	uint64_t value = 0u;

	if (additional < 24u) {
		return additional;
	}

	if (additional == 24u) {
		return cbor_read_byte(cursor);
	}

	if (additional == 25u) {
		value = (uint64_t)cbor_read_byte(cursor) << 8;
		value |= cbor_read_byte(cursor);
		return value;
	}

	if (additional == 26u) {
		for (unsigned int i = 0u; i < 4u; ++i) {
			value = (value << 8) | cbor_read_byte(cursor);
		}
		return value;
	}

	FAIL("Unsupported CBOR additional info");
	return 0u;
}

static uint64_t cbor_read_type(struct cbor_cursor *cursor, uint8_t major)
{
	const uint8_t initial = cbor_read_byte(cursor);

	UNSIGNED_LONGS_EQUAL(major, (uint8_t)(initial >> 5));

	return cbor_read_arg(cursor, (uint8_t)(initial & 0x1fu));
}

static uint64_t cbor_read_uint(struct cbor_cursor *cursor)
{
	return cbor_read_type(cursor, 0u);
}

static uint64_t cbor_read_map_size(struct cbor_cursor *cursor)
{
	return cbor_read_type(cursor, 5u);
}

static uint64_t cbor_read_array_size(struct cbor_cursor *cursor)
{
	return cbor_read_type(cursor, 4u);
}

static uint8_t cbor_peek_major(const struct cbor_cursor *cursor)
{
	CHECK_TRUE(cursor->pos < cursor->len);
	return (uint8_t)(cursor->buf[cursor->pos] >> 5);
}

static void cbor_read_text_equals(struct cbor_cursor *cursor, const char *expected)
{
	const size_t expected_len = strlen(expected);
	const size_t actual_len = (size_t)cbor_read_type(cursor, 3u);

	UNSIGNED_LONGS_EQUAL(expected_len, actual_len);
	CHECK_TRUE(cursor->pos + actual_len <= cursor->len);
	MEMCMP_EQUAL(expected, &cursor->buf[cursor->pos], actual_len);
	cursor->pos += actual_len;
}

static const uint8_t *cbor_read_bstr(struct cbor_cursor *cursor, size_t *len)
{
	*len = (size_t)cbor_read_type(cursor, 2u);
	CHECK_TRUE(cursor->pos + *len <= cursor->len);

	const uint8_t *ptr = &cursor->buf[cursor->pos];
	cursor->pos += *len;

	return ptr;
}

static void assert_metric_payload_matches(const uint8_t *payload, size_t payload_len,
		int32_t expected_value)
{
	struct cbor_cursor cursor = { payload, payload_len, 0u };

	UNSIGNED_LONGS_EQUAL(1u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));

	if (cbor_peek_major(&cursor) == 4u) {
		UNSIGNED_LONGS_EQUAL(5u, cbor_read_array_size(&cursor));
		(void)cbor_read_uint(&cursor);
		(void)cbor_read_uint(&cursor);

		if (cbor_peek_major(&cursor) == 0u) {
			(void)cbor_read_uint(&cursor);
		} else {
			const uint8_t initial = cbor_read_byte(&cursor);
			UNSIGNED_LONGS_EQUAL(1u, (uint8_t)(initial >> 5));
			(void)cbor_read_arg(&cursor, (uint8_t)(initial & 0x1fu));
		}

		if (cbor_peek_major(&cursor) == 0u) {
			(void)cbor_read_uint(&cursor);
		} else {
			const uint8_t initial = cbor_read_byte(&cursor);
			UNSIGNED_LONGS_EQUAL(1u, (uint8_t)(initial >> 5));
			(void)cbor_read_arg(&cursor, (uint8_t)(initial & 0x1fu));
		}
	}

	if (cbor_peek_major(&cursor) == 0u) {
		LONGS_EQUAL(expected_value, (int32_t)cbor_read_uint(&cursor));
	} else {
		const uint8_t initial = cbor_read_byte(&cursor);
		UNSIGNED_LONGS_EQUAL(1u, (uint8_t)(initial >> 5));
		LONGS_EQUAL(expected_value,
				-(int32_t)cbor_read_arg(&cursor, (uint8_t)(initial & 0x1fu)) - 1);
	}

	UNSIGNED_LONGS_EQUAL(payload_len, cursor.pos);
}

static void assert_envelope_payload_with_window(const uint8_t *payload, size_t payload_len,
		uint64_t expected_timestamp, uint64_t expected_window_start,
		uint64_t expected_window_end, bool has_metrics,
		int32_t expected_metric_value, bool has_reason, uint8_t expected_reason)
{
	struct cbor_cursor cursor = { payload, payload_len, 0u };
	const size_t expected_report_entries = (expected_timestamp != 0u ? 3u : 0u)
		+ (has_reason ? 1u : 0u);
	size_t metrics_len = 0u;
	const uint8_t *metrics_payload;

	UNSIGNED_LONGS_EQUAL(has_metrics ? 4u : 3u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	cbor_read_text_equals(&cursor, TEST_SERIAL);
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	cbor_read_text_equals(&cursor, TEST_VERSION);
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(expected_report_entries, cbor_read_map_size(&cursor));

	if (expected_timestamp != 0u) {
		UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
		UNSIGNED_LONGS_EQUAL(expected_timestamp, cbor_read_uint(&cursor));
		UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
		UNSIGNED_LONGS_EQUAL(expected_window_start, cbor_read_uint(&cursor));
		UNSIGNED_LONGS_EQUAL(2u, cbor_read_uint(&cursor));
		UNSIGNED_LONGS_EQUAL(expected_window_end, cbor_read_uint(&cursor));
	}

	if (has_reason) {
		UNSIGNED_LONGS_EQUAL(3u, cbor_read_uint(&cursor));
		UNSIGNED_LONGS_EQUAL(expected_reason, cbor_read_uint(&cursor));
	}

	if (has_metrics) {
		UNSIGNED_LONGS_EQUAL(3u, cbor_read_uint(&cursor));
		metrics_payload = cbor_read_bstr(&cursor, &metrics_len);
		assert_metric_payload_matches(metrics_payload, metrics_len, expected_metric_value);
	}

	UNSIGNED_LONGS_EQUAL(payload_len, cursor.pos);
}

static void assert_envelope_payload(const uint8_t *payload, size_t payload_len,
		uint64_t expected_timestamp, bool has_metrics, int32_t expected_metric_value,
		bool has_reason, uint8_t expected_reason)
{
	assert_envelope_payload_with_window(payload, payload_len,
			expected_timestamp, expected_timestamp, expected_timestamp,
			has_metrics, expected_metric_value, has_reason, expected_reason);
}

static struct pulse make_pulse_conf(void)
{
	struct pulse conf = {
		.token = TEST_TOKEN,
		.serial_number = TEST_SERIAL,
		.software_version = TEST_VERSION,
	};

	return conf;
}

static void init_pulse_default(void)
{
	struct pulse conf = make_pulse_conf();
	pulse_init(&conf);
}

static void init_pulse_async(void)
{
	struct pulse conf = make_pulse_conf();
	conf.async_transport = true;
	pulse_init(&conf);
}

static void init_pulse_with_mfs(void)
{
	struct pulse conf = make_pulse_conf();
	conf.mfs = (struct metricfs *)(uintptr_t)1;
	pulse_init(&conf);
}

static void init_pulse_async_with_mfs(void)
{
	struct pulse conf = make_pulse_conf();
	conf.mfs = (struct metricfs *)(uintptr_t)1;
	conf.async_transport = true;
	pulse_init(&conf);
}

TEST_GROUP(PulseReport)
{
	void setup()
	{
		memset(transmitted_payload, 0, sizeof(transmitted_payload));
		transmitted_payload_len = 0u;
		last_transmit_ctx = NULL;
		fake_timestamp = 0u;
		transport_cancel_calls = 0u;
		prepare_handler_calls = 0u;
		prepare_order = 0u;
		prepare_handler_order = 0u;
		last_prepare_handler_ctx = NULL;
		metricfs_stub_reset();
		metrics_reset();

		init_pulse_default();
	}

	void teardown()
	{
		init_pulse_default();
		mock().checkExpectations();
		mock().clear();
	}
};

TEST(PulseReport, ShouldTransmitCollectedMetricsWhenReportCalled)
{
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(100));

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
	assert_envelope_payload(transmitted_payload, transmitted_payload_len,
			0u, true, 100, false, 0u);
}

TEST(PulseReport, ShouldWrapPayloadInCanonicalEnvelope)
{
	fake_timestamp = 1234u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(100));

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
	assert_envelope_payload(transmitted_payload, transmitted_payload_len,
			1234u, true, 100, false, 0u);
}

TEST(PulseReport, ShouldPreferExplicitMetadataFromConfig)
{
	struct pulse conf = make_pulse_conf();
	conf.serial_number = "cfg-serial";
	conf.software_version = "cfg-version";
	pulse_init(&conf);

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
	CHECK_TRUE(payload_contains("cfg-serial"));
	CHECK_TRUE(payload_contains("cfg-version"));
}

TEST(PulseReport, ShouldReturnIoWhenTransmitFails)
{
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(7));

	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
}

TEST(PulseReport, ShouldReturnTimeoutWhenTransmitTimesOut)
{
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-ETIMEDOUT);

	metrics_set(PulseMetric, METRICS_VALUE(7));

	CHECK_EQUAL(PULSE_STATUS_TIMEOUT, pulse_report());
}

TEST(PulseReport, ShouldReturnNotSupportedWhenTransportIsUnavailable)
{
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-ENOSYS);

	metrics_set(PulseMetric, METRICS_VALUE(7));

	CHECK_EQUAL(PULSE_STATUS_NOT_SUPPORTED, pulse_report());
}

TEST(PulseReport, ShouldTransmitHeartbeatWhenNoMetricsSet)
{
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
	assert_envelope_payload(transmitted_payload, transmitted_payload_len,
			0u, false, 0, false, 0u);
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithNull)
{
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(NULL));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithTooLongToken)
{
	static char long_token[45];
	struct pulse conf = make_pulse_conf();
	memset(long_token, 'x', sizeof(long_token) - 1u);
	long_token[sizeof(long_token) - 1u] = '\0';

	conf.token = long_token;
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithoutToken)
{
	struct pulse conf = make_pulse_conf();
	conf.token = NULL;

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithoutSerialNumber)
{
	struct pulse conf = make_pulse_conf();
	conf.serial_number = NULL;

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithoutSoftwareVersion)
{
	struct pulse conf = make_pulse_conf();
	conf.software_version = NULL;

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithEmptySerialNumber)
{
	struct pulse conf = make_pulse_conf();
	conf.serial_number = "";

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithEmptySoftwareVersion)
{
	struct pulse conf = make_pulse_conf();
	conf.software_version = "";

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
}

TEST(PulseReport, ShouldKeepPreviousInitWhenRejectedInitConfigProvided)
{
	struct pulse conf = make_pulse_conf();
	conf.token = NULL;

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenTokenUpdateCalledWithNull)
{
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_update_token(NULL));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_update_metricfs(NULL));
}

TEST(PulseReport, ShouldAllowTokenRotationAfterInit)
{
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_update_token("rotated-token"));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_TRUE(last_transmit_ctx != NULL);
	STRCMP_EQUAL("rotated-token", last_transmit_ctx->conf.token);
}

TEST(PulseReport, ShouldClearResponseHandlerWhenReinitialized)
{
	struct pulse conf = make_pulse_conf();

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_response_handler(response_handler, &conf));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(last_transmit_ctx != NULL && last_transmit_ctx->on_response != NULL);
	POINTERS_EQUAL(&conf, last_transmit_ctx->response_ctx);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_init(&conf));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(last_transmit_ctx->on_response == NULL);
	POINTERS_EQUAL(NULL, last_transmit_ctx->response_ctx);
}

TEST(PulseReport, ShouldCallPrepareHandlerBeforeMetricsCollection)
{
	int prepare_ctx = 2;
	struct pulse conf = make_pulse_conf();

	pulse_init(&conf);
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_prepare_handler(prepare_handler, &prepare_ctx));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(11));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_EQUAL(1u, prepare_handler_calls);
	POINTERS_EQUAL(&prepare_ctx, last_prepare_handler_ctx);
	CHECK_EQUAL(1u, prepare_handler_order);
}

TEST(PulseReport, ShouldClearPrepareHandlerWhenReinitialized)
{
	struct pulse conf = make_pulse_conf();

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_prepare_handler(prepare_handler, &conf));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(last_transmit_ctx != NULL && last_transmit_ctx->on_prepare != NULL);
	POINTERS_EQUAL(&conf, last_transmit_ctx->prepare_ctx);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_init(&conf));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(last_transmit_ctx->on_prepare == NULL);
	POINTERS_EQUAL(NULL, last_transmit_ctx->prepare_ctx);
}

TEST(PulseReport, ShouldNotCallPrepareHandlerAfterUnregister)
{
	int prepare_ctx = 2;
	struct pulse conf = make_pulse_conf();

	pulse_init(&conf);
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_prepare_handler(prepare_handler, &prepare_ctx));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_set_prepare_handler(NULL, NULL));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(12));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_EQUAL(0u, prepare_handler_calls);
}

TEST(PulseReport, ShouldPassReportCtxToTransmitCallback)
{
	struct pulse conf = make_pulse_conf();
	pulse_init(&conf);

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_TRUE(last_transmit_ctx != NULL);
}

TEST(PulseReport, ShouldReturnNonNullForAllStatusStringConversions)
{
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_OK) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_INVALID_ARGUMENT) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_BAD_FORMAT) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_OVERFLOW) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_IO) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_TIMEOUT) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_NOT_SUPPORTED) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_TOO_SOON) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_EMPTY) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_BACKLOG_PENDING) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_BACKLOG_OVERFLOW) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_NO_MEMORY) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_IN_PROGRESS) != NULL);
	CHECK_TRUE(pulse_stringify_status((pulse_status_t)-99) != NULL);
}

TEST(PulseReport, ShouldReturnInProgressWhenAsyncTransportReturnedEinprogress)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldSetInFlightFlagWhenAsyncTransmitReturnsEinprogress)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldNotCallTransmitTwiceOnFirstCallWhenInProgress)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldUseOriginalPayloadOnReentryWhenInProgress)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	uint8_t first_payload[1024];
	size_t first_len = transmitted_payload_len;
	memcpy(first_payload, transmitted_payload, first_len);

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(99));
	pulse_report();

	LONGS_EQUAL(first_len, transmitted_payload_len);
	MEMCMP_EQUAL(first_payload, transmitted_payload, first_len);
}

TEST(PulseReport, ShouldCallTransmitAgainOnReentryWhenInProgress)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldReturnOkWhenAsyncTransmitEventuallySucceeds)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldClearInFlightAfterSuccessfulAsyncCompletion)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldUpdateLastReportTimeAfterAsyncCompletion)
{
	fake_timestamp = 1000u;
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	fake_timestamp = 1500u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1500u + 3600u - 1u;
	metrics_set(PulseMetric, METRICS_VALUE(6));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 1500u + 3600u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(6));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnIoAndClearInFlightWhenAsyncTransmitFails)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(6));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldSaveToBacklogWhenAsyncTransmitFailsWithMfs)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
	assert_envelope_payload((const uint8_t *)metricfs_stub_data(), metricfs_stub_size(),
			0u, true, 5, true, 1u);
}

TEST(PulseReport, ShouldPreserveMetricsRecordedDuringFlightAbort)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	metrics_set(PulseMetric, METRICS_VALUE(10));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);
	pulse_report();

	uint8_t expected_payload[128];
	size_t expected_len = metricfs_stub_size();
	memcpy(expected_payload, metricfs_stub_data(), expected_len);

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	pulse_report();

	LONGS_EQUAL(expected_len, transmitted_payload_len);
	MEMCMP_EQUAL(expected_payload, transmitted_payload, expected_len);
}

TEST(PulseReport, ShouldNotSaveToBacklogWhenAsyncTransmitFailsWithoutMfs)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(0u, metricfs_count(NULL));
}

TEST(PulseReport, ShouldReturnTimeoutAndClearInFlightWhenAsyncTransmitTimesOut)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-ETIMEDOUT);

	CHECK_EQUAL(PULSE_STATUS_TIMEOUT, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(6));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldNotWriteBacklogWhenAsyncTransportReturnedEinprogress)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldReportEveryCallWhenTimestampIsZero)
{
	mock().expectNCalls(2, "pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReportOnFirstCallWhenIntervalNotYetInitialized)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1001u;
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());
}

TEST(PulseReport, ShouldReturnTooSoonWhenIntervalNotElapsed)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1001u;
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());
}

TEST(PulseReport, ShouldReturnZeroSecUntilNextReportBeforeFirstReport)
{
	fake_timestamp = 1000u;

	UNSIGNED_LONGS_EQUAL(0u, pulse_get_sec_until_next_report());
}

TEST(PulseReport, ShouldReturnRemainingSecUntilNextReport)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1001u;

	UNSIGNED_LONGS_EQUAL(3600u - 1u,
			pulse_get_sec_until_next_report());
}

TEST(PulseReport, ShouldReturnZeroSecUntilNextReportWhenIntervalElapsed)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1000u + 3600u;

	UNSIGNED_LONGS_EQUAL(0u, pulse_get_sec_until_next_report());
}

TEST(PulseReport, ShouldReturnZeroSecUntilNextReportAfterTimestampRollback)
{
	fake_timestamp = 5000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 100u;
	UNSIGNED_LONGS_EQUAL(0u, pulse_get_sec_until_next_report());

	fake_timestamp = 101u;
	UNSIGNED_LONGS_EQUAL(0u,
			pulse_get_sec_until_next_report());
}

TEST(PulseReport, ShouldNotRestartReportWindowWhenQuerySeesTimestampRollback)
{
	fake_timestamp = 5000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 100u;
	UNSIGNED_LONGS_EQUAL(0u, pulse_get_sec_until_next_report());

	fake_timestamp = 100u + 3600u;
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 100u + 3600u + 3600u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnZeroSecUntilNextReportWhenTimestampIsZero)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 0u;

	UNSIGNED_LONGS_EQUAL(0u, pulse_get_sec_until_next_report());
}

TEST(PulseReport, ShouldReturnZeroSecUntilNextReportWhenInFlight)
{
	fake_timestamp = 1000u;
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	UNSIGNED_LONGS_EQUAL(0u, pulse_get_sec_until_next_report());
}

TEST(PulseReport, ShouldReturnZeroSecUntilNextReportWhenBacklogPending)
{
	fake_timestamp = 1000u;
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	metricfs_stub_prime(transmitted_payload, transmitted_payload_len, 1u);
	fake_timestamp = 1001u;

	UNSIGNED_LONGS_EQUAL(0u, pulse_get_sec_until_next_report());
}

TEST(PulseReport, ShouldReportWhenIntervalHasElapsed)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1000u + 3600u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldBypassTimingGateWhenInFlight)
{
	fake_timestamp = 1000u;
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	fake_timestamp = 1001u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldResetPeriodicInitializedOnReinit)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();

	fake_timestamp = 1001u;
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	init_pulse_default();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldHandleTimestampRollbackGracefully)
{
	fake_timestamp = 5000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 100u;

	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());
}

TEST(PulseReport, ShouldReportWhenIntervalElapsedAfterRollback)
{
	fake_timestamp = 5000u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 100u; // Rollback
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 100u + 3600u; // Interval elapsed after rollback
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnIoWhenTransmitFailsAndSavesToBacklog)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnBacklogPendingWhenMoreEntriesRemainAfterSuccessfulTransmit)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();

	metricfs_stub_prime(metricfs_stub_data(), metricfs_stub_size(), 2u);

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldSavePayloadToBacklogWhenTransmitFails)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
	assert_envelope_payload((const uint8_t *)metricfs_stub_data(), metricfs_stub_size(),
			0u, true, 9, true, 1u);
}

TEST(PulseReport, ShouldNotSavePayloadToBacklogWhenMfsIsNull)
{
	init_pulse_default();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
}

TEST(PulseReport, ShouldTransmitBacklogPayloadOnNextCall)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();

	uint8_t saved_payload[1024];
	size_t saved_len = metricfs_stub_size();
	memcpy(saved_payload, metricfs_stub_data(), saved_len);

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	pulse_report();

	LONGS_EQUAL(saved_len, transmitted_payload_len);
	MEMCMP_EQUAL(saved_payload, transmitted_payload, saved_len);
}

TEST(PulseReport, ShouldReturnBacklogOverflowWhenBacklogExceedsDerivedPayloadBound)
{
	uint8_t backlog_payload[300];
	memset(backlog_payload, 0x5A, sizeof(backlog_payload));

	metricfs_stub_prime(backlog_payload, sizeof(backlog_payload), 1u);
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_update_metricfs((struct metricfs *)(uintptr_t)1));

	CHECK_EQUAL(PULSE_STATUS_BACKLOG_OVERFLOW, pulse_report());
	CHECK_EQUAL(0u, transmitted_payload_len);
}

TEST(PulseReport, ShouldDeleteBacklogEntryAfterSuccessfulTransmit)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	pulse_report();
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldNotAdvanceIntervalOnBacklogReplay)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	fake_timestamp = 1000u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	pulse_report();
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	fake_timestamp = 1001u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(10));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnInProgressWhenAsyncBacklogTransmitInProgress)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldDeleteBacklogEntryAfterSuccessfulAsyncBacklogTransmit)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldNotRewriteBacklogWhenAsyncBacklogTransmitFails)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldBypassTimingGateWhenBacklogExists)
{
	fake_timestamp = 1000u;
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();

	fake_timestamp = 1001u;

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldSaveElapsedLiveMetricsToBacklogBeforeReplayingOldestBacklog)
{
	fake_timestamp = 1000u;
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1000u + 3600u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());

	uint8_t oldest_payload[1024];
	size_t oldest_len = metricfs_stub_size();
	memcpy(oldest_payload, metricfs_stub_data(), oldest_len);

	fake_timestamp = 1000u + 3600u + 3600u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_report());

	LONGS_EQUAL(oldest_len, transmitted_payload_len);
	MEMCMP_EQUAL(oldest_payload, transmitted_payload, oldest_len);
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
	assert_envelope_payload_with_window((const uint8_t *)metricfs_stub_data(),
			metricfs_stub_size(), 1000u + 3600u + 3600u, 1000u,
			1000u + 3600u + 3600u, true, 3, false, 0u);
}

TEST(PulseReport, ShouldReturnIoWhenBacklogPeekFails)
{
	static const uint8_t backlog_payload[] = { 0xA1, 0x01, 0x01 };

	init_pulse_with_mfs();
	metricfs_stub_prime(backlog_payload, sizeof(backlog_payload), 1u);
	metricfs_stub_set_peek_first_error(-EIO);

	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
}

TEST(PulseReport, ShouldClearInFlightOnReinit)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	init_pulse_async();
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_cancel());
}

TEST(PulseReport, ShouldCancelTransportWhenReinitWhileInFlight)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	init_pulse_async();
	CHECK_EQUAL(1u, transport_cancel_calls);
}

TEST(PulseReport, ShouldBeAbleToReportAfterReinitWhileInFlight)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	init_pulse_default();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(10));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldAllowMultipleReportsWhenTimestampIsZero)
{
	mock().expectNCalls(3, "pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldCallTransportCancelWhenPulseCancelCalled)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	pulse_cancel();
	CHECK_EQUAL(1u, transport_cancel_calls);
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenCancelCalledWhileNotInFlight)
{
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_cancel());
}

TEST(PulseReport, ShouldReturnOkWhenCancelCalledWhileInFlight)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_cancel());
}

TEST(PulseReport, ShouldClearInFlightAfterCancel)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	pulse_cancel();

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_cancel());
}

TEST(PulseReport, ShouldSaveToBacklogOnCancelWhenMfsAvailableAndNotFromStore)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_cancel());
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
	assert_envelope_payload((const uint8_t *)metricfs_stub_data(), metricfs_stub_size(),
			0u, true, 5, true, 2u);
}

TEST(PulseReport, ShouldNotCallPrepareHandlerAgainWhenCancelSavesBacklog)
{
	int prepare_ctx = 2;
	struct pulse conf = make_pulse_conf();
	conf.mfs = (struct metricfs *)(uintptr_t)1;
	conf.async_transport = true;

	pulse_init(&conf);
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_prepare_handler(prepare_handler, &prepare_ctx));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	prepare_handler_calls = 0u;
	prepare_order = 0u;
	prepare_handler_order = 0u;
	last_prepare_handler_ctx = NULL;

	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_cancel());

	CHECK_EQUAL(0u, prepare_handler_calls);
}

TEST(PulseReport, ShouldNotSaveToBacklogOnCancelWhenMfsNotAvailable)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_cancel());
	CHECK_EQUAL(0u, metricfs_count(NULL));
}

TEST(PulseReport, ShouldNotSaveToBacklogOnCancelWhenFlightFromStore)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_cancel());
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldAllowNewReportAfterCancel)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	pulse_cancel();

	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(10));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenCancelCalledAfterSuccessfulReport)
{
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_cancel());
}

TEST(PulseReport, ShouldUpdateLastReportTimeOnRollbackWhenNoBacklog)
{
	fake_timestamp = 5000u;
	mock().expectOneCall("pulse_transport_transmit").ignoreOtherParameters().andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report(); 

	fake_timestamp = 100u;
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 100u + 3600u - 1u;
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 100u + 3600u;
	mock().expectOneCall("pulse_transport_transmit")
		.ignoreOtherParameters()
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldNotUpdateLastReportTimeOnRollbackDuringBacklogReplay)
{
	init_pulse_with_mfs();
	fake_timestamp = 5000u;

	mock().expectOneCall("pulse_transport_transmit").ignoreOtherParameters().andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report(); 

	fake_timestamp = 10000u;
	mock().expectOneCall("pulse_transport_transmit").ignoreOtherParameters().andReturnValue(-EIO);
	metrics_set(PulseMetric, METRICS_VALUE(2));
	pulse_report();

	fake_timestamp = 100u;
	mock().expectOneCall("pulse_transport_transmit").ignoreOtherParameters().andReturnValue(0);
	pulse_report();

	fake_timestamp = 100u + 3600u;
	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());
}
