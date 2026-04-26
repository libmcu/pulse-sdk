/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"

#define PULSE_HTTPS_TIMEOUT_MS		60000U
#define PULSE_HTTPS_BUFFER_SIZE		4096U
#define PULSE_HTTPS_CONTENT_TYPE	"application/cbor"
#define PULSE_HTTPS_PORT		"443"
#define PULSE_HTTPS_AUTH_HEADER_SIZE	256U
#define PULSE_HTTPS_MAX_HEADERS		3U

typedef struct {
	uint8_t data[PULSE_HTTPS_BUFFER_SIZE];
	size_t len;
	bool truncated;
	bool got_response;
	uint16_t status_code;
	uint8_t recv_buf[PULSE_HTTPS_BUFFER_SIZE];
} response_buffer_t;

typedef struct {
	char auth_header[PULSE_HTTPS_AUTH_HEADER_SIZE];
	const char *fields[PULSE_HTTPS_MAX_HEADERS];
} request_headers_t;

typedef struct {
	response_buffer_t response;
	request_headers_t headers;
	int sock;
} https_session_t;

static https_session_t m_session;

static uint32_t get_transmit_timeout_ms(const struct pulse *conf)
{
	if (conf != NULL && conf->transmit_timeout_ms > 0u) {
		return conf->transmit_timeout_ms;
	}

	return PULSE_HTTPS_TIMEOUT_MS;
}

static int get_http_timeout_ms(const struct pulse *conf, int32_t *timeout_ms)
{
	const uint32_t timeout = get_transmit_timeout_ms(conf);

	if (timeout > (uint32_t)INT32_MAX) {
		return -EOVERFLOW;
	}

	*timeout_ms = (int32_t)timeout;

	return 0;
}

static int map_http_status_code(uint16_t status_code)
{
	if (status_code >= 200u && status_code < 300u) {
		return 0;
	}

	return -EIO;
}

static void reset_session(https_session_t *s)
{
	memset(s, 0, sizeof(*s));
	s->sock = -1;
}

static void close_socket_if_open(https_session_t *s)
{
	if (s->sock >= 0) {
		zsock_close(s->sock);
		s->sock = -1;
	}
}

static int configure_socket_tls(int sock)
{
	int ret;
#if defined(CONFIG_MBEDTLS_BUILTIN_TRUSTED_CERTS)
	const int verify = TLS_PEER_VERIFY_REQUIRED;
#else
	const int verify = TLS_PEER_VERIFY_NONE;
#endif

	ret = zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY,
			&verify, sizeof(verify));
	if (ret < 0) {
		return -errno;
	}

	ret = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME,
			PULSE_INGEST_HOST, strlen(PULSE_INGEST_HOST));
	if (ret < 0) {
		return -errno;
	}

	return 0;
}

static int configure_socket_timeout(int sock, int32_t timeout_ms)
{
	const uint32_t timeout_u32 = (uint32_t)timeout_ms;
	const struct timeval tv = {
		.tv_sec = (time_t)(timeout_u32 / 1000u),
		.tv_usec = (suseconds_t)((timeout_u32 % 1000u) * 1000u),
	};
	int ret = zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if (ret < 0) {
		return -errno;
	}

	ret = zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (ret < 0) {
		return -errno;
	}

	return 0;
}

static int connect_socket(int32_t timeout_ms)
{
	struct zsock_addrinfo hints;
	struct zsock_addrinfo *addresses = NULL;
	struct zsock_addrinfo *addr;
	int sock = -1;
	int ret = -EHOSTUNREACH;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TLS_1_2;
	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		hints.ai_family = AF_INET;
	} else if (IS_ENABLED(CONFIG_NET_IPV6)) {
		hints.ai_family = AF_INET6;
	} else {
		hints.ai_family = AF_UNSPEC;
	}

	ret = zsock_getaddrinfo(PULSE_INGEST_HOST, PULSE_HTTPS_PORT,
			&hints, &addresses);
	if (ret != 0) {
		return -EHOSTUNREACH;
	}

	for (addr = addresses; addr != NULL; addr = addr->ai_next) {
		sock = zsock_socket(addr->ai_family, addr->ai_socktype,
				IPPROTO_TLS_1_2);
		if (sock < 0) {
			ret = -errno;
			continue;
		}

		ret = configure_socket_tls(sock);
		if (ret < 0) {
			zsock_close(sock);
			sock = -1;
			continue;
		}

		ret = configure_socket_timeout(sock, timeout_ms);
		if (ret < 0) {
			zsock_close(sock);
			sock = -1;
			continue;
		}

		if (zsock_connect(sock, addr->ai_addr, addr->ai_addrlen) == 0) {
			ret = 0;
			break;
		}

		ret = -errno;
		zsock_close(sock);
		sock = -1;
	}

	zsock_freeaddrinfo(addresses);

	if (ret < 0) {
		return ret;
	}

	return sock;
}

