/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <psa/crypto.h>

#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include "libmcu/base64.h"

#if !defined(PULSE_WARN)
#define PULSE_WARN(...)
#endif
#if !defined(PULSE_ERROR)
#define PULSE_ERROR(...)
#endif

#define PULSE_COAPS_TIMEOUT_MS_DEFAULT	15000u
#define PULSE_COAPS_RESPONSE_BUFSIZE	4096u
#define PULSE_COAPS_REQUEST_OVERHEAD	64u
#define PULSE_PSK_IDENTITY_HEX_LEN	32u
#define PULSE_PSK_IDENTITY_BUFSIZE	(PULSE_PSK_IDENTITY_HEX_LEN + 1u)
#define PULSE_PSK_KEY_LEN		32u
#define PULSE_DTLS_SEC_TAG		42
#define PULSE_COAP_CIPHERSUITE_COUNT	3u

typedef enum {
	STATE_IDLE = 0,
	STATE_AWAITING_RESPONSE,
} coaps_state_t;

typedef struct {
	uint8_t data[PULSE_COAPS_RESPONSE_BUFSIZE];
	size_t len;
	bool truncated;
} response_buf_t;

typedef struct {
	int sock;
	int64_t start_ms;
	coaps_state_t state;
	bool credentials_registered;
	const struct pulse_report_ctx *rctx;
	response_buf_t response;
	uint8_t recv_buf[PULSE_COAPS_RESPONSE_BUFSIZE + 16u];
} coaps_session_t;

static coaps_session_t m_session = {
	.sock = -1,
};

static int get_timeout_ms(const struct pulse *conf, int32_t *timeout_ms)
{
	if (conf != NULL && conf->transmit_timeout_ms > 0u) {
		if (conf->transmit_timeout_ms > (uint32_t)INT32_MAX) {
			return -EOVERFLOW;
		}

		*timeout_ms = (int32_t)conf->transmit_timeout_ms;
		return 0;
	}

	*timeout_ms = (int32_t)PULSE_COAPS_TIMEOUT_MS_DEFAULT;
	return 0;
}

static void reset_response(response_buf_t *buf)
{
	memset(buf, 0, sizeof(*buf));
}

static void close_socket_if_open(coaps_session_t *s)
{
	if (s->sock >= 0) {
		zsock_close(s->sock);
		s->sock = -1;
	}
}

static void cleanup_credentials(coaps_session_t *s)
{
	if (!s->credentials_registered) {
		return;
	}

	(void)tls_credential_delete(PULSE_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK);
	(void)tls_credential_delete(PULSE_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK_ID);
	s->credentials_registered = false;
}

static void cleanup_session(coaps_session_t *s)
{
	close_socket_if_open(s);
	cleanup_credentials(s);
	s->start_ms = 0;
	s->state = STATE_IDLE;
	s->rctx = NULL;
	reset_response(&s->response);
}

static bool has_session_timed_out(int64_t start_ms, int32_t timeout_ms)
{
	return (k_uptime_get() - start_ms) >= (int64_t)timeout_ms;
}

static int compute_psk_identity(char buf[static PULSE_PSK_IDENTITY_BUFSIZE],
		const char *token)
{
	static const char HEX[] = "0123456789abcdef";
	uint8_t digest[PULSE_PSK_KEY_LEN];
	size_t digest_len = 0u;
	size_t i;

	if (token == NULL) {
		return -EINVAL;
	}

	if (psa_crypto_init() != PSA_SUCCESS) {
		return -EIO;
	}

	if (psa_hash_compute(PSA_ALG_SHA_256,
			(const uint8_t *)token, strlen(token),
			digest, sizeof(digest), &digest_len) != PSA_SUCCESS) {
		return -EIO;
	}

	if (digest_len != sizeof(digest)) {
		return -EIO;
	}

	for (i = 0u; i < PULSE_PSK_IDENTITY_HEX_LEN / 2u; i++) {
		buf[i * 2u] = HEX[(digest[i] >> 4) & 0x0fu];
		buf[i * 2u + 1u] = HEX[digest[i] & 0x0fu];
	}
	buf[PULSE_PSK_IDENTITY_HEX_LEN] = '\0';

	return 0;
}

