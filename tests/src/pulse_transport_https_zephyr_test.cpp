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
#include <string.h>
#include <stdint.h>

#include "zephyr/net/socket.h"
#include "zephyr/net/http/client.h"

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
}

static struct pulse_report_ctx g_ctx;

#define CHECK_TRANSMIT_EQ(expected, payload, datasize, ctx) \
	do { \
		const int actual = pulse_transport_transmit(payload, datasize, ctx); \
		CHECK_EQUAL(expected, actual); \
	} while (0)

#define CHECK_TRANSMIT_NEGATIVE(payload, datasize, ctx) \
	do { \
		const int actual = pulse_transport_transmit(payload, datasize, ctx); \
		CHECK_TRUE(actual < 0); \
	} while (0)

static void expect_connect_sequence(int sock_fd)
{
	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectOneCall("zsock_socket")
		.andReturnValue(sock_fd);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_PEER_VERIFY)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_HOSTNAME)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_SOCKET)
		.withParameter("optname", SO_RCVTIMEO)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_SOCKET)
		.withParameter("optname", SO_SNDTIMEO)
		.andReturnValue(0);
	mock().expectOneCall("zsock_connect").andReturnValue(0);
}

static void expect_happy_path(int status_code)
{
	expect_connect_sequence(42);
	zephyr_http_mock_set_response((uint16_t)status_code, NULL, 0u);
	mock().expectOneCall("http_client_req").andReturnValue(0);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");
}

