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

#define PULSE_REPORT_MESSAGE_TYPE	1u

#if !defined(PULSE_STATIC_PAYLOAD_BUFSIZE)
#define PULSE_STATIC_PAYLOAD_BUFSIZE	0u
#endif

/* metrics_report_prepare() is called inside metrics_report_periodic() after
 * this dry-run, so metric values may grow before the real encode runs.
 * The margin covers that potential CBOR size increase. */
#if !defined(PULSE_PAYLOAD_MARGIN)
#define PULSE_PAYLOAD_MARGIN		8u
#endif

static struct pulse_report_ctx m;

#if PULSE_STATIC_PAYLOAD_BUFSIZE > 0u
static uint8_t payload_storage[PULSE_STATIC_PAYLOAD_BUFSIZE];
#endif

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
		return PULSE_STATUS_OVERFLOW;
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
	case -EALREADY:
		return PULSE_STATUS_ALREADY;
	case -ENOBUFS:
		return PULSE_STATUS_OVERFLOW;
	default:
		return PULSE_STATUS_IO;
	}
}

const struct pulse_report_ctx *pulse_get_report_ctx(void)
{
	return &m;
}

pulse_status_t pulse_report(void)
{
	pulse_status_t status;
	uint8_t *payload_buf = NULL;
	size_t payload_len = 0u;
	size_t payload_bufsize = 0u;
	int err;

	if (!m.initialized) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	payload_len = metrics_collect(NULL, 0u);
	if (payload_len == 0u) {
		return PULSE_STATUS_EMPTY;
	}

	status = allocate_payload_buffer(payload_len + PULSE_PAYLOAD_MARGIN,
			&payload_buf, &payload_bufsize);
	if (status != PULSE_STATUS_OK) {
		return status;
	}

	err = metrics_report_periodic(payload_buf, payload_bufsize,
			NULL, m.user_ctx);

#if PULSE_STATIC_PAYLOAD_BUFSIZE == 0u
	free(payload_buf);
#endif

	return map_metrics_report_error(err);
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
	case PULSE_STATUS_ALREADY:
		return "already";
	case PULSE_STATUS_EMPTY:
		return "empty";
	default:
		return "unknown";
	}
}

pulse_status_t pulse_init(struct pulse *pulse)
{
	if (pulse == NULL || pulse->token == NULL) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	m.conf = *pulse;
	m.user_ctx = pulse->ctx;
	metrics_init(false);
	m.initialized = true;

	return PULSE_STATUS_OK;
}
