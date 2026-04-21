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

#include "libmcu/metrics.h"
#include "libmcu/metricfs.h"
#include "libmcu/metrics_reporter.h"

#define PULSE_INGEST_HOST	"ingest.libmcu.org"
#define PULSE_INGEST_PATH	"/v1"
#define PULSE_INGEST_URL_HTTPS	"https://" PULSE_INGEST_HOST PULSE_INGEST_PATH
#define PULSE_INGEST_URL_COAPS	"coaps://" PULSE_INGEST_HOST PULSE_INGEST_PATH

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
	/* The current report succeeded or was skipped, but undelivered
	 * backlog entries remain in metricfs. Call @ref pulse_report()
	 * again immediately to drain the backlog. */
	PULSE_STATUS_BACKLOG_PENDING	= -9,
	/* A backlog entry's stored payload exceeds the transmit buffer size
	 * (e.g. written under a different schema or a larger payload). The
	 * entry cannot be transmitted and blocks further backlog drain. To
	 * recover, discard the entry by calling metricfs_del_first() directly
	 * on the metricfs instance, then call pulse_report() again. */
	PULSE_STATUS_BACKLOG_OVERFLOW	= -10,
	PULSE_STATUS_NO_MEMORY		= -11,
} pulse_status_t;

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
	 * Optional at init time: may be NULL if the token is not yet available.
	 * Supply it later via pulse_update_token() before calling
	 * pulse_report().
	 */
	const char *token;
	struct metricfs *mfs; /**< Optional. Backlog storage backend.
			NULL disables backlog. Can be changed after init via
			pulse_update_metricfs(). */
	void *ctx; /**< Optional. User context pointer passed through to the
			transmit callback. */
	bool reset_metrics_on_init; /**< When true, resets all metric counters
			during pulse_init(). Set to false (default) to preserve
			accumulated metric values across re-initialisation. */
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
 * Use this when the token is not available at init time (e.g. loaded
 * asynchronously from secure storage).
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
 * @brief Report the current pulse status or metrics.
 *
 * @return Status code indicating success or failure.
 */
pulse_status_t pulse_report(void);

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
