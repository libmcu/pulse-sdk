/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PULSE_H
#define PULSE_H

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdint.h>

#include "libmcu/metrics.h"
#include "libmcu/metricfs.h"

/* 256-bit token encoded in URL-safe Base64, excluding null terminator. */
#define PULSE_TOKEN_LEN			43U
/* Buffer size required to hold the authentication token including null terminator. */
#define PULSE_TOKEN_BUFSIZE		(PULSE_TOKEN_LEN + 1U)

typedef enum {
	PULSE_STATUS_OK			= 0,
	PULSE_STATUS_INVALID_ARGUMENT	= -1,
	PULSE_STATUS_BAD_FORMAT		= -2,
	PULSE_STATUS_OVERFLOW		= -3,
	PULSE_STATUS_IO			= -4,
	PULSE_STATUS_TIMEOUT		= -5,
	PULSE_STATUS_NOT_SUPPORTED	= -6,
	/* The reporting interval has not yet elapsed. No action needed;
	 * the next scheduled call will proceed normally. */
	PULSE_STATUS_TOO_SOON		= -7,
	PULSE_STATUS_EMPTY		= -8,
	/* A backlog entry was delivered and more entries remain, or an
	 * in-progress transfer was cancelled and the snapshot was saved
	 * to the backlog. Call @ref pulse_report() again to continue
	 * draining. Transport errors return the matching error code
	 * instead so the caller can apply appropriate backoff. */
	PULSE_STATUS_BACKLOG_PENDING	= -9,
	/* A backlog entry's stored payload exceeds the transmit buffer size
	 * (e.g. written under a different schema or a larger payload). The
	 * entry cannot be transmitted and blocks further backlog drain. To
	 * recover, discard the entry by calling metricfs_del_first() directly
	 * on the metricfs instance, then call pulse_report() again. */
	PULSE_STATUS_BACKLOG_OVERFLOW	= -10,
	PULSE_STATUS_NO_MEMORY		= -11,
	/* Returned when an async transport is still in progress. The caller
	 * must invoke pulse_report() again to advance the transfer.
	 * No backlog entry was written. */
	PULSE_STATUS_IN_PROGRESS	= -12,
} pulse_status_t;

/**
 * @brief Callback invoked when a response is received from the ingest server.
 *
 * @param[in] data     Response payload bytes.
 * @param[in] datasize Payload length in bytes.
 * @param[in] ctx      User context pointer supplied to
 *                     pulse_set_response_handler().
 */
typedef void (*pulse_response_handler_t)(const void *data, size_t datasize,
		void *ctx);

/**
 * @brief Callback invoked before metrics collection.
 *
 * Use this hook to refresh application-owned state before reporting.
 *
 * @param[in] ctx User context pointer supplied to
 *                pulse_set_prepare_handler().
 */
typedef void (*pulse_prepare_handler_t)(void *ctx);

struct pulse {
	/**
	 * Pointer to a null-terminated authentication token string.
	 *
	 * Ownership: the caller retains ownership. The SDK stores this pointer
	 * as-is and never copies or frees it. The pointed-to buffer MUST remain
	 * valid and unmodified for the entire lifetime of the pulse module
	 * (i.e. from pulse_init() until the module is no longer used).
	 * A static or global buffer is the recommended storage class.
	 *
	 * Length: must not exceed PULSE_TOKEN_LEN characters (excluding null
	 * terminator). pulse_update_token() enforces this limit and returns
	 * PULSE_STATUS_INVALID_ARGUMENT on violation.
	 *
	 * Required at init time: must be non-NULL and not empty.
	 */
	const char *token;
	struct metricfs *mfs; /**< Optional. Backlog storage backend.
			NULL disables backlog. Entries are replayed oldest-first.
			Can be changed after init via pulse_update_metricfs(). */
	const char *serial_number; /**< Required device serial metadata.
			Must be non-NULL, non-empty, and null-terminated.
			The null terminator is not encoded into the payload. */
	const char *software_version; /**< Required software version metadata.
			Must be non-NULL, non-empty, and null-terminated.
			The null terminator is not encoded into the payload. */
	uint32_t transmit_timeout_ms; /**< Optional. Maximum transmit time in
			milliseconds. Set to 0 to use the platform default. */
	bool reset_metrics_on_init; /**< When true, resets all metric counters
			during pulse_init(). Set to false (default) to preserve
			accumulated metric values across re-initialisation. */
	bool async_transport; /**< When true, pulse_report() may return
			PULSE_STATUS_IN_PROGRESS if a transfer is still in
			progress; the caller must invoke pulse_report() again to
			advance it. When false (default), pulse_report() blocks
			until the transfer completes before returning. */
};

/**
 * @brief Initializes the given pulse context.
 *
 * This function sets up the internal state and resources required for the pulse
 * context to operate. Must be called before using other pulse operations.
 *
 * @param[in] pulse Pointer to a pulse context structure to initialize.
 * @return Status code indicating success or failure of initialization.
 */