static int decode_psk(uint8_t key[static PULSE_PSK_KEY_LEN], const char *token)
{
	if (token == NULL) {
		return -EINVAL;
	}

	if (lm_base64url_decode(key, PULSE_PSK_KEY_LEN,
			token, strlen(token)) != PULSE_PSK_KEY_LEN) {
		return -EINVAL;
	}

	return 0;
}

static int register_psk_credentials(coaps_session_t *s, const char *identity,
		const uint8_t *psk, size_t psk_len)
{
	int ret;

	cleanup_credentials(s);

	ret = tls_credential_add(PULSE_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK_ID,
			identity, strlen(identity));
	if (ret < 0) {
		return ret;
	}

	ret = tls_credential_add(PULSE_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK,
			psk, psk_len);
	if (ret < 0) {
		(void)tls_credential_delete(PULSE_DTLS_SEC_TAG,
				TLS_CREDENTIAL_PSK_ID);
		return ret;
	}

	s->credentials_registered = true;

	return 0;
}

static int configure_socket_timeout(int sock, uint32_t timeout_ms)
{
	const struct timeval tv = {
		.tv_sec = (long)(timeout_ms / 1000u),
		.tv_usec = (int)((timeout_ms % 1000u) * 1000u),
	};
	int ret;

	ret = zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (ret < 0) {
		return -errno;
	}

	ret = zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (ret < 0) {
		return -errno;
	}

	return 0;
}

static int configure_dtls_socket(int sock)
{
	static const sec_tag_t sec_tags[] = { PULSE_DTLS_SEC_TAG };
	static const int ciphersuites[PULSE_COAP_CIPHERSUITE_COUNT] = {
		0x00A8,
		0xC0A4,
		0xC0A8,
	};
	const int verify = TLS_PEER_VERIFY_NONE;
	const int role = TLS_DTLS_ROLE_CLIENT;
	int ret;

	ret = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
			sec_tags, sizeof(sec_tags));
	if (ret < 0) {
		return -errno;
	}

	ret = zsock_setsockopt(sock, SOL_TLS, TLS_DTLS_ROLE,
			&role, sizeof(role));
	if (ret < 0) {
		return -errno;
	}

	ret = zsock_setsockopt(sock, SOL_TLS, TLS_CIPHERSUITE_LIST,
			ciphersuites, sizeof(ciphersuites));
	if (ret < 0) {
		return -errno;
	}

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

static int resolve_and_connect(int *sock, uint32_t timeout_ms)
{
	struct zsock_addrinfo hints;
	struct zsock_addrinfo *addresses = NULL;
	struct zsock_addrinfo *addr;
	int fd = -1;
	int ret = -EHOSTUNREACH;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_DTLS_1_2;
	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		hints.ai_family = AF_INET;
	} else if (IS_ENABLED(CONFIG_NET_IPV6)) {
		hints.ai_family = AF_INET6;
	} else {
		hints.ai_family = AF_UNSPEC;
	}

	if (zsock_getaddrinfo(PULSE_INGEST_HOST, "5684", &hints,
			&addresses) != 0) {
		return -EHOSTUNREACH;
	}

	for (addr = addresses; addr != NULL; addr = addr->ai_next) {
		fd = zsock_socket(addr->ai_family, addr->ai_socktype,
				IPPROTO_DTLS_1_2);
		if (fd < 0) {
			ret = -errno;
			continue;
		}

		ret = configure_dtls_socket(fd);
		if (ret < 0) {
			zsock_close(fd);
			fd = -1;
			continue;
		}

		ret = configure_socket_timeout(fd, timeout_ms);
		if (ret < 0) {
			zsock_close(fd);
			fd = -1;
			continue;
		}

		if (zsock_connect(fd, addr->ai_addr, addr->ai_addrlen) == 0) {
			ret = 0;
			break;
		}

		ret = -errno;
		zsock_close(fd);
		fd = -1;
	}

	zsock_freeaddrinfo(addresses);

	if (ret < 0) {
		return ret;
	}

	*sock = fd;
	return 0;
}

static int allocate_request_buf(size_t datasize,
		uint8_t **buf, uint16_t *bufsize)
{
	size_t size;

	if (datasize > (size_t)UINT16_MAX - PULSE_COAPS_REQUEST_OVERHEAD) {
		return -EOVERFLOW;
	}

	size = datasize + PULSE_COAPS_REQUEST_OVERHEAD;
	*buf = (uint8_t *)malloc(size);
	if (*buf == NULL) {
		return -ENOMEM;
	}

	*bufsize = (uint16_t)size;
	return 0;
}

