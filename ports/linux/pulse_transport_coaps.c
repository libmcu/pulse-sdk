/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"

#include <netdb.h>

#include <coap3/coap.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libmcu/base64.h"

#define PULSE_COAPS_TIMEOUT_MS		15000u
#define PULSE_COAPS_BUFFER_SIZE		4096u
#define PULSE_PSK_IDENTITY_LEN		32u
#define PULSE_PSK_IDENTITY_BUFSIZE	(PULSE_PSK_IDENTITY_LEN + 1u)
#define PULSE_PSK_KEY_LEN		32u

typedef struct {
	uint8_t data[PULSE_COAPS_BUFFER_SIZE];
	size_t len;
	bool truncated;
} response_buffer_t;

typedef struct {
	coap_mid_t request_mid;
	response_buffer_t *response;
	coap_pdu_code_t response_code;
	int result;
	bool completed;
} coap_request_state_t;

static void transport_debug(const char *message)
{
	fprintf(stderr, "[pulse/linux] %s\n", message);
}

static void transport_debugf(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "[pulse/linux] ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static uint32_t get_transmit_timeout_ms(const struct pulse *conf)
{
	if (conf != NULL && conf->transmit_timeout_ms > 0u) {
		return conf->transmit_timeout_ms;
	}

	return PULSE_COAPS_TIMEOUT_MS;
}

static void deliver_response(const response_buffer_t *response,
		const struct pulse_report_ctx *ctx)
{
	if (response->len > 0u && ctx != NULL && ctx->on_response != NULL) {
		ctx->on_response(response->data, response->len,
				ctx->response_ctx);
	}
}

static int compute_psk_identity(char *identity, size_t identity_size,
		const char *token)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char digest[SHA256_DIGEST_LENGTH];
	size_t i;

	if (identity == NULL || token == NULL
			|| identity_size < PULSE_PSK_IDENTITY_BUFSIZE) {
		return -EINVAL;
	}

	if (SHA256((const unsigned char *)token, strlen(token), digest)
			== NULL) {
		return -EIO;
	}

	for (i = 0u; i < PULSE_PSK_IDENTITY_LEN / 2u; i++) {
		identity[(i * 2u) + 0u] = hex[(digest[i] >> 4) & 0x0f];
		identity[(i * 2u) + 1u] = hex[digest[i] & 0x0f];
	}
	identity[PULSE_PSK_IDENTITY_LEN] = '\0';

	return 0;
}

static int decode_psk_key(uint8_t *psk_key, size_t psk_key_size,
		const char *token)
{
	if (psk_key == NULL || token == NULL || psk_key_size < PULSE_PSK_KEY_LEN) {
		return -EINVAL;
	}

	if (lm_base64url_decode(psk_key, psk_key_size, token, strlen(token))
			!= PULSE_PSK_KEY_LEN) {
		return -EINVAL;
	}

	return 0;
}

/* OpenSSL 3.x 기본 Security Level 1이 PSK cipher suite를 차단하므로
 * security level 0으로 낮추고 cipher list를 명시해야 함.
 *
 * coap_new_client_session_psk2()는 호출 즉시 ClientHello를 전송하므로,
 * 세션 생성 후 cipher 설정은 이미 늦음. SSL_CTX는 동일 coap_context_t 내
 * 세션들 간에 재사용되므로, probe session을 먼저 생성해 SSL_CTX를 획득하고
 * cipher를 설정한 뒤 실제 세션을 생성하면 올바른 cipher로 ClientHello가 전송됨. */
static const char PULSE_COAPS_CIPHER_LIST[] =
	"PSK-AES128-GCM-SHA256:PSK-AES128-CCM:PSK-AES128-CCM8";

static int configure_ssl_ctx_ciphers(SSL_CTX *ssl_ctx)
{
	SSL_CTX_set_security_level(ssl_ctx, 0);

	if (SSL_CTX_set_cipher_list(ssl_ctx, PULSE_COAPS_CIPHER_LIST) != 1) {
		transport_debug("failed to set PSK cipher list on SSL_CTX");
		return -EIO;
	}

	return 0;
}

