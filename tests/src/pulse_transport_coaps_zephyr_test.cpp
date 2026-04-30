/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "psa/crypto.h"

#include "zephyr/net/coap.h"
#include "zephyr/net/http/client.h"
#include "zephyr/net/socket.h"
#include "zephyr/net/tls_credentials.h"

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
}

#define VALID_TOKEN		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define VALID_TOKEN_PSK_LEN	32u

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

TEST_GROUP(PulseTransportCoapsZephyr)
{
	void setup()
	{
		memset(&g_ctx, 0, sizeof(g_ctx));
		memset(g_response_buf, 0, sizeof(g_response_buf));
		g_response_len = 0u;
		zephyr_http_mock_reset();
		zephyr_socket_mock_set_addrinfo_count(1u);
		zephyr_tls_mock_reset();
		zephyr_coap_mock_reset();
		psa_crypto_mock_reset();
		mock().ignoreOtherCalls();
		pulse_transport_cancel();
		mock().clear();
		mock().ignoreOtherCalls();
	}

	void teardown()
	{
		mock().checkExpectations();
		mock().clear();
	}
};

TEST(PulseTransportCoapsZephyr, ShouldReturnInvalidArgumentWhenPayloadIsNull)
{
	g_ctx.conf.token = VALID_TOKEN;

	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(NULL, 1u, &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnInvalidArgumentWhenDataSizeIsZero)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;

	CHECK_EQUAL(-EINVAL, pulse_transport_transmit(payload, 0u, &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnOverflowWhenPayloadExceedsIntMax)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;

	CHECK_EQUAL(-EOVERFLOW,
			pulse_transport_transmit(payload, (size_t)INT_MAX + 1u,
					&g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnInvalidArgumentWhenTokenIsMissing)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_EQUAL(-EINVAL,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr,
		ShouldReturnInvalidArgumentWhenTokenIsNotValidBase64Url)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "not-a-valid-base64url-token";

	CHECK_EQUAL(-EINVAL,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnIoErrorWhenSha256Fails)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	psa_crypto_mock_set_result(PSA_ERROR_GENERIC_ERROR);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnIoErrorWhenPsaCryptoInitFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	psa_crypto_mock_set_init_result(PSA_ERROR_GENERIC_ERROR);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnHostUnreachableWhenDnsFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(-1);

	CHECK_EQUAL(-EHOSTUNREACH,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr,
		ShouldConfigureDtlsCredentialsAndBuildCoapRequestFromToken)
{
	static const uint8_t payload[] = { 0xaa, 0xbb, 0xcc };
	static const uint8_t response[] = { 0x01, 0x02, 0x03 };
	static const uint8_t expected_psk[VALID_TOKEN_PSK_LEN] = { 0 };
	static const uint8_t dummy_datagram[] = { 0x40 };

	g_ctx.conf.token = VALID_TOKEN;
	g_ctx.conf.transmit_timeout_ms = 5000u;
	g_ctx.on_response = on_response;
	zephyr_socket_mock_set_recv_data(dummy_datagram, sizeof(dummy_datagram));
	zephyr_coap_mock_set_response(COAP_RESPONSE_CODE_CHANGED,
			response, sizeof(response));

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
	CHECK_EQUAL((int)VALID_TOKEN_PSK_LEN,
			(int)zephyr_tls_mock_last_psk_len());
	MEMCMP_EQUAL(expected_psk, zephyr_tls_mock_last_psk(), VALID_TOKEN_PSK_LEN);
	STRCMP_EQUAL("000102030405060708090a0b0c0d0e0f",
			zephyr_tls_mock_last_psk_id());
	CHECK_EQUAL(42, (int)zephyr_tls_mock_last_psk_tag());
	CHECK_EQUAL(42, (int)zephyr_tls_mock_last_psk_id_tag());
	STRCMP_EQUAL("v1", zephyr_coap_mock_last_uri_path());
	CHECK_EQUAL(COAP_CONTENT_FORMAT_APP_CBOR,
			(int)zephyr_coap_mock_last_content_format());
	CHECK_EQUAL(COAP_METHOD_POST,
			(int)zephyr_coap_mock_last_request_code());
	CHECK_EQUAL((int)sizeof(payload),
			(int)zephyr_coap_mock_last_payload_len());
	MEMCMP_EQUAL(payload, zephyr_coap_mock_last_payload(), sizeof(payload));
	CHECK_EQUAL(strlen(VALID_TOKEN),
			(int)psa_crypto_mock_last_input_len());
	MEMCMP_EQUAL(VALID_TOKEN, psa_crypto_mock_last_input(),
			strlen(VALID_TOKEN));
	CHECK_EQUAL((int)sizeof(response), (int)g_response_len);
	MEMCMP_EQUAL(response, g_response_buf, sizeof(response));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnTimeoutWhenNoResponseArrives)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;

	CHECK_EQUAL(-ETIMEDOUT,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr,
		ShouldReturnOverflowWhenTransmitTimeoutTooLarge)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	g_ctx.conf.transmit_timeout_ms = (uint32_t)INT32_MAX + 1u;

	CHECK_EQUAL(-EOVERFLOW,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnIoWhenPollFails)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	zephyr_socket_mock_set_poll_result(-1);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnIoWhenPollReventsContainPollErr)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	zephyr_socket_mock_set_poll_result(1);
	zephyr_socket_mock_set_poll_revents(ZSOCK_POLLERR);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnConnectionResetWhenPollReventsContainPollHup)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	zephyr_socket_mock_set_poll_result(1);
	zephyr_socket_mock_set_poll_revents(ZSOCK_POLLHUP);

	CHECK_EQUAL(-ECONNRESET,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnIoWhenPollReventsContainPollNval)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	zephyr_socket_mock_set_poll_result(1);
	zephyr_socket_mock_set_poll_revents(ZSOCK_POLLNVAL);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnIoWhenResponseCodeIsFailure)
{
	static const uint8_t payload[] = { 0x01 };
	static const char response[] = "invalid_token";
	static const uint8_t dummy_datagram[] = { 0x40 };

	g_ctx.conf.token = VALID_TOKEN;
	zephyr_socket_mock_set_recv_data(dummy_datagram, sizeof(dummy_datagram));
	zephyr_coap_mock_set_response(128u, response, sizeof(response) - 1u);

	CHECK_EQUAL(-EIO,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr,
		ShouldReturnMessageSizeWhenResponseBodyExceedsBuffer)
{
	static const uint8_t payload[] = { 0x01 };
	static uint8_t response[5000];
	static const uint8_t dummy_datagram[] = { 0x40 };

	memset(response, 0xAB, sizeof(response));
	g_ctx.conf.token = VALID_TOKEN;
	zephyr_socket_mock_set_recv_data(dummy_datagram, sizeof(dummy_datagram));
	zephyr_coap_mock_set_response(COAP_RESPONSE_CODE_CHANGED,
			response, sizeof(response));

	CHECK_EQUAL(-EMSGSIZE,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr, ShouldReturnInProgressWhenAsyncAndNoResponseYet)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = VALID_TOKEN;
	g_ctx.conf.async_transport = true;

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}

TEST(PulseTransportCoapsZephyr,
		ShouldCompleteAsyncSessionWhenResponseArrivesOnNextCall)
{
	static const uint8_t payload[] = { 0x01 };
	static const uint8_t response[] = { 0x42, 0x24 };
	static const uint8_t dummy_datagram[] = { 0x40 };

	g_ctx.conf.token = VALID_TOKEN;
	g_ctx.conf.async_transport = true;
	g_ctx.on_response = on_response;

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));

	zephyr_socket_mock_set_recv_data(dummy_datagram, sizeof(dummy_datagram));
	zephyr_coap_mock_set_response(COAP_RESPONSE_CODE_CHANGED,
			response, sizeof(response));

	CHECK_EQUAL(0,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
	CHECK_EQUAL((int)sizeof(response), (int)g_response_len);
	MEMCMP_EQUAL(response, g_response_buf, sizeof(response));
}

TEST(PulseTransportCoapsZephyr, ShouldCancelAsyncSessionAndAllowRestart)
{
	static const uint8_t payload[] = { 0x01 };
	static const uint8_t dummy_datagram[] = { 0x40 };

	g_ctx.conf.token = VALID_TOKEN;
	g_ctx.conf.async_transport = true;

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
	pulse_transport_cancel();

	zephyr_socket_mock_set_recv_data(dummy_datagram, sizeof(dummy_datagram));
	zephyr_coap_mock_set_response(COAP_RESPONSE_CODE_CHANGED, NULL, 0u);

	CHECK_EQUAL(-EINPROGRESS,
			pulse_transport_transmit(payload, sizeof(payload), &g_ctx));
}
