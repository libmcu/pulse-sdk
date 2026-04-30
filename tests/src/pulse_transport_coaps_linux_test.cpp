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

/* base64url("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") = 32 zero bytes 임 */
#define VALID_TOKEN	"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define VALID_TOKEN_PSK_LEN	32u

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
	g_report_ctx.conf.token = VALID_TOKEN;

	CHECK_EQUAL(-EOVERFLOW,
			pulse_transport_transmit(payload, (size_t)INT64_MAX + 1u,
					&g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnNotSupportedWhenAsyncTransportRequested)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = VALID_TOKEN;
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

	g_report_ctx.conf.token = VALID_TOKEN;
	openssl_mock_set_sha256_result(0);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnNoMemoryWhenContextCreationFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = VALID_TOKEN;
	coap_mock_set_context_result(NULL);

	CHECK_EQUAL(-ENOMEM,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnHostUnreachableWhenResolutionFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = VALID_TOKEN;
	coap_mock_set_resolve_result(0);

	CHECK_EQUAL(-EHOSTUNREACH,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnIoWhenSessionCreationFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = VALID_TOKEN;
	coap_mock_set_session_result(NULL);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldConfigureCoapsSessionFromTokenAndPayload)
{
	static const uint8_t payload[] = { 0xaa, 0xbb, 0xcc };
	static const uint8_t response[] = { 0x01, 0x02, 0x03 };
	static const uint8_t expected_psk[VALID_TOKEN_PSK_LEN] = { 0 };

	g_report_ctx.conf.token = VALID_TOKEN;
	g_report_ctx.conf.transmit_timeout_ms = 4321u;
	g_report_ctx.on_response = on_response;
	coap_mock_set_response_payload(response, sizeof(response), 0u,
			sizeof(response));

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
	STRCMP_EQUAL("ingest.libmcu.org", coap_mock_last_client_sni());
	STRCMP_EQUAL("000102030405060708090a0b0c0d0e0f",
			coap_mock_last_psk_identity());
	CHECK_EQUAL((int)VALID_TOKEN_PSK_LEN, (int)coap_mock_last_psk_key_len());
	MEMCMP_EQUAL(expected_psk, coap_mock_last_psk_key(), VALID_TOKEN_PSK_LEN);
	STRCMP_EQUAL("v1", coap_mock_last_uri_path());
	CHECK_EQUAL(COAP_MEDIATYPE_APPLICATION_CBOR,
			(int)coap_mock_last_content_format());
	CHECK_EQUAL(4321, (int)coap_mock_last_timeout_ms());
	CHECK_EQUAL((int)sizeof(payload), (int)coap_mock_last_payload_len());
	MEMCMP_EQUAL(payload, coap_mock_last_payload_data(), sizeof(payload));
	CHECK_EQUAL(strlen(VALID_TOKEN), (int)openssl_mock_last_input_len());
	MEMCMP_EQUAL(VALID_TOKEN, openssl_mock_last_input(), strlen(VALID_TOKEN));
	CHECK_EQUAL((int)sizeof(response), (int)g_response_len);
	MEMCMP_EQUAL(response, g_response, sizeof(response));
}

TEST(PulseTransportCoapsLinux,
		ShouldReturnInvalidArgumentWhenTokenIsNotValidBase64Url)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = "not-a-valid-base64url-token";

	CHECK_EQUAL(-EINVAL,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnTimeoutWhenSendRecvTimesOut)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = VALID_TOKEN;
	coap_mock_set_send_recv_result(-5);

	CHECK_EQUAL(-ETIMEDOUT,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnProtoWhenDtlsHandshakeFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = VALID_TOKEN;
	coap_mock_set_send_recv_result(-7);

	CHECK_EQUAL(-EPROTO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnIoWhenPacketIsNotDeliverable)
{
	static const uint8_t payload[] = { 0x01 };

	g_report_ctx.conf.token = VALID_TOKEN;
	coap_mock_set_send_recv_result(-6);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}

TEST(PulseTransportCoapsLinux, ShouldReturnIoWhenResponseCodeIsFailure)
{
	static const uint8_t payload[] = { 0x01 };
	static const char response[] = "invalid_token";

	g_report_ctx.conf.token = VALID_TOKEN;
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
	g_report_ctx.conf.token = VALID_TOKEN;
	coap_mock_set_response_payload(response, sizeof(response), 0u,
			sizeof(response));

	CHECK_EQUAL(-EMSGSIZE,
			pulse_transport_transmit(payload, sizeof(payload), &g_report_ctx));
}
