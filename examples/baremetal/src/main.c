/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <libmcu/metrics.h>

#include "pulse/pulse.h"

static struct {
	uint8_t buf[128];
	size_t len;
} loopback;

int metrics_report_transmit(const void *data, size_t datasize, void *ctx)
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

	struct pulse conf = { .token = "example-token" };
	if (pulse_init(&conf) != PULSE_STATUS_OK) {
		return 1;
	}

	metrics_set(PulseMetric, METRICS_VALUE(1));

	return (pulse_report() == PULSE_STATUS_OK) ? 0 : 1;
}
