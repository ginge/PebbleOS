/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/bluetooth/responsiveness.h>
#include <host/ble_gap.h>
#include <host/ble_l2cap.h>
#include <pbl/logging/logging.h>

#include "nimble_type_conversions.h"

PBL_LOG_MODULE_DECLARE(bt, CONFIG_BT_LOG_LEVEL);

// Keep a 20-second supervision window for the BK7258 active link.
#define T5AI_ACTIVE_SUPERVISION_TIMEOUT_10MS 2000u

static void prv_l2cap_update_response(uint16_t conn_handle, int status, void *arg) {
  (void)arg;
  if (status != 0) {
    PBL_LOG_ERR("T5AI L2CAP connection parameter request failed: handle=%u status=0x%04x",
                conn_handle, (uint16_t)status);
  }
}

bool pbl_bt_le_connection_parameter_update(const struct pbl_bt_device_internal *addr,
                                              const struct pbl_bt_conn_params_update_req *req) {
  /* Slave-latency transitions remain disabled until they are validated on
   * BK7258. Interval changes at latency 0 are supported. */
  if (req->slave_latency_events != 0u) {
    PBL_LOG_INFO("T5AI retaining active LE parameters; skipped latency=%u update",
                 req->slave_latency_events);
    return false;
  }

  ble_addr_t nimble_addr;
  struct ble_gap_conn_desc desc;
  struct ble_gap_upd_params params;

  pebble_device_to_nimble_addr(addr, &nimble_addr);

  int rc = ble_gap_conn_find_by_addr(&nimble_addr, &desc);
  if (rc != 0) {
    PBL_LOG_ERR("ble_gap_conn_find_by_addr failed: %d", rc);
    return false;
  }

  struct pbl_bt_conn_params_update_req effective_req = *req;
  if (effective_req.supervision_timeout_10ms < T5AI_ACTIVE_SUPERVISION_TIMEOUT_10MS) {
    effective_req.supervision_timeout_10ms = T5AI_ACTIVE_SUPERVISION_TIMEOUT_10MS;
  }
  pebble_conn_update_to_nimble(&effective_req, &params);

  PBL_LOG_INFO("T5AI requesting active connection parameters: "
               "interval=(%u, %u) ms, latency=%u, spvn timeout=%u ms",
               params.itvl_min * BLE_HCI_CONN_ITVL / 1000,
               params.itvl_max * BLE_HCI_CONN_ITVL / 1000, params.latency,
               params.supervision_timeout * BLE_HCI_CONN_SPVN_TMO_UNITS);

  if (desc.role == BLE_GAP_ROLE_SLAVE) {
    /* Do not ask the BK7258 controller to originate the LL Connection
     * Parameters Request procedure. It accepts the HCI command but can fail
     * to emit LE Connection Update Complete, then times the link out. Send
     * the peripheral L2CAP request instead; the phone (central) performs the
     * controller-level update and both sides receive the normal completion
     * event. */
    struct ble_l2cap_sig_update_params l2cap_params = {
        .itvl_min = params.itvl_min,
        .itvl_max = params.itvl_max,
        .slave_latency = params.latency,
        .timeout_multiplier = params.supervision_timeout,
    };
    rc = ble_l2cap_sig_update(desc.conn_handle, &l2cap_params, prv_l2cap_update_response, NULL);
  } else {
    rc = ble_gap_update_params(desc.conn_handle, &params);
  }
  if (rc != 0) {
    PBL_LOG_ERR("connection parameter request failed: 0x%04x", (uint16_t)rc);
    return false;
  }

  return true;
}
