/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include "libmcu/assert.h"

void libmcu_assertion_failed(const uintptr_t *pc, const uintptr_t *lr)
{
	(void)pc;
	(void)lr;
}
