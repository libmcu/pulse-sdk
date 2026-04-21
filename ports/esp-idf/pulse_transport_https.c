/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

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

int metrics_report_transmit(const void *data, size_t datasize, void *ctx)
{
	(void)ctx;
	const struct pulse_report_ctx *rctx = pulse_get_report_ctx();
	const struct pulse *conf = rctx ? &rctx->conf : NULL;
	esp_http_client_handle_t client;
	esp_http_client_config_t config;
	int status_code;
	int ret;
	esp_err_t err;

	if (data == NULL || datasize == 0u) {
		return -EINVAL;
	}

	if (datasize > (size_t)INT_MAX) {
		return -EOVERFLOW;
	}

	config = (esp_http_client_config_t) {
		.url = PULSE_INGEST_URL_HTTPS,
		.timeout_ms = PULSE_HTTPS_TIMEOUT_MS,
		.buffer_size = PULSE_HTTPS_BUFFER_SIZE,
		.buffer_size_tx = PULSE_HTTPS_BUFFER_SIZE,
		.crt_bundle_attach = esp_crt_bundle_attach,
	};

	client = esp_http_client_init(&config);
	if (client == NULL) {
		return -ENOMEM;
	}

	err = esp_http_client_set_method(client, HTTP_METHOD_POST);
	if (err == ESP_OK) {
		err = esp_http_client_set_header(client, "Content-Type",
				PULSE_HTTPS_CONTENT_TYPE);
	}
	if (err == ESP_OK && conf != NULL && conf->token != NULL) {
		char auth[256];
		int n = snprintf(auth, sizeof(auth), "Bearer %s", conf->token);

		if (n > 0 && (size_t)n < sizeof(auth)) {
			err = esp_http_client_set_header(client,
					"Authorization", auth);
		} else {
			err = ESP_FAIL;
		}
	}
	if (err == ESP_OK) {
		err = esp_http_client_set_post_field(client, data,
				(int)datasize);
	}

	ret = map_esp_error(err);
	if (ret == 0) {
		err = esp_http_client_perform(client);
		ret = map_esp_error(err);
	}

	if (ret == 0) {
		status_code = esp_http_client_get_status_code(client);
		if (status_code < 200 || status_code >= 300) {
			ret = -EIO;
		}
	}

	err = esp_http_client_cleanup(client);
	if (ret == 0) {
		ret = map_esp_error(err);
	}

	return ret;
}
