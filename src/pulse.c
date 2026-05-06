/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"
#include "pulse_codec.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "libmcu/compiler.h"
#include "libmcu/metrics_overrides.h"

#include "cbor/cbor.h"

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

static bool is_in_flight(void)
{
	return m.in_flight;
}

static bool is_initialized(void)
{
	return m.initialized;
}

static bool is_required_string_valid(const char *value)
{
	return value != NULL && value[0] != '\0';
}

static bool is_token_valid(const char *token)
{
	return is_required_string_valid(token)
		&& strlen(token) <= PULSE_TOKEN_LEN;
}

static bool is_metadata_valid(const struct pulse *pulse)
{
	return is_required_string_valid(pulse->serial_number)
		&& is_required_string_valid(pulse->software_version);
}

static bool has_backlog(void)
{
	return m.conf.mfs != NULL && metricfs_count(m.conf.mfs) > 0u;
}

static void set_in_flight(bool in_flight)
{
	m.in_flight = in_flight;
}

static void free_flight_buf(void)
{
#if PULSE_STATIC_PAYLOAD_BUFSIZE == 0u
	free(m.flight_buf);
#endif
	m.flight_buf = NULL;
	m.flight_len = 0u;
	m.flight_bufsize = 0u;
	m.flight_window_start = 0u;
	m.flight_window_end = 0u;
	m.flight_reason = PULSE_SNAPSHOT_REASON_LIVE;
	m.flight_from_backlog = false;
	m.live_saved_during_flight = false;
}

static void clear_in_flight(void)
{
	free_flight_buf();
	set_in_flight(false);
}

static void set_last_report_time(uint64_t timestamp)
{
	m.last_report_time = timestamp;
}

static uint64_t get_last_report_time(void)
{
	return m.last_report_time;
}

static void finalize_live_metrics_snapshot(uint64_t timestamp)
{
	metrics_reset();
	if (timestamp != 0u) {
		set_last_report_time(timestamp);
		m.periodic_initialized = true;
	}
}

static bool is_interval_reached(const uint64_t now)
{
	if (now == 0u) {
		return true;
	}

	if (!m.periodic_initialized) {
		return true;
	}

	if (now < get_last_report_time()) {
		return false;
	}

	return (now - get_last_report_time()) >= METRICS_REPORT_INTERVAL_SEC;
}

static void invoke_prepare_chain(void)
{
	if (m.on_prepare != NULL) {
		m.on_prepare(m.prepare_ctx);
	}
}

static void set_live_window_bounds(void)
{
	const uint64_t now = metrics_get_unix_timestamp();

	m.flight_window_end = now;
	if (now == 0u) {
		m.flight_window_start = 0u;
		return;
	}

	if (m.periodic_initialized && get_last_report_time() <= now) {
		m.flight_window_start = get_last_report_time();
	} else {
		m.flight_window_start = now;
	}
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
	struct pulse_envelope_ctx ctx;
	const size_t metrics_max = metrics_count();
	const size_t max_entry_size = get_max_metric_entry_size();
	const size_t max_entries_size = metrics_max * max_entry_size;
	size_t max_metrics_payload_len;

	if (metrics_max > 0u &&
			max_entries_size / metrics_max != max_entry_size) {
		return PULSE_STATUS_OVERFLOW;
	}

	if (current_payload_len > SIZE_MAX - max_entries_size) {
		return PULSE_STATUS_OVERFLOW;
	}

	max_metrics_payload_len = current_payload_len + max_entries_size;
	if (max_metrics_payload_len < current_payload_len) {
		return PULSE_STATUS_OVERFLOW;
	}

	ctx.serial_number = m.conf.serial_number;
	ctx.software_version = m.conf.software_version;
	ctx.timestamp = UINT64_MAX;
	ctx.window_start = UINT64_MAX;
	ctx.window_end = UINT64_MAX;
	ctx.snapshot_reason = UINT8_MAX;

	*bufsize = max_metrics_payload_len + pulse_codec_max_envelope_overhead(
			&ctx, max_metrics_payload_len);
	if (*bufsize < max_metrics_payload_len
			|| *bufsize > SIZE_MAX - PULSE_PAYLOAD_MARGIN) {
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
	case -ECANCELED:
		return PULSE_STATUS_OK;
	case -EPROTO:
		return PULSE_STATUS_IO;
	case -EINPROGRESS:
		return PULSE_STATUS_IN_PROGRESS;
	case -ETIMEDOUT:
		return PULSE_STATUS_TIMEOUT;
	case -ENOBUFS: /* fall through */
	case -ENOENT: /* fall through */
	case -ENOSPC: /* fall through */
	case -EOVERFLOW:
		return PULSE_STATUS_BACKLOG_OVERFLOW;
	case -ENOSYS:
		return PULSE_STATUS_NOT_SUPPORTED;
	default:
		return PULSE_STATUS_IO;
	}
}

