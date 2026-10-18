/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/soc/t5ai/state_trace.h>

#include <system/reboot_reason_platform.h>

PBL_LOG_MODULE_DEFINE(t5ai_reboot_reason, LOG_LEVEL_DEBUG);

static bool s_boot_reason_pending = true;

void reboot_reason_platform_will_set(const RebootReason *reason) {
  t5ai_reboot_trace_event(T5RebootTrace_ReasonSet, reason->code);
  if (reason->code == RebootReasonCode_Watchdog) {
    t5ai_state_trace_freeze((uint32_t)reason->code);
  }
}

void reboot_reason_platform_did_set(const RebootReason *reason) {
  if ((uint32_t)reason->code < (0x7fu - T5AI_RESET_REASON_PEBBLE_BASE)) {
    t5ai_reset_reason_set_ap(T5AI_RESET_REASON_PEBBLE_BASE | (uint8_t)reason->code);
  }
}

void reboot_reason_platform_get(RebootReason *reason) {
  uint32_t first_word;
  memcpy(&first_word, reason, sizeof(first_word));
  if (first_word == 0u && s_boot_reason_pending) {
    const uint8_t hardware_reason = t5ai_reset_reason_get_boot_ap();
    if ((hardware_reason & 0xf0u) == T5AI_RESET_REASON_MAILBOX_BASE) {
      reason->code = RebootReasonCode_MailboxTimeout;
      reason->extra.value = hardware_reason & 0x0fu;
    } else if (hardware_reason >= T5AI_RESET_REASON_PEBBLE_BASE && hardware_reason < 0x7fu) {
      reason->code = (RebootReasonCode)(hardware_reason - T5AI_RESET_REASON_PEBBLE_BASE);
    }
  }
}

void reboot_reason_platform_will_clear(void) {
  if (s_boot_reason_pending) {
    PBL_LOG_INFO("T5AI boot reset sources: AP=%u CP=%u", t5ai_reset_reason_get_boot_ap(),
                 t5ai_reset_reason_get_boot_cp());
  }
  s_boot_reason_pending = false;
}

void reboot_reason_platform_did_clear(void) {
  const uint8_t hardware_reason = t5ai_reset_reason_get_ap();
  if (hardware_reason >= T5AI_RESET_REASON_MAILBOX_BASE) {
    t5ai_reset_reason_set_ap(0u);
  }
}
