/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

extern "C" {
#include "coap3/coap.h"
#include "openssl/ssl.h"
}

struct coap_context_t {
	int reserved;
};

struct coap_session_t {
	int reserved;
};

struct coap_pdu_t {
	int reserved;
};

static coap_context_t g_default_context;
static coap_session_t g_default_session;
static coap_pdu_t g_default_request_pdu;
static coap_pdu_t g_default_response_pdu;
static coap_addr_info_t g_default_addr_info;

static struct {
	coap_context_t *context_result;
	void *context_app_data;
	void *session_app_data;
	coap_response_handler_t response_handler;
	coap_nack_handler_t nack_handler;
	int resolve_enabled;
	coap_session_t *session_result;
	int send_recv_result;
	coap_mid_t last_mid;
	coap_pdu_code_t response_code;
	uint8_t response_payload[8192];
	size_t response_len;
	size_t response_offset;
	size_t response_total;
	int has_response_payload;
	int suppress_response;
	char client_sni[128];
	char psk_identity[128];
	char psk_key[128];
	size_t psk_key_len;
	char uri_path[64];
	uint32_t content_format;
	uint32_t timeout_ms;
	uint8_t payload[8192];
	size_t payload_len;
} g_state;

static void copy_text(char *dst, size_t dst_size, const uint8_t *src, size_t len)
{
	if (dst_size == 0u) {
		return;
	}

	if (len >= dst_size) {
		len = dst_size - 1u;
	}

	if (len > 0u) {
		memcpy(dst, src, len);
	}
	dst[len] = '\0';
}

extern "C" void coap_mock_reset(void)
{
	memset(&g_state, 0, sizeof(g_state));
	g_state.context_result = &g_default_context;
	g_state.context_app_data = NULL;
	g_state.session_app_data = NULL;
	g_state.response_handler = NULL;
	g_state.nack_handler = NULL;
	g_state.resolve_enabled = 1;
	g_state.session_result = &g_default_session;
	g_state.send_recv_result = 0;
	g_state.last_mid = 1;
	g_state.suppress_response = 0;
	g_state.response_code = COAP_RESPONSE_CODE_CHANGED;
	g_default_addr_info.next = NULL;
	g_default_addr_info.scheme = COAP_URI_SCHEME_COAPS;
	g_default_addr_info.proto = COAP_PROTO_DTLS;
}

extern "C" void coap_mock_set_context_result(coap_context_t *ctx)
{
	g_state.context_result = ctx;
}

extern "C" void coap_mock_set_resolve_result(int enabled)
{
	g_state.resolve_enabled = enabled;
}

extern "C" void coap_mock_set_session_result(coap_session_t *session)
{
	g_state.session_result = session;
}

extern "C" void coap_mock_set_send_recv_result(int result)
{
	g_state.send_recv_result = result;
}

extern "C" void coap_mock_suppress_response(void)
{
	g_state.suppress_response = 1;
}

extern "C" void coap_mock_allow_response(void)
{
	g_state.suppress_response = 0;
}

extern "C" void coap_mock_set_response_code(coap_pdu_code_t code)
{
	g_state.response_code = code;
}

extern "C" void coap_mock_set_response_payload(const void *data, size_t len,
		size_t offset, size_t total)
{
	if (len > sizeof(g_state.response_payload)) {
		len = sizeof(g_state.response_payload);
	}

	if (len > 0u) {
		memcpy(g_state.response_payload, data, len);
	}
	g_state.response_len = len;
	g_state.response_offset = offset;
	g_state.response_total = total;
	g_state.has_response_payload = 1;
}

extern "C" const char *coap_mock_last_client_sni(void)
{
	return g_state.client_sni;
}

extern "C" const char *coap_mock_last_psk_identity(void)
{
	return g_state.psk_identity;
}

extern "C" const char *coap_mock_last_psk_key(void)
{
	return g_state.psk_key;
}

extern "C" size_t coap_mock_last_psk_key_len(void)
{
	return g_state.psk_key_len;
}

