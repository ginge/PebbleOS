/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/delay.h>
#include <pbl/kernel/reboot.h>
#include <pbl/soc/t5ai/hci_ipc.h>
#include <pbl/soc/t5ai/reset.h>

#include <system/reboot_reason.h>
[[noreturn]] void pbl_soc_reset(void) {
  t5ai_force_whole_chip_reset(T5AI_RESET_REASON_PEBBLE_BASE | RebootReasonCode_Unknown);
}
static void prv_restart_prepare(enum pbl_sys_off_mode mode, void *data) {
  if (t5ai_hci_ipc_request_system_reboot() == 0)
    pbl_sleep_ms(250);
}
PBL_SYS_OFF_HANDLER_DEFINE(t5ai_restart, PBL_SYS_OFF_RESTART_PREPARE, PBL_SYS_OFF_PRIO_LOW,
                           prv_restart_prepare, nullptr);
