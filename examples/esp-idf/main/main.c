/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse/pulse.h"

void app_main(void)
{
	struct pulse conf = {
		.token = "example-token",
		.serial_number = "esp32-device-1",
		.software_version = "1.0.0",
	};
	pulse_init(&conf);
	pulse_report();
}
