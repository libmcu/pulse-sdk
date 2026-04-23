#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <libmcu/metrics.h>
#include <libmcu/metrics_reporter.h>

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
#include "metricfs_stub.h"

void pulse_transport_cancel(void);
}

static uint8_t transmitted_payload[1024];
static size_t transmitted_payload_len;
static void *last_transmit_ctx;

static uint64_t fake_timestamp;
static unsigned int transport_cancel_calls;

extern "C" uint64_t metrics_get_unix_timestamp(void)
{
	return fake_timestamp;
}

extern "C" int metrics_report_transmit(const void *data, size_t datasize,
		void *ctx)
{
	last_transmit_ctx = ctx;

	if (datasize <= sizeof(transmitted_payload)) {
		memcpy(transmitted_payload, data, datasize);
		transmitted_payload_len = datasize;
	}

	return mock().actualCall("metrics_report_transmit")
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
		metricfs_stub_reset();
		metrics_reset();
		metrics_report_periodic_reset();

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
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(100));

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
	CHECK_TRUE(transmitted_payload_len > 0u);
}

TEST(PulseReport, ShouldReturnIoWhenTransmitFails)
{
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(7));

	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
}

TEST(PulseReport, ShouldReturnTimeoutWhenTransmitTimesOut)
{
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-ETIMEDOUT);

	metrics_set(PulseMetric, METRICS_VALUE(7));

	CHECK_EQUAL(PULSE_STATUS_TIMEOUT, pulse_report());
}

TEST(PulseReport, ShouldReturnNotSupportedWhenTransportIsUnavailable)
{
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-ENOSYS);

	metrics_set(PulseMetric, METRICS_VALUE(7));

	CHECK_EQUAL(PULSE_STATUS_NOT_SUPPORTED, pulse_report());
}

TEST(PulseReport, ShouldReturnEmptyWhenNoMetricsCollected)
{
	CHECK_EQUAL(PULSE_STATUS_EMPTY, pulse_report());
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

	mock().expectOneCall("metrics_report_transmit")
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
	CHECK_TRUE(pulse_get_report_ctx()->on_response != NULL);
	POINTERS_EQUAL(&conf, pulse_get_report_ctx()->response_ctx);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_init(&conf));
	CHECK_TRUE(pulse_get_report_ctx()->on_response == NULL);
	POINTERS_EQUAL(NULL, pulse_get_report_ctx()->response_ctx);
}

TEST(PulseReport, ShouldPassUserCtxDirectlyToTransmitCallback)
{
	int user_data = 42;
	struct pulse conf = { .token = "test-token", .ctx = &user_data };
	pulse_init(&conf);

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	POINTERS_EQUAL(&user_data, last_transmit_ctx);
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldSetInFlightFlagWhenAsyncTransmitReturnsEinprogress)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	CHECK_TRUE(pulse_get_report_ctx()->in_flight);
	CHECK_TRUE(pulse_get_report_ctx()->flight_buf != NULL);
	CHECK_TRUE(pulse_get_report_ctx()->flight_len > 0u);
}

TEST(PulseReport, ShouldNotCallTransmitTwiceOnFirstCallWhenInProgress)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldUseOriginalPayloadOnReentryWhenInProgress)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	uint8_t first_payload[1024];
	size_t first_len = transmitted_payload_len;
	memcpy(first_payload, transmitted_payload, first_len);

	mock().expectOneCall("metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldReturnOkWhenAsyncTransmitEventuallySucceeds)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldClearInFlightAfterSuccessfulAsyncCompletion)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	pulse_report();

	CHECK_FALSE(pulse_get_report_ctx()->in_flight);
	POINTERS_EQUAL(NULL, pulse_get_report_ctx()->flight_buf);
}

TEST(PulseReport, ShouldUpdateLastReportTimeAfterAsyncCompletion)
{
	fake_timestamp = 1000u;
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	fake_timestamp = 1500u;
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	pulse_report();

	LONGS_EQUAL(1500u, pulse_get_report_ctx()->last_report_time);
	CHECK_TRUE(pulse_get_report_ctx()->periodic_initialized);
}

