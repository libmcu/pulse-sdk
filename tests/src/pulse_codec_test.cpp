#include "CppUTest/TestHarness.h"

extern "C" {
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "libmcu/metrics.h"
#include "cbor/cbor.h"

#include "../../src/pulse_codec.h"
}

#define TEST_SERIAL		"device-serial"
#define TEST_VERSION		"1.0.0"
#define SHORT_SERIAL		"a"
#define SHORT_VERSION		"b"

struct cbor_cursor {
	const uint8_t *buf;
	size_t len;
	size_t pos;
};

static uint8_t cbor_read_byte(struct cbor_cursor *cursor)
{
	CHECK_TRUE(cursor->pos < cursor->len);
	return cursor->buf[cursor->pos++];
}

static uint64_t cbor_read_arg(struct cbor_cursor *cursor, uint8_t additional)
{
	uint64_t value = 0u;

	if (additional < 24u) {
		return additional;
	}

	if (additional == 24u) {
		return cbor_read_byte(cursor);
	}

	if (additional == 25u) {
		value = (uint64_t)cbor_read_byte(cursor) << 8;
		value |= cbor_read_byte(cursor);
		return value;
	}

	if (additional == 26u) {
		for (unsigned int i = 0u; i < 4u; ++i) {
			value = (value << 8) | cbor_read_byte(cursor);
		}
		return value;
	}

	FAIL("Unsupported CBOR additional info");
	return 0u;
}

static uint64_t cbor_read_type(struct cbor_cursor *cursor, uint8_t major)
{
	const uint8_t initial = cbor_read_byte(cursor);

	UNSIGNED_LONGS_EQUAL(major, (uint8_t)(initial >> 5));

	return cbor_read_arg(cursor, (uint8_t)(initial & 0x1fu));
}

static uint64_t cbor_read_uint(struct cbor_cursor *cursor)
{
	return cbor_read_type(cursor, 0u);
}

static uint64_t cbor_read_map_size(struct cbor_cursor *cursor)
{
	return cbor_read_type(cursor, 5u);
}

static uint64_t cbor_read_array_size(struct cbor_cursor *cursor)
{
	return cbor_read_type(cursor, 4u);
}

static uint8_t cbor_peek_major(const struct cbor_cursor *cursor)
{
	CHECK_TRUE(cursor->pos < cursor->len);
	return (uint8_t)(cursor->buf[cursor->pos] >> 5);
}

static const uint8_t *cbor_read_bstr(struct cbor_cursor *cursor, size_t *len)
{
	*len = (size_t)cbor_read_type(cursor, 2u);
	CHECK_TRUE(cursor->pos + *len <= cursor->len);

	const uint8_t *ptr = &cursor->buf[cursor->pos];
	cursor->pos += *len;

	return ptr;
}

static void cbor_read_text_equals(struct cbor_cursor *cursor, const char *expected)
{
	const size_t expected_len = strlen(expected);
	const size_t actual_len = (size_t)cbor_read_type(cursor, 3u);

	UNSIGNED_LONGS_EQUAL(expected_len, actual_len);
	CHECK_TRUE(cursor->pos + actual_len <= cursor->len);
	MEMCMP_EQUAL(expected, &cursor->buf[cursor->pos], actual_len);
	cursor->pos += actual_len;
}

