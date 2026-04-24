/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PULSE_OVERRIDES_H
#define PULSE_OVERRIDES_H

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Abort an in-progress transport session.
 *
 * Port hook called by pulse_cancel(). Override this function to perform
 * platform-specific cleanup such as tearing down the transport session.
 * Falls back to a no-op if not overridden.
 */
void pulse_transport_cancel(void);

#if defined(__cplusplus)
}
#endif

#endif /* PULSE_OVERRIDES_H */
