/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>

#include <pbl/drivers/flash.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/mcu/interrupts.h>

#include <board/board.h>
#include <kernel/core_dump.h>
#include <kernel/util/fw_reset.h>
#include <system/bootbits.h>
#include <system/reboot_reason.h>
#include <system/reset.h>

#ifdef CONFIG_SOC_POSIX
#include <posix_host.h>
#endif

#include <cmsis_core.h>

#ifdef CONFIG_SOC_T5AI
#include <pbl/soc/t5ai/hci_ipc.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/kernel/thread.h>
#endif

#ifdef CONFIG_SOC_SF32LB52
#include <bf0_hal.h>
#endif

void system_reset_prepare(void) {
  fw_prepare_for_reset();
  flash_stop();
}

[[noreturn]] void system_reset(void) {
  static bool failure_occurred = false;

  bool already_failed = failure_occurred;
  if (!failure_occurred) {
    // Don't overwrite failure_occurred if a failure has already occurred
    failure_occurred = boot_bit_test(BOOT_BIT_SOFTWARE_FAILURE_OCCURRED);
  }

  // Skip safe teardown if doing so the first time already caused a second reset attempt; or
  // if we're in a critical section, interrupt or if the scheduler has been suspended
  if (!already_failed && !mcu_state_is_isr() && !pbl_irq_is_locked() && (pbl_kernel_is_running())) {
    system_reset_prepare();
    reboot_reason_set_restarted_safely();
#ifdef CONFIG_SOC_T5AI
    if (t5ai_hci_ipc_request_system_reboot() == 0) {
      /* CPU0 owns whole-chip reset on T5AI. A local NVIC reset would leave the
       * CP and Bluetooth radio state alive across a user-requested reboot. */
      pbl_thread_sleep(PBL_MSEC(250u));
    }
#endif
  }

  // If a software failure occurred, do a core dump before resetting
  if (failure_occurred) {
    core_dump_reset(false /* don't force overwrite */);
  } else {
    system_hard_reset();
  }
}

void system_reset_callback(void *data) {
  system_reset();
  (void)data;
}

[[noreturn]] void system_hard_reset(void) {
  // Don't do anything fancy here. We may be in a context where nothing works, not even
  // interrupts. Just reset us.

#ifdef CONFIG_SOC_T5AI
  t5ai_force_whole_chip_reset(T5AI_RESET_REASON_PEBBLE_BASE | RebootReasonCode_Unknown);
#elif defined(CONFIG_SOC_SF32LB52)
  HAL_PMU_Reboot();
#elifdef CONFIG_SOC_POSIX
  posix_host_reboot();
#else
  NVIC_SystemReset();
#endif

  unreachable();
}
