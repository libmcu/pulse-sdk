/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MOCK_ZEPHYR_KERNEL_H
#define MOCK_ZEPHYR_KERNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t k_uptime_get(void);
void zephyr_uptime_mock_reset(void);
void zephyr_uptime_mock_set_ms(int64_t time_ms);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_ZEPHYR_KERNEL_H */