TEST(PulseReport, ShouldReturnIoAndClearInFlightWhenAsyncTransmitFails)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
	CHECK_FALSE(pulse_get_report_ctx()->in_flight);
	POINTERS_EQUAL(NULL, pulse_get_report_ctx()->flight_buf);
}

TEST(PulseReport, ShouldSaveToBacklogWhenAsyncTransmitFailsWithMfs)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldPreserveMetricsRecordedDuringFlightAbort)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	metrics_set(PulseMetric, METRICS_VALUE(10));

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);
	pulse_report();

	uint8_t expected_payload[128];
	metrics_set(PulseMetric, METRICS_VALUE(10));
	size_t expected_len = metrics_collect(expected_payload, sizeof(expected_payload));

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", expected_len)
		.andReturnValue(0);
	pulse_report();

	LONGS_EQUAL(expected_len, transmitted_payload_len);
	MEMCMP_EQUAL(expected_payload, transmitted_payload, expected_len);
}

TEST(PulseReport, ShouldNotSaveToBacklogWhenAsyncTransmitFailsWithoutMfs)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(0u, metricfs_count(NULL));
}

TEST(PulseReport, ShouldReturnTimeoutAndClearInFlightWhenAsyncTransmitTimesOut)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-ETIMEDOUT);

	CHECK_EQUAL(PULSE_STATUS_TIMEOUT, pulse_report());
	CHECK_FALSE(pulse_get_report_ctx()->in_flight);
}

TEST(PulseReport, ShouldNotWriteBacklogWhenAsyncTransportReturnedEinprogress)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldReportEveryCallWhenTimestampIsZero)
{
	mock().expectNCalls(2, "metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
	CHECK_TRUE(pulse_get_report_ctx()->periodic_initialized);
}

TEST(PulseReport, ShouldReturnTooSoonWhenIntervalNotElapsed)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 1000u + 3600u;

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldBypassTimingGateWhenInFlight)
{
	fake_timestamp = 1000u;
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());

	fake_timestamp = 1001u;
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
}

TEST(PulseReport, ShouldResetPeriodicInitializedOnReinit)
{
	fake_timestamp = 1000u;

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();
	CHECK_TRUE(pulse_get_report_ctx()->periodic_initialized);

	init_pulse_default();
	CHECK_FALSE(pulse_get_report_ctx()->periodic_initialized);
}

TEST(PulseReport, ShouldHandleTimestampRollbackGracefully)
{
	fake_timestamp = 5000u;

	mock().expectOneCall("metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	fake_timestamp = 100u; // Rollback
	metrics_set(PulseMetric, METRICS_VALUE(2));
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());

	fake_timestamp = 100u + 3600u; // Interval elapsed after rollback
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnBacklogPendingWhileBacklogRemains)
{
	init_pulse_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_report());

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldSavePayloadToBacklogWhenTransmitFails)
{
	init_pulse_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldNotSavePayloadToBacklogWhenMfsIsNull)
{
	init_pulse_default();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	CHECK_EQUAL(PULSE_STATUS_IO, pulse_report());
}

TEST(PulseReport, ShouldTransmitBacklogPayloadOnNextCall)
{
	init_pulse_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();

	uint8_t saved_payload[1024];
	size_t saved_len = transmitted_payload_len;
	memcpy(saved_payload, transmitted_payload, saved_len);

	mock().expectOneCall("metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	pulse_report();
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldNotAdvanceIntervalOnBacklogReplay)
{
	init_pulse_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(9));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	fake_timestamp = 1000u;
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	pulse_report();
	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	CHECK_EQUAL(0u, pulse_get_report_ctx()->last_report_time);
	CHECK_FALSE(pulse_get_report_ctx()->periodic_initialized);
}

TEST(PulseReport, ShouldReturnInProgressWhenAsyncBacklogTransmitInProgress)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	CHECK_EQUAL(PULSE_STATUS_IN_PROGRESS, pulse_report());
	CHECK_TRUE(pulse_get_report_ctx()->in_flight);
	CHECK_TRUE(pulse_get_report_ctx()->flight_from_store);
}

