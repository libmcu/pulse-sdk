/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "CppUTestExt/MockSupport.h"
#include <errno.h>
#include <string.h>

extern "C" {
#include "zephyr/net/socket.h"
#include "zephyr/net/http/client.h"
}

static struct sockaddr g_fake_addr[2];
static struct zsock_addrinfo g_fake_addrinfo[2];

static struct {
	http_response_cb cb;
	void *user_data;
	struct {
		uint8_t buf[4096];
		size_t len;
	} fragments[2];
	size_t fragment_count;
	uint16_t status_code;
	bool pending;
	bool overflow;
} g_response;

void zephyr_http_mock_set_response(uint16_t status_code,
		const void *body, size_t body_len)
{
	g_response.status_code = status_code;
	g_response.fragment_count = 0u;
	g_response.overflow = false;
	if (body != NULL && body_len > 0u
			&& body_len <= sizeof(g_response.fragments[0].buf)) {
		memcpy(g_response.fragments[0].buf, body, body_len);
		g_response.fragments[0].len = body_len;
		g_response.fragment_count = 1u;
	}
	g_response.pending = true;
}

void zephyr_http_mock_set_fragmented_response(uint16_t status_code,
		const void *body1, size_t body1_len,
		const void *body2, size_t body2_len)
{
	g_response.status_code = status_code;
	g_response.fragment_count = 0u;
	g_response.overflow = false;

	if (body1 != NULL && body1_len > 0u
			&& body1_len <= sizeof(g_response.fragments[0].buf)) {
		memcpy(g_response.fragments[0].buf, body1, body1_len);
		g_response.fragments[0].len = body1_len;
		g_response.fragment_count++;
	}

	if (body2 != NULL && body2_len > 0u
			&& body2_len <= sizeof(g_response.fragments[1].buf)) {
		memcpy(g_response.fragments[1].buf, body2, body2_len);
		g_response.fragments[1].len = body2_len;
		g_response.fragment_count++;
	}

	g_response.pending = true;
}

void zephyr_http_mock_inject_overflow(void)
{
	memset(g_response.fragments[0].buf, 0xAB, sizeof(g_response.fragments[0].buf));
	g_response.status_code = 200u;
	g_response.fragments[0].len = sizeof(g_response.fragments[0].buf);
	g_response.fragment_count = 1u;
	g_response.overflow = true;
	g_response.pending = true;
}

void zephyr_http_mock_reset(void)
{
	memset(&g_response, 0, sizeof(g_response));
	memset(&g_fake_addr, 0, sizeof(g_fake_addr));
	memset(&g_fake_addrinfo, 0, sizeof(g_fake_addrinfo));
	g_fake_addrinfo[0].ai_family = AF_UNSPEC;
	g_fake_addrinfo[0].ai_socktype = SOCK_STREAM;
	g_fake_addrinfo[0].ai_addrlen = (unsigned int)sizeof(g_fake_addr[0]);
	g_fake_addrinfo[0].ai_addr = &g_fake_addr[0];
}

void zephyr_socket_mock_set_addrinfo_count(size_t count)
{
	if (count == 0u) {
		count = 1u;
	}

	if (count > 2u) {
		count = 2u;
	}

	for (size_t i = 0u; i < count; i++) {
		g_fake_addrinfo[i].ai_family = AF_UNSPEC;
		g_fake_addrinfo[i].ai_socktype = SOCK_STREAM;
		g_fake_addrinfo[i].ai_addrlen = (unsigned int)sizeof(g_fake_addr[i]);
		g_fake_addrinfo[i].ai_addr = &g_fake_addr[i];
		g_fake_addrinfo[i].ai_next = (i + 1u < count)
			? &g_fake_addrinfo[i + 1u] : NULL;
	}
}

extern "C" int zsock_getaddrinfo(const char *host, const char *service,
		const struct zsock_addrinfo *hints,
		struct zsock_addrinfo **res)
{
	(void)hints;

	int ret = mock().actualCall("zsock_getaddrinfo")
		.withStringParameter("host", host)
		.withStringParameter("service", service)
		.returnIntValue();

	if (ret == 0) {
		*res = &g_fake_addrinfo[0];
	}

	return ret;
}

extern "C" void zsock_freeaddrinfo(struct zsock_addrinfo *ai)
{
	(void)ai;
	mock().actualCall("zsock_freeaddrinfo");
}

extern "C" int zsock_socket(int family, int type, int proto)
{
	(void)family;
	(void)type;
	(void)proto;

	int ret = mock().actualCall("zsock_socket").returnIntValue();
	if (ret < 0) {
		errno = ENOMEM;
	}
	return ret;
}

extern "C" int zsock_setsockopt(int sock, int level, int optname,
		const void *optval, unsigned int optlen)
{
	(void)sock;
	(void)optval;
	(void)optlen;

	int ret = mock().actualCall("zsock_setsockopt")
		.withParameter("level", level)
		.withParameter("optname", optname)
		.returnIntValue();
	if (ret < 0) {
		errno = EIO;
	}
	return ret;
}

extern "C" int zsock_connect(int sock, const struct sockaddr *addr,
		unsigned int addrlen)
{
	(void)sock;
	(void)addr;
	(void)addrlen;

	int ret = mock().actualCall("zsock_connect").returnIntValue();
	if (ret < 0) {
		errno = ECONNREFUSED;
	}
	return ret;
}

extern "C" int zsock_close(int sock)
{
	(void)sock;
	mock().actualCall("zsock_close");
	return 0;
}

extern "C" int http_client_req(int sock, struct http_request *req,
		int32_t timeout, void *user_data)
{
	(void)sock;
	(void)timeout;

	g_response.cb = req->response;
	g_response.user_data = user_data;

	int ret = mock().actualCall("http_client_req").returnIntValue();

	if (ret == 0 && g_response.pending && g_response.cb != NULL) {
		if (g_response.fragment_count == 0u) {
			struct http_response rsp;
			memset(&rsp, 0, sizeof(rsp));
			rsp.http_status_code = g_response.status_code;
			g_response.cb(&rsp, HTTP_DATA_FINAL, g_response.user_data);
		} else {
			for (size_t i = 0u; i < g_response.fragment_count; i++) {
				struct http_response rsp;
				memset(&rsp, 0, sizeof(rsp));
				rsp.http_status_code = g_response.status_code;
				rsp.body_frag_start = g_response.fragments[i].buf;
				rsp.body_frag_len = (g_response.overflow && i == 0u)
					? sizeof(g_response.fragments[i].buf) + 1u
					: g_response.fragments[i].len;
				g_response.cb(&rsp,
						(i + 1u == g_response.fragment_count)
						? HTTP_DATA_FINAL : HTTP_DATA_MORE,
						g_response.user_data);
			}
		}
		g_response.pending = false;
	}

	return ret;
}