static void assert_metric_payload_equals(const uint8_t *payload, size_t payload_len,
		int32_t expected_value)
{
	struct cbor_cursor cursor = { payload, payload_len, 0u };

	UNSIGNED_LONGS_EQUAL(1u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));

	if (cbor_peek_major(&cursor) == 4u) {
		UNSIGNED_LONGS_EQUAL(5u, cbor_read_array_size(&cursor));
		(void)cbor_read_uint(&cursor);
		(void)cbor_read_uint(&cursor);

		if (cbor_peek_major(&cursor) == 0u) {
			(void)cbor_read_uint(&cursor);
		} else {
			const uint8_t initial = cbor_read_byte(&cursor);
			UNSIGNED_LONGS_EQUAL(1u, (uint8_t)(initial >> 5));
			(void)cbor_read_arg(&cursor, (uint8_t)(initial & 0x1fu));
		}

		if (cbor_peek_major(&cursor) == 0u) {
			(void)cbor_read_uint(&cursor);
		} else {
			const uint8_t initial = cbor_read_byte(&cursor);
			UNSIGNED_LONGS_EQUAL(1u, (uint8_t)(initial >> 5));
			(void)cbor_read_arg(&cursor, (uint8_t)(initial & 0x1fu));
		}
	}

	if (cbor_peek_major(&cursor) == 0u) {
		LONGS_EQUAL(expected_value, (int32_t)cbor_read_uint(&cursor));
	} else {
		const uint8_t initial = cbor_read_byte(&cursor);
		UNSIGNED_LONGS_EQUAL(1u, (uint8_t)(initial >> 5));
		LONGS_EQUAL(expected_value,
				-(int32_t)cbor_read_arg(&cursor, (uint8_t)(initial & 0x1fu)) - 1);
	}

	UNSIGNED_LONGS_EQUAL(payload_len, cursor.pos);
}

TEST_GROUP(PulseCodec)
{
	void setup()
	{
		metrics_reset();
	}

	void teardown()
	{
		metrics_reset();
	}
};

TEST(PulseCodec, ShouldEncodeMetricsAsCanonicalCborMap)
{
	uint8_t buf[8];
	cbor_writer_t writer;

	metrics_set(PulseMetric, METRICS_VALUE(100));

	const size_t len = metrics_collect(buf, sizeof(buf), &writer);
	CHECK_TRUE(len > 0u);
	BYTES_EQUAL(0xA1, buf[0]);
	assert_metric_payload_equals(buf, len, 100);
}

TEST(PulseCodec, ShouldEncodeNegativeMetricValueAsCanonicalCborMap)
{
	uint8_t buf[8];
	cbor_writer_t writer;

	metrics_set(PulseMetric, METRICS_VALUE(-7));

	const size_t len = metrics_collect(buf, sizeof(buf), &writer);
	CHECK_TRUE(len > 0u);
	BYTES_EQUAL(0xA1, buf[0]);
	assert_metric_payload_equals(buf, len, -7);
}

TEST(PulseCodec, ShouldWrapMetricsPayloadInCanonicalEnvelope)
{
	uint8_t buf[64];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = TEST_SERIAL,
		.software_version = TEST_VERSION,
		.timestamp = 1234u,
		.window_start = 1234u,
		.window_end = 1234u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};
	const uint8_t metrics_payload[] = { 0xA1, 0x00, 0x18, 0x64 };
	const uint8_t expected[] = {
		0xA4,
		0x00, 0x01,
		0x01, 0xA2,
		0x00, 0x6D, 'd', 'e', 'v', 'i', 'c', 'e', '-', 's', 'e', 'r', 'i', 'a', 'l',
		0x01, 0x65, '1', '.', '0', '.', '0',
		0x02, 0xA3,
		0x00, 0x19, 0x04, 0xD2,
		0x01, 0x19, 0x04, 0xD2,
		0x02, 0x19, 0x04, 0xD2,
		0x03, 0x44, 0xA1, 0x00, 0x18, 0x64,
	};
	struct cbor_cursor cursor;
	size_t metrics_len = 0u;
	const uint8_t *metrics_ptr;

	memcpy(buf, metrics_payload, sizeof(metrics_payload));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), sizeof(metrics_payload),
					&ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));

	cursor.buf = buf;
	cursor.len = encoded_len;
	cursor.pos = 0u;

	UNSIGNED_LONGS_EQUAL(4u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	cbor_read_text_equals(&cursor, TEST_SERIAL);
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	cbor_read_text_equals(&cursor, TEST_VERSION);
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1234u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1234u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1234u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_uint(&cursor));
	metrics_ptr = cbor_read_bstr(&cursor, &metrics_len);
	assert_metric_payload_equals(metrics_ptr, metrics_len, 100);
	UNSIGNED_LONGS_EQUAL(encoded_len, cursor.pos);
}

