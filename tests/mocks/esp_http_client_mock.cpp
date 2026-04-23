#include "CppUTestExt/MockSupport.h"
#include <string.h>

extern "C" {
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
}

struct esp_http_client {
	int reserved;
};

http_event_handle_cb g_captured_event_handler;
void *g_captured_user_data;

static struct {
	uint8_t buf[4096];
	int len;
	bool pending;
} g_pending_data;

void esp_http_client_mock_inject_data(const void *data, int len)
{
	if (len > 0 && len <= (int)sizeof(g_pending_data.buf)) {
		memcpy(g_pending_data.buf, data, (size_t)len);
		g_pending_data.len = len;
		g_pending_data.pending = true;
	}
}

void esp_http_client_mock_reset_inject(void)
{
	memset(&g_pending_data, 0, sizeof(g_pending_data));
}

extern "C" esp_err_t esp_crt_bundle_attach(void *conf)
{
	(void)conf;

	return ESP_OK;
}

extern "C" esp_http_client_handle_t esp_http_client_init(
		const esp_http_client_config_t *config)
{
	g_captured_event_handler = config->event_handler;
	g_captured_user_data = config->user_data;

	return (esp_http_client_handle_t)mock().actualCall("esp_http_client_init")
		.withStringParameter("url", config->url)
		.withParameter("timeout_ms", config->timeout_ms)
		.withParameter("buffer_size", config->buffer_size)
		.withParameter("buffer_size_tx", config->buffer_size_tx)
		.withPointerParameter("crt_bundle_attach",
				(void *)config->crt_bundle_attach)
		.withParameter("is_async", (int)config->is_async)
		.returnPointerValue();
}

extern "C" esp_err_t esp_http_client_set_method(
		esp_http_client_handle_t client, esp_http_client_method_t method)
{
	return (esp_err_t)mock().actualCall("esp_http_client_set_method")
		.withPointerParameter("client", client)
		.withParameter("method", (int)method)
		.returnIntValue();
}

extern "C" esp_err_t esp_http_client_set_header(
		esp_http_client_handle_t client, const char *key, const char *value)
{
	return (esp_err_t)mock().actualCall("esp_http_client_set_header")
		.withPointerParameter("client", client)
		.withStringParameter("key", key)
		.withStringParameter("value", value)
		.returnIntValue();
}

extern "C" esp_err_t esp_http_client_set_post_field(
		esp_http_client_handle_t client, const char *data, int len)
{
	return (esp_err_t)mock().actualCall("esp_http_client_set_post_field")
		.withPointerParameter("client", client)
		.withMemoryBufferParameter("data", (const unsigned char *)data,
				(size_t)len)
		.withParameter("len", len)
		.returnIntValue();
}

extern "C" esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
	esp_err_t ret = (esp_err_t)mock().actualCall("esp_http_client_perform")
		.withPointerParameter("client", client)
		.returnIntValue();

	if (ret == ESP_OK && g_pending_data.pending
			&& g_captured_event_handler != NULL) {
		esp_http_client_event_t evt = {};
		evt.event_id = HTTP_EVENT_ON_DATA;
		evt.data = g_pending_data.buf;
		evt.data_len = g_pending_data.len;
		evt.user_data = g_captured_user_data;
		g_captured_event_handler(&evt);
		g_pending_data.pending = false;
	}

	return ret;
}

extern "C" int esp_http_client_get_status_code(
		esp_http_client_handle_t client)
{
	return mock().actualCall("esp_http_client_get_status_code")
		.withPointerParameter("client", client)
		.returnIntValue();
}

extern "C" int64_t esp_http_client_get_content_length(
		esp_http_client_handle_t client)
{
	return (int64_t)mock().actualCall("esp_http_client_get_content_length")
		.withPointerParameter("client", client)
		.returnLongIntValue();
}

extern "C" esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
	return (esp_err_t)mock().actualCall("esp_http_client_cleanup")
		.withPointerParameter("client", client)
		.returnIntValue();
}
