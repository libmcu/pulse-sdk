#include "CppUTest/TestHarness.h"

extern "C" {
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "curl/curl.h"

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
}

static struct pulse_report_ctx g_report_ctx;
static char g_response_text[8192];
static size_t g_response_len;

static void on_response(const void *data, size_t datasize, void *ctx)
{
	(void)ctx;
	if (datasize >= sizeof(g_response_text)) {
		datasize = sizeof(g_response_text) - 1u;
	}
	memcpy(g_response_text, data, datasize);
	g_response_text[datasize] = '\0';
	g_response_len = datasize;
}

TEST_GROUP(PulseTransportHttpsLinux)
{
	void setup()
	{
		memset(&g_report_ctx, 0, sizeof(g_report_ctx));
		memset(g_response_text, 0, sizeof(g_response_text));
		g_response_len = 0u;
		curl_mock_reset();
		pulse_transport_cancel();
	}

	void teardown()
	{
		curl_mock_reset();
	}
};

TEST(PulseTransportHttpsLinux, ShouldReturnInvalidArgumentWhenPayloadIsNull)
{
	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(NULL, 1u, &g_report_ctx));
}

TEST(PulseTransportHttpsLinux, ShouldReturnInvalidArgumentWhenDataSizeIsZero)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(payload, 0u, &g_report_ctx));
}

TEST(PulseTransportHttpsLinux, ShouldReturnOverflowWhenPayloadExceedsCurlRange)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EOVERFLOW,
			pulse_transport_transmit(payload, (size_t)INT64_MAX + 1u,
					&g_report_ctx));
}

