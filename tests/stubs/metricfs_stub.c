/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "libmcu/metricfs.h"
#include "libmcu/kvstore.h"

struct metricfs *metricfs_create(struct kvstore *kvstore,
		const char *prefix, const size_t max_metrics)
{
	(void)kvstore;
	(void)prefix;
	(void)max_metrics;
	return 0;
}

void metricfs_destroy(struct metricfs *fs)
{
	(void)fs;
}

int metricfs_write(struct metricfs *fs,
		const void *data, const size_t datasize, metricfs_id_t *id)
{
	(void)fs;
	(void)data;
	(void)datasize;
	(void)id;
	return 0;
}

uint16_t metricfs_count(const struct metricfs *fs)
{
	(void)fs;
	return 0;
}

int metricfs_iterate(struct metricfs *fs,
		metricfs_iterator_t cb, void *cb_ctx,
		void *buf, const size_t bufsize, const size_t max_metrics)
{
	(void)fs;
	(void)cb;
	(void)cb_ctx;
	(void)buf;
	(void)bufsize;
	(void)max_metrics;
	return 0;
}

int metricfs_peek(struct metricfs *fs,
		const metricfs_id_t id, void *buf, const size_t bufsize)
{
	(void)fs;
	(void)id;
	(void)buf;
	(void)bufsize;
	return 0;
}

int metricfs_peek_first(struct metricfs *fs,
		void *buf, const size_t bufsize, metricfs_id_t *id)
{
	(void)fs;
	(void)buf;
	(void)bufsize;
	(void)id;
	return 0;
}

int metricfs_read_first(struct metricfs *fs,
		void *buf, const size_t bufsize, metricfs_id_t *id)
{
	(void)fs;
	(void)buf;
	(void)bufsize;
	(void)id;
	return 0;
}

int metricfs_del_first(struct metricfs *fs, metricfs_id_t *id)
{
	(void)fs;
	(void)id;
	return 0;
}

int metricfs_clear(struct metricfs *fs)
{
	(void)fs;
	return 0;
}
