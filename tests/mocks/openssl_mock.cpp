/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

extern "C" {
#include "openssl/sha.h"
#include "openssl/ssl.h"
}

struct ssl_ctx_st {
	int reserved;
};

struct ssl_st {
	int reserved;
};

static ssl_ctx_st g_default_ssl_ctx;
static ssl_st g_default_ssl;

static struct {
	int sha256_enabled;
	unsigned char digest[SHA256_DIGEST_LENGTH];
	unsigned char input[256];
	size_t input_len;
} g_state;

static struct {
	int cipher_list_result;
	char last_cipher_list[256];
	int last_security_level;
} g_ssl_state;

extern "C" void openssl_mock_reset(void)
{
	size_t i;

	memset(&g_state, 0, sizeof(g_state));
	g_state.sha256_enabled = 1;
	for (i = 0u; i < SHA256_DIGEST_LENGTH; i++) {
		g_state.digest[i] = (unsigned char)i;
	}

	memset(&g_ssl_state, 0, sizeof(g_ssl_state));
	g_ssl_state.cipher_list_result = 1;
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

extern "C" void openssl_ssl_mock_reset(void)
{
	memset(&g_ssl_state, 0, sizeof(g_ssl_state));
	g_ssl_state.cipher_list_result = 1;
}

extern "C" void openssl_ssl_mock_set_cipher_list_result(int result)
{
	g_ssl_state.cipher_list_result = result;
}

extern "C" SSL *openssl_ssl_mock_get_ssl(void)
{
	return &g_default_ssl;
}

extern "C" SSL_CTX *openssl_ssl_mock_get_ssl_ctx(void)
{
	return &g_default_ssl_ctx;
}

extern "C" const char *openssl_ssl_mock_last_cipher_list(void)
{
	return g_ssl_state.last_cipher_list;
}

extern "C" int openssl_ssl_mock_last_security_level(void)
{
	return g_ssl_state.last_security_level;
}

extern "C" SSL_CTX *SSL_get_SSL_CTX(const SSL *ssl)
{
	(void)ssl;
	return &g_default_ssl_ctx;
}

extern "C" int SSL_CTX_set_security_level(SSL_CTX *ctx, int level)
{
	(void)ctx;
	g_ssl_state.last_security_level = level;
	return 1;
}

extern "C" int SSL_CTX_set_cipher_list(SSL_CTX *ctx, const char *str)
{
	(void)ctx;
	if (str != NULL) {
		size_t len = strlen(str);
		if (len >= sizeof(g_ssl_state.last_cipher_list)) {
			len = sizeof(g_ssl_state.last_cipher_list) - 1u;
		}
		memcpy(g_ssl_state.last_cipher_list, str, len);
		g_ssl_state.last_cipher_list[len] = '\0';
	}
	return g_ssl_state.cipher_list_result;
}
