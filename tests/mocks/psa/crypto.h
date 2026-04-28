/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PSA_CRYPTO_H
#define PSA_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t psa_status_t;
typedef uint32_t psa_algorithm_t;

#define PSA_SUCCESS			((psa_status_t)0)
#define PSA_ERROR_GENERIC_ERROR		((psa_status_t)-132)

#define PSA_ALG_SHA_256			((psa_algorithm_t)0x02000009u)
#define PSA_HASH_LENGTH(alg)		32u

psa_status_t psa_hash_compute(psa_algorithm_t alg,
		const uint8_t *input, size_t input_length,
		uint8_t *hash, size_t hash_size,
		size_t *hash_length);

void psa_crypto_mock_reset(void);
void psa_crypto_mock_set_result(psa_status_t result);
void psa_crypto_mock_set_digest(const uint8_t *digest, size_t len);
size_t psa_crypto_mock_last_input_len(void);
const uint8_t *psa_crypto_mock_last_input(void);

#ifdef __cplusplus
}
#endif

#endif /* PSA_CRYPTO_H */
