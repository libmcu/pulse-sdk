/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <time.h>

#include "libmcu/metrics.h"

#include <zephyr/kernel.h>

static struct k_spinlock spinlock;
static k_spinlock_key_t spinlock_key;

void metrics_lock(void)
{
	spinlock_key = k_spin_lock(&spinlock);
}

void metrics_unlock(void)
{
	k_spin_unlock(&spinlock, spinlock_key);
}

uint64_t metrics_get_unix_timestamp(void)
{
	time_t now = time(NULL);

	if (now == (time_t)-1) {
		return 0;
	}

	return (uint64_t)now;
}
