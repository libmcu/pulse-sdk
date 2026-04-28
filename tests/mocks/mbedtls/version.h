/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MBEDTLS_VERSION_H
#define MBEDTLS_VERSION_H

#ifndef MBEDTLS_VERSION_NUMBER
#  ifdef MBEDTLS_VERSION_NUMBER_MOCK
#    define MBEDTLS_VERSION_NUMBER	MBEDTLS_VERSION_NUMBER_MOCK
#  else
#    define MBEDTLS_VERSION_NUMBER	0x04000000
#  endif
#endif

#endif /* MBEDTLS_VERSION_H */