TEST_GROUP(PulseTransportHttpsZephyr)
{
	void setup()
	{
		memset(&g_ctx, 0, sizeof(g_ctx));
		zephyr_http_mock_reset();
		zephyr_socket_mock_set_addrinfo_count(1u);
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

TEST(PulseTransportHttpsZephyr, ShouldReturnInvalidArgumentWhenPayloadIsNull)
{
	CHECK_TRANSMIT_EQ(-EINVAL, NULL, 1u, &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnInvalidArgumentWhenDataSizeIsZero)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_TRANSMIT_EQ(-EINVAL, payload, 0u, &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnOverflowWhenDataSizeExceedsInt32Max)
{
	static const uint8_t payload[] = { 0x01 };

	CHECK_TRANSMIT_EQ(-EOVERFLOW, payload, (size_t)INT32_MAX + 1u, &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnNotSupportedWhenAsyncTransportRequested)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.async_transport = true;

	CHECK_TRANSMIT_EQ(-ENOTSUP, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnOverflowWhenTokenTooLong)
{
	static const uint8_t payload[] = { 0x01 };
	static char long_token[250];

	memset(long_token, 'x', sizeof(long_token) - 1u);
	long_token[sizeof(long_token) - 1u] = '\0';
	g_ctx.conf.token = long_token;

	CHECK_TRANSMIT_EQ(-EOVERFLOW, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnHostUnreachableWhenDnsResolutionFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(-1);

	CHECK_TRANSMIT_EQ(-EHOSTUNREACH, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnErrorWhenSocketCreationFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectOneCall("zsock_socket").andReturnValue(-1);
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_NEGATIVE(payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnErrorWhenTlsPeerVerifyConfigFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectOneCall("zsock_socket").andReturnValue(42);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_PEER_VERIFY)
		.andReturnValue(-1);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_NEGATIVE(payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnErrorWhenTlsHostnameConfigFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectOneCall("zsock_socket").andReturnValue(42);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_PEER_VERIFY)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_HOSTNAME)
		.andReturnValue(-1);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_NEGATIVE(payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnErrorWhenSocketReceiveTimeoutConfigFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectOneCall("zsock_socket").andReturnValue(42);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_PEER_VERIFY)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_HOSTNAME)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_SOCKET)
		.withParameter("optname", SO_RCVTIMEO)
		.andReturnValue(-1);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_NEGATIVE(payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnErrorWhenSocketSendTimeoutConfigFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectOneCall("zsock_socket").andReturnValue(42);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_PEER_VERIFY)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_HOSTNAME)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_SOCKET)
		.withParameter("optname", SO_RCVTIMEO)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_SOCKET)
		.withParameter("optname", SO_SNDTIMEO)
		.andReturnValue(-1);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_NEGATIVE(payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnErrorWhenConnectFails)
{
	static const uint8_t payload[] = { 0x01 };

	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectOneCall("zsock_socket").andReturnValue(42);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_PEER_VERIFY)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.withParameter("optname", TLS_HOSTNAME)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_SOCKET)
		.withParameter("optname", SO_RCVTIMEO)
		.andReturnValue(0);
	mock().expectOneCall("zsock_setsockopt")
		.withParameter("level", SOL_SOCKET)
		.withParameter("optname", SO_SNDTIMEO)
		.andReturnValue(0);
	mock().expectOneCall("zsock_connect").andReturnValue(-1);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_NEGATIVE(payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldRetryNextAddressWhenFirstConnectFails)
{
	static const uint8_t payload[] = { 0x01 };

	zephyr_socket_mock_set_addrinfo_count(2u);
	mock().expectOneCall("zsock_getaddrinfo")
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectNCalls(2, "zsock_socket").andReturnValue(42);
	mock().expectNCalls(4, "zsock_setsockopt")
		.withParameter("level", SOL_TLS)
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectNCalls(4, "zsock_setsockopt")
		.withParameter("level", SOL_SOCKET)
		.ignoreOtherParameters()
		.andReturnValue(0);
	mock().expectOneCall("zsock_connect").andReturnValue(-1);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_connect").andReturnValue(0);
	zephyr_http_mock_set_response(200u, NULL, 0u);
	mock().expectOneCall("http_client_req").andReturnValue(0);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnErrorWhenHttpRequestFails)
{
	static const uint8_t payload[] = { 0x01 };

	expect_connect_sequence(42);
	mock().expectOneCall("http_client_req").andReturnValue(-EIO);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_NEGATIVE(payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnIoWhenNoResponseReceived)
{
	static const uint8_t payload[] = { 0x01 };

	expect_connect_sequence(42);
	mock().expectOneCall("http_client_req").andReturnValue(0);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_EQ(-EIO, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnIoWhenStatusCodeIsBelowSuccessRange)
{
	static const uint8_t payload[] = { 0x01 };

	expect_happy_path(199);

	CHECK_TRANSMIT_EQ(-EIO, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnIoWhenStatusCodeIsAboveSuccessRange)
{
	static const uint8_t payload[] = { 0x01 };

	expect_happy_path(300);

	CHECK_TRANSMIT_EQ(-EIO, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnOkWhenStatusCodeIs200)
{
	static const uint8_t payload[] = { 0x01 };

	expect_happy_path(200);

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnOkWhenStatusCodeIs299)
{
	static const uint8_t payload[] = { 0x01 };

	expect_happy_path(299);

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldReturnMessageTooLargeWhenResponseBodyOverflowsBuffer)
{
	static const uint8_t payload[] = { 0x01 };

	expect_connect_sequence(42);
	zephyr_http_mock_inject_overflow();
	mock().expectOneCall("http_client_req").andReturnValue(0);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_EQ(-EMSGSIZE, payload, sizeof(payload), &g_ctx);
}

static void test_response_handler(const void *data, size_t datasize, void *ctx)
{
	mock().actualCall("pulse_response_handler")
		.withMemoryBufferParameter("data",
				(const unsigned char *)data, datasize)
		.withParameter("datasize", datasize)
		.withPointerParameter("ctx", ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldForwardResponseBodyToRegisteredHandler)
{
	static const uint8_t payload[] = { 0x01 };
	static const char body[] = "ok";
	int response_ctx = 0;

	g_ctx.on_response = test_response_handler;
	g_ctx.response_ctx = &response_ctx;

	expect_connect_sequence(42);
	zephyr_http_mock_set_response(200u, body, sizeof(body) - 1u);
	mock().expectOneCall("http_client_req").andReturnValue(0);
	mock().expectOneCall("pulse_response_handler")
		.withMemoryBufferParameter("data",
				(const unsigned char *)body, sizeof(body) - 1u)
		.withParameter("datasize", sizeof(body) - 1u)
		.withPointerParameter("ctx", &response_ctx);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldAccumulateResponseAcrossMultipleFragments)
{
	static const uint8_t payload[] = { 0x01 };
	static const char body1[] = "he";
	static const char body2[] = "llo";
	static const char expected[] = "hello";
	int response_ctx = 0;

	g_ctx.on_response = test_response_handler;
	g_ctx.response_ctx = &response_ctx;

	expect_connect_sequence(42);
	zephyr_http_mock_set_fragmented_response(200u,
			body1, sizeof(body1) - 1u,
			body2, sizeof(body2) - 1u);
	mock().expectOneCall("http_client_req").andReturnValue(0);
	mock().expectOneCall("pulse_response_handler")
		.withMemoryBufferParameter("data",
				(const unsigned char *)expected, sizeof(expected) - 1u)
		.withParameter("datasize", sizeof(expected) - 1u)
		.withPointerParameter("ctx", &response_ctx);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldNotCallResponseHandlerWhenResponseBodyIsEmpty)
{
	static const uint8_t payload[] = { 0x01 };
	int response_ctx = 0;

	g_ctx.on_response = test_response_handler;
	g_ctx.response_ctx = &response_ctx;

	expect_happy_path(200);

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldSendAuthorizationHeaderWhenTokenProvided)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.token = "test-token";

	expect_happy_path(202);

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldUseConfiguredTransmitTimeout)
{
	static const uint8_t payload[] = { 0x01 };

	g_ctx.conf.transmit_timeout_ms = 1000u;

	expect_happy_path(200);

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), &g_ctx);
}

TEST(PulseTransportHttpsZephyr, ShouldSucceedWithDefaultsWhenReportContextIsNull)
{
	static const uint8_t payload[] = { 0x01 };

	expect_connect_sequence(42);
	zephyr_http_mock_set_response(200u, NULL, 0u);
	mock().expectOneCall("http_client_req").andReturnValue(0);
	mock().expectOneCall("zsock_close");
	mock().expectOneCall("zsock_freeaddrinfo");

	CHECK_TRANSMIT_EQ(0, payload, sizeof(payload), NULL);
}
