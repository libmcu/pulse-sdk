/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse.h"

int main(void)
{
	struct pulse conf = {
		.token = "example-token",
		.serial_number = "zephyr-device-1",
		.software_version = "1.0.0",
	};
	pulse_init(&conf);
	pulse_report();
	return 0;
}