TEST(PulseCodec, ShouldOmitOptionalFieldsForHeartbeatEnvelope)
{
	uint8_t buf[64];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = TEST_SERIAL,
		.software_version = TEST_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};
	const uint8_t expected[] = {
		0xA3,
		0x00, 0x01,
		0x01, 0xA2,
		0x00, 0x6D, 'd', 'e', 'v', 'i', 'c', 'e', '-', 's', 'e', 'r', 'i', 'a', 'l',
		0x01, 0x65, '1', '.', '0', '.', '0',
		0x02, 0xA0,
	};

	memset(buf, 0, sizeof(buf));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));
}

TEST(PulseCodec, ShouldRejectInvalidEnvelopeArguments)
{
	uint8_t buf[16] = { 0 };
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx invalid_ctx = {
		.serial_number = "",
		.software_version = TEST_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT,
			pulse_codec_wrap_metrics_payload(NULL, sizeof(buf), 0u,
					&invalid_ctx, &encoded_len));
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u,
					NULL, &encoded_len));
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u,
					&invalid_ctx, NULL));
	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u,
					&invalid_ctx, &encoded_len));
}

TEST(PulseCodec, ShouldReturnOverflowWhenEnvelopeBufferTooSmall)
{
	uint8_t buf[44] = { 0xA1, 0x00, 0x18, 0x64 };
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = TEST_SERIAL,
		.software_version = TEST_VERSION,
		.timestamp = 1234u,
		.window_start = 1234u,
		.window_end = 1234u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};

	CHECK_EQUAL(PULSE_STATUS_OVERFLOW,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 4u, &ctx, &encoded_len));
}

TEST(PulseCodec, ShouldRejectNullSerialNumber)
{
	uint8_t buf[64] = { 0 };
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = NULL,
		.software_version = TEST_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
}

TEST(PulseCodec, ShouldRejectNullSoftwareVersion)
{
	uint8_t buf[64] = { 0 };
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = TEST_SERIAL,
		.software_version = NULL,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
}

TEST(PulseCodec, ShouldRejectEmptySoftwareVersion)
{
	uint8_t buf[64] = { 0 };
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = TEST_SERIAL,
		.software_version = "",
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};

	CHECK_EQUAL(PULSE_STATUS_INVALID_ARGUMENT,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
}

TEST(PulseCodec, ShouldNotModifyEncodedLenOnError)
{
	const size_t sentinel = 0xBEEFu;
	size_t encoded_len;
	uint8_t buf[4] = { 0 };
	struct pulse_envelope_ctx invalid_ctx = {
		.serial_number = "",
		.software_version = TEST_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};
	struct pulse_envelope_ctx valid_ctx = {
		.serial_number = TEST_SERIAL,
		.software_version = TEST_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};

	encoded_len = sentinel;
	pulse_codec_wrap_metrics_payload(NULL, sizeof(buf), 0u, &valid_ctx, &encoded_len);
	UNSIGNED_LONGS_EQUAL(sentinel, encoded_len);

	encoded_len = sentinel;
	pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &invalid_ctx, &encoded_len);
	UNSIGNED_LONGS_EQUAL(sentinel, encoded_len);

	/* total_len >> bufsize → OVERFLOW, encoded_len must remain unchanged */
	encoded_len = sentinel;
	pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 4u, &valid_ctx, &encoded_len);
	UNSIGNED_LONGS_EQUAL(sentinel, encoded_len);
}

/*
 * Heartbeat with SHORT_SERIAL/"SHORT_VERSION produces exactly 13 bytes:
 *   A3 00 01 01 A2 00 61 61 01 61 62 02 A0
 * Verifies the lower bound of exact-fit buffer.
 */
TEST(PulseCodec, ShouldSucceedWhenBufferSizeIsExact)
{
	const uint8_t expected[] = {
		0xA3,
		0x00, 0x01,
		0x01, 0xA2,
		0x00, 0x61, SHORT_SERIAL[0],
		0x01, 0x61, SHORT_VERSION[0],
		0x02, 0xA0,
	};
	uint8_t buf[sizeof(expected)];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};

	memset(buf, 0xFF, sizeof(buf));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));
}

