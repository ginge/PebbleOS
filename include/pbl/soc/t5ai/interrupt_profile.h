/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#ifdef CONFIG_T5AI_INTERRUPT_PROFILE
uint32_t t5ai_interrupt_profile_irq_enter(void);
void t5ai_interrupt_profile_irq_exit(unsigned id, uint32_t start);
void t5ai_interrupt_profile_start(void);
void t5ai_interrupt_profile_dump(void);
#else
static inline uint32_t t5ai_interrupt_profile_irq_enter(void) {
  return 0;
}
static inline void t5ai_interrupt_profile_irq_exit(unsigned id, uint32_t start) {
  (void)id;
  (void)start;
}
static inline void t5ai_interrupt_profile_start(void) {
}
static inline void t5ai_interrupt_profile_dump(void) {
}
#endif