static int build_request(uint8_t *buf, uint16_t bufsize,
		const void *data, size_t datasize, size_t *request_len)
{
	struct coap_packet request;
	int ret;

	ret = coap_packet_init(&request, buf, bufsize,
			COAP_VERSION_1, COAP_TYPE_CON,
			COAP_TOKEN_MAX_LEN, coap_next_token(),
			COAP_METHOD_POST, coap_next_id());
	if (ret < 0) {
		return ret;
	}

	ret = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
			(const uint8_t *)"v1", 2u);
	if (ret < 0) {
		return ret;
	}

	ret = coap_append_option_int(&request, COAP_OPTION_CONTENT_FORMAT,
			COAP_CONTENT_FORMAT_APP_CBOR);
	if (ret < 0) {
		return ret;
	}

	if (datasize > 0u) {
		ret = coap_packet_append_payload_marker(&request);
		if (ret < 0) {
			return ret;
		}

		ret = coap_packet_append_payload(&request,
				(uint8_t *)(uintptr_t)data, datasize);
		if (ret < 0) {
			return ret;
		}
	}

	*request_len = request.offset;
	return 0;
}

static void deliver_response(const response_buf_t *buf,
		const struct pulse_report_ctx *ctx)
{
	if (buf->len > 0u && ctx != NULL && ctx->on_response != NULL) {
		ctx->on_response(buf->data, buf->len, ctx->response_ctx);
	}
}

static int parse_response(coaps_session_t *s, size_t received_len)
{
	struct coap_packet packet;
	struct coap_option options[4];
	const uint8_t *payload;
	uint16_t payload_len = 0u;
	int ret;

	ret = coap_packet_parse(&packet, s->recv_buf, (uint16_t)received_len,
			options, (uint8_t)
			(sizeof(options) / sizeof(options[0])));
	if (ret < 0) {
		return ret;
	}

	payload = coap_packet_get_payload(&packet, &payload_len);
	if (payload_len > sizeof(s->response.data)) {
		s->response.truncated = true;
		return -EMSGSIZE;
	}

	if (payload != NULL && payload_len > 0u) {
		memcpy(s->response.data, payload, payload_len);
		s->response.len = payload_len;
	}

	if (coap_header_get_code(&packet) != COAP_RESPONSE_CODE_CHANGED) {
		return -EIO;
	}

	return 0;
}

static int receive_response(coaps_session_t *s, int timeout_ms, bool *ready)
{
	struct zsock_pollfd pfd = {
		.fd = s->sock,
		.events = ZSOCK_POLLIN,
		.revents = 0,
	};
	int ret;
	int received;

	ret = zsock_poll(&pfd, 1, timeout_ms);
	if (ret < 0) {
		return -errno;
	}

	if (ret == 0) {
		*ready = false;
		return 0;
	}

	if ((pfd.revents & ZSOCK_POLLERR) != 0u) {
		*ready = false;
		return -EIO;
	}

	if ((pfd.revents & ZSOCK_POLLHUP) != 0u) {
		*ready = false;
		return -ECONNRESET;
	}

	if ((pfd.revents & ZSOCK_POLLNVAL) != 0u) {
		*ready = false;
		return -EIO;
	}

	if ((pfd.revents & ZSOCK_POLLIN) == 0) {
		*ready = false;
		return 0;
	}

	received = zsock_recv(s->sock, s->recv_buf, sizeof(s->recv_buf), 0);
	if (received < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			*ready = false;
			return 0;
		}

		return -errno;
	}

	*ready = true;
	return parse_response(s, (size_t)received);
}