/* probe session을 생성해 내부 SSL_CTX에 cipher 설정을 주입한 뒤 즉시 해제함.
 * 이후 같은 coap_context_t에서 생성되는 세션은 동일 SSL_CTX를 재사용하므로
 * ClientHello에 올바른 cipher suite가 포함됨. */
static int prime_ssl_ctx_via_probe(coap_context_t *coap_ctx,
		const coap_address_t *addr, coap_dtls_cpsk_t *psk)
{
	coap_session_t *probe;
	coap_tls_library_t tls_lib;
	SSL *ssl;
	SSL_CTX *ssl_ctx;
	int ret;

	probe = coap_new_client_session_psk2(coap_ctx, NULL, addr,
			COAP_PROTO_DTLS, psk);
	if (probe == NULL) {
		transport_debug("probe session creation failed");
		return -EIO;
	}

	ssl = (SSL *)coap_session_get_tls(probe, &tls_lib);
	if (ssl == NULL || tls_lib != COAP_TLS_LIBRARY_OPENSSL) {
		transport_debug("failed to get SSL object from probe session");
		coap_session_release(probe);
		return -EIO;
	}

	ssl_ctx = SSL_get_SSL_CTX(ssl);
	ret = configure_ssl_ctx_ciphers(ssl_ctx);
	coap_session_release(probe);

	return ret;
}

static int resolve_coap_address(coap_addr_info_t **info_list)
{
	const coap_str_const_t host = {
		.length = strlen(PULSE_INGEST_HOST),
		.s = (const uint8_t *)PULSE_INGEST_HOST,
	};

	*info_list = coap_resolve_address_info(&host, 0u,
			(uint16_t)PULSE_INGEST_PORT_COAPS, 0u, 0u, AI_NUMERICSERV,
			1 << COAP_URI_SCHEME_COAPS, COAP_RESOLVE_TYPE_REMOTE);
	if (*info_list == NULL) {
		transport_debug("failed to resolve CoAP DTLS address");
		return -EHOSTUNREACH;
	}

	return 0;
}

static int build_coap_request(coap_pdu_t **request, coap_session_t *session,
		const void *data, size_t datasize)
{
	uint8_t content_format[4];
	const size_t content_format_len =
		coap_encode_var_safe(content_format, sizeof(content_format),
				COAP_MEDIATYPE_APPLICATION_CBOR);

	*request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, session);
	if (*request == NULL) {
		transport_debug("failed to allocate CoAP request PDU");
		return -ENOMEM;
	}

	if (!coap_add_option(*request, COAP_OPTION_URI_PATH, 2u,
				(const uint8_t *)"v1")) {
		transport_debug("failed to add CoAP Uri-Path option");
		return -EIO;
	}

	if (!coap_add_option(*request, COAP_OPTION_CONTENT_FORMAT,
				content_format_len, content_format)) {
		transport_debug("failed to add CoAP Content-Format option");
		return -EIO;
	}

	if (datasize > 0u && !coap_add_data(*request, datasize,
				(const uint8_t *)data)) {
		transport_debug("failed to add CoAP payload");
		return -EIO;
	}

	return 0;
}

static void copy_coap_response_payload(response_buffer_t *response,
		const coap_pdu_t *response_pdu)
{
	const uint8_t *payload = NULL;
	size_t len = 0u;
	size_t offset = 0u;
	size_t total = 0u;

	if (!coap_get_data_large(response_pdu, &len, &payload, &offset, &total)) {
		return;
	}

	if (payload == NULL) {
		return;
	}

	if (offset != 0u || len != total || total > sizeof(response->data)) {
		response->truncated = true;
	}

	if (len > sizeof(response->data)) {
		len = sizeof(response->data);
	}

	if (len > 0u) {
		memcpy(response->data, payload, len);
		response->len = len;
	}
}

static void log_coap_error_payload(const response_buffer_t *response)
{
	char message[129];
	size_t n;

	if (response->len == 0u) {
		return;
	}

	n = response->len < sizeof(message) - 1u ? response->len
			: sizeof(message) - 1u;
	memcpy(message, response->data, n);
	message[n] = '\0';
	transport_debugf("CoAP error body=%s", message);
}