extern "C" const char *coap_mock_last_uri_path(void)
{
	return g_state.uri_path;
}

extern "C" uint32_t coap_mock_last_content_format(void)
{
	return g_state.content_format;
}

extern "C" uint32_t coap_mock_last_timeout_ms(void)
{
	return g_state.timeout_ms;
}

extern "C" size_t coap_mock_last_payload_len(void)
{
	return g_state.payload_len;
}

extern "C" const uint8_t *coap_mock_last_payload_data(void)
{
	return g_state.payload;
}

extern "C" void coap_startup(void)
{
}

extern "C" void coap_cleanup(void)
{
}

extern "C" coap_context_t *coap_new_context(void *params)
{
	(void)params;
	return g_state.context_result;
}

extern "C" void coap_free_context(coap_context_t *ctx)
{
	(void)ctx;
}

extern "C" coap_addr_info_t *coap_resolve_address_info(
		const coap_str_const_t *address, uint16_t port, uint16_t secure_port,
		uint16_t ws_port, uint16_t ws_secure_port, int ai_hints_flags,
		int scheme_hint_bits, int type)
{
	(void)address;
	(void)port;
	(void)secure_port;
	(void)ws_port;
	(void)ws_secure_port;
	(void)ai_hints_flags;
	(void)scheme_hint_bits;
	(void)type;

	if (!g_state.resolve_enabled) {
		return NULL;
	}

	return &g_default_addr_info;
}

extern "C" void coap_free_address_info(coap_addr_info_t *info_list)
{
	(void)info_list;
}

extern "C" void coap_session_set_app_data(coap_session_t *session, void *data)
{
	(void)session;
	g_state.session_app_data = data;
}

extern "C" void *coap_session_get_app_data(const coap_session_t *session)
{
	(void)session;
	return g_state.session_app_data;
}

extern "C" void coap_context_set_app_data(coap_context_t *context, void *data)
{
	(void)context;
	g_state.context_app_data = data;
}

extern "C" void *coap_context_get_app_data(const coap_context_t *context)
{
	(void)context;
	return g_state.context_app_data;
}

extern "C" void coap_register_response_handler(coap_context_t *context,
		coap_response_handler_t handler)
{
	(void)context;
	g_state.response_handler = handler;
}

extern "C" void coap_register_nack_handler(coap_context_t *context,
		coap_nack_handler_t handler)
{
	(void)context;
	g_state.nack_handler = handler;
}

extern "C" coap_session_t *coap_new_client_session_psk2(coap_context_t *ctx,
		const coap_address_t *local_if, const coap_address_t *server,
		coap_proto_t proto, coap_dtls_cpsk_t *setup_data)
{
	(void)ctx;
	(void)local_if;
	(void)server;
	(void)proto;

	if (setup_data->client_sni != NULL) {
		copy_text(g_state.client_sni, sizeof(g_state.client_sni),
				(const uint8_t *)setup_data->client_sni,
				strlen(setup_data->client_sni));
	}
	copy_text(g_state.psk_identity, sizeof(g_state.psk_identity),
			setup_data->psk_info.identity.s,
			setup_data->psk_info.identity.length);
	copy_text(g_state.psk_key, sizeof(g_state.psk_key),
			setup_data->psk_info.key.s,
			setup_data->psk_info.key.length);
	g_state.psk_key_len = setup_data->psk_info.key.length;

	return g_state.session_result;
}

extern "C" coap_context_t *coap_session_get_context(const coap_session_t *session)
{
	(void)session;
	return g_state.context_result;
}

extern "C" void coap_session_release(coap_session_t *session)
{
	(void)session;
}

extern "C" coap_pdu_t *coap_new_pdu(coap_pdu_type_t type,
		coap_pdu_code_t code, coap_session_t *session)
{
	(void)type;
	(void)code;
	(void)session;
	return &g_default_request_pdu;
}

