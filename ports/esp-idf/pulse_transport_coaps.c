/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <coap3/coap.h>
#include <mbedtls/version.h>

#if MBEDTLS_VERSION_NUMBER >= 0x04000000
#include <psa/crypto.h>
#else
#include <mbedtls/sha256.h>
#endif

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"

#define COAPS_TIMEOUT_MS_DEFAULT	15000u
#define COAPS_RESPONSE_BUFSIZE		4096u
#define PSK_IDENTITY_HEX_LEN		32u
#define PSK_IDENTITY_BUFSIZE		(PSK_IDENTITY_HEX_LEN + 1u)

typedef enum {
	STATE_IDLE,
	STATE_IN_PROGRESS,
} coaps_state_t;

typedef struct {
	uint8_t data[COAPS_RESPONSE_BUFSIZE];
	size_t len;
	bool truncated;
} response_buf_t;

typedef struct {
	coap_mid_t mid;
	response_buf_t *response;
	coap_pdu_code_t code;
	int error;
	bool done;
} exchange_t;

typedef struct {
	coap_context_t *ctx;
	coap_session_t *session;
	exchange_t ex;
	response_buf_t response;
	coaps_state_t state;
	const struct pulse_report_ctx *rctx;
	bool coap_started;
} coaps_session_t;

static coaps_session_t m_session;

static uint32_t get_timeout_ms(const struct pulse *conf)
{
	if (conf != NULL && conf->transmit_timeout_ms > 0u) {
		return conf->transmit_timeout_ms;
	}

	return COAPS_TIMEOUT_MS_DEFAULT;
}

static int compute_psk_identity(char buf[static PSK_IDENTITY_BUFSIZE],
		const char *token)
{
	static const char HEX[] = "0123456789abcdef";
	uint8_t digest[32];
	size_t i;

#if MBEDTLS_VERSION_NUMBER >= 0x04000000
	size_t digest_len;

	if (psa_crypto_init() != PSA_SUCCESS) {
		return -EIO;
	}

	if (psa_hash_compute(PSA_ALG_SHA_256,
				(const uint8_t *)token, strlen(token),
				digest, sizeof(digest),
				&digest_len) != PSA_SUCCESS) {
		return -EIO;
	}

	if (digest_len != sizeof(digest)) {
		return -EIO;
	}
#else
	if (mbedtls_sha256((const unsigned char *)token, strlen(token),
				digest, 0) != 0) {
		return -EIO;
	}
#endif

	for (i = 0u; i < PSK_IDENTITY_HEX_LEN / 2u; i++) {
		buf[i * 2u]       = HEX[(digest[i] >> 4) & 0x0fu];
		buf[i * 2u + 1u]  = HEX[digest[i] & 0x0fu];
	}
	buf[PSK_IDENTITY_HEX_LEN] = '\0';

	return 0;
}

static void fill_psk_config(coap_dtls_cpsk_t *psk,
		const char *identity, const char *token)
{
	memset(psk, 0, sizeof(*psk));
	psk->version = COAP_DTLS_CPSK_SETUP_VERSION;
	psk->client_sni = PULSE_INGEST_HOST;
	psk->psk_info.identity.s = (const uint8_t *)identity;
	psk->psk_info.identity.length = strlen(identity);
	psk->psk_info.key.s = (const uint8_t *)token;
	psk->psk_info.key.length = strlen(token);
}

static int resolve_server(coap_addr_info_t **info)
{
	const coap_str_const_t host = {
		.length = sizeof(PULSE_INGEST_HOST) - 1u,
		.s = (const uint8_t *)PULSE_INGEST_HOST,
	};

	*info = coap_resolve_address_info(&host, 0u,
			(uint16_t)PULSE_INGEST_PORT_COAPS, 0u, 0u, 0,
			1 << COAP_URI_SCHEME_COAPS, COAP_RESOLVE_TYPE_REMOTE);

	return (*info != NULL) ? 0 : -EHOSTUNREACH;
}