TEST(PulseCodec, ShouldReturnOverflowWhenBufferIsOneByteTooSmall)
{
	uint8_t buf[12]; /* heartbeat needs 13 bytes */
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};

	CHECK_EQUAL(PULSE_STATUS_OVERFLOW,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
}

/*
 * With SHORT_SERIAL/SHORT_VERSION and zero timestamps, header_len = 15.
 * metrics_len = 20 > 15 = header_len triggers an overlapping memmove
 * (dst region [15,34] overlaps src region [0,19] at bytes [15,19]).
 * Verifies that memmove correctly preserves all metrics bytes.
 *
 * Expected CBOR (35 bytes):
 *   A4 00 01 01 A2 00 61 61 01 61 62 02 A0 03 54   <- header (15 B)
 *   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13  <- metrics (20 B)
 */
TEST(PulseCodec, ShouldPreserveMetricsContentWhenMetricsOverlapHeader)
{
	static const uint8_t metrics_input[20] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		0x10, 0x11, 0x12, 0x13,
	};
	const uint8_t expected[] = {
		0xA4, 0x00, 0x01, 0x01, 0xA2,
		0x00, 0x61, SHORT_SERIAL[0],
		0x01, 0x61, SHORT_VERSION[0],
		0x02, 0xA0, 0x03, 0x54,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		0x10, 0x11, 0x12, 0x13,
	};
	uint8_t buf[64];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};
	struct cbor_cursor cursor;
	size_t bstr_len = 0u;
	const uint8_t *bstr_ptr;

	memcpy(buf, metrics_input, sizeof(metrics_input));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf),
					sizeof(metrics_input), &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));

	cursor.buf = buf;
	cursor.len = encoded_len;
	cursor.pos = 0u;
	UNSIGNED_LONGS_EQUAL(4u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	cbor_read_text_equals(&cursor, SHORT_SERIAL);
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	cbor_read_text_equals(&cursor, SHORT_VERSION);
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_uint(&cursor));
	bstr_ptr = cbor_read_bstr(&cursor, &bstr_len);
	UNSIGNED_LONGS_EQUAL(sizeof(metrics_input), bstr_len);
	MEMCMP_EQUAL(metrics_input, bstr_ptr, bstr_len);
	UNSIGNED_LONGS_EQUAL(encoded_len, cursor.pos);
}

/*
 * Expected CBOR (15 bytes) for heartbeat with BACKLOG_FAILURE reason:
 *   A3 00 01 01 A2 00 61 61 01 61 62 02 A1 03 01
 */
TEST(PulseCodec, ShouldIncludeSnapshotReasonWhenBacklogFailure)
{
	const uint8_t expected[] = {
		0xA3, 0x00, 0x01, 0x01, 0xA2,
		0x00, 0x61, SHORT_SERIAL[0],
		0x01, 0x61, SHORT_VERSION[0],
		0x02, 0xA1,
		0x03, PULSE_SNAPSHOT_REASON_BACKLOG_FAILURE,
	};
	uint8_t buf[sizeof(expected)];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_BACKLOG_FAILURE,
	};
	struct cbor_cursor cursor;

	memset(buf, 0, sizeof(buf));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));

	cursor.buf = buf;
	cursor.len = encoded_len;
	cursor.pos = 0u;
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	cbor_read_text_equals(&cursor, SHORT_SERIAL);
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	cbor_read_text_equals(&cursor, SHORT_VERSION);
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(PULSE_SNAPSHOT_REASON_BACKLOG_FAILURE, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(encoded_len, cursor.pos);
}

/*
 * Expected CBOR (15 bytes) for heartbeat with BACKLOG_CANCEL reason:
 *   A3 00 01 01 A2 00 61 61 01 61 62 02 A1 03 02
 */
