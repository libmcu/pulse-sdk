/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PULSE_INTERNAL_H
#define PULSE_INTERNAL_H

#if defined(__cplusplus)
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

#include "pulse/pulse.h"

#define PULSE_INGEST_HOST	"ingest.libmcu.org"
#define PULSE_INGEST_PATH	"/v1"
#define PULSE_INGEST_URL_HTTPS	"https://" PULSE_INGEST_HOST PULSE_INGEST_PATH
#define PULSE_INGEST_URL_COAPS	"coaps://" PULSE_INGEST_HOST PULSE_INGEST_PATH

/* 256-bit token encoded in URL-safe Base64, excluding null terminator. */
#define PULSE_TOKEN_LEN			43U
/* Buffer size required to hold the authentication token including null terminator. */
#define PULSE_TOKEN_BUFSIZE		(PULSE_TOKEN_LEN + 1U)

struct pulse_report_ctx {
	void *user_ctx;
	struct pulse conf;

	pulse_response_handler_t on_response;
	void *response_ctx;
	pulse_prepare_handler_t on_prepare;
	void *prepare_ctx;

	uint64_t last_report_time;

	uint8_t *flight_buf;
	size_t flight_len;
	size_t flight_bufsize;
	uint64_t flight_window_start;
	uint64_t flight_window_end;
	uint8_t flight_reason;
	bool flight_from_backlog;
	bool in_flight;

	bool initialized;
	bool periodic_initialized;
};

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx);

/**
 * @brief Abort an in-progress transport session.
 *
 * Port hook called by pulse_cancel(). Override this function to perform
 * platform-specific cleanup such as tearing down the transport session.
 * Falls back to a no-op if not overridden.
 */
void pulse_transport_cancel(void);

#if defined(__cplusplus)
}
#endif

#endif /* PULSE_INTERNAL_H */
