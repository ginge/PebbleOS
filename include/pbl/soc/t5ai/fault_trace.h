/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
// Fatal diagnostic only. Bypasses the logical mailbox worker; never consumes RX/ACKs.
bool t5ai_fault_trace_words(const uint32_t *words, size_t count);
#endif
