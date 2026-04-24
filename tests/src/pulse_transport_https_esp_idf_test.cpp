#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
}

static struct pulse_report_ctx g_report_ctx;

TEST_GROUP(PulseTransportHttpsEspIdf)
{
	void setup()
	{
		memset(&g_report_ctx, 0, sizeof(g_report_ctx));
		g_captured_event_handler = NULL;
		g_captured_user_data = NULL;
		esp_timer_mock_reset();
		esp_http_client_mock_reset_inject();
		mock().ignoreOtherCalls();
		pulse_transport_cancel();
		mock().clear();
	}

	void teardown()
	{
		mock().checkExpectations();
		mock().clear();
	}
};

TEST(PulseTransportHttpsEspIdf, ShouldReturnInvalidArgumentWhenPayloadIsNull)
{
	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(NULL, 1u, &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnInvalidArgumentWhenDataSizeIsZero)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(payload, 0u, &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnOverflowWhenDataSizeExceedsIntMax)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EOVERFLOW,
			pulse_transport_transmit(payload,
					(size_t)INT_MAX + 1u, &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnNoMemoryWhenClientInitFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue((void *)0);

	CHECK_EQUAL(-ENOMEM,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldInitClientWithAsyncAndEventHandler)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	g_report_ctx.conf.async_transport = true;

	mock().expectOneCall("esp_http_client_init")
		.withStringParameter("url", "https://ingest.libmcu.org/v1")
		.withParameter("timeout_ms", 60000)
		.withParameter("buffer_size", 4096)
		.withParameter("buffer_size_tx", 4096)
		.withPointerParameter("crt_bundle_attach",
				(void *)esp_crt_bundle_attach)
		.withParameter("is_async", (int)true)
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx);
}

TEST(PulseTransportHttpsEspIdf, ShouldInitClientWithConfiguredTransmitTimeout)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	g_report_ctx.conf.transmit_timeout_ms = 1234u;

	mock().expectOneCall("esp_http_client_init")
		.withStringParameter("url", "https://ingest.libmcu.org/v1")
		.withParameter("timeout_ms", 1234)
		.withParameter("buffer_size", 4096)
		.withParameter("buffer_size_tx", 4096)
		.withPointerParameter("crt_bundle_attach",
				(void *)esp_crt_bundle_attach)
		.withParameter("is_async", (int)false)
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx);
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenSetMethodFails)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenContentTypeHeaderFails)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EIO, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenPostFieldFails)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenAuthorizationHeaderFails)
{
	static const uint8_t payload[] = { 0x01 };
	g_report_ctx.conf.token = "mytoken";
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header")
		.withStringParameter("key", "Content-Type")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header")
		.withStringParameter("key", "Authorization")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EIO, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenTokenTooLongToFitAuthHeader)
{
	static const uint8_t payload[] = { 0x01 };
	static char long_token[250];
	void *handle = (void *)0x1234;

	memset(long_token, 'x', sizeof(long_token) - 1u);
	long_token[sizeof(long_token) - 1u] = '\0';
	g_report_ctx.conf.token = long_token;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header")
		.withStringParameter("key", "Content-Type")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnEinprogressWhenAsyncPerformIsNotYetComplete)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	g_report_ctx.conf.async_transport = true;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnEinprogressOnSubsequentCallWhenAsyncStillInProgress)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	g_report_ctx.conf.async_transport = true;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));

	mock().checkExpectations();
	mock().clear();

	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldCompleteAndCleanupWhenAsyncPerformSucceeds)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	g_report_ctx.conf.async_transport = true;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));

	mock().checkExpectations();
	mock().clear();

	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(202);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf,
		ShouldInitClientWithSyncModeWhenAsyncTransportFalse)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init")
		.withStringParameter("url", "https://ingest.libmcu.org/v1")
		.withParameter("is_async", (int)false)
		.ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx);
}

TEST(PulseTransportHttpsEspIdf,
		ShouldReturnTimeoutWhenSyncPerformReturnsEagain)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	g_report_ctx.conf.transmit_timeout_ms = 1u;
	esp_timer_mock_set_step(1000);

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-ETIMEDOUT,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenServerRespondsWithFailureStatus)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(500);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EIO, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldCleanupClientWhenCancelCalledWhileInProgress)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	g_report_ctx.conf.async_transport = true;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));

	mock().checkExpectations();
	mock().clear();

	mock().expectOneCall("esp_http_client_cleanup")
		.withPointerParameter("client", handle)
		.andReturnValue((int)ESP_OK);

	pulse_transport_cancel();
}

TEST(PulseTransportHttpsEspIdf, ShouldBeIdempotentWhenCancelCalledWhileIdle)
{
	pulse_transport_cancel();
}

