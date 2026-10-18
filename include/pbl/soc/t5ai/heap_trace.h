/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#ifdef CONFIG_T5AI_HEAP_TRACE
void t5ai_heap_trace_dump(void);
#else
static inline void t5ai_heap_trace_dump(void) {
}
#endif
