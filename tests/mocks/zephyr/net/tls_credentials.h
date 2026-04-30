/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MOCK_ZEPHYR_NET_TLS_CREDENTIALS_H
#define MOCK_ZEPHYR_NET_TLS_CREDENTIALS_H

#include <stddef.h>
#include <stdint.h>

#define ZSOCK_SOL_TLS			282

#define ZSOCK_TLS_PEER_VERIFY		4
#define ZSOCK_TLS_PEER_VERIFY_NONE	0
#define ZSOCK_TLS_PEER_VERIFY_OPTIONAL	1
#define ZSOCK_TLS_PEER_VERIFY_REQUIRED	2

#define ZSOCK_TLS_SEC_TAG_LIST		1
#define ZSOCK_TLS_HOSTNAME		2
#define ZSOCK_TLS_CIPHERSUITE_LIST	3
#define ZSOCK_TLS_DTLS_ROLE		6

#define ZSOCK_TLS_DTLS_ROLE_CLIENT	0
#define ZSOCK_TLS_DTLS_ROLE_SERVER	1

#define SOL_TLS				ZSOCK_SOL_TLS
#define TLS_PEER_VERIFY			ZSOCK_TLS_PEER_VERIFY
#define TLS_PEER_VERIFY_NONE		ZSOCK_TLS_PEER_VERIFY_NONE
#define TLS_PEER_VERIFY_OPTIONAL	ZSOCK_TLS_PEER_VERIFY_OPTIONAL
#define TLS_PEER_VERIFY_REQUIRED	ZSOCK_TLS_PEER_VERIFY_REQUIRED
#define TLS_SEC_TAG_LIST		ZSOCK_TLS_SEC_TAG_LIST
#define TLS_HOSTNAME			ZSOCK_TLS_HOSTNAME
#define TLS_CIPHERSUITE_LIST		ZSOCK_TLS_CIPHERSUITE_LIST
#define TLS_DTLS_ROLE			ZSOCK_TLS_DTLS_ROLE
#define TLS_DTLS_ROLE_CLIENT		ZSOCK_TLS_DTLS_ROLE_CLIENT
#define TLS_DTLS_ROLE_SERVER		ZSOCK_TLS_DTLS_ROLE_SERVER

typedef int sec_tag_t;

enum tls_credential_type {
	TLS_CREDENTIAL_NONE = 0,
	TLS_CREDENTIAL_CA_CERTIFICATE,
	TLS_CREDENTIAL_SERVER_CERTIFICATE,
	TLS_CREDENTIAL_PRIVATE_KEY,
	TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
	TLS_CREDENTIAL_PSK,
	TLS_CREDENTIAL_PSK_ID,
};

int tls_credential_add(sec_tag_t tag, enum tls_credential_type type,
		const void *cred, size_t credlen);
int tls_credential_delete(sec_tag_t tag, enum tls_credential_type type);

void zephyr_tls_mock_reset(void);
void zephyr_tls_mock_set_add_result(enum tls_credential_type type, int result);
void zephyr_tls_mock_set_delete_result(enum tls_credential_type type, int result);
sec_tag_t zephyr_tls_mock_last_psk_tag(void);
sec_tag_t zephyr_tls_mock_last_psk_id_tag(void);
const uint8_t *zephyr_tls_mock_last_psk(void);
size_t zephyr_tls_mock_last_psk_len(void);
const char *zephyr_tls_mock_last_psk_id(void);
size_t zephyr_tls_mock_last_psk_id_len(void);

#endif /* MOCK_ZEPHYR_NET_TLS_CREDENTIALS_H */
