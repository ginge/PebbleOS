/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef enum {
  T5RebootTrace_HeartbeatAttempt = 1,
  T5RebootTrace_HeartbeatSkip,
  T5RebootTrace_HeartbeatSend,
  T5RebootTrace_HeartbeatAck,
  T5RebootTrace_HeartbeatResponse,
  T5RebootTrace_MailboxTimeout,
  T5RebootTrace_ReasonSet,
  T5RebootTrace_ForceReset,
  T5RebootTrace_CPResetRequest,
} T5RebootTraceEvent;
#ifdef CONFIG_T5AI_REBOOT_TRACE
void t5ai_reboot_trace_watchdog(bool running);
void t5ai_reboot_trace_capture_boot(void);
void t5ai_reboot_trace_start(void);
void t5ai_reboot_trace_event(T5RebootTraceEvent event, uint32_t value);
#else
static inline void t5ai_reboot_trace_watchdog(bool running) {
  (void)running;
}
static inline void t5ai_reboot_trace_capture_boot(void) {
}
static inline void t5ai_reboot_trace_start(void) {
}
static inline void t5ai_reboot_trace_event(T5RebootTraceEvent event, uint32_t value) {
  (void)event;
  (void)value;
}
#endif
