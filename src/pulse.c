/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if !defined(PULSE_STATIC_PAYLOAD_BUFSIZE)
#define PULSE_STATIC_PAYLOAD_BUFSIZE	0u
#endif

/* metrics_report_prepare() is called inside metrics_report_periodic() after
 * this dry-run, so metric values may grow before the real encode runs.
 * The margin covers that potential CBOR size increase. */
#if !defined(PULSE_PAYLOAD_MARGIN)
#define PULSE_PAYLOAD_MARGIN		8u
#endif

static struct pulse_report_ctx m;

#if PULSE_STATIC_PAYLOAD_BUFSIZE > 0u
static uint8_t payload_storage[PULSE_STATIC_PAYLOAD_BUFSIZE];
#endif

static size_t get_cbor_encoded_uint_size(uint64_t value)
{
	if (value < 24u) {
		return 1u;
	} else if (value <= 0xffu) {
		return 2u;
	} else if (value <= 0xffffu) {
		return 3u;
	} else if (value <= 0xffffffffu) {
		return 5u;
	}

	return 9u;
}

static size_t get_cbor_encoded_int_size(int64_t value)
{
	if (value >= 0) {
		return get_cbor_encoded_uint_size((uint64_t)value);
	}

	return get_cbor_encoded_uint_size((uint64_t)(-1 - value));
}

static size_t get_max_metric_entry_size(void)
{
	const size_t key_size = get_cbor_encoded_uint_size(UINT16_MAX);
	const size_t value_size = get_cbor_encoded_int_size(INT32_MIN);

#if defined(METRICS_SCHEMA_IBS)
	return key_size
		+ get_cbor_encoded_uint_size(5u)
		+ get_cbor_encoded_uint_size(UINT8_MAX)
		+ get_cbor_encoded_uint_size(UINT8_MAX)
		+ get_cbor_encoded_int_size(INT32_MIN)
		+ get_cbor_encoded_int_size(INT32_MAX)
		+ value_size;
#else
	return key_size + value_size;
#endif
}

static pulse_status_t derive_payload_bufsize(size_t current_payload_len,
		size_t *bufsize)
{
	const size_t metrics_max = metrics_count();
	const size_t max_entry_size = get_max_metric_entry_size();
	const size_t max_entries_size = metrics_max * max_entry_size;

	if (metrics_max > 0u &&
			max_entries_size / metrics_max != max_entry_size) {
		return PULSE_STATUS_OVERFLOW;
	}

        /* pulse-sdk.cmake/pulse-sdk.mk include libmcu's CBOR metrics encoder
         * for the supported integrations. Under that encoder,
         * metrics_collect(NULL, 0) includes metadata/header bytes even when no
         * metrics are currently set, so current_payload_len is a usable
         * header-size floor. If integrators replace the encoder contract and
         * return 0 here, this bounded estimate may become conservative and
         * report overflow instead of retrying dynamically. */
	if (current_payload_len > SIZE_MAX - max_entries_size) {
		return PULSE_STATUS_OVERFLOW;
	}

	*bufsize = current_payload_len + max_entries_size;
	if (*bufsize > SIZE_MAX - PULSE_PAYLOAD_MARGIN) {
		return PULSE_STATUS_OVERFLOW;
	}

	*bufsize += PULSE_PAYLOAD_MARGIN;

	return PULSE_STATUS_OK;
}

static pulse_status_t allocate_payload_buffer(size_t payload_len,
		uint8_t **buf, size_t *bufsize)
{
#if PULSE_STATIC_PAYLOAD_BUFSIZE > 0u
	if (payload_len > sizeof(payload_storage)) {
		return PULSE_STATUS_OVERFLOW;
	}
	*buf = payload_storage;
	*bufsize = sizeof(payload_storage);
#else
	*buf = (uint8_t *)malloc(payload_len);
	if (*buf == NULL) {
		return PULSE_STATUS_NO_MEMORY;
	}
	*bufsize = payload_len;
#endif

	return PULSE_STATUS_OK;
}

