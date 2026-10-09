/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system/reboot_reason.h"

#include <pbl/logging/logging.h>
PBL_LOG_MODULE_DEFINE(t5ai_reboot_reason, LOG_LEVEL_DEBUG);
#include "pbl/mcu/interrupts.h"
#include <pbl/soc/t5ai/retained.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/soc/t5ai/state_trace.h>

#include <pbl/soc/t5ai/kernel.h>

_Static_assert(sizeof(RebootReason) == sizeof(uint32_t[4]),
               "RebootReason is a funny size");

void reboot_reason_set(RebootReason *reason) {
  if (reason->code == RebootReasonCode_Watchdog) {
    t5ai_state_trace_freeze((uint32_t)reason->code);
  }
  uint32_t *raw = (uint32_t *)reason;
  if (t5ai_retained_read(REBOOT_REASON_REGISTER_1)) {
    if (!mcu_state_is_isr() && __get_BASEPRI() == 0 &&
        pbl_kernel_is_running()) {
      PBL_LOG_WRN("Reboot reason is already set");
    }
    return;
  }

  t5ai_retained_write(REBOOT_REASON_REGISTER_1, raw[0]);
  t5ai_retained_write(REBOOT_REASON_STUCK_TASK_PC, raw[1]);
  t5ai_retained_write(REBOOT_REASON_STUCK_TASK_LR, raw[2]);
  t5ai_retained_write(REBOOT_REASON_STUCK_TASK_CALLBACK, raw[3]);
  if ((uint32_t)reason->code < (0x7fu - T5AI_RESET_REASON_PEBBLE_BASE)) {
    t5ai_reset_reason_set_ap(T5AI_RESET_REASON_PEBBLE_BASE | (uint8_t)reason->code);
  }
}

void reboot_reason_set_restarted_safely(void) {
  RebootReason reason;
  reboot_reason_get(&reason);
  reason.restarted_safely = true;

  uint32_t *raw = (uint32_t *)&reason;
  t5ai_retained_write(REBOOT_REASON_REGISTER_1, *raw);
}

void reboot_reason_get(RebootReason *reason) {
  uint32_t *raw = (uint32_t *)reason;
  raw[0] = t5ai_retained_read(REBOOT_REASON_REGISTER_1);
  raw[1] = t5ai_retained_read(REBOOT_REASON_STUCK_TASK_PC);
  raw[2] = t5ai_retained_read(REBOOT_REASON_STUCK_TASK_LR);
  raw[3] = t5ai_retained_read(REBOOT_REASON_STUCK_TASK_CALLBACK);
  if (raw[0] == 0u) {
    const uint8_t hardware_reason = t5ai_reset_reason_get_ap();
    if ((hardware_reason & 0xf0u) == T5AI_RESET_REASON_MAILBOX_BASE) {
      reason->code = RebootReasonCode_MailboxTimeout;
      reason->extra.value = hardware_reason & 0x0fu;
    } else if (hardware_reason >= T5AI_RESET_REASON_PEBBLE_BASE &&
               hardware_reason < 0x7fu) {
      reason->code = (RebootReasonCode)(hardware_reason - T5AI_RESET_REASON_PEBBLE_BASE);
    }
  }
}

void reboot_reason_clear(void) {
  t5ai_retained_write(REBOOT_REASON_REGISTER_1, 0);
  t5ai_retained_write(REBOOT_REASON_STUCK_TASK_PC, 0);
  t5ai_retained_write(REBOOT_REASON_STUCK_TASK_LR, 0);
  t5ai_retained_write(REBOOT_REASON_STUCK_TASK_CALLBACK, 0);
  const uint8_t hardware_reason = t5ai_reset_reason_get_ap();
  if (hardware_reason >= T5AI_RESET_REASON_MAILBOX_BASE) {
    t5ai_reset_reason_set_ap(0u);
  }
}

uint32_t reboot_get_slot_of_last_launched_app(void) {
  return t5ai_retained_read(SLOT_OF_LAST_LAUNCHED_APP);
}

void reboot_set_slot_of_last_launched_app(uint32_t app_slot) {
  t5ai_retained_write(SLOT_OF_LAST_LAUNCHED_APP, app_slot);
}
