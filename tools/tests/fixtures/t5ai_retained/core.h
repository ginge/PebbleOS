/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
uint32_t test_mask_get(void);
void test_mask_set(uint32_t mask);
void test_barrier(void);
#define __get_PRIMASK()     test_mask_get()
#define __disable_irq()     test_mask_set(1)
#define __set_PRIMASK(mask) test_mask_set(mask)
#define __DSB()             test_barrier()