static pulse_status_t map_metrics_report_error(int err)
{
	switch (err) {
	case 0:
		return PULSE_STATUS_OK;
	case -EINVAL:
		return PULSE_STATUS_INVALID_ARGUMENT;
	case -EBADMSG:
		return PULSE_STATUS_BAD_FORMAT;
	case -EALREADY:
		return PULSE_STATUS_TOO_SOON;
	case -EAGAIN:
		return PULSE_STATUS_BACKLOG_PENDING;
	case -ETIMEDOUT:
		return PULSE_STATUS_TIMEOUT;
	case -ENOBUFS: /* fall through */
	case -EOVERFLOW:
		return PULSE_STATUS_BACKLOG_OVERFLOW;
	case -ENOSYS:
		return PULSE_STATUS_NOT_SUPPORTED;
	default:
		return PULSE_STATUS_IO;
	}
}

static bool has_backlog(void)
{
	return m.conf.mfs != NULL && metricfs_count(m.conf.mfs) > 0u;
}

const struct pulse_report_ctx *pulse_get_report_ctx(void)
{
	return &m;
}

pulse_status_t pulse_update_token(const char *token)
{
	/* Lock-free pointer replacement only. Concurrent pulse_report() calls
	 * are not synchronized by this API. */
	if (token == NULL || strlen(token) > PULSE_TOKEN_LEN) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	m.conf.token = token;

	return PULSE_STATUS_OK;
}

pulse_status_t pulse_update_metricfs(struct metricfs *mfs)
{
	/* Lock-free pointer replacement only. Concurrent pulse_report() calls
	 * are not synchronized by this API. */
	m.conf.mfs = mfs;

	return PULSE_STATUS_OK;
}

pulse_status_t pulse_set_response_handler(pulse_response_handler_t handler,
		void *ctx)
{
	/* Lock-free pointer replacement only. Concurrent transport callbacks
	 * are not synchronized by this API. */
	m.on_response = handler;
	m.response_ctx = ctx;

	return PULSE_STATUS_OK;
}

pulse_status_t pulse_report(void)
{
	pulse_status_t status;
	uint8_t *payload_buf = NULL;
	size_t payload_len = 0u;
	size_t payload_bufsize = 0u;
	int err;

	if (!m.initialized || m.conf.token == NULL) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	payload_len = metrics_collect(NULL, 0u);
	if (payload_len == 0u) {
		if (!has_backlog()) {
			return PULSE_STATUS_EMPTY;
		}
	}

	status = derive_payload_bufsize(payload_len, &payload_bufsize);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	status = allocate_payload_buffer(payload_bufsize,
			&payload_buf, &payload_bufsize);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	err = metrics_report_periodic(payload_buf, payload_bufsize,
			m.conf.mfs, m.user_ctx);

#if PULSE_STATIC_PAYLOAD_BUFSIZE == 0u
	free(payload_buf);
#endif

	return map_metrics_report_error(err);
}

const char *pulse_stringify_status(pulse_status_t status)
{
	switch (status) {
	case PULSE_STATUS_OK:
		return "ok";
	case PULSE_STATUS_INVALID_ARGUMENT:
		return "invalid argument";
	case PULSE_STATUS_BAD_FORMAT:
		return "bad format";
	case PULSE_STATUS_OVERFLOW:
		return "overflow";
	case PULSE_STATUS_IO:
		return "i/o error";
	case PULSE_STATUS_TIMEOUT:
		return "timeout";
	case PULSE_STATUS_NOT_SUPPORTED:
		return "not supported";
	case PULSE_STATUS_TOO_SOON:
		return "too soon";
	case PULSE_STATUS_EMPTY:
		return "empty";
	case PULSE_STATUS_BACKLOG_PENDING:
		return "backlog pending";
	case PULSE_STATUS_BACKLOG_OVERFLOW:
		return "backlog overflow";
	case PULSE_STATUS_NO_MEMORY:
		return "no memory";
	default:
		return "unknown";
	}
}

pulse_status_t pulse_init(struct pulse *pulse)
{
	bool force_reset = false;

	if (pulse == NULL) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	if (pulse->token != NULL && strlen(pulse->token) > PULSE_TOKEN_LEN) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	m.conf = *pulse;
	m.user_ctx = pulse->ctx;
	m.on_response = NULL;
	m.response_ctx = NULL;
	force_reset = pulse->reset_metrics_on_init;

	metrics_init(force_reset);
	m.initialized = true;

	return PULSE_STATUS_OK;
}
