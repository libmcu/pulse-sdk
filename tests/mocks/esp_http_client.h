/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ESP_HTTP_CLIENT_H
#define ESP_HTTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

typedef int32_t esp_err_t;

#define ESP_OK			0
#define ESP_FAIL		-1
#define ESP_ERR_TIMEOUT		-2
#define ESP_ERR_HTTP_EAGAIN	-3

typedef enum {
	HTTP_METHOD_GET = 0,
	HTTP_METHOD_POST,
	HTTP_METHOD_PUT,
	HTTP_METHOD_PATCH,
	HTTP_METHOD_DELETE,
} esp_http_client_method_t;

typedef enum {
	HTTP_EVENT_ERROR = 0,
	HTTP_EVENT_ON_CONNECTED,
	HTTP_EVENT_HEADERS_SENT,
	HTTP_EVENT_HEADER_SENT = HTTP_EVENT_HEADERS_SENT,
	HTTP_EVENT_ON_HEADER,
	HTTP_EVENT_ON_DATA,
	HTTP_EVENT_ON_FINISH,
	HTTP_EVENT_DISCONNECTED,
	HTTP_EVENT_REDIRECT,
} esp_http_client_event_id_t;

struct esp_http_client;
typedef struct esp_http_client *esp_http_client_handle_t;

typedef struct esp_http_client_event {
	esp_http_client_event_id_t event_id;
	esp_http_client_handle_t client;
	void *data;
	int data_len;
	void *user_data;
	char *header_key;
	char *header_value;
} esp_http_client_event_t;

typedef esp_err_t (*esp_crt_bundle_attach_t)(void *conf);
typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);

typedef struct {
	const char *url;
	int timeout_ms;
	int buffer_size;
	int buffer_size_tx;
	esp_crt_bundle_attach_t crt_bundle_attach;
	http_event_handle_cb event_handler;
	void *user_data;
	bool is_async;
	const char *user_agent;
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

extern http_event_handle_cb g_captured_event_handler;
extern void *g_captured_user_data;

void esp_http_client_mock_inject_data(const void *data, int len);
void esp_http_client_mock_reset_inject(void);

#endif /* ESP_HTTP_CLIENT_H */
