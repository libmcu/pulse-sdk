/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse.h"

int main(void)
{
	struct pulse conf = { .token = "example-token" };
	pulse_init(&conf);
	pulse_report();
	return 0;
}
