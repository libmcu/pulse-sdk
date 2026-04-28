/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

extern "C" {
#include "psa/crypto.h"
}

static struct {
	psa_status_t result;
	psa_status_t init_result;
	uint8_t digest[32];
	uint8_t last_input[256];
	size_t last_input_len;
} g_state;

extern "C" void psa_crypto_mock_reset(void)
{
	size_t i;

	memset(&g_state, 0, sizeof(g_state));
	for (i = 0u; i < 32u; i++) {
		g_state.digest[i] = (uint8_t)i;
	}
}

extern "C" void psa_crypto_mock_set_result(psa_status_t result)
{
	g_state.result = result;
}

extern "C" void psa_crypto_mock_set_init_result(psa_status_t result)
{
	g_state.init_result = result;
}

extern "C" void psa_crypto_mock_set_digest(const uint8_t *digest, size_t len)
{
	if (len > sizeof(g_state.digest)) {
		len = sizeof(g_state.digest);
	}
	memcpy(g_state.digest, digest, len);
}

extern "C" size_t psa_crypto_mock_last_input_len(void)
{
	return g_state.last_input_len;
}

extern "C" const uint8_t *psa_crypto_mock_last_input(void)
{
	return g_state.last_input;
}

extern "C" psa_status_t psa_crypto_init(void)
{
	return g_state.init_result;
}

extern "C" psa_status_t psa_hash_compute(psa_algorithm_t alg,
		const uint8_t *input, size_t input_length,
		uint8_t *hash, size_t hash_size, size_t *hash_length)
{
	size_t copy_len;

	(void)alg;

	copy_len = input_length < sizeof(g_state.last_input)
			? input_length : sizeof(g_state.last_input);
	if (copy_len > 0u) {
		memcpy(g_state.last_input, input, copy_len);
	}
	g_state.last_input_len = input_length;

	if (g_state.result == PSA_SUCCESS) {
		size_t out_len = 32u < hash_size ? 32u : hash_size;

		memcpy(hash, g_state.digest, out_len);
		*hash_length = out_len;
	}

	return g_state.result;
}
