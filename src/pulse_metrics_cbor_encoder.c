/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "libmcu/metrics.h"
#include "libmcu/metrics_overrides.h"

#include "cbor/base.h"
#include "cbor/encoder.h"

static cbor_writer_t writer;

static size_t cbor_encoded_uint_size(uint64_t value)
{
	if (value < 24u) {
		return 1u;
	} else if (value <= 0xffu) {
		return 2u;
	} else if (value <= 0xffffu) {
		return 3u;
	} else if (value <= 0xffffffffu) {
		return 5u;
	}

	return 9u;
}

static size_t cbor_encoded_int_size(int64_t value)
{
	if (value >= 0) {
		return cbor_encoded_uint_size((uint64_t)value);
	}

	return cbor_encoded_uint_size((uint64_t)(-1 - value));
}

static size_t cbor_encoded_metric_value_size(int32_t value)
{
	return cbor_encoded_int_size((int64_t)value);
}

#if defined(METRICS_SCHEMA_IBS)
static size_t cbor_encoded_schema_value_size(const struct metric_schema *schema,
		int32_t value)
{
	return cbor_encoded_uint_size(5u)
		+ cbor_encoded_uint_size((uint64_t)schema->type)
		+ cbor_encoded_uint_size((uint64_t)schema->unit)
		+ cbor_encoded_int_size((int64_t)schema->range_min)
		+ cbor_encoded_int_size((int64_t)schema->range_max)
		+ cbor_encoded_metric_value_size(value);
}
#endif

size_t metrics_encode_header(void *buf, size_t bufsize,
		uint32_t nr_total, uint32_t nr_updated)
{
	(void)nr_total;

	if (nr_updated == 0u) {
		return 0u;
	}

	if (buf == NULL) {
		return cbor_encoded_uint_size((uint64_t)nr_updated);
	}

	cbor_writer_init(&writer, buf, bufsize);
	if (cbor_encode_map(&writer, nr_updated) != CBOR_SUCCESS) {
		return 0u;
	}

	return cbor_writer_len(&writer);
}

#if defined(METRICS_SCHEMA_IBS)
size_t metrics_encode_each(void *buf, size_t bufsize,
		metric_key_t key, int32_t value,
		const struct metric_schema *schema)
{
	(void)buf;
	(void)bufsize;

	if (buf == NULL) {
		return cbor_encoded_uint_size((uint64_t)key)
			+ cbor_encoded_schema_value_size(schema, value);
	}

	const size_t len = cbor_writer_len(&writer);

	cbor_encode_unsigned_integer(&writer, (uint64_t)key);

	/* value → [class, unit, range_min, range_max, value] */
	cbor_encode_array(&writer, 5);

	cbor_encode_unsigned_integer(&writer, (uint64_t)schema->type);
	cbor_encode_unsigned_integer(&writer, (uint64_t)schema->unit);

	if (schema->range_min >= 0) {
		cbor_encode_unsigned_integer(&writer, (uint64_t)schema->range_min);
	} else {
		cbor_encode_negative_integer(&writer, schema->range_min);
	}

	if (schema->range_max >= 0) {
		cbor_encode_unsigned_integer(&writer, (uint64_t)schema->range_max);
	} else {
		cbor_encode_negative_integer(&writer, schema->range_max);
	}

	if (value >= 0) {
		cbor_encode_unsigned_integer(&writer, (uint64_t)value);
	} else {
		cbor_encode_negative_integer(&writer, value);
	}

	return cbor_writer_len(&writer) - len;
}
#else
size_t metrics_encode_each(void *buf, size_t bufsize,
		metric_key_t key, int32_t value)
{
	(void)buf;
	(void)bufsize;

	if (buf == NULL) {
		return cbor_encoded_uint_size((uint64_t)key)
			+ cbor_encoded_metric_value_size(value);
	}

	const size_t len = cbor_writer_len(&writer);

	cbor_encode_unsigned_integer(&writer, (uint64_t)key);

	if (value >= 0) {
		cbor_encode_unsigned_integer(&writer, (uint64_t)value);
	} else {
		cbor_encode_negative_integer(&writer, value);
	}

	return cbor_writer_len(&writer) - len;
}
#endif
