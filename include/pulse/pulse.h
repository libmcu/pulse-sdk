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
#include "libmcu/metrics_reporter.h"

#define PULSE_INGEST_HOST	"ingest.libmcu.org"
#define PULSE_INGEST_PATH	"/v1"
#define PULSE_INGEST_URL_HTTPS	"https://" PULSE_INGEST_HOST PULSE_INGEST_PATH
#define PULSE_INGEST_URL_COAPS	"coaps://" PULSE_INGEST_HOST PULSE_INGEST_PATH

typedef enum {
	PULSE_STATUS_OK			= 0,
	PULSE_STATUS_INVALID_ARGUMENT	= -1,
	PULSE_STATUS_BAD_FORMAT		= -2,
	PULSE_STATUS_OVERFLOW		= -3,
	PULSE_STATUS_IO			= -4,
	PULSE_STATUS_TIMEOUT		= -5,
	PULSE_STATUS_NOT_SUPPORTED	= -6,
	PULSE_STATUS_ALREADY		= -7,
	PULSE_STATUS_EMPTY		= -8,
} pulse_status_t;

struct pulse {
	const char *token; /**< Required. Pointer to authentication token.
		Used internally; must not be freed or modified while in use. */
	void *ctx;
};

/**
 * @brief Initialize the pulse module.
 *
 * @param[in] pulse Pointer to the pulse structure to initialize.
 * @return Status code indicating success or failure.
 */
pulse_status_t pulse_init(struct pulse *pulse);

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
