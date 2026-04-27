/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OPENSSL_SSL_H
#define OPENSSL_SSL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

SSL_CTX *SSL_get_SSL_CTX(const SSL *ssl);
int SSL_CTX_set_security_level(SSL_CTX *ctx, int level);
int SSL_CTX_set_cipher_list(SSL_CTX *ctx, const char *str);

void openssl_ssl_mock_reset(void);
void openssl_ssl_mock_set_cipher_list_result(int result);
SSL *openssl_ssl_mock_get_ssl(void);
SSL_CTX *openssl_ssl_mock_get_ssl_ctx(void);
const char *openssl_ssl_mock_last_cipher_list(void);
int openssl_ssl_mock_last_security_level(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENSSL_SSL_H */
