/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "libmcu/metricfs.h"

uint16_t metricfs_count(const struct metricfs *fs)
{
	(void)fs;

	return 0u;
}

int metricfs_peek_first(struct metricfs *fs,
		void *buf, const size_t bufsize, metricfs_id_t *id)
{
	(void)fs;
	(void)buf;
	(void)bufsize;
	(void)id;

	return -ENOENT;
}

int metricfs_del_first(struct metricfs *fs, metricfs_id_t *id)
{
	(void)fs;
	(void)id;

	return -ENOENT;
}

int metricfs_write(struct metricfs *fs,
		const void *data, const size_t datasize, metricfs_id_t *id)
{
	(void)fs;
	(void)data;
	(void)datasize;
	(void)id;

	return -ENOSYS;
}