static int start_session(coaps_session_t *s, const void *data, size_t datasize,
		const struct pulse_report_ctx *rctx, bool async)
{
	char identity[PULSE_PSK_IDENTITY_BUFSIZE];
	uint8_t psk[PULSE_PSK_KEY_LEN];
	uint8_t *request_buf = NULL;
	uint16_t request_bufsize = 0u;
	size_t request_len = 0u;
	int ret;
	int32_t timeout_ms;

	reset_response(&s->response);
	s->rctx = rctx;

	ret = compute_psk_identity(identity, rctx->conf.token);
	if (ret < 0) {
		PULSE_ERROR("psk identity failed: err=%d", ret);
		goto out;
	}

	ret = decode_psk(psk, rctx->conf.token);
	if (ret < 0) {
		PULSE_ERROR("psk decode failed: err=%d", ret);
		goto out;
	}

	ret = register_psk_credentials(s, identity, psk, sizeof(psk));
	if (ret < 0) {
		PULSE_ERROR("coaps credential add failed: err=%d", ret);
		goto out;
	}

	ret = allocate_request_buf(datasize, &request_buf, &request_bufsize);
	if (ret < 0) {
		PULSE_ERROR("coaps request alloc failed: err=%d", ret);
		goto out;
	}

	ret = build_request(request_buf, request_bufsize, data, datasize,
			&request_len);
	if (ret < 0) {
		PULSE_ERROR("coaps request build failed: err=%d", ret);
		goto out;
	}

	ret = get_timeout_ms(&rctx->conf, &timeout_ms);
	if (ret < 0) {
		PULSE_ERROR("coaps timeout invalid: err=%d", ret);
		goto out;
	}

	ret = resolve_and_connect(&s->sock, (uint32_t)timeout_ms);
	if (ret < 0) {
		PULSE_ERROR("coaps connect failed: err=%d", ret);
		goto out;
	}

	ret = zsock_send(s->sock, request_buf, request_len, 0);
	if (ret < 0) {
		ret = -errno;
		PULSE_ERROR("coaps send failed: err=%d", ret);
		goto out;
	}

	if ((size_t)ret != request_len) {
		PULSE_ERROR("coaps send short: err=%d", -EIO);
		ret = -EIO;
		goto out;
	}

	s->start_ms = k_uptime_get();
	s->state = STATE_AWAITING_RESPONSE;
	free(request_buf);

	if (async) {
		return -EINPROGRESS;
	}

	return 0;

out:
	free(request_buf);
	cleanup_session(s);
	return ret;
}

static int finish_session(coaps_session_t *s, int ret)
{
	if (ret == 0) {
		deliver_response(&s->response, s->rctx);
	}

	cleanup_session(s);
	return ret;
}

void pulse_transport_cancel(void)
{
	cleanup_session(&m_session);
}

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	bool ready = false;
	int ret;
	int32_t timeout_ms;

	if (data == NULL || datasize == 0u) {
		PULSE_WARN("coaps transmit invalid args: err=%d", -EINVAL);
		return -EINVAL;
	}

	if (datasize > (size_t)INT_MAX) {
		PULSE_ERROR("coaps transmit overflow: err=%d", -EOVERFLOW);
		return -EOVERFLOW;
	}

	if (ctx == NULL || ctx->conf.token == NULL) {
		PULSE_WARN("coaps transmit missing token: err=%d", -EINVAL);
		return -EINVAL;
	}

	if (m_session.state == STATE_IDLE) {
		ret = start_session(&m_session, data, datasize, ctx,
				ctx->conf.async_transport);
		if (ret != 0) {
			return ret;
		}
	}

	ret = get_timeout_ms(&ctx->conf, &timeout_ms);
	if (ret < 0) {
		PULSE_ERROR("coaps timeout invalid: err=%d", ret);
		return finish_session(&m_session, ret);
	}

	ret = receive_response(&m_session, ctx->conf.async_transport
			? 0 : timeout_ms,
			&ready);
	if (ret < 0) {
		PULSE_ERROR("coaps receive failed: err=%d", ret);
		return finish_session(&m_session, ret);
	}

	if (!ready) {
		if (ctx->conf.async_transport &&
				has_session_timed_out(m_session.start_ms, timeout_ms)) {
			PULSE_ERROR("coaps timeout: err=%d", -ETIMEDOUT);
			return finish_session(&m_session, -ETIMEDOUT);
		}

		if (ctx->conf.async_transport) {
			return -EINPROGRESS;
		}

		PULSE_ERROR("coaps timeout: err=%d", -ETIMEDOUT);
		return finish_session(&m_session, -ETIMEDOUT);
	}

	return finish_session(&m_session, 0);
}
