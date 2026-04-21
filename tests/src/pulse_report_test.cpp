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
}

static uint8_t transmitted_payload[256];
static size_t transmitted_payload_len;
static void *last_transmit_ctx;

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
		metrics_reset();
		metrics_report_periodic_reset();

		struct pulse conf = { .token = "test-token" };
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

TEST(PulseReport, ShouldReturnInvalidArgumentWhenInitCalledWithNullToken)
{
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(NULL));
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
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_ALREADY) != NULL);
	CHECK_TRUE(pulse_stringify_status(PULSE_STATUS_EMPTY) != NULL);
	CHECK_TRUE(pulse_stringify_status((pulse_status_t)-99) != NULL);
}

TEST(PulseReport, ShouldReturnInvalidArgumentWhenTokenIsNull)
{
	struct pulse conf = { .token = NULL };

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT, pulse_init(&conf));
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
