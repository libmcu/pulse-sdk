/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <stdint.h>

int64_t esp_timer_get_time(void);
void esp_timer_mock_reset(void);
void esp_timer_mock_set_time(int64_t time_us);
void esp_timer_mock_set_step(int64_t step_us);

#endif /* ESP_TIMER_H */
