/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#define T5AI_SLEEP_PROFILE_CANDIDATES 8u
typedef struct {
  uint32_t id, remaining_ms, period_ms;
  uintptr_t callback, context;
} T5AISleepTimerCandidate;
unsigned t5ai_sleep_profile_timer_candidates(T5AISleepTimerCandidate *out);

void t5ai_sleep_profile_timers_ready(void);
unsigned t5ai_sleep_profile_timers_untracked(void);