TEST(PulseCodec, ShouldIncludeSnapshotReasonWhenBacklogCancel)
{
	const uint8_t expected[] = {
		0xA3, 0x00, 0x01, 0x01, 0xA2,
		0x00, 0x61, SHORT_SERIAL[0],
		0x01, 0x61, SHORT_VERSION[0],
		0x02, 0xA1,
		0x03, PULSE_SNAPSHOT_REASON_BACKLOG_CANCEL,
	};
	uint8_t buf[sizeof(expected)];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_BACKLOG_CANCEL,
	};
	struct cbor_cursor cursor;

	memset(buf, 0, sizeof(buf));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));

	cursor.buf = buf;
	cursor.len = encoded_len;
	cursor.pos = 0u;
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_map_size(&cursor));
	cbor_read_uint(&cursor); cbor_read_uint(&cursor); /* schema key+val */
	cbor_read_uint(&cursor); cbor_read_map_size(&cursor); /* device key+map */
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_SERIAL);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_VERSION);
	cbor_read_uint(&cursor); /* report key */
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(PULSE_SNAPSHOT_REASON_BACKLOG_CANCEL, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(encoded_len, cursor.pos);
}

/*
 * timestamp=1234, window_start=0, window_end=0 → report map has exactly 1 entry.
 * Expected CBOR (17 bytes):
 *   A3 00 01 01 A2 00 61 61 01 61 62 02 A1 00 19 04 D2
 */
TEST(PulseCodec, ShouldEncodeOnlyTimestampInReportMapWhenWindowFieldsAreZero)
{
	const uint8_t expected[] = {
		0xA3, 0x00, 0x01, 0x01, 0xA2,
		0x00, 0x61, SHORT_SERIAL[0],
		0x01, 0x61, SHORT_VERSION[0],
		0x02, 0xA1,
		0x00, 0x19, 0x04, 0xD2,
	};
	uint8_t buf[sizeof(expected)];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 1234u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};
	struct cbor_cursor cursor;

	memset(buf, 0, sizeof(buf));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));

	cursor.buf = buf;
	cursor.len = encoded_len;
	cursor.pos = 0u;
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_map_size(&cursor));
	cbor_read_uint(&cursor); cbor_read_uint(&cursor);
	cbor_read_uint(&cursor); cbor_read_map_size(&cursor);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_SERIAL);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_VERSION);
	cbor_read_uint(&cursor); /* report key */
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor)); /* timestamp key */
	UNSIGNED_LONGS_EQUAL(1234u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(encoded_len, cursor.pos);
}

/*
 * timestamp=0, window_start=100, window_end=200 → report map has exactly 2 entries.
 * Expected CBOR (19 bytes):
 *   A3 00 01 01 A2 00 61 61 01 61 62 02 A2 01 18 64 02 18 C8
 */
TEST(PulseCodec, ShouldEncodeWindowFieldsButNotTimestampWhenTimestampIsZero)
{
	const uint8_t expected[] = {
		0xA3, 0x00, 0x01, 0x01, 0xA2,
		0x00, 0x61, SHORT_SERIAL[0],
		0x01, 0x61, SHORT_VERSION[0],
		0x02, 0xA2,
		0x01, 0x18, 0x64, /* window_start = 100 */
		0x02, 0x18, 0xC8, /* window_end   = 200 */
	};
	uint8_t buf[sizeof(expected)];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 0u,
		.window_start = 100u,
		.window_end = 200u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};
	struct cbor_cursor cursor;

	memset(buf, 0, sizeof(buf));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));

	cursor.buf = buf;
	cursor.len = encoded_len;
	cursor.pos = 0u;
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_map_size(&cursor));
	cbor_read_uint(&cursor); cbor_read_uint(&cursor);
	cbor_read_uint(&cursor); cbor_read_map_size(&cursor);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_SERIAL);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_VERSION);
	cbor_read_uint(&cursor); /* report key */
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor)); /* window_start key */
	UNSIGNED_LONGS_EQUAL(100u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_uint(&cursor)); /* window_end key */
	UNSIGNED_LONGS_EQUAL(200u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(encoded_len, cursor.pos);
}

/*
 * metrics_len = 1 is the minimum non-zero payload.
 * Expected CBOR (16 bytes):
 *   A4 00 01 01 A2 00 61 61 01 61 62 02 A0 03 41 AB
 */
