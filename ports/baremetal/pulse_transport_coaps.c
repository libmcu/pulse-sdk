/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stddef.h>

struct pulse_report_ctx;

int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	(void)data;
	(void)datasize;
	(void)ctx;

	return -ENOTSUP;
}
