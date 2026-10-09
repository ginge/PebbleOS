/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#ifdef CONFIG_T5AI_STATE_TRACE
void t5ai_state_trace_start(void);
void t5ai_state_trace_freeze(uint32_t reason);
#else
static inline void t5ai_state_trace_start(void) {
}
static inline void t5ai_state_trace_freeze(uint32_t reason) {
  (void)reason;
}
#endif
