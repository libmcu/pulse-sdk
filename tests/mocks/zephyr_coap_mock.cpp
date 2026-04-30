/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>

extern "C" {
#include "zephyr/net/coap.h"
}

static struct {
	char last_uri_path[32];
	uint32_t last_content_format;
	uint8_t last_payload[4096];
	size_t last_payload_len;
	uint8_t last_request_code;
	int parse_result;
	uint8_t response_code;
	uint8_t response_payload[5000];
	size_t response_payload_len;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t next_id;
} g_state;

extern "C" void zephyr_coap_mock_reset(void)
{
	memset(&g_state, 0, sizeof(g_state));
	for (size_t i = 0u; i < sizeof(g_state.token); i++) {
		g_state.token[i] = (uint8_t)i;
	}
	g_state.response_code = COAP_RESPONSE_CODE_CHANGED;
	g_state.next_id = 1u;
}

extern "C" void zephyr_coap_mock_set_parse_result(int result)
{
	g_state.parse_result = result;
}

extern "C" void zephyr_coap_mock_set_response(uint8_t code,
		const void *payload, size_t payload_len)
{
	g_state.response_code = code;
	if (payload_len > sizeof(g_state.response_payload)) {
		payload_len = sizeof(g_state.response_payload);
	}
	if (payload != NULL && payload_len > 0u) {
		memcpy(g_state.response_payload, payload, payload_len);
	}
	g_state.response_payload_len = payload_len;
}

extern "C" const char *zephyr_coap_mock_last_uri_path(void)
{
	return g_state.last_uri_path;
}

extern "C" uint32_t zephyr_coap_mock_last_content_format(void)
{
	return g_state.last_content_format;
}

extern "C" size_t zephyr_coap_mock_last_payload_len(void)
{
	return g_state.last_payload_len;
}

extern "C" const uint8_t *zephyr_coap_mock_last_payload(void)
{
	return g_state.last_payload;
}

extern "C" uint8_t zephyr_coap_mock_last_request_code(void)
{
	return g_state.last_request_code;
}

extern "C" int coap_packet_parse(struct coap_packet *cpkt, uint8_t *data,
		uint16_t len, struct coap_option *options, uint8_t opt_num)
{
	(void)data;
	(void)len;
	(void)options;
	(void)opt_num;

	if (g_state.parse_result != 0) {
		return g_state.parse_result;
	}

	memset(cpkt, 0, sizeof(*cpkt));
	cpkt->code = g_state.response_code;
	cpkt->payload = g_state.response_payload_len > 0u
		? g_state.response_payload : NULL;
	cpkt->payload_len = (uint16_t)g_state.response_payload_len;

	return 0;
}

extern "C" int coap_packet_init(struct coap_packet *cpkt, uint8_t *data,
		uint16_t max_len, uint8_t ver, uint8_t type, uint8_t token_len,
		const uint8_t *token, uint8_t code, uint16_t id)
{
	(void)ver;
	(void)type;

	if (cpkt == NULL || data == NULL || token_len > COAP_TOKEN_MAX_LEN) {
		return -EINVAL;
	}

	memset(cpkt, 0, sizeof(*cpkt));
	cpkt->data = data;
	cpkt->max_len = max_len;
	cpkt->hdr_len = (uint16_t)(4u + token_len);
	cpkt->offset = cpkt->hdr_len;
	cpkt->code = code;
	cpkt->id = id;
	cpkt->token_len = token_len;
	if (token != NULL && token_len > 0u) {
		memcpy(cpkt->token, token, token_len);
	}
	g_state.last_request_code = code;

	return 0;
}

extern "C" int coap_packet_append_option(struct coap_packet *cpkt,
		uint16_t code, const uint8_t *value, uint16_t len)
{
	if (cpkt == NULL) {
		return -EINVAL;
	}

	if (code == COAP_OPTION_URI_PATH) {
		size_t copy_len = len < sizeof(g_state.last_uri_path) - 1u
			? len : sizeof(g_state.last_uri_path) - 1u;
		memcpy(g_state.last_uri_path, value, copy_len);
		g_state.last_uri_path[copy_len] = '\0';
	}

	cpkt->opt_len++;
	cpkt->offset = (uint16_t)(cpkt->offset + len + 1u);

	return 0;
}

extern "C" int coap_append_option_int(struct coap_packet *cpkt,
		uint16_t code, unsigned int val)
{
	if (cpkt == NULL) {
		return -EINVAL;
	}

	if (code == COAP_OPTION_CONTENT_FORMAT) {
		g_state.last_content_format = val;
	}

	cpkt->opt_len++;
	cpkt->offset = (uint16_t)(cpkt->offset + 2u);

	return 0;
}

extern "C" int coap_packet_append_payload_marker(struct coap_packet *cpkt)
{
	if (cpkt == NULL) {
		return -EINVAL;
	}

	cpkt->offset++;
	return 0;
}

extern "C" int coap_packet_append_payload(struct coap_packet *cpkt,
		uint8_t *payload, size_t payload_len)
{
	if (cpkt == NULL || (payload == NULL && payload_len > 0u)) {
		return -EINVAL;
	}

	if (payload_len > sizeof(g_state.last_payload)) {
		return -ENOMEM;
	}

	if (payload_len > 0u) {
		memcpy(g_state.last_payload, payload, payload_len);
	}
	g_state.last_payload_len = payload_len;
	cpkt->payload = g_state.last_payload;
	cpkt->payload_len = (uint16_t)payload_len;
	cpkt->offset = (uint16_t)(cpkt->offset + payload_len);

	return 0;
}

extern "C" const uint8_t *coap_packet_get_payload(const struct coap_packet *cpkt,
		uint16_t *len)
{
	if (cpkt == NULL || len == NULL) {
		return NULL;
	}

	*len = cpkt->payload_len;
	return cpkt->payload;
}

extern "C" uint8_t coap_header_get_code(const struct coap_packet *cpkt)
{
	return cpkt != NULL ? cpkt->code : 0u;
}

extern "C" uint8_t *coap_next_token(void)
{
	return g_state.token;
}

extern "C" uint16_t coap_next_id(void)
{
	return g_state.next_id++;
}
