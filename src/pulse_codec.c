/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pulse_codec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cbor/base.h"
#include "cbor/encoder.h"

enum pulse_envelope_key {
	PULSE_ENVELOPE_KEY_SCHEMA = 0,
	PULSE_ENVELOPE_KEY_DEVICE = 1,
	PULSE_ENVELOPE_KEY_REPORT = 2,
	PULSE_ENVELOPE_KEY_METRICS = 3,
};

enum pulse_device_key {
	PULSE_DEVICE_KEY_SERIAL = 0,
	PULSE_DEVICE_KEY_SOFTWARE_VERSION = 1,
};

enum pulse_report_key {
	PULSE_REPORT_KEY_TIMESTAMP = 0,
	PULSE_REPORT_KEY_WINDOW_START = 1,
	PULSE_REPORT_KEY_WINDOW_END = 2,
	PULSE_REPORT_KEY_REASON = 3,
};

enum {
	PULSE_ENVELOPE_SCHEMA_V1 = 1,
};

static bool is_valid_codec_string(const char *value)
{
	return value != NULL && value[0] != '\0';
}

static size_t get_cbor_encoded_uint_size(uint64_t value)
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

static size_t get_cbor_encoded_text_size(const char *text)
{
	const size_t len = (text != NULL) ? strlen(text) : 0u;

	return get_cbor_encoded_uint_size((uint64_t)len) + len;
}

static size_t get_cbor_encoded_bstr_header_size(size_t len)
{
	return get_cbor_encoded_uint_size((uint64_t)len);
}

static size_t get_envelope_header_size(const struct pulse_envelope_ctx *ctx,
		size_t metrics_len)
{
	const bool has_metrics_payload = metrics_len > 0u;
	size_t report_count = 0u;

	if (ctx->timestamp != 0u) {
		report_count++;
	}
	if (ctx->window_start != 0u) {
		report_count++;
	}
	if (ctx->window_end != 0u) {
		report_count++;
	}
	if (ctx->snapshot_reason != PULSE_SNAPSHOT_REASON_LIVE) {
		report_count++;
	}

	return get_cbor_encoded_uint_size(has_metrics_payload ? 4u : 3u)
		+ get_cbor_encoded_uint_size(PULSE_ENVELOPE_KEY_SCHEMA)
		+ get_cbor_encoded_uint_size(PULSE_ENVELOPE_SCHEMA_V1)
		+ get_cbor_encoded_uint_size(PULSE_ENVELOPE_KEY_DEVICE)
		+ get_cbor_encoded_uint_size(2u)
		+ get_cbor_encoded_uint_size(PULSE_DEVICE_KEY_SERIAL)
		+ get_cbor_encoded_text_size(ctx->serial_number)
		+ get_cbor_encoded_uint_size(PULSE_DEVICE_KEY_SOFTWARE_VERSION)
		+ get_cbor_encoded_text_size(ctx->software_version)
		+ get_cbor_encoded_uint_size(PULSE_ENVELOPE_KEY_REPORT)
		+ get_cbor_encoded_uint_size(report_count)
		+ ((ctx->timestamp != 0u)
			? get_cbor_encoded_uint_size(PULSE_REPORT_KEY_TIMESTAMP)
				+ get_cbor_encoded_uint_size(ctx->timestamp)
			: 0u)
		+ ((ctx->window_start != 0u)
			? get_cbor_encoded_uint_size(PULSE_REPORT_KEY_WINDOW_START)
				+ get_cbor_encoded_uint_size(ctx->window_start)
			: 0u)
		+ ((ctx->window_end != 0u)
			? get_cbor_encoded_uint_size(PULSE_REPORT_KEY_WINDOW_END)
				+ get_cbor_encoded_uint_size(ctx->window_end)
			: 0u)
		+ ((ctx->snapshot_reason != PULSE_SNAPSHOT_REASON_LIVE)
			? get_cbor_encoded_uint_size(PULSE_REPORT_KEY_REASON)
				+ get_cbor_encoded_uint_size(ctx->snapshot_reason)
			: 0u)
		+ (has_metrics_payload
			? get_cbor_encoded_uint_size(PULSE_ENVELOPE_KEY_METRICS)
				+ get_cbor_encoded_bstr_header_size(metrics_len)
			: 0u);
}

