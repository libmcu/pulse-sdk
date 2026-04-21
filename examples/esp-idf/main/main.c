/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse.h"

void app_main(void)
{
	struct pulse conf = { .token = "example-token" };
	pulse_init(&conf);
	pulse_report();
}
