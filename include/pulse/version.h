/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PULSE_VERSION_H
#define PULSE_VERSION_H

#if defined(__cplusplus)
extern "C" {
#endif

#if !defined(MAKE_VERSION)
#define MAKE_VERSION(major, minor, patch)	\
	(((major) << 16) | ((minor) << 8) | (patch))
#endif

#define PULSE_VERSION_MAJOR	0
#define PULSE_VERSION_MINOR	2
#define PULSE_VERSION_BUILD	0
#define PULSE_VERSION		MAKE_VERSION(PULSE_VERSION_MAJOR, \
					PULSE_VERSION_MINOR, \
					PULSE_VERSION_BUILD)

#if defined(__cplusplus)
}
#endif

#endif /* PULSE_VERSION_H */
