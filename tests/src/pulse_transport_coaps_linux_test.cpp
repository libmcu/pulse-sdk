#include "CppUTest/TestHarness.h"

extern "C" {
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "coap3/coap.h"
#include "openssl/sha.h"

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
}

static struct pulse_report_ctx g_report_ctx;
static uint8_t g_response[8192];
static size_t g_response_len;

static void on_response(const void *data, size_t datasize, void *ctx)
{
	(void)ctx;
	if (datasize > sizeof(g_response)) {
		datasize = sizeof(g_response);
	}
	memcpy(g_response, data, datasize);
	g_response_len = datasize;
}

TEST_GROUP(PulseTransportCoapsLinux)
{
	void setup()
	{
		memset(&g_report_ctx, 0, sizeof(g_report_ctx));
		memset(g_response, 0, sizeof(g_response));
		g_response_len = 0u;
		coap_mock_reset();
		openssl_mock_reset();
		pulse_transport_cancel();
	}
};

TEST(PulseTransportCoapsLinux, ShouldReturnInvalidArgumentWhenPayloadIsNull)
{
	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(NULL, 1u, &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnInvalidArgumentWhenDataSizeIsZero)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(payload, 0u, &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnOverflowWhenPayloadExceedsRange)
{
	static const uint8_t payload[] = { 0x01 };
	g_report_ctx.conf.token = "test-token";

	CHECK_EQUAL(-EOVERFLOW,
			pulse_transport_transmit(payload, (size_t)INT64_MAX + 1u,
					&g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnNotSupportedWhenAsyncTransportRequested)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = "test-token";
	g_report_ctx.conf.async_transport = true;

	CHECK_EQUAL(-ENOTSUP,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnInvalidArgumentWhenTokenIsMissing)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EINVAL,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnIoWhenSha256Fails)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = "test-token";
	openssl_mock_set_sha256_result(0);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnNoMemoryWhenContextCreationFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = "test-token";
	coap_mock_set_context_result(NULL);

	CHECK_EQUAL(-ENOMEM,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnHostUnreachableWhenResolutionFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = "test-token";
	coap_mock_set_resolve_result(0);

	CHECK_EQUAL(-EHOSTUNREACH,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnIoWhenSessionCreationFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = "test-token";
	coap_mock_set_session_result(NULL);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldConfigureCoapsSessionFromTokenAndPayload)
{
	static const uint8_t payload[] = { 0xaa, 0xbb, 0xcc };
	static const uint8_t response[] = { 0x01, 0x02, 0x03 };

	g_report_ctx.conf.token = "token-123";
	g_report_ctx.conf.transmit_timeout_ms = 4321u;
	g_report_ctx.on_response = on_response;
	coap_mock_set_response_payload(response, sizeof(response), 0u,
			sizeof(response));

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	STRCMP_EQUAL("ingest.libmcu.org", coap_mock_last_client_sni());
	STRCMP_EQUAL("000102030405060708090a0b0c0d0e0f",
			coap_mock_last_psk_identity());
	STRCMP_EQUAL("token-123", coap_mock_last_psk_key());
	STRCMP_EQUAL("v1", coap_mock_last_uri_path());
	CHECK_EQUAL(COAP_MEDIATYPE_APPLICATION_CBOR,
			(int)coap_mock_last_content_format());
	CHECK_EQUAL(4321, (int)coap_mock_last_timeout_ms());
	CHECK_EQUAL((int)sizeof(payload), (int)coap_mock_last_payload_len());
	MEMCMP_EQUAL(payload, coap_mock_last_payload_data(), sizeof(payload));
	CHECK_EQUAL(strlen("token-123"), (int)openssl_mock_last_input_len());
	MEMCMP_EQUAL("token-123", openssl_mock_last_input(), strlen("token-123"));
	CHECK_EQUAL((int)sizeof(response), (int)g_response_len);
	MEMCMP_EQUAL(response, g_response, sizeof(response));
}

TEST(PulseTransportCoapsLinux, ShouldReturnTimeoutWhenSendRecvTimesOut)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = "test-token";
	coap_mock_set_send_recv_result(-5);

	CHECK_EQUAL(-ETIMEDOUT,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnIoWhenResponseCodeIsFailure)
{
	static const uint8_t payload[] = { 0x01 };
	static const char response[] = "invalid_token";

	g_report_ctx.conf.token = "test-token";
	coap_mock_set_response_code((coap_pdu_code_t)128);
	coap_mock_set_response_payload(response, strlen(response), 0u,
			strlen(response));

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnMessageSizeWhenResponseBodyExceedsBuffer)
{
	static const uint8_t payload[] = { 0x01 };
	static uint8_t response[5000];

	memset(response, 0x5a, sizeof(response));
	g_report_ctx.conf.token = "test-token";
	coap_mock_set_response_payload(response, sizeof(response), 0u,
			sizeof(response));

	CHECK_EQUAL(-EMSGSIZE,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}