/* NOTE: Any metric changes that occur after metrics_collect() and before
 * metrics_reset() are silently lost regardless of the transmission outcome.
 * This is an inherent limitation of the current single-buffer design.
 * Alternatives: (1) double-buffer at the metrics layer, or (2) drop
 * snapshot_reason from the envelope and store the original flight buffer
 * as-is on abort, eliminating the need for re-collection entirely.
 *
 * with_user_callback: Pass true on the normal report path to invoke the
 * user-registered prepare handler before collecting. Pass false when
 * re-collecting after a failed transmit (e.g. abort_flight) to avoid
 * invoking the handler a second time. */
static pulse_status_t collect_live_payload(uint8_t reason,
		bool with_user_callback)
{
	struct pulse_envelope_ctx ctx;
	size_t metrics_len;
	cbor_writer_t writer;

	m.flight_reason = reason;
	if (with_user_callback) {
		invoke_prepare_chain();
	}
	set_live_window_bounds();

	metrics_len = metrics_collect(m.flight_buf, m.flight_bufsize, &writer);
	if (metrics_len > m.flight_bufsize) {
		return PULSE_STATUS_OVERFLOW;
	}

	ctx.serial_number = m.conf.serial_number;
	ctx.software_version = m.conf.software_version;
	ctx.timestamp = m.flight_window_end;
	ctx.window_start = m.flight_window_start;
	ctx.window_end = m.flight_window_end;
	ctx.snapshot_reason = m.flight_reason;

	return pulse_codec_wrap_metrics_payload(m.flight_buf, m.flight_bufsize,
			metrics_len, &ctx, &m.flight_len);
}

static bool save_current_flight_to_backlog(void)
{
	return m.flight_len > 0u && metricfs_write(m.conf.mfs,
			m.flight_buf, m.flight_len, NULL) == 0;
}

static pulse_status_t save_aborted_flight_to_backlog(int txn_err, bool *saved)
{
	if (m.live_saved_during_flight) {
		/* Preserve the original in-flight payload as-is. This keeps
		 * its LIVE reason and may append it after newer backlog data.
		 * A fuller fix should store the original payload separately
		 * from the mutable flight buffer, then rebuild or annotate that
		 * payload with BACKLOG_FAILURE/BACKLOG_CANCEL before enqueueing
		 * it in chronological order, without re-collecting from the
		 * already-reset metrics store. */
		*saved = save_current_flight_to_backlog();
		return PULSE_STATUS_OK;
	}

	pulse_status_t status = collect_live_payload((txn_err == -ECANCELED)
			? PULSE_SNAPSHOT_REASON_BACKLOG_CANCEL
			: PULSE_SNAPSHOT_REASON_BACKLOG_FAILURE,
		false);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	*saved = save_current_flight_to_backlog();
	if (*saved) {
		finalize_live_metrics_snapshot(m.flight_window_end);
	}

	return PULSE_STATUS_OK;
}

