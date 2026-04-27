/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OPENSSL_SHA_H
#define OPENSSL_SHA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_LENGTH 32

unsigned char *SHA256(const unsigned char *data, size_t len,
		unsigned char *digest);

void openssl_mock_reset(void);
void openssl_mock_set_sha256_result(int enabled);
void openssl_mock_set_sha256_digest(const unsigned char *digest, size_t len);
size_t openssl_mock_last_input_len(void);
const unsigned char *openssl_mock_last_input(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENSSL_SHA_H */