TEST(PulseTransportHttpsLinux, ShouldReturnNotSupportedWhenAsyncTransportRequested)
{
	static const uint8_t payload[] = { 0x01 };
	g_report_ctx.conf.async_transport = true;

	CHECK_EQUAL(-ENOTSUP,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsLinux, ShouldReturnNoMemoryWhenCurlInitFails)
{
	static const uint8_t payload[] = { 0x01 };
	curl_mock_set_init_handle(NULL);

	CHECK_EQUAL(-ENOMEM,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(1, curl_mock_global_init_call_count());
	CHECK_EQUAL(1, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldReturnIoWhenCurlGlobalInitFails)
{
	static const uint8_t payload[] = { 0x01 };
	curl_mock_set_global_init_result(CURLE_OUT_OF_MEMORY);

	CHECK_EQUAL(-ENOMEM,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(1, curl_mock_global_init_call_count());
	CHECK_EQUAL(0, curl_mock_cleanup_call_count());
	CHECK_EQUAL(0, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldConfigureCurlRequestFromReportContext)
{
	static const uint8_t payload[] = { 0x0a, 0x0b, 0x0c };
	struct curl_slist *headers;

	g_report_ctx.conf.token = "test-token";
	g_report_ctx.conf.transmit_timeout_ms = 1234u;
	curl_mock_set_response_code(202);

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	STRCMP_EQUAL("https://ingest.libmcu.org/v1", curl_mock_last_url());
	CHECK_EQUAL(1234, curl_mock_last_timeout_ms());
	CHECK_EQUAL(1, curl_mock_last_post_enabled());
	CHECK_EQUAL(1, curl_mock_last_nosignal());
	POINTERS_EQUAL(payload, curl_mock_last_post_fields());
	CHECK_EQUAL((curl_off_t)sizeof(payload), curl_mock_last_post_size());
	CHECK(curl_mock_last_write_callback() != NULL);
	CHECK(curl_mock_last_write_data() != NULL);

	headers = curl_mock_last_headers();
	CHECK(headers != NULL);
	STRCMP_EQUAL("Content-Type: application/cbor", headers->data);
	CHECK(headers->next != NULL);
	STRCMP_EQUAL("Authorization: Bearer test-token", headers->next->data);
	CHECK(headers->next->next == NULL);
	CHECK_EQUAL(1, curl_mock_cleanup_call_count());
	CHECK_EQUAL(1, curl_mock_global_init_call_count());
	CHECK_EQUAL(1, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldReturnOverflowWhenAuthorizationHeaderExceedsLimit)
{
	static const uint8_t payload[] = { 0x01 };
	static const char token[] =
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

	g_report_ctx.conf.token = token;

	CHECK_EQUAL(-EOVERFLOW,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(1, curl_mock_cleanup_call_count());
	CHECK_EQUAL(1, curl_mock_global_init_call_count());
	CHECK_EQUAL(1, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldReturnNoHostWhenCurlPerformCannotResolveHost)
{
	static const uint8_t payload[] = { 0x01 };

	curl_mock_set_perform_result(CURLE_COULDNT_RESOLVE_HOST);

	CHECK_EQUAL(-EHOSTUNREACH,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(1, curl_mock_cleanup_call_count());
	CHECK_EQUAL(1, curl_mock_global_init_call_count());
	CHECK_EQUAL(1, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldReturnConnrefusedWhenCurlPerformCannotConnect)
{
	static const uint8_t payload[] = { 0x01 };

	curl_mock_set_perform_result(CURLE_COULDNT_CONNECT);

	CHECK_EQUAL(-ECONNREFUSED,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(1, curl_mock_cleanup_call_count());
	CHECK_EQUAL(1, curl_mock_global_init_call_count());
	CHECK_EQUAL(1, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldReturnNoMemoryWhenCurlPerformIsOutOfMemory)
{
	static const uint8_t payload[] = { 0x01 };

	curl_mock_set_perform_result(CURLE_OUT_OF_MEMORY);

	CHECK_EQUAL(-ENOMEM,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(1, curl_mock_cleanup_call_count());
	CHECK_EQUAL(1, curl_mock_global_init_call_count());
	CHECK_EQUAL(1, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldReturnTimeoutWhenCurlPerformTimesOut)
{
	static const uint8_t payload[] = { 0x01 };
	curl_mock_set_perform_result(CURLE_OPERATION_TIMEDOUT);

	CHECK_EQUAL(-ETIMEDOUT,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(1, curl_mock_cleanup_call_count());
	CHECK_EQUAL(1, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldBalanceCurlGlobalLifecycleAcrossConsecutiveCalls)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));

	CHECK_EQUAL(2, curl_mock_global_init_call_count());
	CHECK_EQUAL(2, curl_mock_cleanup_call_count());
	CHECK_EQUAL(2, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldRecoverFromGlobalInitFailureWithoutRefcountLeak)
{
	static const uint8_t payload[] = { 0x01 };

	curl_mock_set_global_init_result(CURLE_COULDNT_CONNECT);
	CHECK_EQUAL(-ECONNREFUSED,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(1, curl_mock_global_init_call_count());
	CHECK_EQUAL(0, curl_mock_global_cleanup_call_count());

	curl_mock_set_global_init_result(CURLE_OK);
	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(2, curl_mock_global_init_call_count());
	CHECK_EQUAL(1, curl_mock_global_cleanup_call_count());
}

TEST(PulseTransportHttpsLinux, ShouldReturnIoWhenResponseStatusIsFailure)
{
	static const uint8_t payload[] = { 0x01 };
	curl_mock_set_response_code(401);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportHttpsLinux, ShouldDeliverResponseBodyWhenServerReturnsPayload)
{
	static const uint8_t payload[] = { 0x01 };
	static const char response[] = "accepted";

	g_report_ctx.on_response = on_response;
	curl_mock_set_response_code(202);
	curl_mock_inject_response(response, strlen(response));

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	CHECK_EQUAL(strlen(response), g_response_len);
	STRCMP_EQUAL(response, g_response_text);
}

TEST(PulseTransportHttpsLinux, ShouldReturnMessageSizeWhenResponseBodyExceedsBuffer)
{
	static const uint8_t payload[] = { 0x01 };
	static char response[5000];

	memset(response, 'x', sizeof(response));
	curl_mock_set_response_code(200);
	curl_mock_inject_response(response, sizeof(response));

	CHECK_EQUAL(-EMSGSIZE,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}
