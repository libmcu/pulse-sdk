/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <time.h>

#include "libmcu/metrics.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

void metrics_lock(void)
{
	taskENTER_CRITICAL(&spinlock);
}

void metrics_unlock(void)
{
	taskEXIT_CRITICAL(&spinlock);
}

uint64_t metrics_get_unix_timestamp(void)
{
	time_t now = time(NULL);

	if (now == (time_t)-1) {
		return 0;
	}

	return (uint64_t)now;
}
