/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

void t5ai_tick_start(void);

#ifdef CONFIG_T5AI_TICKLESS_WFI
#include <stdbool.h>
#include <stdint.h>
// Call with interrupts masked, only around ordinary WFI after idle confirmation.
bool t5ai_tick_prepare_idle(uint32_t max_ticks);
void t5ai_tick_resume_idle(void);
#endif

#ifdef CONFIG_T5AI_TICK_PROFILE
#include <stdint.h>
typedef struct {
  uint32_t irqs, ticks, maximum, extended, early, aborted;
} T5AITickProfile;
extern T5AITickProfile t5ai_tick_profile;
void t5ai_tick_profile_start(void);
#endif