static int check_coap_response(coap_pdu_code_t code,
		response_buffer_t *response)
{
	transport_debugf("CoAP response code=%u", (unsigned int)code);

	if (code != COAP_RESPONSE_CODE_CHANGED) {
		log_coap_error_payload(response);
		return -EIO;
	}

	if (response->truncated) {
		transport_debug("CoAP response body exceeded local buffer");
		return -EMSGSIZE;
	}

	return 0;
}

static int map_coap_nack_reason(coap_nack_reason_t reason)
{
	switch (reason) {
	case COAP_NACK_TOO_MANY_RETRIES:
		return -ETIMEDOUT;
	case COAP_NACK_NOT_DELIVERABLE:
		return -EIO;
	case COAP_NACK_TLS_FAILED:
	case COAP_NACK_TLS_LAYER_FAILED:
		return -EPROTO;
	case COAP_NACK_WS_LAYER_FAILED:
	case COAP_NACK_WS_FAILED:
	case COAP_NACK_RST:
	case COAP_NACK_ICMP_ISSUE:
	case COAP_NACK_BAD_RESPONSE:
	default:
		return -EIO;
	}
}

static coap_request_state_t *get_request_state(coap_session_t *session)
{
	if (session == NULL) {
		return NULL;
	}

	return (coap_request_state_t *)coap_session_get_app_data(session);
}

static coap_response_t handle_coap_response(coap_session_t *session,
		const coap_pdu_t *sent, const coap_pdu_t *received,
		coap_mid_t mid)
{
	coap_request_state_t *state = get_request_state(session);

	(void)sent;

	if (state == NULL || state->completed || received == NULL
			|| mid != state->request_mid) {
		return COAP_RESPONSE_OK;
	}

	state->response_code = coap_pdu_get_code(received);
	copy_coap_response_payload(state->response, received);
	state->result = 0;
	state->completed = true;

	return COAP_RESPONSE_OK;
}

static void handle_coap_nack(coap_session_t *session, const coap_pdu_t *sent,
		coap_nack_reason_t reason, coap_mid_t mid)
{
	coap_request_state_t *state = get_request_state(session);

	(void)sent;

	if (state == NULL || state->completed || mid != state->request_mid) {
		return;
	}

	state->result = map_coap_nack_reason(reason);
	state->completed = true;
}

static int coap_send_recv_compat(coap_context_t *coap_ctx,
		coap_session_t *session, coap_pdu_t **request_pdu,
		response_buffer_t *response, coap_pdu_code_t *response_code,
		uint32_t timeout_ms)
{
	coap_request_state_t state;
	int io_result;
	uint32_t remaining = timeout_ms;

	if (coap_ctx == NULL || session == NULL || request_pdu == NULL
			|| *request_pdu == NULL || response == NULL
			|| response_code == NULL) {
		return -EINVAL;
	}

	memset(&state, 0, sizeof(state));
	state.response = response;

	coap_session_set_app_data(session, &state);
	coap_register_response_handler(coap_ctx, handle_coap_response);
	coap_register_nack_handler(coap_ctx, handle_coap_nack);

	state.request_mid = coap_send(session, *request_pdu);
	if (state.request_mid == COAP_INVALID_MID) {
		coap_register_response_handler(coap_ctx, NULL);
		coap_register_nack_handler(coap_ctx, NULL);
		coap_session_set_app_data(session, NULL);
		return -EIO;
	}

	*request_pdu = NULL;

	while (!state.completed && remaining > 0u) {
		io_result = coap_io_process(coap_ctx, remaining);
		if (io_result < 0) {
			state.result = -EIO;
			state.completed = true;
			break;
		}

		if ((uint32_t)io_result >= remaining) {
			remaining = 0u;
		} else {
			remaining -= (uint32_t)io_result;
		}

		if (!state.completed && io_result == 0 && !coap_io_pending(coap_ctx)) {
			break;
		}
	}

	coap_register_response_handler(coap_ctx, NULL);
	coap_register_nack_handler(coap_ctx, NULL);
	coap_session_set_app_data(session, NULL);

	if (!state.completed) {
		return -ETIMEDOUT;
	}

	if (state.result < 0) {
		return state.result;
	}

	*response_code = state.response_code;
	return 0;
}

