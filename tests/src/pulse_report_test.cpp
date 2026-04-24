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

static void init_pulse_default(void)
{
	struct pulse conf = { .token = "test-token", .mfs = NULL };
	pulse_init(&conf);
}

static void init_pulse_async(void)
{
	struct pulse conf = {
		.token = "test-token",
		.mfs = NULL,
		.async_transport = true,
	};
	pulse_init(&conf);
}

static void init_pulse_with_mfs(void)
{
	struct pulse conf = {
		.token = "test-token",
		.mfs = (struct metricfs *)(uintptr_t)1,
	};
	pulse_init(&conf);
}

static void init_pulse_async_with_mfs(void)
{
	struct pulse conf = {
		.token = "test-token",
		.mfs = (struct metricfs *)(uintptr_t)1,
		.async_transport = true,
	};
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(100));

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
	CHECK_TRUE(transmitted_payload_len > 0u);
}

TEST(PulseReport, ShouldReturnIoWhenTransmitFails)
{
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(7));

	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
}

TEST(PulseReport, ShouldReturnTimeoutWhenTransmitTimesOut)
{
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-ETIMEDOUT);

	metrics_set(PulseMetric, METRICS_VALUE(7));

	CHECK_EQUAL(PULSE_STATUS_TIMEOUT, pulse_report());
}

TEST(PulseReport, ShouldReturnNotSupportedWhenTransportIsUnavailable)
{
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithNull)
{
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(NULL));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithTooLongToken)
{
	static char long_token[45];
	memset(long_token, 'x', sizeof(long_token) - 1u);
	long_token[sizeof(long_token) - 1u] = '\0';

	struct pulse conf = { .token = long_token };
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenTokenIsNullAndUpdateCalledWithNull)
{
	struct pulse conf = { .token = NULL };

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_init(&conf));
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_update_token(NULL));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_update_metricfs(NULL));
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenReportCalledBeforeInit)
{
	struct pulse conf = { .token = NULL };
	pulse_init(&conf);

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_report());
}

TEST(PulseReport, ShouldAllowLateTokenSet)
{
	struct pulse conf = { .token = NULL };
	pulse_init(&conf);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_update_token("late-token"));
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_update_metricfs((struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldClearResponseHandlerWhenReinitialized)
{
	struct pulse conf = { .token = "test-token" };

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_response_handler(response_handler, &conf));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(last_transmit_ctx != NULL && last_transmit_ctx->on_response != NULL);
	POINTERS_EQUAL(&conf, last_transmit_ctx->response_ctx);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_init(&conf));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(last_transmit_ctx->on_response == NULL);
	POINTERS_EQUAL(NULL, last_transmit_ctx->response_ctx);
}

TEST(PulseReport, ShouldCallPrepareHandlerBeforeMetricsCollection)
{
	int prepare_ctx = 2;
	struct pulse conf = { .token = "test-token" };

	pulse_init(&conf);
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_prepare_handler(prepare_handler, &prepare_ctx));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(11));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_EQUAL(1u, prepare_handler_calls);
	POINTERS_EQUAL(&prepare_ctx, last_prepare_handler_ctx);
	CHECK_EQUAL(1u, prepare_handler_order);
}

TEST(PulseReport, ShouldClearPrepareHandlerWhenReinitialized)
{
	struct pulse conf = { .token = "test-token" };

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_prepare_handler(prepare_handler, &conf));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(last_transmit_ctx != NULL && last_transmit_ctx->on_prepare != NULL);
	POINTERS_EQUAL(&conf, last_transmit_ctx->prepare_ctx);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_init(&conf));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(last_transmit_ctx->on_prepare == NULL);
	POINTERS_EQUAL(NULL, last_transmit_ctx->prepare_ctx);
}

TEST(PulseReport, ShouldNotCallPrepareHandlerAfterUnregister)
{
	int prepare_ctx = 2;
	struct pulse conf = { .token = "test-token" };

	pulse_init(&conf);
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_prepare_handler(prepare_handler, &prepare_ctx));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_set_prepare_handler(NULL, NULL));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(12));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_EQUAL(0u, prepare_handler_calls);
}

TEST(PulseReport, ShouldPassReportCtxToTransmitCallback)
{
	int user_data = 42;
	struct pulse conf = { .token = "test-token", .ctx = &user_data };
	pulse_init(&conf);

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_TRUE(last_transmit_ctx != NULL);
	POINTERS_EQUAL(&user_data, last_transmit_ctx->user_ctx);
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldSetInFlightFlagWhenAsyncTransmitReturnsEinprogress)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldNotCallTransmitTwiceOnFirstCallWhenInProgress)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldUseOriginalPayloadOnReentryWhenInProgress)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	uint8_t first_payload[1024];
	size_t first_len = transmitted_payload_len;
	memcpy(first_payload, transmitted_payload, first_len);

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldReturnOkWhenAsyncTransmitEventuallySucceeds)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldClearInFlightAfterSuccessfulAsyncCompletion)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	fake_timestamp = 1500u;
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1500u + 3600u - 1u;
	metrics_set(PulseMetric, METRICS_VALUE(6));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 1500u + 3600u;
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(6));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnIoAndClearInFlightWhenAsyncTransmitFails)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(6));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldSaveToBacklogWhenAsyncTransmitFailsWithMfs)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldPreserveMetricsRecordedDuringFlightAbort)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	metrics_set(PulseMetric, METRICS_VALUE(10));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);
	pulse_report();

	uint8_t expected_payload[128];
	metrics_set(PulseMetric, METRICS_VALUE(10));
	size_t expected_len = metrics_collect(expected_payload, sizeof(expected_payload));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", expected_len)
		.andReturnValue(0);
	pulse_report();

	LONGS_EQUAL(expected_len, transmitted_payload_len);
	MEMCMP_EQUAL(expected_payload, transmitted_payload, expected_len);
}

