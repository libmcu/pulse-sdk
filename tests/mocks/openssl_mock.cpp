/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

extern "C" {
#include "openssl/sha.h"
}

static struct {
	int sha256_enabled;
	unsigned char digest[SHA256_DIGEST_LENGTH];
	unsigned char input[256];
	size_t input_len;
} g_state;

extern "C" void openssl_mock_reset(void)
{
	size_t i;

	memset(&g_state, 0, sizeof(g_state));
	g_state.sha256_enabled = 1;
	for (i = 0u; i < SHA256_DIGEST_LENGTH; i++) {
		g_state.digest[i] = (unsigned char)i;
	}
}

extern "C" void openssl_mock_set_sha256_result(int enabled)
{
	g_state.sha256_enabled = enabled;
}

extern "C" void openssl_mock_set_sha256_digest(const unsigned char *digest,
		size_t len)
{
	if (len > SHA256_DIGEST_LENGTH) {
		len = SHA256_DIGEST_LENGTH;
	}

	memcpy(g_state.digest, digest, len);
}

extern "C" size_t openssl_mock_last_input_len(void)
{
	return g_state.input_len;
}

extern "C" const unsigned char *openssl_mock_last_input(void)
{
	return g_state.input;
}

extern "C" unsigned char *SHA256(const unsigned char *data, size_t len,
		unsigned char *digest)
{
	if (!g_state.sha256_enabled) {
		return NULL;
	}

	if (len > sizeof(g_state.input)) {
		len = sizeof(g_state.input);
	}

	if (len > 0u) {
		memcpy(g_state.input, data, len);
	}
	g_state.input_len = len;
	memcpy(digest, g_state.digest, SHA256_DIGEST_LENGTH);

	return digest;
}
