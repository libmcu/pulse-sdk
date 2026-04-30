/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"

#include <curl/curl.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PULSE_HTTPS_TIMEOUT_MS		60000L
#define PULSE_HTTPS_BUFFER_SIZE		4096U
#define PULSE_HTTPS_AUTH_HEADER_SIZE	256U

typedef struct {
	uint8_t data[PULSE_HTTPS_BUFFER_SIZE];
	size_t len;
	bool truncated;
} response_buffer_t;

typedef struct {
	char auth_header[PULSE_HTTPS_AUTH_HEADER_SIZE];
	struct curl_slist *list;
} request_headers_t;

static pthread_mutex_t curl_global_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int curl_global_refcount;

static void transport_debug(const char *message)
{
	fprintf(stderr, "[pulse/linux] %s\n", message);
}

static void transport_debugf(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "[pulse/linux] ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static long get_transmit_timeout_ms(const struct pulse *conf)
{
	if (conf != NULL && conf->transmit_timeout_ms > 0u) {
		return (long)conf->transmit_timeout_ms;
	}

	return PULSE_HTTPS_TIMEOUT_MS;
}

static int map_curl_error(CURLcode code)
{
	/* Map libcurl failure classes to errno-style return codes for callers/tests. */
	switch (code) {
	case CURLE_OK:
		return 0;
	case CURLE_OPERATION_TIMEDOUT:
		return -ETIMEDOUT;
	case CURLE_OUT_OF_MEMORY:
		return -ENOMEM;
	case CURLE_COULDNT_RESOLVE_HOST:
		return -EHOSTUNREACH;
	case CURLE_COULDNT_CONNECT:
		return -ECONNREFUSED;
	case CURLE_WRITE_ERROR:
	case CURLE_RECV_ERROR:
		return -EIO;
	default:
		return -EIO;
	}
}

static int acquire_curl_global(void)
{
	int ret = 0;

	pthread_mutex_lock(&curl_global_lock);
	if (curl_global_refcount == 0u) {
		CURLcode err = curl_global_init(CURL_GLOBAL_DEFAULT);

		if (err != CURLE_OK) {
			ret = map_curl_error(err);
		} else {
			curl_global_refcount = 1u;
		}
	} else {
		curl_global_refcount++;
	}
	pthread_mutex_unlock(&curl_global_lock);

	return ret;
}

static void release_curl_global(void)
{
	pthread_mutex_lock(&curl_global_lock);
	if (curl_global_refcount > 0u) {
		curl_global_refcount--;
		if (curl_global_refcount == 0u) {
			curl_global_cleanup();
		}
	}
	pthread_mutex_unlock(&curl_global_lock);
}

static size_t on_http_response(char *ptr, size_t size, size_t nmemb,
		void *userdata)
{
	response_buffer_t *response = (response_buffer_t *)userdata;
	const size_t bytes = size * nmemb;
	const size_t remaining = sizeof(response->data) - response->len;
	const size_t to_copy = bytes < remaining ? bytes : remaining;

	if (bytes > remaining) {
		response->truncated = true;
	}

	if (to_copy > 0u) {
		memcpy(response->data + response->len, ptr, to_copy);
		response->len += to_copy;
	}

	return bytes;
}

static int append_header(request_headers_t *headers, const char *data)
{
	struct curl_slist *next = curl_slist_append(headers->list, data);

	if (next == NULL) {
		return -ENOMEM;
	}

	headers->list = next;

	return 0;
}

static int build_headers(request_headers_t *headers, const struct pulse *conf)
{
	int ret;

	memset(headers, 0, sizeof(*headers));

	ret = append_header(headers, "Content-Type: application/cbor");
	if (ret < 0) {
		return ret;
	}

	if (conf == NULL || conf->token == NULL) {
		return 0;
	}

	if (snprintf(headers->auth_header, sizeof(headers->auth_header),
				"Authorization: Bearer %s", conf->token)
			>= (int)sizeof(headers->auth_header)) {
		return -EOVERFLOW;
	}

	return append_header(headers, headers->auth_header);
}

static void release_headers(request_headers_t *headers)
{
	if (headers->list != NULL) {
		curl_slist_free_all(headers->list);
		headers->list = NULL;
	}
}

static int configure_request(CURL *handle, request_headers_t *headers,
		response_buffer_t *response, const void *data, size_t datasize,
		const struct pulse *conf)
{
	CURLcode err;

	err = curl_easy_setopt(handle, CURLOPT_URL, PULSE_INGEST_URL_HTTPS);
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	err = curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers->list);
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	err = curl_easy_setopt(handle, CURLOPT_POST, 1L);
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	err = curl_easy_setopt(handle, CURLOPT_POSTFIELDS, data);
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	err = curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE,
			(curl_off_t)datasize);
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	err = curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
			get_transmit_timeout_ms(conf));
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	err = curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	err = curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, on_http_response);
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	err = curl_easy_setopt(handle, CURLOPT_WRITEDATA, response);
	if (err != CURLE_OK) {
		return map_curl_error(err);
	}

	return 0;
}

