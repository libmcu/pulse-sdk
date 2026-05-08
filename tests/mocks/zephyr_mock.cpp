/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "CppUTestExt/MockSupport.h"
#include <errno.h>
#include <string.h>

extern "C" {
#include "zephyr/kernel.h"
#include "zephyr/net/socket.h"
#include "zephyr/net/http/client.h"
#include "zephyr/net/tls_credentials.h"
}

static struct sockaddr g_fake_addr[2];
static struct zsock_addrinfo g_fake_addrinfo[2];
static int64_t g_uptime_ms;

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

static struct {
	uint8_t recv_buf[8192];
	size_t recv_len;
	int recv_pending;
	int send_error;
	int recv_error;
	int poll_result;
	int poll_revents;
} g_socket_state;

static struct {
	int add_result[8];
	int delete_result[8];
	sec_tag_t last_psk_tag;
	sec_tag_t last_psk_id_tag;
	uint8_t last_psk[64];
	size_t last_psk_len;
	char last_psk_id[64];
	size_t last_psk_id_len;
} g_tls_state;

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
	memset(&g_socket_state, 0, sizeof(g_socket_state));
	g_socket_state.poll_revents = -1;
	memset(&g_tls_state, 0, sizeof(g_tls_state));
	memset(&g_fake_addr, 0, sizeof(g_fake_addr));
	memset(&g_fake_addrinfo, 0, sizeof(g_fake_addrinfo));
	g_fake_addrinfo[0].ai_family = AF_UNSPEC;
	g_fake_addrinfo[0].ai_socktype = SOCK_STREAM;
	g_fake_addrinfo[0].ai_addrlen = (unsigned int)sizeof(g_fake_addr[0]);
	g_fake_addrinfo[0].ai_addr = &g_fake_addr[0];
}

extern "C" void zephyr_uptime_mock_reset(void)
{
	g_uptime_ms = 0;
}

extern "C" void zephyr_uptime_mock_set_ms(int64_t time_ms)
{
	g_uptime_ms = time_ms;
}

