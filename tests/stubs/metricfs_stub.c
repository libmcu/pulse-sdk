/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "libmcu/metricfs.h"
#include "libmcu/kvstore.h"

#include "metricfs_stub.h"

#include <string.h>

static uint8_t stored_data[1024];
static size_t stored_size;
static uint16_t stored_count;
static int peek_first_error;

void metrics_lock(void)
{
}

void metrics_unlock(void)
{
}

void metricfs_stub_reset(void)
{
	memset(stored_data, 0, sizeof(stored_data));
	stored_size = 0;
	stored_count = 0;
	peek_first_error = 0;
}

void metricfs_stub_prime(const void *data, size_t datasize, uint16_t count)
{
	if (data != NULL && datasize <= sizeof(stored_data)) {
		memcpy(stored_data, data, datasize);
		stored_size = datasize;
	} else {
		stored_size = 0;
	}

	stored_count = count;
}

void metricfs_stub_set_peek_first_error(int err)
{
	peek_first_error = err;
}

const void *metricfs_stub_data(void)
{
	return stored_data;
}

size_t metricfs_stub_size(void)
{
	return stored_size;
}

struct metricfs *metricfs_create(struct kvstore *kvstore,
		const char *prefix, const size_t max_metrics)
{
	(void)kvstore;
	(void)prefix;
	(void)max_metrics;
	return (struct metricfs *)kvstore;
}

void metricfs_destroy(struct metricfs *fs)
{
	(void)fs;
}

int metricfs_write(struct metricfs *fs,
		const void *data, const size_t datasize, metricfs_id_t *id)
{
	(void)fs;
	if (data != NULL && datasize <= sizeof(stored_data)) {
		memcpy(stored_data, data, datasize);
		stored_size = datasize;
	}
	stored_count++;
	(void)id;
	return 0;
}

uint16_t metricfs_count(const struct metricfs *fs)
{
	(void)fs;
	return stored_count;
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
	if (peek_first_error != 0) {
		return peek_first_error;
	}

	if (buf == NULL || stored_count == 0) {
		return 0;
	}
	if (stored_size > bufsize) {
		return (int)stored_size;
	}
	memcpy(buf, stored_data, stored_size);
	(void)id;
	return (int)stored_size;
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
	if (stored_count > 0) {
		stored_count--;
	}
	if (stored_count == 0) {
		stored_size = 0;
	}
	return 0;
}

int metricfs_clear(struct metricfs *fs)
{
	(void)fs;
	stored_count = 0;
	stored_size = 0;
	return 0;
}
