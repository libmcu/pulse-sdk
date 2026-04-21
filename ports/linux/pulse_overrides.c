/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>
#include <time.h>

#include "libmcu/metrics.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void metrics_lock(void)
{
	pthread_mutex_lock(&lock);
}

void metrics_unlock(void)
{
	pthread_mutex_unlock(&lock);
}

uint64_t metrics_get_unix_timestamp(void)
{
	time_t now = time(NULL);

	if (now == (time_t)-1) {
		return 0;
	}

	return (uint64_t)now;
}