static int check_response_status(CURL *handle)
{
	long status_code = 0;
	CURLcode err = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE,
			&status_code);

	if (err != CURLE_OK) {
		transport_debug("failed to query HTTP response status");
		return map_curl_error(err);
	}

	transport_debugf("HTTP status=%ld", status_code);

	if (status_code < 200L || status_code >= 300L) {
		transport_debug("server returned non-success status");
		return -EIO;
	}

	return 0;
}

static void deliver_response(const response_buffer_t *response,
		const struct pulse_report_ctx *ctx)
{
	if (response->len > 0u && ctx != NULL && ctx->on_response != NULL) {
		ctx->on_response(response->data, response->len, ctx->response_ctx);
	}
}

void pulse_transport_cancel(void)
{
}

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	const struct pulse *conf = ctx != NULL ? &ctx->conf : NULL;
	request_headers_t headers = { 0 };
	response_buffer_t response = { 0 };
	CURL *handle = NULL;
	int ret;
	bool curl_global_started = false;

	if (data == NULL || datasize == 0u) {
		transport_debug("invalid transmit arguments");
		return -EINVAL;
	}

	if (datasize > (size_t)INT64_MAX) {
		transport_debug("payload too large for curl request");
		return -EOVERFLOW;
	}

	if (conf != NULL && conf->async_transport) {
		transport_debug("async transport is not supported on linux");
		return -ENOTSUP;
	}

	ret = acquire_curl_global();
	if (ret < 0) {
		transport_debug("curl_global_init failed");
		return ret;
	}
	curl_global_started = true;

	handle = curl_easy_init();
	if (handle == NULL) {
		transport_debug("curl_easy_init failed");
		ret = -ENOMEM;
		goto out;
	}

	transport_debugf("sending report bytes=%zu timeout_ms=%ld",
			datasize, get_transmit_timeout_ms(conf));
	ret = build_headers(&headers, conf);
	if (ret < 0) {
		transport_debug("failed to build request headers");
		goto out;
	}

	ret = configure_request(handle, &headers, &response, data, datasize, conf);
	if (ret < 0) {
		transport_debug("failed to configure curl request");
		goto out;
	}
	ret = map_curl_error(curl_easy_perform(handle));
	if (ret < 0) {
		transport_debugf("curl perform failed err=%d", ret);
		goto out;
	}

	ret = check_response_status(handle);
	if (ret < 0) {
		goto out;
	}

	if (response.truncated) {
		transport_debug("response body exceeded local buffer");
		ret = -EMSGSIZE;
		goto out;
	}

	transport_debugf("report delivered response_bytes=%zu", response.len);
	deliver_response(&response, ctx);
	ret = 0;

out:
	release_headers(&headers);
	if (handle != NULL) {
		curl_easy_cleanup(handle);
	}
	if (curl_global_started) {
		release_curl_global();
	}

	return ret;
}