extern "C" int64_t k_uptime_get(void)
{
	return g_uptime_ms;
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

void zephyr_socket_mock_set_send_error(int err)
{
	g_socket_state.send_error = err;
}

void zephyr_socket_mock_set_recv_error(int err)
{
	g_socket_state.recv_error = err;
}

void zephyr_socket_mock_set_poll_result(int result)
{
	g_socket_state.poll_result = result;
}

void zephyr_socket_mock_set_poll_revents(int revents)
{
	g_socket_state.poll_revents = revents;
}

void zephyr_socket_mock_set_recv_data(const void *data, size_t len)
{
	if (len > sizeof(g_socket_state.recv_buf)) {
		len = sizeof(g_socket_state.recv_buf);
	}

	if (data != NULL && len > 0u) {
		memcpy(g_socket_state.recv_buf, data, len);
	}

	g_socket_state.recv_len = len;
	g_socket_state.recv_pending = 1;
}

void zephyr_socket_mock_set_recv_pending(int pending)
{
	g_socket_state.recv_pending = pending;
}

void zephyr_tls_mock_reset(void)
{
	memset(&g_tls_state, 0, sizeof(g_tls_state));
}

void zephyr_tls_mock_set_add_result(enum tls_credential_type type, int result)
{
	if ((unsigned int)type < (sizeof(g_tls_state.add_result) /
					sizeof(g_tls_state.add_result[0]))) {
		g_tls_state.add_result[type] = result;
	}
}

void zephyr_tls_mock_set_delete_result(enum tls_credential_type type, int result)
{
	if ((unsigned int)type < (sizeof(g_tls_state.delete_result) /
					sizeof(g_tls_state.delete_result[0]))) {
		g_tls_state.delete_result[type] = result;
	}
}

sec_tag_t zephyr_tls_mock_last_psk_tag(void)
{
	return g_tls_state.last_psk_tag;
}

sec_tag_t zephyr_tls_mock_last_psk_id_tag(void)
{
	return g_tls_state.last_psk_id_tag;
}

const uint8_t *zephyr_tls_mock_last_psk(void)
{
	return g_tls_state.last_psk;
}

size_t zephyr_tls_mock_last_psk_len(void)
{
	return g_tls_state.last_psk_len;
}

const char *zephyr_tls_mock_last_psk_id(void)
{
	return g_tls_state.last_psk_id;
}

size_t zephyr_tls_mock_last_psk_id_len(void)
{
	return g_tls_state.last_psk_id_len;
}

extern "C" int tls_credential_add(sec_tag_t tag, enum tls_credential_type type,
		const void *cred, size_t credlen)
{
	mock().actualCall("tls_credential_add")
		.withParameter("type", type);

	if ((unsigned int)type < (sizeof(g_tls_state.add_result) /
					sizeof(g_tls_state.add_result[0]))
			&& g_tls_state.add_result[type] != 0) {
		return g_tls_state.add_result[type];
	}

	if (type == TLS_CREDENTIAL_PSK) {
		g_tls_state.last_psk_tag = tag;
		g_tls_state.last_psk_len = credlen < sizeof(g_tls_state.last_psk)
			? credlen : sizeof(g_tls_state.last_psk);
		if (cred != NULL && g_tls_state.last_psk_len > 0u) {
			memcpy(g_tls_state.last_psk, cred, g_tls_state.last_psk_len);
		}
	} else if (type == TLS_CREDENTIAL_PSK_ID) {
		g_tls_state.last_psk_id_tag = tag;
		g_tls_state.last_psk_id_len = credlen < sizeof(g_tls_state.last_psk_id) - 1u
			? credlen : sizeof(g_tls_state.last_psk_id) - 1u;
		if (cred != NULL && g_tls_state.last_psk_id_len > 0u) {
			memcpy(g_tls_state.last_psk_id, cred, g_tls_state.last_psk_id_len);
		}
		g_tls_state.last_psk_id[g_tls_state.last_psk_id_len] = '\0';
	}

	return 0;
}

extern "C" int tls_credential_delete(sec_tag_t tag, enum tls_credential_type type)
{
	(void)tag;
	mock().actualCall("tls_credential_delete")
		.withParameter("type", type);

	if ((unsigned int)type < (sizeof(g_tls_state.delete_result) /
					sizeof(g_tls_state.delete_result[0]))
			&& g_tls_state.delete_result[type] != 0) {
		return g_tls_state.delete_result[type];
	}

	return 0;
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

extern "C" int zsock_send(int sock, const void *buf, size_t len, int flags)
{
	(void)sock;
	(void)buf;
	(void)flags;
	mock().actualCall("zsock_send");

	if (g_socket_state.send_error != 0) {
		errno = -g_socket_state.send_error;
		return -1;
	}

	return (int)len;
}

extern "C" int zsock_recv(int sock, void *buf, size_t max_len, int flags)
{
	(void)sock;
	(void)flags;
	mock().actualCall("zsock_recv");

	if (g_socket_state.recv_error != 0) {
		errno = g_socket_state.recv_error;
		return -1;
	}

	if (!g_socket_state.recv_pending) {
		errno = EAGAIN;
		return -1;
	}

	size_t copy_len = g_socket_state.recv_len < max_len
		? g_socket_state.recv_len : max_len;
	if (copy_len > 0u) {
		memcpy(buf, g_socket_state.recv_buf, copy_len);
	}
	g_socket_state.recv_pending = 0;

	return (int)copy_len;
}

extern "C" int zsock_poll(struct zsock_pollfd *fds, int nfds, int timeout)
{
	(void)timeout;
	mock().actualCall("zsock_poll");

	if (g_socket_state.poll_result < 0) {
		errno = EIO;
		return -1;
	}

	if (g_socket_state.poll_result > 0) {
		for (int i = 0; i < nfds; i++) {
			fds[i].revents = (short)(g_socket_state.poll_revents >= 0
					? g_socket_state.poll_revents : ZSOCK_POLLIN);
		}
		return g_socket_state.poll_result;
	}

	for (int i = 0; i < nfds; i++) {
		fds[i].revents = g_socket_state.recv_pending ? ZSOCK_POLLIN : 0;
		if (g_socket_state.poll_revents >= 0) {
			fds[i].revents = (short)g_socket_state.poll_revents;
		}
	}

	return g_socket_state.recv_pending ? 1 : 0;
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