static int build_coap_pdu(coap_pdu_t **pdu, coap_session_t *session,
		const void *data, size_t len)
{
	uint8_t content_fmt[4];
	size_t content_fmt_len;

	*pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, session);
	if (*pdu == NULL) {
		return -ENOMEM;
	}

	content_fmt_len = coap_encode_var_safe(content_fmt, sizeof(content_fmt),
			COAP_MEDIATYPE_APPLICATION_CBOR);

	if (!coap_add_option(*pdu, COAP_OPTION_URI_PATH, 2u,
				(const uint8_t *)"v1")
			|| !coap_add_option(*pdu, COAP_OPTION_CONTENT_FORMAT,
				content_fmt_len, content_fmt)
			|| (len > 0u && !coap_add_data(*pdu, len,
					(const uint8_t *)data))) {
		coap_delete_pdu(*pdu);
		*pdu = NULL;
		return -EIO;
	}

	return 0;
}

static void accumulate_payload(response_buf_t *buf, const coap_pdu_t *pdu)
{
	const uint8_t *payload = NULL;
	size_t len = 0u;
	size_t offset = 0u;
	size_t total = 0u;

	if (!coap_get_data_large(pdu, &len, &payload, &offset, &total)) {
		return;
	}

	if (payload == NULL || len == 0u) {
		return;
	}

	if (offset != 0u || len != total || total > sizeof(buf->data)) {
		buf->truncated = true;
	}

	if (len > sizeof(buf->data)) {
		len = sizeof(buf->data);
	}

	memcpy(buf->data, payload, len);
	buf->len = len;
}

static int map_nack_reason(coap_nack_reason_t reason)
{
	switch (reason) {
	case COAP_NACK_TOO_MANY_RETRIES:
		return -ETIMEDOUT;
	case COAP_NACK_NOT_DELIVERABLE:
	case COAP_NACK_TLS_FAILED:
	case COAP_NACK_TLS_LAYER_FAILED:
		return -ECANCELED;
	default:
		return -EIO;
	}
}

static coap_response_t on_coap_response(coap_session_t *session,
		const coap_pdu_t *sent, const coap_pdu_t *received,
		coap_mid_t mid)
{
	exchange_t *ex = (exchange_t *)coap_session_get_app_data(session);

	(void)sent;

	if (ex == NULL || ex->done || received == NULL || mid != ex->mid) {
		return COAP_RESPONSE_OK;
	}

	ex->code = coap_pdu_get_code(received);
	accumulate_payload(ex->response, received);
	ex->done = true;

	return COAP_RESPONSE_OK;
}

static void on_coap_nack(coap_session_t *session, const coap_pdu_t *sent,
		coap_nack_reason_t reason, coap_mid_t mid)
{
	exchange_t *ex = (exchange_t *)coap_session_get_app_data(session);

	(void)sent;

	if (ex == NULL || ex->done || mid != ex->mid) {
		return;
	}

	ex->error = map_nack_reason(reason);
	ex->done = true;
}

static int evaluate_response(coap_pdu_code_t code, const response_buf_t *buf)
{
	if (code != COAP_RESPONSE_CODE_CHANGED) {
		return -EIO;
	}

	return buf->truncated ? -EMSGSIZE : 0;
}

static void deliver_response(const response_buf_t *buf,
		const struct pulse_report_ctx *ctx)
{
	if (buf->len > 0u && ctx != NULL && ctx->on_response != NULL) {
		ctx->on_response(buf->data, buf->len, ctx->response_ctx);
	}
}

static void cleanup_session(coaps_session_t *s)
{
	if (s->session != NULL) {
		coap_register_response_handler(s->ctx, NULL);
		coap_register_nack_handler(s->ctx, NULL);
		coap_session_set_app_data(s->session, NULL);
		coap_session_release(s->session);
		s->session = NULL;
	}
	if (s->ctx != NULL) {
		coap_free_context(s->ctx);
		s->ctx = NULL;
	}
	if (s->coap_started) {
		coap_cleanup();
		s->coap_started = false;
	}
	s->state = STATE_IDLE;
	s->rctx = NULL;
}