static void init_request_headers(request_headers_t *headers)
{
	memset(headers, 0, sizeof(*headers));
	headers->fields[0] = "Content-Type: " PULSE_HTTPS_CONTENT_TYPE "\r\n";
}

static int build_auth_header(request_headers_t *headers, const struct pulse *conf)
{
	if (conf == NULL || conf->token == NULL) {
		headers->fields[1] = NULL;
		headers->fields[2] = NULL;
		return 0;
	}

	const int n = snprintf(headers->auth_header, sizeof(headers->auth_header),
			"Authorization: Bearer %s\r\n", conf->token);
	if (n <= 0 || (size_t)n >= sizeof(headers->auth_header)) {
		return -EOVERFLOW;
	}

	headers->fields[1] = headers->auth_header;
	headers->fields[2] = NULL;

	return 0;
}

static int on_http_response(struct http_response *rsp,
		enum http_final_call final_data, void *user_data)
{
	response_buffer_t *response = (response_buffer_t *)user_data;

	(void)final_data;

	response->got_response = true;
	response->status_code = rsp->http_status_code;

	if (rsp->body_frag_len == 0u) {
		return 0;
	}

	const size_t remaining = sizeof(response->data) - response->len;
	const size_t to_copy = rsp->body_frag_len < remaining
		? rsp->body_frag_len : remaining;

	if (rsp->body_frag_len > remaining) {
		response->truncated = true;
	}

	memcpy(response->data + response->len, rsp->body_frag_start, to_copy);
	response->len += to_copy;

	if (response->truncated) {
		return -EMSGSIZE;
	}

	return 0;
}

static void init_request(struct http_request *req, https_session_t *s,
		const void *data, size_t datasize)
{
	memset(req, 0, sizeof(*req));
	req->method = HTTP_POST;
	req->url = PULSE_INGEST_PATH;
	req->protocol = "HTTP/1.1";
	req->host = PULSE_INGEST_HOST;
	req->port = PULSE_HTTPS_PORT;
	req->header_fields = s->headers.fields;
	req->payload = (const char *)data;
	req->payload_len = datasize;
	req->response = on_http_response;
	req->recv_buf = s->response.recv_buf;
	req->recv_buf_len = sizeof(s->response.recv_buf);
}

static void deliver_response(const response_buffer_t *response,
		const struct pulse_report_ctx *ctx)
{
	if (response->len > 0u && ctx != NULL && ctx->on_response != NULL) {
		ctx->on_response(response->data, response->len, ctx->response_ctx);
	}
}

void pulse_transport_cancel(void)
{
}

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	struct http_request req;
	const struct pulse *conf = ctx != NULL ? &ctx->conf : NULL;
	int32_t timeout_ms;
	int ret;

	if (data == NULL || datasize == 0u) {
		return -EINVAL;
	}

	if (datasize > (size_t)INT32_MAX) {
		return -EOVERFLOW;
	}

	if (conf != NULL && conf->async_transport) {
		return -ENOTSUP;
	}

	reset_session(&m_session);
	ret = get_http_timeout_ms(conf, &timeout_ms);
	if (ret < 0) {
		return ret;
	}

	init_request_headers(&m_session.headers);
	ret = build_auth_header(&m_session.headers, conf);
	if (ret < 0) {
		reset_session(&m_session);
		return ret;
	}

	m_session.sock = connect_socket(timeout_ms);
	if (m_session.sock < 0) {
		ret = m_session.sock;
		reset_session(&m_session);
		return ret;
	}

	init_request(&req, &m_session, data, datasize);
	ret = http_client_req(m_session.sock, &req, timeout_ms, &m_session.response);
	close_socket_if_open(&m_session);

	if (m_session.response.truncated) {
		reset_session(&m_session);
		return -EMSGSIZE;
	}

	if (ret < 0) {
		reset_session(&m_session);
		return ret;
	}

	if (!m_session.response.got_response) {
		reset_session(&m_session);
		return -EIO;
	}

	ret = map_http_status_code(m_session.response.status_code);
	if (ret < 0) {
		reset_session(&m_session);
		return ret;
	}

	deliver_response(&m_session.response, ctx);
	reset_session(&m_session);
	return 0;
}
