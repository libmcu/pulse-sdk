/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <libmcu/metrics.h>

#include "pulse/pulse.h"
#include "pulse/pulse_internal.h"

static struct {
	uint8_t buf[128];
	size_t len;
} loopback;

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	(void)ctx;
	if (datasize > sizeof(loopback.buf)) {
		return -EOVERFLOW;
	}

	memcpy(loopback.buf, data, datasize);
	loopback.len = datasize;

	return 0;
}

int main(void)
{
	memset(&loopback, 0, sizeof(loopback));

	struct pulse conf = {
		.token = "example-token",
		.serial_number = "generic-device-1",
		.software_version = "1.0.0",
	};
	if (pulse_init(&conf) != PULSE_STATUS_OK) {
		return 1;
	}

	metrics_set(PulseMetric, METRICS_VALUE(42));

	if (pulse_report() != PULSE_STATUS_OK) {
		return 1;
	}

	printf("encoded %zu bytes\n", loopback.len);

	return 0;
}