TEST(PulseReport, ShouldDeleteBacklogEntryAfterSuccessfulAsyncBacklogTransmit)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());

	CHECK_EQUAL(0u, metricfs_count((const struct metricfs *)(uintptr_t)1));
	CHECK_FALSE(pulse_get_report_ctx()->in_flight);
}

TEST(PulseReport, ShouldNotRewriteBacklogWhenAsyncBacklogTransmitFails)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);
	pulse_report();

	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldBypassTimingGateWhenBacklogExists)
{
	fake_timestamp = 1000u;
	init_pulse_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();

	fake_timestamp = 1001u;

	mock().expectOneCall("metrics_report_transmit")
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
	CHECK_FALSE(pulse_get_report_ctx()->in_flight);
	POINTERS_EQUAL(NULL, pulse_get_report_ctx()->flight_buf);
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldClearInFlightOnReinit)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_TRUE(pulse_get_report_ctx()->in_flight);

	init_pulse_async();
	CHECK_FALSE(pulse_get_report_ctx()->in_flight);
	POINTERS_EQUAL(NULL, pulse_get_report_ctx()->flight_buf);
}

TEST(PulseReport, ShouldCancelTransportWhenReinitWhileInFlight)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	init_pulse_default();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(10));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldAllowMultipleReportsWhenTimestampIsZero)
{
	mock().expectNCalls(3, "metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_cancel());
}

TEST(PulseReport, ShouldClearInFlightAfterCancel)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	pulse_cancel();

	CHECK_FALSE(pulse_get_report_ctx()->in_flight);
	POINTERS_EQUAL(NULL, pulse_get_report_ctx()->flight_buf);
}

TEST(PulseReport, ShouldSaveToBacklogOnCancelWhenMfsAvailableAndNotFromStore)
{
	init_pulse_async_with_mfs();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_BACKLOG_PENDING, pulse_cancel());
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldNotSaveToBacklogOnCancelWhenMfsNotAvailable)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
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

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EIO);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_cancel());
	CHECK_EQUAL(1u, metricfs_count((const struct metricfs *)(uintptr_t)1));
}

TEST(PulseReport, ShouldAllowNewReportAfterCancel)
{
	init_pulse_async();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(-EINPROGRESS);

	metrics_set(PulseMetric, METRICS_VALUE(5));
	pulse_report();
	pulse_cancel();

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(10));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenCancelCalledAfterSuccessfulReport)
{
	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report();

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_cancel());
}

TEST(PulseReport, ShouldUpdateLastReportTimeOnRollbackWhenNoBacklog)
{
	fake_timestamp = 5000u;
	mock().expectOneCall("metrics_report_transmit").withParameter("datasize", (size_t)8).andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report(); 

	fake_timestamp = 100u; 
	CHECK_EQUAL(PULSE_STATUS_TOO_SOON, pulse_report());
	LONGS_EQUAL(100u, pulse_get_report_ctx()->last_report_time);
}

TEST(PulseReport, ShouldNotUpdateLastReportTimeOnRollbackDuringBacklogReplay)
{
	init_pulse_with_mfs();
	fake_timestamp = 5000u;

	mock().expectOneCall("metrics_report_transmit").withParameter("datasize", (size_t)8).andReturnValue(0);
	metrics_set(PulseMetric, METRICS_VALUE(1));
	pulse_report(); 

	fake_timestamp = 10000u;
	mock().expectOneCall("metrics_report_transmit").withParameter("datasize", (size_t)8).andReturnValue(-EIO);
	metrics_set(PulseMetric, METRICS_VALUE(2));
	pulse_report();

	fake_timestamp = 100u; 
	mock().expectOneCall("metrics_report_transmit").withParameter("datasize", (size_t)8).andReturnValue(0);
	pulse_report(); 

	LONGS_EQUAL(5000u, pulse_get_report_ctx()->last_report_time);
}
