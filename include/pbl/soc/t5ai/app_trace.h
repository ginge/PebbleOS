/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#ifdef CONFIG_T5AI_APP_TRACE
void t5ai_app_trace_tick(const uint32_t *frame, uint32_t exc_return);
void t5ai_app_trace_dump(void);
#else
static inline void t5ai_app_trace_dump(void) {
}
#endif