static int start_session(coaps_session_t *s, const void *data, size_t datasize,
		const struct pulse_report_ctx *rctx)
{
	const struct pulse *conf = &rctx->conf;
	char psk_id[PSK_IDENTITY_BUFSIZE];
	coap_dtls_cpsk_t psk;
	coap_addr_info_t *addr = NULL;
	coap_pdu_t *pdu = NULL;
	int ret;

	memset(&s->response, 0, sizeof(s->response));
	memset(&s->ex, 0, sizeof(s->ex));
	s->ex.response = &s->response;
	s->rctx = rctx;

	ret = compute_psk_identity(psk_id, conf->token);
	if (ret != 0) {
		goto fail;
	}

	s->ctx = coap_new_context(NULL);
	if (s->ctx == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	ret = resolve_server(&addr);
	if (ret != 0) {
		goto fail;
	}

	fill_psk_config(&psk, psk_id, conf->token);
	s->session = coap_new_client_session_psk2(s->ctx, NULL, &addr->addr,
			COAP_PROTO_DTLS, &psk);
	coap_free_address_info(addr);
	addr = NULL;

	if (s->session == NULL) {
		ret = -EIO;
		goto fail;
	}

	coap_session_set_app_data(s->session, &s->ex);
	coap_register_response_handler(s->ctx, on_coap_response);
	coap_register_nack_handler(s->ctx, on_coap_nack);

	ret = build_coap_pdu(&pdu, s->session, data, datasize);
	if (ret != 0) {
		goto fail;
	}

	s->ex.mid = coap_send(s->session, pdu);
	pdu = NULL;

	if (s->ex.mid == COAP_INVALID_MID) {
		ret = -EIO;
		goto fail;
	}

	s->state = STATE_IN_PROGRESS;
	return 0;

fail:
	if (pdu != NULL) {
		coap_delete_pdu(pdu);
	}
	if (addr != NULL) {
		coap_free_address_info(addr);
	}
	cleanup_session(s);
	return ret;
}

static int advance_session(coaps_session_t *s, bool async,
		uint32_t timeout_ms)
{
	uint32_t remaining = timeout_ms;
	int elapsed;

	do {
		elapsed = coap_io_process(s->ctx, async ? 0u : remaining);

		if (elapsed < 0) {
			cleanup_session(s);
			return -EIO;
		}

		if (async) {
			break;
		}

		if ((uint32_t)elapsed >= remaining) {
			remaining = 0u;
		} else {
			remaining -= (uint32_t)elapsed;
		}

		if (!s->ex.done && elapsed == 0 && !coap_io_pending(s->ctx)) {
			break;
		}
	} while (!s->ex.done && remaining > 0u);

	if (!s->ex.done) {
		if (async) {
			return -EINPROGRESS;
		}
		cleanup_session(s);
		return -ETIMEDOUT;
	}

	int ret = s->ex.error;

	if (ret == 0) {
		ret = evaluate_response(s->ex.code, &s->response);
	}
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
	const struct pulse *conf = ctx != NULL ? &ctx->conf : NULL;

	if (data == NULL || datasize == 0u) {
		return -EINVAL;
	}

	if (datasize > (size_t)INT_MAX) {
		return -EOVERFLOW;
	}

	if (conf == NULL || conf->token == NULL) {
		return -EINVAL;
	}

	const bool async = conf->async_transport;

	if (m_session.state == STATE_IDLE) {
		coap_startup();
		m_session.coap_started = true;
		int ret = start_session(&m_session, data, datasize, ctx);
		if (ret != 0) {
			return ret;
		}
	}

	return advance_session(&m_session, async, get_timeout_ms(conf));
}
