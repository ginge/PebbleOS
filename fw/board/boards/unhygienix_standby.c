/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>

#include <pbl/logging/logging.h>

#include <kernel/util/standby.h>

PBL_LOG_MODULE_DEFINE(unhygienix_standby, LOG_LEVEL_DEBUG);

int enter_standby(RebootReasonCode reason) {
  (void)reason;
  // VBUS bypasses the battery latch; BAT_CHG is not a USB-presence signal.
  // No qualified storage-quiescence / battery-only power-off path exists yet.
  PBL_LOG_WRN("Board power-off unavailable; shutdown request rejected");
  return -ENOTSUP;
}
