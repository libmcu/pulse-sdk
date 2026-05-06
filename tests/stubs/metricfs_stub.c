/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "libmcu/metricfs.h"
#include "libmcu/kvstore.h"

#include "metricfs_stub.h"

#include <errno.h>
#include <string.h>

#define METRICFS_STUB_MAX_ENTRIES	8u

static uint8_t stored_data[METRICFS_STUB_MAX_ENTRIES][1024];
static size_t stored_size[METRICFS_STUB_MAX_ENTRIES];
static uint16_t stored_count;
static int peek_first_error;
static int write_error;

static uint16_t stored_entry_count(void)
{
	return stored_count <= METRICFS_STUB_MAX_ENTRIES
		? stored_count : METRICFS_STUB_MAX_ENTRIES;
}

void metrics_lock(void)
{
}

void metrics_unlock(void)
{
}

void metricfs_stub_reset(void)
{
	memset(stored_data, 0, sizeof(stored_data));
	memset(stored_size, 0, sizeof(stored_size));
	stored_count = 0;
	peek_first_error = 0;
	write_error = 0;
}

void metricfs_stub_prime(const void *data, size_t datasize, uint16_t count)
{
	stored_count = count;
	for (uint16_t i = 0u; i < stored_entry_count(); ++i) {
		if (data != NULL && datasize <= sizeof(stored_data[i])) {
			memcpy(stored_data[i], data, datasize);
			stored_size[i] = datasize;
		} else {
			stored_size[i] = 0;
		}
	}
}

void metricfs_stub_set_peek_first_error(int err)
{
	peek_first_error = err;
}

void metricfs_stub_set_write_error(int err)
{
	write_error = err;
}

const void *metricfs_stub_data(void)
{
	return stored_data[0];
}

size_t metricfs_stub_size(void)
{
	return stored_size[0];
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
	if (write_error != 0) {
		return write_error;
	}

	if (stored_count >= METRICFS_STUB_MAX_ENTRIES) {
		return -ENOBUFS;
	}

	if (datasize > sizeof(stored_data[stored_count])) {
		return -EOVERFLOW;
	}

	if (data != NULL) {
		memcpy(stored_data[stored_count], data, datasize);
		stored_size[stored_count] = datasize;
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
	if (stored_size[0] > bufsize) {
		return (int)stored_size[0];
	}
	memcpy(buf, stored_data[0], stored_size[0]);
	(void)id;
	return (int)stored_size[0];
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
		for (uint16_t i = 1u; i < stored_entry_count(); ++i) {
			memcpy(stored_data[i - 1u], stored_data[i], stored_size[i]);
			stored_size[i - 1u] = stored_size[i];
		}
		stored_count--;
	}
	if (stored_count < METRICFS_STUB_MAX_ENTRIES) {
		stored_size[stored_count] = 0;
	}
	return 0;
}

int metricfs_clear(struct metricfs *fs)
{
	(void)fs;
	stored_count = 0;
	memset(stored_size, 0, sizeof(stored_size));
	return 0;
}
