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
}

static uint8_t transmitted_payload[1024];
static size_t transmitted_payload_len;
static void *last_transmit_ctx;
static void response_handler(const void *data, size_t datasize, void *ctx)
{
	(void)data;
	(void)datasize;
	(void)ctx;
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

TEST_GROUP(PulseReport)
{
	void setup()
	{
		memset(transmitted_payload, 0, sizeof(transmitted_payload));
		transmitted_payload_len = 0u;
		last_transmit_ctx = NULL;
		metricfs_stub_reset();
		metrics_reset();
		metrics_report_periodic_reset();

		struct pulse conf = { .token = "test-token", .mfs = NULL };
		pulse_init(&conf);
	}

	void teardown()
	{
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

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithNullToken)
{
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(NULL));
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

TEST(PulseReport, ShouldReturnBacklogPendingWhileBacklogRemains)
{
	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_update_metricfs((struct metricfs *)(uintptr_t)1));

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

TEST(PulseReport, ShouldAllowInitWithoutTokenUntilTokenIsSet)
{
	struct pulse conf = { .token = NULL };

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_init(&conf));
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_report());
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_update_token("late-token"));
 	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_update_metricfs((struct metricfs *)(uintptr_t)1));

	mock().expectOneCall("metrics_report_transmit")
		.withParameter("datasize", (size_t)8)
		.andReturnValue(0);

	metrics_set(PulseMetric, METRICS_VALUE(3));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_report());
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

TEST(PulseReport, ShouldReturnEmptyWhenNoMetricsCollected)
{
	CHECK_EQUAL(PULSE_STATUS_EMPTY, pulse_report());
}

TEST(PulseReport, ShouldReturnOkForAllStatusStringConversions)
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
	CHECK_TRUE(pulse_stringify_status((pulse_status_t)-99) != NULL);
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenTokenIsNull)
{
	struct pulse conf = { .token = NULL };

	CHECK_EQUAL(PULSE_STATUS_OK, pulse_init(&conf));
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_update_token(NULL));
	CHECK_EQUAL(PULSE_STATUS_OK, pulse_update_metricfs(NULL));
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

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithTooLongToken)
{
	static char long_token[45];
	memset(long_token, 'x', sizeof(long_token) - 1u);
	long_token[sizeof(long_token) - 1u] = '\0';

	struct pulse conf = { .token = long_token };
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
}
