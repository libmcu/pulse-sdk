/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PULSE_INTERNAL_H
#define PULSE_INTERNAL_H

#include <stddef.h>
#include <stdbool.h>

#include "pulse/pulse.h"

#define pulse_container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

/* user_ctx MUST remain the first member: metrics reporter callbacks receive
 * &pulse_report_ctx as ctx, and callers cast ctx directly to their user type. */
struct pulse_report_ctx {
	void *user_ctx;
	struct pulse conf;

	pulse_response_handler_t on_response;
	void *response_ctx;

	uint64_t last_report_time;

	uint8_t *flight_buf;
	size_t flight_len;
	size_t flight_bufsize;
	bool flight_from_store;
	bool in_flight;

	bool initialized;
	bool periodic_initialized;
};

const struct pulse_report_ctx *pulse_get_report_ctx(void);

#endif /* PULSE_INTERNAL_H */
