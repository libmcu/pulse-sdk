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

#if !defined(PULSE_PAYLOAD_MARGIN)
#define PULSE_PAYLOAD_MARGIN		8u
#endif

#if !defined(METRICS_REPORT_INTERVAL_SEC)
#define METRICS_REPORT_INTERVAL_SEC	3600U
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
	case -EINPROGRESS:
		return PULSE_STATUS_IN_PROGRESS;
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

static bool interval_elapsed(void)
{
	uint64_t now = metrics_get_unix_timestamp();

	if (now == 0u) {
		return true;
	}

	if (!m.periodic_initialized) {
		return true;
	}

	if (now < m.last_report_time) {
		m.last_report_time = now;
	}

	return (now - m.last_report_time) >= METRICS_REPORT_INTERVAL_SEC;
}

static void free_flight_buf(void)
{
#if PULSE_STATIC_PAYLOAD_BUFSIZE == 0u
	free(m.flight_buf);
#endif
	m.flight_buf = NULL;
	m.flight_len = 0u;
	m.flight_bufsize = 0u;
	m.flight_from_store = false;
}

static void clear_in_flight(void)
{
	free_flight_buf();
	m.in_flight = false;
}

static pulse_status_t commit_flight(void)
{
	int err = 0;

	if (m.flight_from_store) {
		err = metricfs_del_first(m.conf.mfs, NULL);
	} else {
		metrics_reset();
	}

	uint64_t now = metrics_get_unix_timestamp();
	if (now != 0u) {
		m.last_report_time = now;
		m.periodic_initialized = true;
	}

	clear_in_flight();

	return (err == 0) ? PULSE_STATUS_OK
		: map_metrics_report_error(err);
}

static pulse_status_t abort_flight(int transmit_err)
{
	bool saved_to_backlog = false;

	if (!m.flight_from_store && m.conf.mfs != NULL) {
		if (metricfs_write(m.conf.mfs,
				m.flight_buf, m.flight_len, NULL) == 0) {
			metrics_reset();
			saved_to_backlog = true;
		}
	}

	clear_in_flight();

	if (saved_to_backlog) {
		return PULSE_STATUS_BACKLOG_PENDING;
	}

	return map_metrics_report_error(transmit_err);
}

static pulse_status_t phase_transmit(void)
{
	int err = metrics_report_transmit(m.flight_buf,
			m.flight_len, m.user_ctx);

	if (err == 0) {
		return commit_flight();
	}

	if (err == -EINPROGRESS) {
		return PULSE_STATUS_IN_PROGRESS;
	}

	return abort_flight(err);
}

static pulse_status_t collect_from_store(void)
{
	int n = metricfs_peek_first(m.conf.mfs,
			m.flight_buf, m.flight_bufsize, NULL);

	if (n > (int)m.flight_bufsize) {
		free_flight_buf();
		return PULSE_STATUS_BACKLOG_OVERFLOW;
	}

	if (n <= 0) {
		free_flight_buf();
		return PULSE_STATUS_EMPTY;
	}

	m.flight_len = (size_t)n;
	m.flight_from_store = true;
	m.in_flight = true;

	return phase_transmit();
}

static pulse_status_t collect_from_metrics(void)
{
	metrics_report_prepare(m.user_ctx);

	m.flight_len = metrics_collect(m.flight_buf, m.flight_bufsize);
	if (m.flight_len == 0u) {
		free_flight_buf();
		return has_backlog() ?
			PULSE_STATUS_BACKLOG_PENDING : PULSE_STATUS_EMPTY;
	}

	if (m.flight_len > m.flight_bufsize) {
		free_flight_buf();
		return PULSE_STATUS_OVERFLOW;
	}

	m.flight_from_store = false;
	m.in_flight = true;

	return phase_transmit();
}

static pulse_status_t phase_collect(void)
{
	pulse_status_t status;
	size_t payload_len;
	size_t payload_bufsize;

	payload_len = metrics_collect(NULL, 0u);
	if (payload_len == 0u && !has_backlog()) {
		return PULSE_STATUS_EMPTY;
	}

	status = derive_payload_bufsize(payload_len, &payload_bufsize);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	status = allocate_payload_buffer(payload_bufsize,
			&m.flight_buf, &m.flight_bufsize);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	if (has_backlog()) {
		return collect_from_store();
	}

	return collect_from_metrics();
}

const struct pulse_report_ctx *pulse_get_report_ctx(void)
{
	return &m;
}

pulse_status_t pulse_update_token(const char *token)
{
	if (token == NULL || strlen(token) > PULSE_TOKEN_LEN) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	m.conf.token = token;

	return PULSE_STATUS_OK;
}

pulse_status_t pulse_update_metricfs(struct metricfs *mfs)
{
	m.conf.mfs = mfs;

	return PULSE_STATUS_OK;
}

pulse_status_t pulse_set_response_handler(pulse_response_handler_t handler,
		void *ctx)
{
	m.on_response = handler;
	m.response_ctx = ctx;

	return PULSE_STATUS_OK;
}

pulse_status_t pulse_report(void)
{
	if (!m.initialized || m.conf.token == NULL) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	if (m.in_flight) {
		return phase_transmit();
	}

	if (!interval_elapsed() && !has_backlog()) {
		return PULSE_STATUS_TOO_SOON;
	}

	return phase_collect();
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
	case PULSE_STATUS_IN_PROGRESS:
		return "in progress";
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

	if (m.in_flight) {
		clear_in_flight();
	}

	m.conf = *pulse;
	m.user_ctx = pulse->ctx;
	m.on_response = NULL;
	m.response_ctx = NULL;
	m.last_report_time = 0u;
	m.periodic_initialized = false;
	force_reset = pulse->reset_metrics_on_init;

	metrics_init(force_reset);
	m.initialized = true;

	return PULSE_STATUS_OK;
}
