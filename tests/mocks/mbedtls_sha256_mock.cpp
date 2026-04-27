/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

extern "C" {
#include "mbedtls/sha256.h"
}

static struct {
	int result;
	unsigned char digest[32];
	unsigned char last_input[256];
	size_t last_input_len;
} g_state;

extern "C" void mbedtls_sha256_mock_reset(void)
{
	size_t i;

	memset(&g_state, 0, sizeof(g_state));
	for (i = 0u; i < 32u; i++) {
		g_state.digest[i] = (unsigned char)i;
	}
}

extern "C" void mbedtls_sha256_mock_set_result(int result)
{
	g_state.result = result;
}

extern "C" void mbedtls_sha256_mock_set_digest(const unsigned char *digest,
		size_t len)
{
	if (len > sizeof(g_state.digest)) {
		len = sizeof(g_state.digest);
	}
	memcpy(g_state.digest, digest, len);
}

extern "C" size_t mbedtls_sha256_mock_last_input_len(void)
{
	return g_state.last_input_len;
}

extern "C" const unsigned char *mbedtls_sha256_mock_last_input(void)
{
	return g_state.last_input;
}

extern "C" int mbedtls_sha256(const unsigned char *input, size_t ilen,
		unsigned char output[32], int is224)
{
	size_t copy_len;

	(void)is224;

	copy_len = ilen < sizeof(g_state.last_input) ? ilen
			: sizeof(g_state.last_input);
	if (copy_len > 0u) {
		memcpy(g_state.last_input, input, copy_len);
	}
	g_state.last_input_len = ilen;

	if (g_state.result == 0) {
		memcpy(output, g_state.digest, 32u);
	}

	return g_state.result;
}
