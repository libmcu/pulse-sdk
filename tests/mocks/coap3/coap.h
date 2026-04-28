/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef COAP3_COAP_H
#define COAP3_COAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	COAP_PROTO_DTLS = 1,
} coap_proto_t;

typedef enum {
	COAP_MESSAGE_CON = 0,
} coap_pdu_type_t;

typedef enum {
	COAP_REQUEST_CODE_POST = 2,
} coap_pdu_code_t;

typedef enum {
	COAP_RESPONSE_OK = 0,
	COAP_RESPONSE_FAIL,
} coap_response_t;

typedef enum {
	COAP_NACK_TOO_MANY_RETRIES = 0,
	COAP_NACK_NOT_DELIVERABLE,
	COAP_NACK_RST,
	COAP_NACK_TLS_FAILED,
	COAP_NACK_ICMP_ISSUE,
	COAP_NACK_BAD_RESPONSE,
	COAP_NACK_TLS_LAYER_FAILED,
	COAP_NACK_WS_LAYER_FAILED,
	COAP_NACK_WS_FAILED,
} coap_nack_reason_t;

typedef int coap_mid_t;

typedef struct {
	size_t length;
	const uint8_t *s;
} coap_bin_const_t;

typedef struct {
	size_t length;
	const uint8_t *s;
} coap_str_const_t;

typedef struct coap_address_t {
	uint8_t opaque[32];
} coap_address_t;

typedef struct coap_addr_info_t {
	struct coap_addr_info_t *next;
	int scheme;
	coap_proto_t proto;
	coap_address_t addr;
} coap_addr_info_t;

typedef struct coap_context_t coap_context_t;
typedef struct coap_session_t coap_session_t;
typedef struct coap_pdu_t coap_pdu_t;

typedef enum {
	COAP_TLS_LIBRARY_NONE = 0,
	COAP_TLS_LIBRARY_OPENSSL,
	COAP_TLS_LIBRARY_GNUTLS,
	COAP_TLS_LIBRARY_MBEDTLS,
	COAP_TLS_LIBRARY_TINYDTLS,
} coap_tls_library_t;

void *coap_session_get_tls(const coap_session_t *session,
		coap_tls_library_t *tls_lib);

typedef struct {
	coap_bin_const_t identity;
	coap_bin_const_t key;
} coap_dtls_cpsk_info_t;

typedef struct coap_dtls_cpsk_t {
	uint8_t version;
	uint8_t reserved[7];
	const char *client_sni;
	coap_dtls_cpsk_info_t psk_info;
} coap_dtls_cpsk_t;

#define COAP_DTLS_CPSK_SETUP_VERSION	1
#define COAP_URI_SCHEME_COAPS		2
#define COAP_RESOLVE_TYPE_REMOTE	1
#define COAP_OPTION_URI_PATH		11
#define COAP_OPTION_CONTENT_FORMAT	12
#define COAP_MEDIATYPE_APPLICATION_CBOR	60
#define COAP_RESPONSE_CODE_CHANGED	((coap_pdu_code_t)68)
#define COAP_INVALID_MID		(-1)

typedef coap_response_t (*coap_response_handler_t)(coap_session_t *session,
		const coap_pdu_t *sent, const coap_pdu_t *received,
		coap_mid_t mid);
typedef void (*coap_nack_handler_t)(coap_session_t *session,
		const coap_pdu_t *sent, coap_nack_reason_t reason,
		coap_mid_t mid);

void coap_startup(void);
void coap_cleanup(void);
coap_context_t *coap_new_context(void *params);
void coap_free_context(coap_context_t *ctx);
coap_addr_info_t *coap_resolve_address_info(const coap_str_const_t *address,
		uint16_t port, uint16_t secure_port, uint16_t ws_port,
		uint16_t ws_secure_port, int ai_hints_flags, int scheme_hint_bits,
		int type);
void coap_free_address_info(coap_addr_info_t *info_list);
void coap_context_set_app_data(coap_context_t *context, void *data);
void *coap_context_get_app_data(const coap_context_t *context);
void coap_register_response_handler(coap_context_t *context,
		coap_response_handler_t handler);
void coap_register_nack_handler(coap_context_t *context,
		coap_nack_handler_t handler);
coap_session_t *coap_new_client_session_psk2(coap_context_t *ctx,
		const coap_address_t *local_if, const coap_address_t *server,
		coap_proto_t proto, coap_dtls_cpsk_t *setup_data);
coap_context_t *coap_session_get_context(const coap_session_t *session);
void coap_session_release(coap_session_t *session);
void coap_session_set_app_data(coap_session_t *session, void *data);
void *coap_session_get_app_data(const coap_session_t *session);
coap_pdu_t *coap_new_pdu(coap_pdu_type_t type, coap_pdu_code_t code,
		coap_session_t *session);
int coap_add_option(coap_pdu_t *pdu, uint16_t number, size_t length,
		const uint8_t *data);
int coap_add_data(coap_pdu_t *pdu, size_t len, const uint8_t *data);
size_t coap_encode_var_safe(uint8_t *buffer, size_t size, uint32_t value);
coap_mid_t coap_send(coap_session_t *session, coap_pdu_t *pdu);
int coap_io_process(coap_context_t *ctx, uint32_t timeout_ms);
int coap_io_pending(coap_context_t *context);
int coap_send_recv(coap_session_t *session, coap_pdu_t *request_pdu,
		coap_pdu_t **response_pdu, uint32_t timeout_ms);
coap_pdu_code_t coap_pdu_get_code(const coap_pdu_t *pdu);
coap_mid_t coap_pdu_get_mid(const coap_pdu_t *pdu);
int coap_get_data_large(const coap_pdu_t *pdu, size_t *len,
		const uint8_t **data, size_t *offset, size_t *total);
void coap_delete_pdu(coap_pdu_t *pdu);

void coap_mock_reset(void);
void coap_mock_set_context_result(coap_context_t *ctx);
void coap_mock_set_resolve_result(int enabled);
void coap_mock_set_session_result(coap_session_t *session);
void coap_mock_set_send_recv_result(int result);
void coap_mock_suppress_response(void);
void coap_mock_allow_response(void);
void coap_mock_set_response_code(coap_pdu_code_t code);
void coap_mock_set_response_payload(const void *data, size_t len,
		size_t offset, size_t total);
const char *coap_mock_last_client_sni(void);
const char *coap_mock_last_psk_identity(void);
const char *coap_mock_last_psk_key(void);
const char *coap_mock_last_uri_path(void);
uint32_t coap_mock_last_content_format(void);
uint32_t coap_mock_last_timeout_ms(void);
size_t coap_mock_last_payload_len(void);
const uint8_t *coap_mock_last_payload_data(void);

#ifdef __cplusplus
}
#endif

#endif /* COAP3_COAP_H */
