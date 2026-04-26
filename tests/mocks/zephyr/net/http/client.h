/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MOCK_ZEPHYR_NET_HTTP_CLIENT_H
#define MOCK_ZEPHYR_NET_HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

enum http_method {
	HTTP_DELETE = 0,
	HTTP_GET,
	HTTP_HEAD,
	HTTP_POST,
	HTTP_PUT,
	HTTP_PATCH,
};

enum http_final_call {
	HTTP_DATA_MORE = 0,
	HTTP_DATA_FINAL,
};

struct http_response {
	const uint8_t *body_frag_start;
	size_t body_frag_len;
	uint16_t http_status_code;
};

typedef int (*http_response_cb)(struct http_response *rsp,
		enum http_final_call final_data, void *user_data);

struct http_request {
	enum http_method method;
	const char *url;
	const char *protocol;
	const char *host;
	const char *port;
	const char **header_fields;
	const char *payload;
	size_t payload_len;
	http_response_cb response;
	uint8_t *recv_buf;
	size_t recv_buf_len;
};

int http_client_req(int sock, struct http_request *req,
		int32_t timeout, void *user_data);

void zephyr_http_mock_set_response(uint16_t status_code,
		const void *body, size_t body_len);
void zephyr_http_mock_set_fragmented_response(uint16_t status_code,
		const void *body1, size_t body1_len,
		const void *body2, size_t body2_len);
void zephyr_http_mock_inject_overflow(void);
void zephyr_http_mock_reset(void);

#endif /* MOCK_ZEPHYR_NET_HTTP_CLIENT_H */
