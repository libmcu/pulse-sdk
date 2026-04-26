/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MOCK_ZEPHYR_NET_SOCKET_H
#define MOCK_ZEPHYR_NET_SOCKET_H

#include "zephyr/posix/sys/time.h"
#include "zephyr/net/tls_credentials.h"
#include <stddef.h>

#define NET_SOCK_STREAM		1
#define NET_AF_UNSPEC		0
#define NET_AF_INET		2
#define NET_AF_INET6		10

#define NET_IPPROTO_TLS_1_2	258

#define ZSOCK_SOL_SOCKET	1
#define ZSOCK_SO_RCVTIMEO	20
#define ZSOCK_SO_SNDTIMEO	21

#define SOCK_STREAM		NET_SOCK_STREAM
#define AF_UNSPEC		NET_AF_UNSPEC
#define AF_INET			NET_AF_INET
#define AF_INET6		NET_AF_INET6
#define IPPROTO_TLS_1_2		NET_IPPROTO_TLS_1_2
#define SOL_SOCKET		ZSOCK_SOL_SOCKET
#define SO_RCVTIMEO		ZSOCK_SO_RCVTIMEO
#define SO_SNDTIMEO		ZSOCK_SO_SNDTIMEO

#define IS_ENABLED(x)		(!!(x))
#define CONFIG_NET_IPV4		0
#define CONFIG_NET_IPV6		0

struct sockaddr {
	int sa_family;
	char sa_data[14];
};

struct zsock_addrinfo {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	unsigned int ai_addrlen;
	struct sockaddr *ai_addr;
	struct zsock_addrinfo *ai_next;
};

int zsock_getaddrinfo(const char *host, const char *service,
		const struct zsock_addrinfo *hints,
		struct zsock_addrinfo **res);
void zephyr_socket_mock_set_addrinfo_count(size_t count);
void zsock_freeaddrinfo(struct zsock_addrinfo *ai);
int zsock_socket(int family, int type, int proto);
int zsock_setsockopt(int sock, int level, int optname,
		const void *optval, unsigned int optlen);
int zsock_connect(int sock, const struct sockaddr *addr, unsigned int addrlen);
int zsock_close(int sock);

#endif /* MOCK_ZEPHYR_NET_SOCKET_H */
