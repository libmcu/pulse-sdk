/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define PULSE_WEAK	__attribute__((weak))
#else
#define PULSE_WEAK
#endif

struct pulse_report_ctx;

PULSE_WEAK int pulse_transport_transmit(const void *data, size_t datasize,
		const struct pulse_report_ctx *ctx)
{
	(void)data;
	(void)datasize;
	(void)ctx;

	return -EIO;
}
