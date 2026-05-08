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

#if !defined(PULSE_WARN)
#define PULSE_WARN(...)
#endif
#if !defined(PULSE_ERROR)
#define PULSE_ERROR(...)
#endif
#if !defined(PULSE_INFO)
#define PULSE_INFO(...)
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

struct pulse_timing_state {
	uint64_t flight_window_start;
	uint64_t flight_window_end;
	uint64_t last_report_time;
	bool periodic_initialized;
};

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
	m.live_presave_during_flight = false;
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

static void save_timing_state(struct pulse_timing_state *state)
{
	state->flight_window_start = m.flight_window_start;
	state->flight_window_end = m.flight_window_end;
	state->last_report_time = get_last_report_time();
	state->periodic_initialized = m.periodic_initialized;
}

static void restore_timing_state(const struct pulse_timing_state *state)
{
	m.flight_window_start = state->flight_window_start;
	m.flight_window_end = state->flight_window_end;
	set_last_report_time(state->last_report_time);
	m.periodic_initialized = state->periodic_initialized;
}

static void update_last_report_time_for_window(uint64_t window_end)
{
	if (window_end != 0u) {
		set_last_report_time(window_end);
		m.periodic_initialized = true;
	}
}

static void finalize_live_metrics_snapshot(uint64_t timestamp)
{
	/* NOTE: Metrics recorded after collection and before this reset are
	 * not included in either the transmitted or saved snapshot. */
	metrics_reset();
	update_last_report_time_for_window(timestamp);
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

static bool is_live_presave_interval_reached(const uint64_t now)
{
	if (is_in_flight() && !m.flight_from_backlog &&
			m.flight_window_end != 0u) {
		const uint64_t anchor = get_last_report_time() > m.flight_window_end
			? get_last_report_time() : m.flight_window_end;

		if (now < anchor) {
			return false;
		}

		return (now - anchor) >= METRICS_REPORT_INTERVAL_SEC;
	}

	return is_interval_reached(now);
}

static uint64_t get_live_window_start(uint64_t now,
		uint64_t last_report_time)
{
	if (now == 0u) {
		return 0u;
	}

	return (m.periodic_initialized && last_report_time < now)
		? last_report_time : 0u;
}

static void get_live_window_bounds(uint64_t now,
		uint64_t *window_start, uint64_t *window_end)
{
	uint64_t last_report_time = get_last_report_time();

	if (is_in_flight() && !m.flight_from_backlog
			&& m.flight_window_end > last_report_time) {
		last_report_time = m.flight_window_end;
	}

	*window_end = now;
	*window_start = get_live_window_start(now, last_report_time);
}

static void invoke_prepare_chain(void)
{
	if (m.on_prepare != NULL) {
		m.on_prepare(m.prepare_ctx);
	}
}

static void set_live_window_bounds(uint64_t now)
{
	const uint64_t last_report_time = get_last_report_time();

	m.flight_window_end = now;
	m.flight_window_start = get_live_window_start(now, last_report_time);

	PULSE_INFO("live window bounds: now=%llu last=%llu initialized=%u start=%llu end=%llu",
			(unsigned long long)now,
			(unsigned long long)last_report_time,
			m.periodic_initialized ? 1u : 0u,
			(unsigned long long)m.flight_window_start,
			(unsigned long long)m.flight_window_end);
	if (m.flight_window_start != 0u
			&& m.flight_window_end <= m.flight_window_start) {
		PULSE_WARN("invalid live window bounds: start=%llu end=%llu",
				(unsigned long long)m.flight_window_start,
				(unsigned long long)m.flight_window_end);
	}
}

static void set_envelope_ctx(struct pulse_envelope_ctx *ctx,
		uint64_t timestamp, uint64_t window_start, uint64_t window_end,
		uint8_t reason)
{
	ctx->serial_number = m.conf.serial_number;
	ctx->software_version = m.conf.software_version;
	ctx->timestamp = timestamp;
	ctx->window_start = window_start;
	ctx->window_end = window_end;
	ctx->snapshot_reason = reason;
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

	set_envelope_ctx(&ctx, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT8_MAX);

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

static pulse_status_t collect_live_payload_to_buffer(uint8_t reason,
		bool with_user_callback, uint64_t now,
		uint8_t *buf, size_t bufsize,
		size_t *encoded_len, uint64_t *window_end);

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
		bool with_user_callback, uint64_t now)
{
	struct pulse_envelope_ctx ctx;
	size_t metrics_len;
	cbor_writer_t writer;

	m.flight_reason = reason;
	if (with_user_callback) {
		invoke_prepare_chain();
	}
	set_live_window_bounds(now);

	set_envelope_ctx(&ctx, m.flight_window_end, m.flight_window_start,
			m.flight_window_end, m.flight_reason);

	metrics_len = metrics_collect(m.flight_buf, m.flight_bufsize, &writer);
	if (metrics_len > m.flight_bufsize) {
		return PULSE_STATUS_OVERFLOW;
	}

	return pulse_codec_wrap_metrics_payload(m.flight_buf, m.flight_bufsize,
			metrics_len, &ctx, &m.flight_len);
}

static bool save_current_flight_to_backlog(void)
{
	if (m.flight_len == 0u) {
		return false;
	}

	int err = metricfs_write(m.conf.mfs, m.flight_buf, m.flight_len, NULL);
	if (err != 0) {
		PULSE_ERROR("current flight backlog write failed: err=%d", err);
		return false;
	}

	return true;
}

static pulse_status_t save_aborted_flight_to_backlog(int txn_err,
		bool *saved, uint64_t now)
{
	if (m.live_presave_during_flight) {
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
		false, now);
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
	/* Re-read time at abort so slow transports do not finalize against
	 * the earlier collection timestamp. */
	const uint64_t now = metrics_get_unix_timestamp();
	bool saved = false;

	if (!m.flight_from_backlog && m.conf.mfs != NULL) {
		const pulse_status_t status =
			save_aborted_flight_to_backlog(txn_err, &saved, now);
		if (status != PULSE_STATUS_OK) {
			PULSE_ERROR("abort save failed: status=%d err=%d",
					(int)status, txn_err);
			clear_in_flight();
			return status;
		}
	}

	clear_in_flight();
	if (saved || txn_err == -ECANCELED) {
		PULSE_INFO("transmit abort: err=%d saved=%u",
				txn_err, saved ? 1u : 0u);
	}

	if (saved && txn_err == -ECANCELED) {
		return PULSE_STATUS_BACKLOG_PENDING;
	}

	return map_metrics_report_error(txn_err);
}

static pulse_status_t commit_flight(void)
{
	const bool was_from_backlog = m.flight_from_backlog;
	const bool live_presave_during_flight = m.live_presave_during_flight;
	int err = 0;

	if (m.flight_from_backlog) {
		err = metricfs_del_first(m.conf.mfs, NULL);
	} else if (!live_presave_during_flight) {
		/* Use transmit completion time, not collection time, for the
		 * next report interval after a blocking transport call. */
		finalize_live_metrics_snapshot(metrics_get_unix_timestamp());
	}

	clear_in_flight();

	if (err != 0) {
		PULSE_ERROR("backlog delete failed: err=%d", err);
		return map_metrics_report_error(err);
	}

	return ((was_from_backlog || live_presave_during_flight) && has_backlog())
		? PULSE_STATUS_BACKLOG_PENDING : PULSE_STATUS_OK;
}

static pulse_status_t collect_from_backlog(void)
{
	int n = metricfs_peek_first(m.conf.mfs,
			m.flight_buf, m.flight_bufsize, NULL);

	if (n > (int)m.flight_bufsize) {
		PULSE_ERROR("backlog peek overflow: len=%d", n);
		return PULSE_STATUS_BACKLOG_OVERFLOW;
	}

	if (n == 0) {
		return PULSE_STATUS_EMPTY;
	}

	if (n < 0) {
		PULSE_ERROR("backlog peek failed: err=%d", n);
		return map_metrics_report_error(n);
	}

	m.flight_len = (size_t)n;
	m.flight_from_backlog = true;
	set_in_flight(true);

	return PULSE_STATUS_OK;
}

static pulse_status_t collect_from_live_metrics(uint64_t now)
{
	pulse_status_t status =
		collect_live_payload(PULSE_SNAPSHOT_REASON_LIVE, true, now);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	m.flight_from_backlog = false;
	set_in_flight(true);

	return PULSE_STATUS_OK;
}

static pulse_status_t collect_live_payload_to_buffer(uint8_t reason,
		bool with_user_callback, uint64_t now,
		uint8_t *buf, size_t bufsize,
		size_t *encoded_len, uint64_t *window_end)
{
	struct pulse_envelope_ctx ctx;
	size_t metrics_len = 0u;
	cbor_writer_t writer;
	const uint64_t saved_window_start = m.flight_window_start;
	const uint64_t saved_window_end = m.flight_window_end;
	uint64_t window_start;
	uint64_t local_window_end;

	if (with_user_callback) {
		invoke_prepare_chain();
	}
	get_live_window_bounds(now, &window_start, &local_window_end);

	metrics_len = metrics_collect(buf, bufsize, &writer);
	if (metrics_len > bufsize) {
		m.flight_window_start = saved_window_start;
		m.flight_window_end = saved_window_end;
		return PULSE_STATUS_OVERFLOW;
	}

	set_envelope_ctx(&ctx, local_window_end, window_start, local_window_end,
			reason);

	const pulse_status_t status =
		pulse_codec_wrap_metrics_payload(buf, bufsize,
			metrics_len, &ctx, encoded_len);
	if (status != PULSE_STATUS_OK) {
		m.flight_window_start = saved_window_start;
		m.flight_window_end = saved_window_end;
	} else if (window_end != NULL) {
		*window_end = local_window_end;
	}

	return status;
}

static pulse_status_t write_live_metrics_to_backlog(uint64_t now)
{
	pulse_status_t status =
		collect_live_payload(PULSE_SNAPSHOT_REASON_LIVE, true, now);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	int err = metricfs_write(m.conf.mfs, m.flight_buf, m.flight_len, NULL);
	if (err != 0) {
		PULSE_ERROR("live metrics backlog write failed: err=%d", err);
		return map_metrics_report_error(err);
	}

	finalize_live_metrics_snapshot(m.flight_window_end);

	return PULSE_STATUS_OK;
}

static pulse_status_t allocate_presave_buffer(uint8_t **buf,
		size_t *bufsize)
{
	const size_t payload_len = metrics_collect(NULL, 0u, NULL);
	pulse_status_t status = derive_payload_bufsize(payload_len, bufsize);

	if (status != PULSE_STATUS_OK) {
		return status;
	}

	*buf = (uint8_t *)malloc(*bufsize);
	return (*buf != NULL) ? PULSE_STATUS_OK : PULSE_STATUS_NO_MEMORY;
}

static void commit_live_presave(uint64_t window_end)
{
	metrics_reset();
	update_last_report_time_for_window(window_end);
	m.live_presave_during_flight = true;
}

static pulse_status_t presave_live_metrics_in_flight(uint64_t now)
{
	struct pulse_timing_state saved_state;
	uint8_t *presave_buf = NULL;
	size_t payload_bufsize = 0u;
	size_t encoded_len = 0u;
	uint64_t presave_window_end = 0u;
	pulse_status_t status;

	if (!is_live_presave_interval_reached(now)) {
		return PULSE_STATUS_OK;
	}

	status = allocate_presave_buffer(&presave_buf, &payload_bufsize);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	save_timing_state(&saved_state);
	status = collect_live_payload_to_buffer(PULSE_SNAPSHOT_REASON_LIVE,
			true, now, presave_buf, payload_bufsize, &encoded_len,
			&presave_window_end);
	if (status == PULSE_STATUS_EMPTY) {
		status = PULSE_STATUS_OK;
		goto restore;
	}
	if (status != PULSE_STATUS_OK) {
		goto restore;
	}

	const int err =
		metricfs_write(m.conf.mfs, presave_buf, encoded_len, NULL);
	if (err != 0) {
		PULSE_ERROR("live presave backlog write failed: err=%d", err);
		status = map_metrics_report_error(err);
		goto restore;
	}

	commit_live_presave(presave_window_end);
	goto out;

restore:
	restore_timing_state(&saved_state);
out:
	free(presave_buf);

	return status;
}

static pulse_status_t save_live_metrics_to_backlog(uint64_t now)
{
	if (!is_in_flight()) {
		return write_live_metrics_to_backlog(now);
	}

	return presave_live_metrics_in_flight(now);
}

static void presave_live_metrics_before_backlog(uint64_t now)
{
	if (now != 0u && m.periodic_initialized
			&& is_live_presave_interval_reached(now)) {
		(void)save_live_metrics_to_backlog(now);
	}
}

static pulse_status_t collect_next_payload(bool backlog_pending, uint64_t now)
{
	if (!backlog_pending) {
		return collect_from_live_metrics(now);
	}

	presave_live_metrics_before_backlog(now);

	return collect_from_backlog();
}

static pulse_status_t do_collect(uint64_t now)
{
	const size_t payload_len = metrics_collect(NULL, 0u, NULL);
	size_t payload_bufsize;
	pulse_status_t status = derive_payload_bufsize(payload_len,
			&payload_bufsize);

	if (status != PULSE_STATUS_OK) {
		return status;
	}

	status = allocate_payload_buffer(payload_bufsize,
			&m.flight_buf, &m.flight_bufsize);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	const bool backlog_pending = has_backlog();
	status = collect_next_payload(backlog_pending, now);
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
	const uint64_t last_report_time = get_last_report_time();

	if (!is_initialized() || is_in_flight() || has_backlog() || now == 0u
			|| !m.periodic_initialized) {
		return 0u;
	}

	if (now < last_report_time) {
		return 0u;
	}

	const uint64_t elapsed_sec = now - last_report_time;
	if (elapsed_sec >= METRICS_REPORT_INTERVAL_SEC) {
		return 0u;
	}

	return METRICS_REPORT_INTERVAL_SEC - elapsed_sec;
}

static pulse_status_t report_in_flight(uint64_t now)
{
	if (now && m.conf.mfs != NULL && m.periodic_initialized
			&& is_live_presave_interval_reached(now)) {
		(void)save_live_metrics_to_backlog(now);
	}

	return do_transmit();
}

static pulse_status_t check_live_report_interval(uint64_t now)
{
	if (now != 0u && now < get_last_report_time()) {
		set_last_report_time(now);
		return PULSE_STATUS_TOO_SOON;
	}

	return is_interval_reached(now)
		? PULSE_STATUS_OK : PULSE_STATUS_TOO_SOON;
}

pulse_status_t pulse_report(void)
{
	if (!is_initialized()) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	const uint64_t now = metrics_get_unix_timestamp();

	if (is_in_flight()) {
		return report_in_flight(now);
	}

	if (!has_backlog()) {
		pulse_status_t status = check_live_report_interval(now);
		if (status != PULSE_STATUS_OK) {
			return status;
		}
	}

	pulse_status_t status = do_collect(now);
	if (status != PULSE_STATUS_OK) {
		return status;
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
	m.live_presave_during_flight = false;
	m.periodic_initialized = false;
	force_reset = pulse->reset_metrics_on_init;
	set_last_report_time(0u);

	metrics_init(force_reset);
	m.initialized = true;

	return PULSE_STATUS_OK;
}
