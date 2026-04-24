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
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"

#include "libmcu/metrics_overrides.h"
#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
#include "pulse/pulse_overrides.h"

#define PULSE_HTTPS_TIMEOUT_MS		60000
#define PULSE_HTTPS_BUFFER_SIZE		4096
#define PULSE_HTTPS_CONTENT_TYPE	"application/cbor"

typedef enum {
	STATE_IDLE,
	STATE_IN_PROGRESS,
} https_state_t;

typedef struct {
	uint8_t buf[PULSE_HTTPS_BUFFER_SIZE];
	size_t len;
	bool truncated;
} response_buf_t;

typedef struct {
	esp_http_client_handle_t client;
	response_buf_t response;
	https_state_t state;
} https_session_t;

static https_session_t m_session;

static uint32_t get_transmit_timeout_ms(const struct pulse *conf)
{
	if (conf != NULL && conf->transmit_timeout_ms > 0u) {
		return conf->transmit_timeout_ms;
	}

	return PULSE_HTTPS_TIMEOUT_MS;
}

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
	if (evt->event_id != HTTP_EVENT_ON_DATA) {
		return ESP_OK;
	}

	response_buf_t *rb = (response_buf_t *)evt->user_data;
	size_t remaining = sizeof(rb->buf) - rb->len;
	size_t to_copy = (size_t)evt->data_len < remaining
			? (size_t)evt->data_len : remaining;

	if ((size_t)evt->data_len > remaining) {
		rb->truncated = true;
	}

	memcpy(rb->buf + rb->len, evt->data, to_copy);
	rb->len += to_copy;

	return ESP_OK;
}

static int map_esp_error(esp_err_t err)
{
	if (err == ESP_OK) {
		return 0;
	}

	if (err == ESP_ERR_TIMEOUT) {
		return -ETIMEDOUT;
	}

	if (err == ESP_ERR_HTTP_EAGAIN) {
		return -EAGAIN;
	}

	return -EIO;
}

static esp_http_client_handle_t create_client(response_buf_t *rb,
		const struct pulse *conf, bool is_async)
{
	const esp_http_client_config_t config = {
		.url = PULSE_INGEST_URL_HTTPS,
		.timeout_ms = (int)get_transmit_timeout_ms(conf),
		.buffer_size = PULSE_HTTPS_BUFFER_SIZE,
		.buffer_size_tx = PULSE_HTTPS_BUFFER_SIZE,
		.crt_bundle_attach = esp_crt_bundle_attach,
		.event_handler = on_http_event,
		.user_data = rb,
		.is_async = is_async,
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

static esp_err_t configure_request(esp_http_client_handle_t client,
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

static int check_response_status(esp_http_client_handle_t client)
{
	int status_code = esp_http_client_get_status_code(client);

	if (status_code < 200 || status_code >= 300) {
		return -EIO;
	}

	return 0;
}

static void deliver_response(const response_buf_t *rb,
		const struct pulse_report_ctx *rctx)
{
	if (rb->len > 0 && rctx != NULL && rctx->on_response != NULL) {
		rctx->on_response(rb->buf, rb->len, rctx->response_ctx);
	}
}

static void reset_session(https_session_t *s)
{
	s->client = NULL;
	s->state = STATE_IDLE;
	memset(&s->response, 0, sizeof(s->response));
}

static int finalize_session(https_session_t *s,
		const struct pulse_report_ctx *rctx)
{
	int ret = check_response_status(s->client);

	if (ret == 0 && s->response.truncated) {
		ret = -EMSGSIZE;
	}

	if (ret == 0) {
		deliver_response(&s->response, rctx);
	}

	esp_err_t cleanup_err = esp_http_client_cleanup(s->client);
	reset_session(s);

	if (ret == 0) {
		ret = map_esp_error(cleanup_err);
	}

	return ret;
}

static int start_session(https_session_t *s, const void *data, size_t datasize,
		const struct pulse_report_ctx *rctx, bool async)
{
	const struct pulse *conf = rctx ? &rctx->conf : NULL;

	s->client = create_client(&s->response, conf, async);
	if (s->client == NULL) {
		return -ENOMEM;
	}

	esp_err_t err = configure_request(s->client, data, datasize, conf);
	if (err != ESP_OK) {
		esp_http_client_cleanup(s->client);
		reset_session(s);
		return map_esp_error(err);
	}

	s->state = STATE_IN_PROGRESS;

	return 0;
}

static int advance_session(https_session_t *s,
		const struct pulse_report_ctx *rctx, bool async)
{
	const struct pulse *conf = rctx ? &rctx->conf : NULL;
	const uint64_t timeout_us =
		(uint64_t)get_transmit_timeout_ms(conf) * 1000u;
	const uint64_t start_us = async ? 0u : (uint64_t)esp_timer_get_time();
	esp_err_t err;

	do {
		err = esp_http_client_perform(s->client);
		if (((uint64_t)esp_timer_get_time() - start_us) >= timeout_us) {
			break;
		}
	} while (!async && err == ESP_ERR_HTTP_EAGAIN);

	if (!async && err == ESP_ERR_HTTP_EAGAIN) {
		esp_http_client_cleanup(s->client);
		reset_session(s);
		return -ETIMEDOUT;
	}

	if (err == ESP_ERR_HTTP_EAGAIN) {
		return -EINPROGRESS;
	}

	if (err != ESP_OK) {
		esp_http_client_cleanup(s->client);
		reset_session(s);
		return map_esp_error(err);
	}

	return finalize_session(s, rctx);
}

void pulse_transport_cancel(void)
{
	if (m_session.state != STATE_IDLE) {
		esp_http_client_cleanup(m_session.client);
	}
	reset_session(&m_session);
}

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	if (data == NULL || datasize == 0u) {
		return -EINVAL;
	}

	if (datasize > (size_t)INT_MAX) {
		return -EOVERFLOW;
	}

	const bool async = ctx && ctx->conf.async_transport;

	if (m_session.state == STATE_IDLE) {
		int ret = start_session(&m_session, data, datasize, ctx, async);
		if (ret != 0) {
			return ret;
		}
	}

	return advance_session(&m_session, ctx, async);
}