extern "C" int coap_add_option(coap_pdu_t *pdu, uint16_t number,
		size_t length, const uint8_t *data)
{
	(void)pdu;

	if (number == COAP_OPTION_URI_PATH) {
		copy_text(g_state.uri_path, sizeof(g_state.uri_path), data, length);
	}
	if (number == COAP_OPTION_CONTENT_FORMAT && length > 0u) {
		g_state.content_format = data[0];
	}

	return 1;
}

extern "C" int coap_add_data(coap_pdu_t *pdu, size_t len, const uint8_t *data)
{
	(void)pdu;

	if (len > sizeof(g_state.payload)) {
		len = sizeof(g_state.payload);
	}

	if (len > 0u) {
		memcpy(g_state.payload, data, len);
	}
	g_state.payload_len = len;

	return 1;
}

extern "C" size_t coap_encode_var_safe(uint8_t *buffer, size_t size,
		uint32_t value)
{
	if (size == 0u) {
		return 0u;
	}

	buffer[0] = (uint8_t)value;
	return 1u;
}

extern "C" coap_mid_t coap_send(coap_session_t *session, coap_pdu_t *pdu)
{
	(void)session;
	(void)pdu;
	return g_state.last_mid;
}

extern "C" int coap_io_process(coap_context_t *ctx, uint32_t timeout_ms)
{
	(void)ctx;
	g_state.timeout_ms = timeout_ms;

	if (g_state.send_recv_result < 0) {
		if (g_state.nack_handler != NULL) {
			coap_nack_reason_t reason = COAP_NACK_BAD_RESPONSE;

			if (g_state.send_recv_result == -5) {
				reason = COAP_NACK_TOO_MANY_RETRIES;
			} else if (g_state.send_recv_result == -6) {
				reason = COAP_NACK_NOT_DELIVERABLE;
			} else if (g_state.send_recv_result == -7) {
				reason = COAP_NACK_TLS_FAILED;
			}

			g_state.nack_handler(&g_default_session, &g_default_request_pdu,
					reason, g_state.last_mid);
		}

		return 1;
	}

	if (g_state.response_handler != NULL) {
		if (!g_state.suppress_response) {
			g_state.response_handler(&g_default_session,
					&g_default_request_pdu,
					&g_default_response_pdu,
					g_state.last_mid);
		}
	}

	return 1;
}

extern "C" int coap_io_pending(coap_context_t *context)
{
	(void)context;
	return 0;
}

extern "C" int coap_send_recv(coap_session_t *session, coap_pdu_t *request_pdu,
		coap_pdu_t **response_pdu, uint32_t timeout_ms)
{
	(void)session;
	(void)request_pdu;
	g_state.timeout_ms = timeout_ms;

	if (response_pdu != NULL) {
		*response_pdu = &g_default_response_pdu;
	}

	return g_state.send_recv_result;
}

extern "C" coap_pdu_code_t coap_pdu_get_code(const coap_pdu_t *pdu)
{
	(void)pdu;
	return g_state.response_code;
}

extern "C" coap_mid_t coap_pdu_get_mid(const coap_pdu_t *pdu)
{
	(void)pdu;
	return g_state.last_mid;
}

extern "C" int coap_get_data_large(const coap_pdu_t *pdu, size_t *len,
		const uint8_t **data, size_t *offset, size_t *total)
{
	(void)pdu;

	if (!g_state.has_response_payload) {
		return 0;
	}

	if (len != NULL) {
		*len = g_state.response_len;
	}
	if (data != NULL) {
		*data = g_state.response_payload;
	}
	if (offset != NULL) {
		*offset = g_state.response_offset;
	}
	if (total != NULL) {
		*total = g_state.response_total;
	}

	return 1;
}

extern "C" void coap_delete_pdu(coap_pdu_t *pdu)
{
	(void)pdu;
}

extern "C" void *coap_session_get_tls(const coap_session_t *session,
		coap_tls_library_t *tls_lib)
{
	(void)session;
	if (tls_lib != NULL) {
		*tls_lib = COAP_TLS_LIBRARY_OPENSSL;
	}
	return openssl_ssl_mock_get_ssl();
}
