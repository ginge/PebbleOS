/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#define GPIO_IRQn         55
#define PBL_SOC_IRQN_GPIO 55
extern uint32_t t5ai_test_primask;
static inline uint32_t __get_PRIMASK(void) {
  return t5ai_test_primask;
}
static inline void __disable_irq(void) {
  t5ai_test_primask = 1;
}
static inline void __enable_irq(void) {
  t5ai_test_primask = 0;
}
static inline void NVIC_ClearPendingIRQ(int irq) {
}
static inline void NVIC_SetPriority(int irq, unsigned priority) {
}
static inline void NVIC_EnableIRQ(int irq) {
}
