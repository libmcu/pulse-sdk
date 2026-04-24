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
	bool flight_from_backlog;
	bool in_flight;

	bool initialized;
	bool periodic_initialized;
};

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx);

#endif /* PULSE_INTERNAL_H */