void pulse_transport_cancel(void)
{
}

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	const struct pulse *conf = ctx != NULL ? &ctx->conf : NULL;
	coap_addr_info_t *info_list = NULL;
	coap_context_t *coap_ctx = NULL;
	coap_session_t *session = NULL;
	coap_pdu_t *request_pdu = NULL;
	response_buffer_t response;
	coap_pdu_code_t response_code = 0;
	coap_dtls_cpsk_t psk;
	char psk_identity[PULSE_PSK_IDENTITY_BUFSIZE];
	uint8_t psk_key[PULSE_PSK_KEY_LEN];
	int ret;
	int send_result;

	if (data == NULL || datasize == 0u) {
		transport_debug("invalid transmit arguments");
		return -EINVAL;
	}

	if (datasize > (size_t)INT64_MAX) {
		transport_debug("payload too large for CoAP request");
		return -EOVERFLOW;
	}

	if (conf == NULL || conf->token == NULL) {
		transport_debug("CoAP DTLS requires a token");
		return -EINVAL;
	}

	if (conf->async_transport) {
		transport_debug("async transport is not supported on linux");
		return -ENOTSUP;
	}

	ret = compute_psk_identity(psk_identity, sizeof(psk_identity), conf->token);
	if (ret < 0) {
		transport_debug("failed to derive DTLS PSK identity");
		return ret;
	}

	ret = decode_psk_key(psk_key, sizeof(psk_key), conf->token);
	if (ret < 0) {
		transport_debug("failed to decode DTLS PSK key from token");
		return ret;
	}

	coap_startup();
	coap_ctx = coap_new_context(NULL);
	if (coap_ctx == NULL) {
		transport_debug("coap_new_context failed");
		coap_cleanup();
		return -ENOMEM;
	}

	ret = resolve_coap_address(&info_list);
	if (ret < 0) {
		coap_free_context(coap_ctx);
		coap_cleanup();
		return ret;
	}

	memset(&psk, 0, sizeof(psk));
	psk.version = COAP_DTLS_CPSK_SETUP_VERSION;
	psk.client_sni = PULSE_INGEST_HOST;
	psk.psk_info.identity.s = (const uint8_t *)psk_identity;
	psk.psk_info.identity.length = strlen(psk_identity);
	psk.psk_info.key.s = psk_key;
	psk.psk_info.key.length = sizeof(psk_key);

	transport_debugf("sending CoAP DTLS report bytes=%ld timeout_ms=%u",
			(long)datasize, (unsigned int)get_transmit_timeout_ms(conf));
	ret = prime_ssl_ctx_via_probe(coap_ctx, &info_list->addr, &psk);
	if (ret < 0) {
		goto out;
	}

	session = coap_new_client_session_psk2(coap_ctx, NULL, &info_list->addr,
			COAP_PROTO_DTLS, &psk);
	if (session == NULL) {
		transport_debug("failed to create CoAP DTLS session");
		ret = -EIO;
		goto out;
	}

	ret = build_coap_request(&request_pdu, session, data, datasize);
	if (ret < 0) {
		goto out;
	}

	memset(&response, 0, sizeof(response));
	send_result = coap_send_recv_compat(coap_ctx, session, &request_pdu,
			&response, &response_code, get_transmit_timeout_ms(conf));
	if (send_result < 0) {
		ret = send_result;
		transport_debugf("coap_send_recv failed err=%d", send_result);
		goto out;
	}

	ret = check_coap_response(response_code, &response);
	if (ret == 0) {
		transport_debugf("CoAP report delivered response_bytes=%ld",
				(long)response.len);
		deliver_response(&response, ctx);
	}

out:
	if (request_pdu != NULL) {
		coap_delete_pdu(request_pdu);
	}
	if (session != NULL) {
		coap_session_release(session);
	}
	if (info_list != NULL) {
		coap_free_address_info(info_list);
	}
	coap_free_context(coap_ctx);
	coap_cleanup();

	return ret;
}
