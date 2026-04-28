/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MBEDTLS_SHA256_H
#define MBEDTLS_SHA256_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int mbedtls_sha256(const unsigned char *input, size_t ilen,
		unsigned char output[32], int is224);

void mbedtls_sha256_mock_reset(void);
void mbedtls_sha256_mock_set_result(int result);
void mbedtls_sha256_mock_set_digest(const unsigned char *digest, size_t len);
size_t mbedtls_sha256_mock_last_input_len(void);
const unsigned char *mbedtls_sha256_mock_last_input(void);

#ifdef __cplusplus
}
#endif

#endif /* MBEDTLS_SHA256_H */
