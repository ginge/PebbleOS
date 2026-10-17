/* SPDX-FileCopyrightText: 2025 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/bluetooth/pairing_confirm.h>
#include <pbl/logging/logging.h>

#include <host/ble_hs.h>
#include <host/ble_sm.h>

#include "pairing_confirm_internal.h"

PBL_LOG_MODULE_DECLARE(bt, CONFIG_BT_LOG_LEVEL);

void pbl_bt_pairing_confirm(const struct pbl_bt_pairing_confirm_ctx *ctx, bool is_confirmed) {
  if (ctx == NULL) {
    return;
  }
  struct ble_sm_io key = {
    .action = BLE_SM_IOACT_NUMCMP,
    .numcmp_accept = is_confirmed,
  };
  int rc = ble_sm_inject_io(ctx->conn_handle, &key);

  PBL_LOG_DBG("ble_sm_inject_io rc=0x%04x", (uint16_t)rc);
}
