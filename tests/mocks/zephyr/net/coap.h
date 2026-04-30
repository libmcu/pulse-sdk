/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MOCK_ZEPHYR_NET_COAP_H
#define MOCK_ZEPHYR_NET_COAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COAP_VERSION_1			1u
#define COAP_TYPE_CON			0u
#define COAP_OPTION_URI_PATH		11u
#define COAP_OPTION_CONTENT_FORMAT	12u
#define COAP_METHOD_POST		2u
#define COAP_CONTENT_FORMAT_APP_CBOR	60u
#define COAP_RESPONSE_CODE_CHANGED	68u
#define COAP_TOKEN_MAX_LEN		8u

struct coap_option {
	uint16_t code;
	const uint8_t *value;
	uint16_t len;
};

struct coap_packet {
	uint8_t *data;
	uint16_t max_len;
	uint16_t offset;
	uint16_t hdr_len;
	uint16_t opt_len;
	uint16_t payload_len;
	const uint8_t *payload;
	uint8_t code;
	uint16_t id;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t token_len;
};

int coap_packet_parse(struct coap_packet *cpkt, uint8_t *data, uint16_t len,
		struct coap_option *options, uint8_t opt_num);
int coap_packet_init(struct coap_packet *cpkt, uint8_t *data, uint16_t max_len,
		uint8_t ver, uint8_t type, uint8_t token_len,
		const uint8_t *token, uint8_t code, uint16_t id);
int coap_packet_append_option(struct coap_packet *cpkt, uint16_t code,
		const uint8_t *value, uint16_t len);
int coap_append_option_int(struct coap_packet *cpkt, uint16_t code,
		unsigned int val);
int coap_packet_append_payload_marker(struct coap_packet *cpkt);
int coap_packet_append_payload(struct coap_packet *cpkt, uint8_t *payload,
		size_t payload_len);
const uint8_t *coap_packet_get_payload(const struct coap_packet *cpkt,
		uint16_t *len);
uint8_t coap_header_get_code(const struct coap_packet *cpkt);
uint8_t *coap_next_token(void);
uint16_t coap_next_id(void);

void zephyr_coap_mock_reset(void);
void zephyr_coap_mock_set_parse_result(int result);
void zephyr_coap_mock_set_response(uint8_t code,
		const void *payload, size_t payload_len);
const char *zephyr_coap_mock_last_uri_path(void);
uint32_t zephyr_coap_mock_last_content_format(void);
size_t zephyr_coap_mock_last_payload_len(void);
const uint8_t *zephyr_coap_mock_last_payload(void);
uint8_t zephyr_coap_mock_last_request_code(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_ZEPHYR_NET_COAP_H */