static pulse_status_t abort_flight(int txn_err)
{
	bool saved = false;

	if (!m.flight_from_backlog && m.conf.mfs != NULL) {
		const pulse_status_t status =
			save_aborted_flight_to_backlog(txn_err, &saved);
		if (status != PULSE_STATUS_OK) {
			clear_in_flight();
			return status;
		}
	}

	clear_in_flight();

	if (saved && txn_err == -ECANCELED) {
		return PULSE_STATUS_BACKLOG_PENDING;
	}

	return map_metrics_report_error(txn_err);
}

static pulse_status_t commit_flight(void)
{
	const bool was_from_backlog = m.flight_from_backlog;
	const bool live_saved_during_flight = m.live_saved_during_flight;
	int err = 0;

	if (m.flight_from_backlog) {
		err = metricfs_del_first(m.conf.mfs, NULL);
	} else if (!live_saved_during_flight) {
		finalize_live_metrics_snapshot(metrics_get_unix_timestamp());
	}

	clear_in_flight();

	if (err != 0) {
		return map_metrics_report_error(err);
	}

	return ((was_from_backlog || live_saved_during_flight) && has_backlog())
		? PULSE_STATUS_BACKLOG_PENDING : PULSE_STATUS_OK;
}

static pulse_status_t collect_from_backlog(void)
{
	int n = metricfs_peek_first(m.conf.mfs,
			m.flight_buf, m.flight_bufsize, NULL);

	if (n > (int)m.flight_bufsize) {
		return PULSE_STATUS_BACKLOG_OVERFLOW;
	}

	if (n == 0) {
		return PULSE_STATUS_EMPTY;
	}

	if (n < 0) {
		return map_metrics_report_error(n);
	}

	m.flight_len = (size_t)n;
	m.flight_from_backlog = true;
	set_in_flight(true);

	return PULSE_STATUS_OK;
}

static pulse_status_t collect_from_live_metrics(void)
{
	pulse_status_t status =
		collect_live_payload(PULSE_SNAPSHOT_REASON_LIVE, true);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	m.flight_from_backlog = false;
	set_in_flight(true);

	return PULSE_STATUS_OK;
}

static pulse_status_t write_live_metrics_to_backlog(void)
{
	pulse_status_t status =
		collect_live_payload(PULSE_SNAPSHOT_REASON_LIVE, true);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	int err = metricfs_write(m.conf.mfs, m.flight_buf, m.flight_len, NULL);
	if (err != 0) {
		return map_metrics_report_error(err);
	}

	finalize_live_metrics_snapshot(m.flight_window_end);

	return PULSE_STATUS_OK;
}

static pulse_status_t save_live_metrics_to_backlog(void)
{
	if (!is_in_flight()) {
		return write_live_metrics_to_backlog();
	}

	uint8_t *const saved_buf = (uint8_t *)malloc(m.flight_len);
	if (saved_buf == NULL) {
		return PULSE_STATUS_NO_MEMORY;
	}

	const size_t saved_len = m.flight_len;
	const uint64_t saved_window_start = m.flight_window_start;
	const uint64_t saved_window_end = m.flight_window_end;
	const uint8_t saved_reason = m.flight_reason;
	const bool saved_from_backlog = m.flight_from_backlog;
	const uint64_t saved_last_report_time = get_last_report_time();
	const bool saved_periodic_initialized = m.periodic_initialized;

	memcpy(saved_buf, m.flight_buf, saved_len);
	if (!saved_from_backlog && saved_window_end != 0u) {
		set_last_report_time(saved_window_end);
		m.periodic_initialized = true;
	}

	pulse_status_t status = write_live_metrics_to_backlog();
	if (status == PULSE_STATUS_OK && !saved_from_backlog) {
		m.live_saved_during_flight = true;
	}
	if (status != PULSE_STATUS_OK) {
		set_last_report_time(saved_last_report_time);
		m.periodic_initialized = saved_periodic_initialized;
	}

	memcpy(m.flight_buf, saved_buf, saved_len);
	free(saved_buf);

	m.flight_len = saved_len;
	m.flight_window_start = saved_window_start;
	m.flight_window_end = saved_window_end;
	m.flight_reason = saved_reason;
	m.flight_from_backlog = saved_from_backlog;

	return status;
}

