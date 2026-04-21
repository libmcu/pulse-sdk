#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"

int metrics_report_transmit(const void *data, size_t datasize, void *ctx);
}

static struct pulse_report_ctx g_report_ctx;

extern "C" const struct pulse_report_ctx *pulse_get_report_ctx(void)
{
	return &g_report_ctx;
}

TEST_GROUP(PulseTransportHttpsEspIdf)
{
	void setup()
	{
		memset(&g_report_ctx, 0, sizeof(g_report_ctx));
	}

	void teardown()
	{
		mock().checkExpectations();
		mock().clear();
	}
};

TEST(PulseTransportHttpsEspIdf, ShouldReturnInvalidArgumentWhenPayloadIsMissing)
{
	CHECK_EQUAL(-EINVAL, metrics_report_transmit(NULL, 1u, NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldPostMetricsOverHttpsWhenRequestSucceeds)
{
	static const uint8_t payload[] = { 0xA1, 0x01, 0x18, 0x64 };
	static char response[4] = { 'o', 'k', '\r', '\n' };
	void *handle = (void *)0x1234;

	mock().expectOneCall("esp_http_client_init")
		.withStringParameter("url", "https://ingest.libmcu.org/v1")
		.withParameter("timeout_ms", 60000)
		.withParameter("buffer_size", 4096)
		.withParameter("buffer_size_tx", 4096)
		.withPointerParameter("crt_bundle_attach",
				(void *)esp_crt_bundle_attach)
		.andReturnValue(handle);
	mock().expectOneCall("esp_http_client_set_method")
		.withPointerParameter("client", handle)
		.withParameter("method", (int)HTTP_METHOD_POST)
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_header")
		.withPointerParameter("client", handle)
		.withStringParameter("key", "Content-Type")
		.withStringParameter("value", "application/cbor")
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_set_post_field")
		.withPointerParameter("client", handle)
		.withMemoryBufferParameter("data", payload, sizeof(payload))
		.withParameter("len", (int)sizeof(payload))
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_perform")
		.withPointerParameter("client", handle)
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.withPointerParameter("client", handle)
		.andReturnValue(202);
	mock().expectOneCall("esp_http_client_get_content_length")
		.withPointerParameter("client", handle)
		.andReturnValue((long)-1);
	mock().expectOneCall("esp_http_client_read_response")
		.withPointerParameter("client", handle)
		.withOutputParameterReturning("buffer", response, sizeof(response))
		.withParameter("len", 4096)
		.andReturnValue((int)sizeof(response));
	mock().expectOneCall("esp_http_client_cleanup")
		.withPointerParameter("client", handle)
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenServerRespondsWithFailure)
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

	CHECK_EQUAL(-EIO, metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldCleanupHandleWhenSetupFails)
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

	CHECK_EQUAL(-EIO, metrics_report_transmit(payload, sizeof(payload), NULL));
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
			metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnInvalidArgumentWhenDataSizeIsZero)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EINVAL, metrics_report_transmit(payload, 0u, NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnNoMemoryWhenClientInitFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("esp_http_client_init").ignoreOtherParameters()
		.andReturnValue((void *)0);

	CHECK_EQUAL(-ENOMEM,
			metrics_report_transmit(payload, sizeof(payload), NULL));
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
			metrics_report_transmit(payload, sizeof(payload), NULL));
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
			metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnOkWhenStatusCodeIs200)
{
	static const uint8_t payload[] = { 0x01 };
	static char response[] = "ok";
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
	mock().expectOneCall("esp_http_client_get_content_length")
		.ignoreOtherParameters()
		.andReturnValue((long)-1);
	mock().expectOneCall("esp_http_client_read_response")
		.ignoreOtherParameters()
		.withOutputParameterReturning("buffer", response, sizeof(response) - 1)
		.andReturnValue((int)(sizeof(response) - 1));
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldSendAuthorizationHeaderWhenTokenProvided)
{
	static const uint8_t payload[] = { 0xA1, 0x01, 0x18, 0x64 };
	static char response[] = "ok";
	g_report_ctx.conf.token = "k7Dj2mXpRvN8qL5w";
	void *handle = (void *)0x1234;

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
	mock().expectOneCall("esp_http_client_get_content_length")
		.ignoreOtherParameters()
		.andReturnValue((long)-1);
	mock().expectOneCall("esp_http_client_read_response")
		.ignoreOtherParameters()
		.withOutputParameterReturning("buffer", response, sizeof(response) - 1)
		.andReturnValue((int)(sizeof(response) - 1));
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, metrics_report_transmit(payload, sizeof(payload), NULL));
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

	CHECK_EQUAL(-EIO, metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnOkWhenStatusCodeIs299)
{
	static const uint8_t payload[] = { 0x01 };
	static char response[] = "accepted";
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
	mock().expectOneCall("esp_http_client_get_content_length")
		.ignoreOtherParameters()
		.andReturnValue((long)-1);
	mock().expectOneCall("esp_http_client_read_response")
		.ignoreOtherParameters()
		.withOutputParameterReturning("buffer", response, sizeof(response) - 1)
		.andReturnValue((int)(sizeof(response) - 1));
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, metrics_report_transmit(payload, sizeof(payload), NULL));
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
			metrics_report_transmit(payload, sizeof(payload), NULL));
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
			metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnOverflowWhenDataSizeExceedsIntMax)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EOVERFLOW,
			metrics_report_transmit(payload,
					(size_t)INT_MAX + 1u, NULL));
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
			metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnIoWhenCleanupFailsAfterSuccess)
{
	static const uint8_t payload[] = { 0x01 };
	static char response[] = "ok";
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
	mock().expectOneCall("esp_http_client_get_content_length")
		.ignoreOtherParameters()
		.andReturnValue((long)-1);
	mock().expectOneCall("esp_http_client_read_response")
		.ignoreOtherParameters()
		.withOutputParameterReturning("buffer", response, sizeof(response) - 1)
		.andReturnValue((int)(sizeof(response) - 1));
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_FAIL);

	CHECK_EQUAL(-EIO,
			metrics_report_transmit(payload, sizeof(payload), NULL));
}

