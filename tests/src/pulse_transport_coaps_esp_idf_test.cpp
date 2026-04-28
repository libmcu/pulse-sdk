#include "CppUTest/TestHarness.h"

extern "C" {
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "coap3/coap.h"
#include "psa/crypto.h"

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
}

static struct pulse_report_ctx g_ctx;
static uint8_t g_response_buf[8192];
static size_t g_response_len;

static void on_response(const void *data, size_t datasize, void *ctx)
{
	(void)ctx;
	if (datasize > sizeof(g_response_buf)) {
		datasize = sizeof(g_response_buf);
	}
	memcpy(g_response_buf, data, datasize);
	g_response_len = datasize;
}

TEST_GROUP(PulseTransportCoapsEspIdf)
{
	void setup()
	{
		memset(&g_ctx, 0, sizeof(g_ctx));
		memset(g_response_buf, 0, sizeof(g_response_buf));
		g_response_len = 0u;
		coap_mock_reset();
		psa_crypto_mock_reset();
		pulse_transport_cancel();
	}
};

TEST(PulseTransportCoapsEspIdf, ShouldReturnInvalidArgumentWhenPayloadIsNull)
{
	g_ctx.conf.token = "test-token";

	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(NULL, 1u, &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnInvalidArgumentWhenDataSizeIsZero)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";

	CHECK_EQUAL(-EINVAL,
			pulse_transport_transmit(payload, 0u, &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnOverflowWhenPayloadExceedsIntMax)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";

	CHECK_EQUAL(-EOVERFLOW,
			pulse_transport_transmit(payload,
					(size_t)INT_MAX + 1u, &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnInProgressWhenAsyncAndNotYetDone)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	g_ctx.conf.async_transport = true;
	coap_mock_suppress_response();

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldResumeSessionOnSecondCallWhenAsyncInProgress)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	g_ctx.conf.async_transport = true;
	coap_mock_suppress_response();

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldCompleteAsyncSessionWhenResponseArrivesOnSubsequentCall)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	g_ctx.conf.async_transport = true;
	coap_mock_suppress_response();

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));

	coap_mock_allow_response();

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnInvalidArgumentWhenTokenIsMissing)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EINVAL,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnIoErrorWhenSha256Fails)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	psa_crypto_mock_set_result(PSA_ERROR_GENERIC_ERROR);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnNoMemoryWhenContextCreationFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	coap_mock_set_context_result(NULL);

	CHECK_EQUAL(-ENOMEM,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldReturnHostUnreachableWhenAddressResolutionFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	coap_mock_set_resolve_result(0);

	CHECK_EQUAL(-EHOSTUNREACH,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnIoErrorWhenSessionCreationFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	coap_mock_set_session_result(NULL);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldConfigureDtlsSessionWithDerivedIdentityAndRawTokenKey)
{
	static const uint8_t payload[] = { 0xaa, 0xbb, 0xcc };
	static const uint8_t response[] = { 0x01, 0x02, 0x03 };

	g_ctx.conf.token = "token-123";
	g_ctx.conf.transmit_timeout_ms = 5000u;
	g_ctx.on_response = on_response;
	coap_mock_set_response_payload(response, sizeof(response), 0u,
			sizeof(response));

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));

	STRCMP_EQUAL("ingest.libmcu.org", coap_mock_last_client_sni());
	STRCMP_EQUAL("000102030405060708090a0b0c0d0e0f",
			coap_mock_last_psk_identity());
	STRCMP_EQUAL("token-123", coap_mock_last_psk_key());
	STRCMP_EQUAL("v1", coap_mock_last_uri_path());
	CHECK_EQUAL(COAP_MEDIATYPE_APPLICATION_CBOR,
			(int)coap_mock_last_content_format());
	CHECK_EQUAL(5000, (int)coap_mock_last_timeout_ms());
	CHECK_EQUAL((int)sizeof(payload), (int)coap_mock_last_payload_len());
	MEMCMP_EQUAL(payload, coap_mock_last_payload_data(), sizeof(payload));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldPassRawTokenBytesAsSha256InputForIdentityDerivation)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "token-123";

	pulse_transport_transmit(payload, sizeof(payload), &g_ctx);

	CHECK_EQUAL(strlen("token-123"),
			(int)psa_crypto_mock_last_input_len());
	MEMCMP_EQUAL("token-123", psa_crypto_mock_last_input(),
			strlen("token-123"));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldUseDefaultTimeoutWhenTransmitTimeoutNotConfigured)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";

	pulse_transport_transmit(payload, sizeof(payload), &g_ctx);

	CHECK_EQUAL(15000, (int)coap_mock_last_timeout_ms());
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnTimeoutOnNackTooManyRetries)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	coap_mock_set_send_recv_result(-5);

	CHECK_EQUAL(-ETIMEDOUT,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldReturnCancelledOnNackNotDeliverable)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	coap_mock_set_send_recv_result(-6);

	CHECK_EQUAL(-ECANCELED,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldReturnIoErrorWhenResponseCodeIsNotChanged)
{
	static const uint8_t payload[] = { 0x01 };
	static const char error_body[] = "invalid_token";

	g_ctx.conf.token = "test-token";
	coap_mock_set_response_code((coap_pdu_code_t)129); /* 4.01 Unauthorized */
	coap_mock_set_response_payload(error_body, strlen(error_body), 0u,
			strlen(error_body));

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldReturnMessageSizeWhenResponseBodyExceedsBuffer)
{
	static const uint8_t payload[] = { 0x01 };
	static uint8_t large_response[5000];

	memset(large_response, 0x5a, sizeof(large_response));
	g_ctx.conf.token = "test-token";
	coap_mock_set_response_payload(large_response, sizeof(large_response),
			0u, sizeof(large_response));

	CHECK_EQUAL(-EMSGSIZE,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsEspIdf, ShouldDeliverResponseBodyToRegisteredHandler)
{
	static const uint8_t payload[] = { 0x01 };
	static const uint8_t response[] = { 0xA1, 0x01, 0x02 };

	g_ctx.conf.token = "test-token";
	g_ctx.on_response = on_response;
	coap_mock_set_response_payload(response, sizeof(response), 0u,
			sizeof(response));

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
	CHECK_EQUAL((int)sizeof(response), (int)g_response_len);
	MEMCMP_EQUAL(response, g_response_buf, sizeof(response));
}

TEST(PulseTransportCoapsEspIdf,
		ShouldNotCallResponseHandlerWhenResponseBodyIsEmpty)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";
	g_ctx.on_response = on_response;

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
	CHECK_EQUAL(0u, g_response_len);
}
