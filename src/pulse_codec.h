/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PULSE_CODEC_H
#define PULSE_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "pulse/pulse.h"

enum pulse_snapshot_reason {
	PULSE_SNAPSHOT_REASON_LIVE = 0,
	PULSE_SNAPSHOT_REASON_BACKLOG_FAILURE = 1,
	PULSE_SNAPSHOT_REASON_BACKLOG_CANCEL = 2,
};

struct pulse_envelope_ctx {
	const char *serial_number;
	const char *software_version;
	uint64_t timestamp;
	uint64_t window_start;
	uint64_t window_end;
	uint8_t snapshot_reason;
};

size_t pulse_codec_max_envelope_overhead(const struct pulse_envelope_ctx *ctx,
		size_t metrics_len);

pulse_status_t pulse_codec_wrap_metrics_payload(uint8_t *buf, size_t bufsize,
		size_t metrics_len, const struct pulse_envelope_ctx *ctx,
		size_t *encoded_len);

#endif /* PULSE_CODEC_H */