static void test_response_handler(const void *data, size_t datasize, void *ctx)
{
	mock().actualCall("pulse_response_handler")
		.withMemoryBufferParameter("data", (const unsigned char *)data,
				datasize)
		.withParameter("datasize", datasize)
		.withPointerParameter("ctx", ctx);
}

TEST(PulseTransportHttpsEspIdf, ShouldForwardResponseBodyToRegisteredHandler)
{
	static const uint8_t payload[] = { 0x01 };
	static char response[] = "pong";
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
	mock().expectOneCall("esp_http_client_perform").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);
	mock().expectOneCall("esp_http_client_get_status_code")
		.ignoreOtherParameters()
		.andReturnValue(200);
	mock().expectOneCall("esp_http_client_get_content_length")
		.ignoreOtherParameters()
		.andReturnValue((long)-1);
	mock().expectOneCall("esp_http_client_read_response")
		.ignoreOtherParameters()
		.withOutputParameterReturning("buffer", response, sizeof(response) - 1)
		.andReturnValue((int)(sizeof(response) - 1));
	mock().expectOneCall("pulse_response_handler")
		.withMemoryBufferParameter("data", (const unsigned char *)response,
				sizeof(response) - 1)
		.withParameter("datasize", sizeof(response) - 1)
		.withPointerParameter("ctx", &response_ctx);
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnMsgSizeWhenResponseIsTruncated)
{
	static const uint8_t payload[] = { 0x01 };
	static char partial_response[] = "pa";
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
	mock().expectOneCall("esp_http_client_get_content_length")
		.ignoreOtherParameters()
		.andReturnValue((long)100);
	mock().expectOneCall("esp_http_client_read_response")
		.ignoreOtherParameters()
		.withOutputParameterReturning("buffer", partial_response,
				sizeof(partial_response) - 1)
		.andReturnValue((int)(sizeof(partial_response) - 1));
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(-EMSGSIZE,
			metrics_report_transmit(payload, sizeof(payload), NULL));
}

TEST(PulseTransportHttpsEspIdf, ShouldReturnOkWhenContentLengthIsUnknown)
{
	static const uint8_t payload[] = { 0x01 };
	static char response[] = "ok";
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
	mock().expectOneCall("esp_http_client_get_content_length")
		.ignoreOtherParameters()
		.andReturnValue((long)-1);
	mock().expectOneCall("esp_http_client_read_response")
		.ignoreOtherParameters()
		.withOutputParameterReturning("buffer", response, sizeof(response) - 1)
		.andReturnValue((int)(sizeof(response) - 1));
	mock().expectOneCall("esp_http_client_cleanup").ignoreOtherParameters()
		.andReturnValue((int)ESP_OK);

	CHECK_EQUAL(0, metrics_report_transmit(payload, sizeof(payload), NULL));
}