static pulse_status_t do_collect(void)
{
	pulse_status_t status;
	size_t payload_len;
	size_t payload_bufsize;

	payload_len = metrics_collect(NULL, 0u, NULL);

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
		const uint64_t now = metrics_get_unix_timestamp();

		if (now != 0u && m.periodic_initialized
				&& is_interval_reached(now)) {
			(void)save_live_metrics_to_backlog();
		}

		status = collect_from_backlog();
	} else {
		status = collect_from_live_metrics();
	}

	if (status != PULSE_STATUS_OK) {
		free_flight_buf();
	}

	return status;
}

static pulse_status_t do_transmit(void)
{
	int err = pulse_transport_transmit(m.flight_buf, m.flight_len, &m);

	if (err == 0) {
		return commit_flight();
	}

	if (err == -EINPROGRESS) {
		return PULSE_STATUS_IN_PROGRESS;
	}

	return abort_flight(err);
}

pulse_status_t pulse_update_token(const char *token)
{
	if (!is_token_valid(token)) {
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

pulse_status_t pulse_set_prepare_handler(pulse_prepare_handler_t handler,
		void *ctx)
{
	m.on_prepare = handler;
	m.prepare_ctx = ctx;

	return PULSE_STATUS_OK;
}

uint32_t pulse_get_sec_until_next_report(void)
{
	const uint64_t now = metrics_get_unix_timestamp();

	if (!is_initialized() || is_in_flight() || has_backlog() || now == 0u
			|| !m.periodic_initialized) {
		return 0u;
	}

	if (now < get_last_report_time()) {
		return 0u;
	}

	const uint64_t elapsed_sec = now - get_last_report_time();
	if (elapsed_sec >= METRICS_REPORT_INTERVAL_SEC) {
		return 0u;
	}

	return METRICS_REPORT_INTERVAL_SEC - elapsed_sec;
}

pulse_status_t pulse_report(void)
{
	if (!is_initialized()) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	if (is_in_flight()) {
		const uint64_t now = metrics_get_unix_timestamp();
		if (now && m.conf.mfs != NULL && m.periodic_initialized
				&& is_interval_reached(now)) {
			(void)save_live_metrics_to_backlog();
		}

		return do_transmit();
	}

	if (!has_backlog()) {
		const uint64_t now = metrics_get_unix_timestamp();

		if (now != 0u && now < get_last_report_time()) {
			set_last_report_time(now);
			return PULSE_STATUS_TOO_SOON;
		}

		if (!is_interval_reached(now)) {
			return PULSE_STATUS_TOO_SOON;
		}
	}

	const pulse_status_t collect_status = do_collect();
	if (collect_status != PULSE_STATUS_OK) {
		return collect_status;
	}

	return do_transmit();
}

LIBMCU_WEAK void pulse_transport_cancel(void) {}

pulse_status_t pulse_cancel(void)
{
	if (!is_in_flight()) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	pulse_transport_cancel();

	return abort_flight(-ECANCELED);
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

	if (!is_token_valid(pulse->token) || !is_metadata_valid(pulse)) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	if (is_in_flight()) {
		pulse_transport_cancel();
		clear_in_flight();
	}

	m.conf = *pulse;
	m.on_response = NULL;
	m.response_ctx = NULL;
	m.on_prepare = NULL;
	m.prepare_ctx = NULL;
	m.live_saved_during_flight = false;
	m.periodic_initialized = false;
	force_reset = pulse->reset_metrics_on_init;
	set_last_report_time(0u);

	metrics_init(force_reset);
	m.initialized = true;

	return PULSE_STATUS_OK;
}