size_t pulse_codec_max_envelope_overhead(const struct pulse_envelope_ctx *ctx,
		size_t metrics_len)
{
	return get_envelope_header_size(ctx, metrics_len);
}

pulse_status_t pulse_codec_wrap_metrics_payload(uint8_t *buf, size_t bufsize,
		size_t metrics_len, const struct pulse_envelope_ctx *ctx,
		size_t *encoded_len)
{
	if (buf == NULL || ctx == NULL || encoded_len == NULL) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	const size_t header_len = get_envelope_header_size(ctx, metrics_len);
	const size_t total_len = header_len + metrics_len;
	const bool has_metrics_payload = metrics_len > 0u;
	const uint64_t report_count = (uint64_t)((ctx->timestamp != 0u)
			+ (ctx->window_start != 0u)
			+ (ctx->window_end != 0u)
			+ (ctx->snapshot_reason != PULSE_SNAPSHOT_REASON_LIVE));
	cbor_writer_t writer;

	if (!is_valid_codec_string(ctx->serial_number)
			|| !is_valid_codec_string(ctx->software_version)) {
		return PULSE_STATUS_INVALID_ARGUMENT;
	}

	if (total_len > bufsize) {
		return PULSE_STATUS_OVERFLOW;
	}

	if (has_metrics_payload) {
		memmove(buf + header_len, buf, metrics_len);
	}

	cbor_writer_init(&writer, buf, bufsize);

	cbor_encode_map(&writer, has_metrics_payload ? 4u : 3u);
	  cbor_encode_unsigned_integer(&writer, PULSE_ENVELOPE_KEY_SCHEMA);
	  cbor_encode_unsigned_integer(&writer, PULSE_ENVELOPE_SCHEMA_V1);
	  cbor_encode_unsigned_integer(&writer, PULSE_ENVELOPE_KEY_DEVICE);
	  cbor_encode_map(&writer, 2u);
	    cbor_encode_unsigned_integer(&writer, PULSE_DEVICE_KEY_SERIAL);
	    cbor_encode_text_string(&writer, ctx->serial_number, strlen(ctx->serial_number));
	    cbor_encode_unsigned_integer(&writer, PULSE_DEVICE_KEY_SOFTWARE_VERSION);
	    cbor_encode_text_string(&writer, ctx->software_version, strlen(ctx->software_version));
	  cbor_encode_unsigned_integer(&writer, PULSE_ENVELOPE_KEY_REPORT);
	  cbor_encode_map(&writer, report_count);
	if (ctx->timestamp != 0u) {
		cbor_encode_unsigned_integer(&writer, PULSE_REPORT_KEY_TIMESTAMP);
		cbor_encode_unsigned_integer(&writer, ctx->timestamp);
	}
	if (ctx->window_start != 0u) {
		cbor_encode_unsigned_integer(&writer, PULSE_REPORT_KEY_WINDOW_START);
		cbor_encode_unsigned_integer(&writer, ctx->window_start);
	}
	if (ctx->window_end != 0u) {
		cbor_encode_unsigned_integer(&writer, PULSE_REPORT_KEY_WINDOW_END);
		cbor_encode_unsigned_integer(&writer, ctx->window_end);
	}
	if (ctx->snapshot_reason != PULSE_SNAPSHOT_REASON_LIVE) {
		cbor_encode_unsigned_integer(&writer, PULSE_REPORT_KEY_REASON);
		cbor_encode_unsigned_integer(&writer, ctx->snapshot_reason);
	}

	if (has_metrics_payload) {
		cbor_encode_unsigned_integer(&writer, PULSE_ENVELOPE_KEY_METRICS);
		cbor_encode_byte_string(&writer, buf + header_len, metrics_len);
	}

	if (cbor_writer_len(&writer) != total_len) {
		return PULSE_STATUS_BAD_FORMAT;
	}

	*encoded_len = total_len;

	return PULSE_STATUS_OK;
}
