/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "libmcu/metrics_overrides.h"
#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"

#define PULSE_HTTPS_TIMEOUT_MS		60000
#define PULSE_HTTPS_BUFFER_SIZE		4096
#define PULSE_HTTPS_CONTENT_TYPE	"application/cbor"

static int map_esp_error(esp_err_t err)
{
	if (err == ESP_OK) {
		return 0;
	}

	if (err == ESP_ERR_TIMEOUT) {
		return -ETIMEDOUT;
	}

	return -EIO;
}

static esp_http_client_handle_t build_http_client(void)
{
	const esp_http_client_config_t config = {
		.url = PULSE_INGEST_URL_HTTPS,
		.timeout_ms = PULSE_HTTPS_TIMEOUT_MS,
		.buffer_size = PULSE_HTTPS_BUFFER_SIZE,
		.buffer_size_tx = PULSE_HTTPS_BUFFER_SIZE,
		.crt_bundle_attach = esp_crt_bundle_attach,
	};

	return esp_http_client_init(&config);
}

static esp_err_t set_auth_header(esp_http_client_handle_t client,
		const char *token)
{
	char auth[256];
	int n = snprintf(auth, sizeof(auth), "Bearer %s", token);

	if (n <= 0 || (size_t)n >= sizeof(auth)) {
		return ESP_FAIL;
	}

	return esp_http_client_set_header(client, "Authorization", auth);
}

static esp_err_t configure_http_request(esp_http_client_handle_t client,
		const void *data, size_t datasize, const struct pulse *conf)
{
	esp_err_t err;

	err = esp_http_client_set_method(client, HTTP_METHOD_POST);
	if (err != ESP_OK) {
		return err;
	}

	err = esp_http_client_set_header(client, "Content-Type",
			PULSE_HTTPS_CONTENT_TYPE);
	if (err != ESP_OK) {
		return err;
	}

	if (conf != NULL && conf->token != NULL) {
		err = set_auth_header(client, conf->token);
		if (err != ESP_OK) {
			return err;
		}
	}

	return esp_http_client_set_post_field(client, (const char *)data,
			(int)datasize);
}

static int process_http_response(esp_http_client_handle_t client,
		const struct pulse_report_ctx *rctx)
{
	int status_code = esp_http_client_get_status_code(client);

	if (status_code < 200 || status_code >= 300) {
		return -EIO;
	}

	int64_t content_length = esp_http_client_get_content_length(client);

	uint8_t *response = (uint8_t *)malloc(PULSE_HTTPS_BUFFER_SIZE);
	if (response == NULL) {
		return -ENOMEM;
	}

	int response_len = esp_http_client_read_response(client,
			(char *)response, PULSE_HTTPS_BUFFER_SIZE);

	if (response_len < 0) {
		free(response);
		return -EIO;
	}

	if (content_length > 0 && (int64_t)response_len < content_length) {
		free(response);
		return -EMSGSIZE;
	}

	if (response_len > 0 && rctx != NULL && rctx->on_response != NULL) {
		rctx->on_response(response, (size_t)response_len,
				rctx->response_ctx);
	}

	free(response);
	return 0;
}

int metrics_report_transmit(const void *data, size_t datasize, void *ctx)
{
	(void)ctx;
	const struct pulse_report_ctx *rctx = pulse_get_report_ctx();
	const struct pulse *conf = rctx ? &rctx->conf : NULL;
	int ret;

	if (data == NULL || datasize == 0u) {
		return -EINVAL;
	}

	if (datasize > (size_t)INT_MAX) {
		return -EOVERFLOW;
	}

	esp_http_client_handle_t client = build_http_client();
	if (client == NULL) {
		return -ENOMEM;
	}

	ret = map_esp_error(configure_http_request(client, data, datasize,
			conf));
	if (ret == 0) {
		ret = map_esp_error(esp_http_client_perform(client));
	}
	if (ret == 0) {
		ret = process_http_response(client, rctx);
	}

	esp_err_t cleanup_err = esp_http_client_cleanup(client);
	if (ret == 0) {
		ret = map_esp_error(cleanup_err);
	}

	return ret;
}