TEST(PulseTransportHttpsEspIdf, ShouldResetStateAfterCancelSoNextTransmitStartsFresh)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle1 = (void *)0x1234;
	void *handle2 = (void *)0x5678;
	g_report_ctx.conf.async_transport = true;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle1);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);

	pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx);

	mock().checkExpectations();
	mock().clear();

	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	pulse_transport_cancel();

	mock().checkExpectations();
	mock().clear();

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle2);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnOkWhenStatusCodeIs200)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnOkWhenStatusCodeIs299)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(299);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenStatusCodeIs199)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(199);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenStatusCodeIs300)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(300);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldMapEspTimeoutToErrnoTimeout)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_ERR_TIMEOUT);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-ETIMEDOUT,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenCleanupFailsAfterSuccess)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf, ShouldSendAuthorizationHeaderWhenTokenProvided)
{
	static const uint8_t payload[] = { 0xA1, 0x01, 0x18, 0x64 };
	void *handle = (void *)0x1234;
	g_report_ctx.conf.token = "k7Dj2mXpRvN8qL5w";

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header")
		.withPointerParameter("client", handle)
		.withStringParameter("key", "Content-Type")
		.withStringParameter("value", "application/cbor")
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header")
		.withPointerParameter("client", handle)
		.withStringParameter("key", "Authorization")
		.withStringParameter("value", "Bearer k7Dj2mXpRvN8qL5w")
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(202);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

static void test_response_handler(const void *data, size_t datasize, void *ctx)
{
	mock().actualCall("pulse_response_handler")
		.withMemoryBufferParameter("data", (const unsigned char *)data,
				datasize)
		.withParameter("datasize", datasize)
		.withPointerParameter("ctx", ctx);
}

TEST(PulseTransportHttpsEspIdf,
		ShouldForwardResponseBodyToRegisteredHandlerViaEventCallback)
{
	static const uint8_t payload[] = { 0x01 };
	static const char response_body[] = "pong";
	void *handle = (void *)0x1234;
	int response_ctx = 7;

	g_report_ctx.on_response = test_response_handler;
	g_report_ctx.response_ctx = &response_ctx;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform")
		.withPointerParameter("client", handle)
		.andReturnValue((int)ESP_OK);

	esp_http_client_mock_inject_data(response_body,
			(int)(sizeof(response_body) - 1u));

	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("pulse_response_handler")
		.withMemoryBufferParameter("data",
				(const unsigned char *)response_body,
				sizeof(response_body) - 1u)
		.withParameter("datasize", sizeof(response_body) - 1u)
		.withPointerParameter("ctx", &response_ctx);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf,
		ShouldAccumulateChunkedResponseBodyAcrossMultipleOnDataEvents)
{
	static const uint8_t payload[] = { 0x01 };
	static const char chunk1[] = "he";
	static const char chunk2[] = "llo";
	static const char expected[] = "hello";
	void *handle = (void *)0x1234;
	int response_ctx = 0;
	g_report_ctx.conf.async_transport = true;

	g_report_ctx.on_response = test_response_handler;
	g_report_ctx.response_ctx = &response_ctx;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform")
		.withPointerParameter("client", handle)
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));

	if (g_captured_event_handler != NULL) {
		esp_http_client_event_t evt = {};
		evt.event_id = HTTP_EVENT_ON_DATA;
		evt.data = const_cast<char *>(chunk1);
		evt.data_len = (int)(sizeof(chunk1) - 1u);
		evt.user_data = g_captured_user_data;
		g_captured_event_handler(&evt);
	}

	mock().checkExpectations();
	mock().clear();

	esp_http_client_mock_inject_data(chunk2, (int)(sizeof(chunk2) - 1u));

	mock().expectOneCall("esp_http_client_perform")
		.withPointerParameter("client", handle)
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("pulse_response_handler")
		.withMemoryBufferParameter("data",
				(const unsigned char *)expected,
				sizeof(expected) - 1u)
		.withParameter("datasize", sizeof(expected) - 1u)
		.withPointerParameter("ctx", &response_ctx);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf,
		ShouldReturnMessageTooLargeWhenResponseExceedsBuffer)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	int response_ctx = 0;
	char first_chunk[4096];
	static const char overflow_chunk[] = "!";

	memset(first_chunk, 'a', sizeof(first_chunk));
	g_report_ctx.conf.async_transport = true;
	g_report_ctx.on_response = test_response_handler;
	g_report_ctx.response_ctx = &response_ctx;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform")
		.withPointerParameter("client", handle)
		.andReturnValue((int)ESP_ERR_HTTP_EAGAIN);

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));

	if (g_captured_event_handler != NULL) {
		esp_http_client_event_t evt = {};
		evt.event_id = HTTP_EVENT_ON_DATA;
		evt.data = first_chunk;
		evt.data_len = (int)sizeof(first_chunk);
		evt.user_data = g_captured_user_data;
		g_captured_event_handler(&evt);

		evt.data = const_cast<char *>(overflow_chunk);
		evt.data_len = (int)(sizeof(overflow_chunk) - 1u);
		g_captured_event_handler(&evt);
	}

	mock().checkExpectations();
	mock().clear();

	mock().expectOneCall("esp_http_client_perform")
		.withPointerParameter("client", handle)
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EMSGSIZE,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf,
		ShouldNotCallResponseHandlerWhenNoDataReceivedFromServer)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle = (void *)0x1234;
	int response_ctx = 0;

	g_report_ctx.on_response = test_response_handler;
	g_report_ctx.response_ctx = &response_ctx;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(202);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsEspIdf,
		ShouldResetStateAfterCompletionSoNextCallStartsFresh)
{
	static const uint8_t payload[] = { 0x01 };
	void *handle1 = (void *)0x1234;
	void *handle2 = (void *)0x5678;

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle1);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));

	mock().checkExpectations();
	mock().clear();

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue(handle2);
	mock().expectOneCall("esp_http_client_set_method").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}
