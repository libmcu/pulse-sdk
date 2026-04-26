/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MOCK_ZEPHYR_NET_TLS_CREDENTIALS_H
#define MOCK_ZEPHYR_NET_TLS_CREDENTIALS_H

#define SOL_TLS				282

#define TLS_PEER_VERIFY			4
#define TLS_PEER_VERIFY_NONE		0
#define TLS_PEER_VERIFY_OPTIONAL	1
#define TLS_PEER_VERIFY_REQUIRED	2

#define TLS_SEC_TAG_LIST		1
#define TLS_HOSTNAME			2

typedef int sec_tag_t;

#endif /* MOCK_ZEPHYR_NET_TLS_CREDENTIALS_H */