TEST(PulseReport, ShouldNotSaveToBacklogWhenAsyncTransmitFailsWithoutMfs)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(0u, metricfs_count(NULL));
}

TEST(PulseReport, ShouldReturnTimeoutAndClearInFlightWhenAsyncTransmitTimesOut)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-ETIMEDOUT);

	CHECK_EQUAL(PULSE_STATUS_TIMEOUT, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(6));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldNotWriteBacklogWhenAsyncTransportReturnedEinprogress)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldReportEveryCallWhenTimestampIsZero)
{
	mock().expectNCalls(2, "pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1001u;
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());
}

TEST(PulseReport, ShouldReportWhenIntervalHasElapsed)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1000u + 3600u;

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldBypassTimingGateWhenInFlight)
{
	fake_timestamp = 1000u;
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	fake_timestamp = 1001u;
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldResetPeriodicInitializedOnReinit)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();

	fake_timestamp = 1001u;
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	init_pulse_default();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldHandleTimestampRollbackGracefully)
{
	fake_timestamp = 5000u;

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 100u; // Rollback
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 100u + 3600u; // Interval elapsed after rollback
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnBacklogPendingWhileBacklogRemains)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldSavePayloadToBacklogWhenTransmitFails)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldNotSavePayloadToBacklogWhenMfsIsNull)
{
	init_pulse_default();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
}

TEST(PulseReport, ShouldTransmitBacklogPayloadOnNextCall)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();

	uint8_t saved_payload[1024];
	size_t saved_len = transmitted_payload_len;
	memcpy(saved_payload, transmitted_payload, saved_len);

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	pulse_report();
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldNotAdvanceIntervalOnBacklogReplay)
{
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	fake_timestamp = 1000u;
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	pulse_report();
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	fake_timestamp = 1001u;
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(10));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnInProgressWhenAsyncBacklogTransmitInProgress)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldDeleteBacklogEntryAfterSuccessfulAsyncBacklogTransmit)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldBypassTimingGateWhenBacklogExists)
{
	fake_timestamp = 1000u;
	init_pulse_with_mfs();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();

	fake_timestamp = 1001u;

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
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
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	init_pulse_default();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(10));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldAllowMultipleReportsWhenTimestampIsZero)
{
	mock().expectNCalls(3, "pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_cancel());
}

TEST(PulseReport, ShouldClearInFlightAfterCancel)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_cancel());
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldCallPrepareHandlerBeforeMetricsReportPrepareWhenCancelSavesBacklog)
{
	int report_ctx = 1;
	int prepare_ctx = 2;
	struct pulse conf = {
		.token = "test-token",
		.mfs = (struct metricfs *)(uintptr_t)1,
		.ctx = &report_ctx,
		.async_transport = true,
	};

	pulse_init(&conf);
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_set_prepare_handler(prepare_handler, &prepare_ctx));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	prepare_handler_calls = 0u;
	prepare_order = 0u;
	prepare_handler_order = 0u;
	last_prepare_handler_ctx = NULL;

	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_cancel());

	CHECK_EQUAL(1u, prepare_handler_calls);
	POINTERS_EQUAL(&prepare_ctx, last_prepare_handler_ctx);
	CHECK_EQUAL(1u, prepare_handler_order);
}

TEST(PulseReport, ShouldNotSaveToBacklogOnCancelWhenMfsNotAvailable)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
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
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_cancel());
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldAllowNewReportAfterCancel)
{
	init_pulse_async();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	pulse_cancel();

	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(10));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenCancelCalledAfterSuccessfulReport)
{
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_cancel());
}

TEST(PulseReport, ShouldUpdateLastReportTimeOnRollbackWhenNoBacklog)
{
	fake_timestamp = 5000u;
	mock().expectOneCall("pulse_transport_transmit").withParameter("datasize", (size_t)8).andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report(); 

	fake_timestamp = 100u;
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 100u + 3600u - 1u;
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 100u + 3600u;
	mock().expectOneCall("pulse_transport_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldNotUpdateLastReportTimeOnRollbackDuringBacklogReplay)
{
	init_pulse_with_mfs();
	fake_timestamp = 5000u;

	mock().expectOneCall("pulse_transport_transmit").withParameter("datasize", (size_t)8).andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report(); 

	fake_timestamp = 10000u;
	mock().expectOneCall("pulse_transport_transmit").withParameter("datasize", (size_t)8).andReturnValue(-EIO);
	metrics_set(PulseMetric, METRICS_VALUE(2));
	pulse_report();

	fake_timestamp = 100u;
	mock().expectOneCall("pulse_transport_transmit").withParameter("datasize", (size_t)8).andReturnValue(0);
	pulse_report();

	fake_timestamp = 100u + 3600u;
	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());
}
