/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ESP_HTTP_CLIENT_H
#define ESP_HTTP_CLIENT_H

#include <stdint.h>

typedef int32_t esp_err_t;

#define ESP_OK			0
#define ESP_FAIL		-1
#define ESP_ERR_TIMEOUT		-2

typedef enum {
	HTTP_METHOD_GET = 0,
	HTTP_METHOD_POST,
	HTTP_METHOD_PUT,
	HTTP_METHOD_PATCH,
	HTTP_METHOD_DELETE,
} esp_http_client_method_t;

struct esp_http_client;
typedef struct esp_http_client *esp_http_client_handle_t;

typedef esp_err_t (*esp_crt_bundle_attach_t)(void *conf);

typedef struct {
	const char *url;
	int timeout_ms;
	int buffer_size;
	int buffer_size_tx;
	esp_crt_bundle_attach_t crt_bundle_attach;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(
		const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client,
		esp_http_client_method_t method);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
		const char *key, const char *value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
		const char *data, int len);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
int esp_http_client_read_response(esp_http_client_handle_t client,
		char *buffer, int len);

#endif /* ESP_HTTP_CLIENT_H */