pulse_status_t pulse_init(struct pulse *pulse);

/**
 * @brief Update the authentication token after initialization.
 *
 * The token pointer must remain valid for the lifetime of the module.
 * Use this to rotate or replace the token after initialization.
 *
 * @param[in] token Non-NULL pointer to the authentication token string.
 *                  The pointer is borrowed: the buffer must remain valid
 *                  and unmodified until the pulse module is no longer used.
 *                  Use a static or global buffer. Length must not exceed
 *                  PULSE_TOKEN_LEN characters (excluding null terminator).
 * @return PULSE_STATUS_OK on success.
 * @return PULSE_STATUS_INVALID_ARGUMENT if @p token is NULL
 *                                       or exceeds PULSE_TOKEN_LEN.
 */
pulse_status_t pulse_update_token(const char *token);

/**
 * @brief Update the metricfs backlog backend after initialization.
 *
 * Replaces the metricfs instance used for backlog persistence. Pass NULL
 * to disable backlog storage at runtime (already-stored entries are not
 * discarded; they simply will not be drained until a non-NULL instance is
 * supplied again).
 *
 * @param[in] mfs Pointer to metricfs instance, or NULL to disable.
 * @return PULSE_STATUS_OK always.
 */
pulse_status_t pulse_update_metricfs(struct metricfs *mfs);

/**
 * @brief Register a handler called when the ingest server returns a response.
 *
 * Decouples the SDK transport layer from application-specific response
 * processing. Pass NULL to unregister. The supplied @p ctx is forwarded
 * to the handler on every invocation and is independent of the context
 * stored in struct pulse.
 *
 * @param[in] handler Response handler, or NULL to disable.
 * @param[in] ctx     User context pointer forwarded to @p handler.
 * @return PULSE_STATUS_OK always.
 */
pulse_status_t pulse_set_response_handler(pulse_response_handler_t handler,
		void *ctx);

/**
 * @brief Register a handler called before metrics collection.
 *
 * Pass NULL to unregister. The supplied @p ctx is forwarded to the handler on
 * every invocation and is independent of the context stored in struct pulse.
 *
 * @param[in] handler Prepare handler, or NULL to disable.
 * @param[in] ctx     User context pointer forwarded to @p handler.
 * @return PULSE_STATUS_OK always.
 */
pulse_status_t pulse_set_prepare_handler(pulse_prepare_handler_t handler,
		void *ctx);

/**
 * @brief Report the current pulse status or metrics.
 *
 * Not thread-safe. Must not be called concurrently with pulse_update_token(),
 * pulse_update_metricfs(), pulse_set_response_handler(),
 * pulse_set_prepare_handler(), or pulse_cancel().
 *
 * @return Status code indicating success or failure.
 */
pulse_status_t pulse_report(void);

/**
 * @brief Return milliseconds until the next live report may be sent.
 *
 * Not thread-safe. Must not be called concurrently with pulse_report(),
 * pulse_init(), pulse_update_token(), pulse_update_metricfs(),
 * pulse_set_response_handler(), pulse_set_prepare_handler(), or
 * pulse_cancel().
 *
 * Returns 0 when the module is not initialized, when a live report may be
 * attempted immediately, when backlog or in-flight transfer handling can
 * proceed, or when timestamp-based periodic gating is not active. A return
 * value of 0 does not imply that pulse_report() will succeed.
 *
 * @return Milliseconds until the next live report window.
 */
uint32_t pulse_get_ms_until_next_report(void);

/**
 * @brief Cancel an in-progress async transfer.
 *
 * May only be called when pulse_report() previously returned
 * PULSE_STATUS_IN_PROGRESS. If a backlog backend is configured and the
 * in-flight payload originated from live metrics (not from the backlog),
 * the *current* metric state is re-encoded and saved to the backlog before
 * clearing state, and PULSE_STATUS_BACKLOG_PENDING is returned. The original
 * in-flight snapshot is discarded; any metric updates recorded after the
 * initial pulse_report() call are included in the saved entry.
 * Otherwise PULSE_STATUS_OK is returned. After a successful cancel,
 * pulse_report() may be called again.
 *
 * @return PULSE_STATUS_BACKLOG_PENDING if the current metric state was saved.
 * @return PULSE_STATUS_OK if cancelled without saving.
 * @return PULSE_STATUS_INVALID_ARGUMENT if no transfer is in progress.
 */
pulse_status_t pulse_cancel(void);

/**
 * @brief Convert a pulse_status_t value to a human-readable string.
 *
 * @param[in] status Status code to stringify.
 * @return Pointer to a constant string describing the status.
 */
const char *pulse_stringify_status(pulse_status_t status);

#if defined(__cplusplus)
}
#endif

#endif /* PULSE_H */