TEST(PulseCodec, ShouldHandleSingleByteMetricsPayload)
{
	const uint8_t metrics_byte = 0xABu;
	const uint8_t expected[] = {
		0xA4, 0x00, 0x01, 0x01, 0xA2,
		0x00, 0x61, SHORT_SERIAL[0],
		0x01, 0x61, SHORT_VERSION[0],
		0x02, 0xA0,
		0x03, 0x41, 0xAB,
	};
	uint8_t buf[sizeof(expected)];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 0u,
		.window_start = 0u,
		.window_end = 0u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};
	struct cbor_cursor cursor;
	size_t bstr_len = 0u;
	const uint8_t *bstr_ptr;

	buf[0] = metrics_byte;

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 1u, &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));

	cursor.buf = buf;
	cursor.len = encoded_len;
	cursor.pos = 0u;
	UNSIGNED_LONGS_EQUAL(4u, cbor_read_map_size(&cursor));
	cbor_read_uint(&cursor); cbor_read_uint(&cursor);
	cbor_read_uint(&cursor); cbor_read_map_size(&cursor);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_SERIAL);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_VERSION);
	cbor_read_uint(&cursor); cbor_read_map_size(&cursor); /* report key + empty map */
	cbor_read_uint(&cursor); /* metrics key */
	bstr_ptr = cbor_read_bstr(&cursor, &bstr_len);
	UNSIGNED_LONGS_EQUAL(1u, bstr_len);
	BYTES_EQUAL(metrics_byte, bstr_ptr[0]);
	UNSIGNED_LONGS_EQUAL(encoded_len, cursor.pos);
}

/*
 * Timestamps >= 0x10000 require 4-byte CBOR uint (0x1A prefix).
 * Verifies correct multi-byte encoding for all three timestamp fields.
 * Expected CBOR (31 bytes):
 *   A3 00 01 01 A2 00 61 61 01 61 62
 *   02 A3
 *   00 1A 00 01 00 00   <- timestamp   = 65536
 *   01 1A 00 01 00 01   <- window_start = 65537
 *   02 1A 00 01 00 02   <- window_end   = 65538
 */
TEST(PulseCodec, ShouldEncodeMultiByteTimestampInReport)
{
	const uint8_t expected[] = {
		0xA3, 0x00, 0x01, 0x01, 0xA2,
		0x00, 0x61, SHORT_SERIAL[0],
		0x01, 0x61, SHORT_VERSION[0],
		0x02, 0xA3,
		0x00, 0x1A, 0x00, 0x01, 0x00, 0x00,
		0x01, 0x1A, 0x00, 0x01, 0x00, 0x01,
		0x02, 0x1A, 0x00, 0x01, 0x00, 0x02,
	};
	uint8_t buf[sizeof(expected)];
	size_t encoded_len = 0u;
	struct pulse_envelope_ctx ctx = {
		.serial_number = SHORT_SERIAL,
		.software_version = SHORT_VERSION,
		.timestamp = 65536u,
		.window_start = 65537u,
		.window_end = 65538u,
		.snapshot_reason = PULSE_SNAPSHOT_REASON_LIVE,
	};
	struct cbor_cursor cursor;

	memset(buf, 0, sizeof(buf));

	CHECK_EQUAL(PULSE_STATUS_OK,
			pulse_codec_wrap_metrics_payload(buf, sizeof(buf), 0u, &ctx, &encoded_len));
	UNSIGNED_LONGS_EQUAL(sizeof(expected), encoded_len);
	MEMCMP_EQUAL(expected, buf, sizeof(expected));

	cursor.buf = buf;
	cursor.len = encoded_len;
	cursor.pos = 0u;
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_map_size(&cursor));
	cbor_read_uint(&cursor); cbor_read_uint(&cursor);
	cbor_read_uint(&cursor); cbor_read_map_size(&cursor);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_SERIAL);
	cbor_read_uint(&cursor); cbor_read_text_equals(&cursor, SHORT_VERSION);
	cbor_read_uint(&cursor); /* report key */
	UNSIGNED_LONGS_EQUAL(3u, cbor_read_map_size(&cursor));
	UNSIGNED_LONGS_EQUAL(0u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(65536u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(1u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(65537u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(2u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(65538u, cbor_read_uint(&cursor));
	UNSIGNED_LONGS_EQUAL(encoded_len, cursor.pos);
}
