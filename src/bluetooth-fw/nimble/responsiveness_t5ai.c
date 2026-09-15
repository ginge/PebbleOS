/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <bluetooth/responsiveness.h>
#include <host/ble_gap.h>
#include <pbl/logging/logging.h>

#include "nimble_type_conversions.h"

PBL_LOG_MODULE_DECLARE(bt, CONFIG_BT_LOG_LEVEL);

#define T5AI_ACTIVE_SUPERVISION_TIMEOUT_10MS 2000u

bool bt_driver_le_connection_parameter_update(const BTDeviceInternal *addr,
                                              const BleConnectionParamsUpdateReq *req) {
  /* The BK7258 controller reliably establishes and maintains the active
   * latency-0 parameters, but changing an established Android link to the
   * stock low-power state makes later transitions and sustained PPoG traffic
   * end in HCI Connection Timeout (0x08). Keep the active parameters until
   * the controller-side transition is made reliable. This changes power use,
   * not Pebble Protocol behavior or payload selection. */
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

  BleConnectionParamsUpdateReq effective_req = *req;
  if (effective_req.supervision_timeout_10ms < T5AI_ACTIVE_SUPERVISION_TIMEOUT_10MS) {
    effective_req.supervision_timeout_10ms = T5AI_ACTIVE_SUPERVISION_TIMEOUT_10MS;
  }
  pebble_conn_update_to_nimble(&effective_req, &params);

  PBL_LOG_INFO("T5AI requesting active connection parameters: "
               "interval=(%u, %u) ms, latency=%u, spvn timeout=%u ms",
               params.itvl_min * BLE_HCI_CONN_ITVL / 1000,
               params.itvl_max * BLE_HCI_CONN_ITVL / 1000, params.latency,
               params.supervision_timeout * BLE_HCI_CONN_SPVN_TMO_UNITS);

  rc = ble_gap_update_params(desc.conn_handle, &params);
  if (rc != 0) {
    PBL_LOG_ERR("ble_gap_update_params failed: 0x%04x", (uint16_t)rc);
    return false;
  }

  return true;
}
