#include "CppUTestExt/MockSupport.h"

extern "C" {
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
}

struct esp_http_client {
	int reserved;
};

extern "C" esp_err_t esp_crt_bundle_attach(void *conf)
{
	(void)conf;

	return ESP_OK;
}

extern "C" esp_http_client_handle_t esp_http_client_init(
		const esp_http_client_config_t *config)
{
	return (esp_http_client_handle_t)mock().actualCall("esp_http_client_init")
		.withStringParameter("url", config->url)
		.withParameter("timeout_ms", config->timeout_ms)
		.withParameter("buffer_size", config->buffer_size)
		.withParameter("buffer_size_tx", config->buffer_size_tx)
		.withPointerParameter("crt_bundle_attach",
				(void *)config->crt_bundle_attach)
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
	return (esp_err_t)mock().actualCall("esp_http_client_perform")
		.withPointerParameter("client", client)
		.returnIntValue();
}

extern "C" int esp_http_client_get_status_code(
		esp_http_client_handle_t client)
{
	return mock().actualCall("esp_http_client_get_status_code")
		.withPointerParameter("client", client)
		.returnIntValue();
}

extern "C" esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
	return (esp_err_t)mock().actualCall("esp_http_client_cleanup")
		.withPointerParameter("client", client)
		.returnIntValue();
}
