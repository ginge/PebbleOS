/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "gap_le_connect_params.h"
#include "gap_le_connection.h"

#include "comm/bluetooth_analytics.h"
#include "comm/bt_conn_mgr.h"
#include "comm/bt_lock.h"
#include "bluetooth/responsiveness.h"
#include "pbl/logging/logging.h"
#include "pbl/services/analytics/analytics.h"
#include "pbl/services/new_timer/new_timer.h"

extern void conn_mgr_handle_desired_state_granted(GAPLEConnection *hdl,
                                                  ResponseTimeState granted_state);

/* Keep the unstable low-power transition disabled, but do request Pebble's
 * active 15 ms interval. Android can establish this board at 30 ms, which made
 * service discovery and initial sync consume most of the phone's timeout. */
void gap_le_connect_params_request(GAPLEConnection *connection,
                                   ResponseTimeState desired_state) {
  new_timer_stop(connection->param_update_info.watchdog_timer);
  connection->param_update_info.attempts = 0;
  connection->param_update_info.is_request_pending = false;

  if (desired_state == ResponseTimeMin &&
      connection->conn_params.conn_interval_1_25ms > 12u) {
    const BleConnectionParamsUpdateReq request = {
        .interval_min_1_25ms = 12u,
        .interval_max_1_25ms = 12u,
        .slave_latency_events = 0u,
        .supervision_timeout_10ms = 2000u,
    };
    const bool accepted =
        bt_driver_le_connection_parameter_update(&connection->device, &request);
    PBL_LOG_INFO("T5AI conn param request: current=%u latency=%u target=12 accepted=%u",
                 connection->conn_params.conn_interval_1_25ms,
                 connection->conn_params.slave_latency_events, accepted);
    if (accepted) {
      connection->param_update_info.is_request_pending = true;
      return;
    }
  }

  conn_mgr_handle_desired_state_granted(connection, desired_state);
}

void gap_le_connect_params_setup_connection(GAPLEConnection *connection, TimerID timer) {
  connection->param_update_info.watchdog_timer = timer;
}

void gap_le_connect_params_cleanup_by_connection(GAPLEConnection *connection) {
  new_timer_delete(connection->param_update_info.watchdog_timer);
}

ResponseTimeState gap_le_connect_params_get_actual_state(GAPLEConnection *connection) {
  return connection->conn_params.slave_latency_events == 0u ? ResponseTimeMin : ResponseTimeMax;
}

void gap_le_connect_params_re_evaluate(GAPLEConnection *connection) {
  conn_mgr_handle_desired_state_granted(
      connection, conn_mgr_get_latency_for_le_connection(connection, NULL));
}

void bt_driver_handle_le_conn_params_update_event(const BleConnectionUpdateCompleteEvent *event) {
  bt_lock();
  GAPLEConnection *connection = gap_le_connection_by_addr(&event->dev_address);
  PBL_LOG_INFO("T5AI conn param result: status=0x%02x interval=%u latency=%u timeout=%u",
               event->status, event->conn_params.conn_interval_1_25ms,
               event->conn_params.slave_latency_events,
               event->conn_params.supervision_timeout_10ms);
  if (connection == NULL) {
    bt_unlock();
    return;
  }

  connection->param_update_info.is_request_pending = false;
  if (event->status == HciStatusCode_Success) {
    connection->conn_params = event->conn_params;
    if (!connection->local_is_master) {
      bluetooth_analytics_handle_connection_params_update(&event->conn_params);
      PBL_ANALYTICS_ADD(ble_conn_param_update_count, 1);
    }
  }
  /* A rejected controller request must not leave the response-time state
   * pending forever. Keep using the established latency-zero link and let the
   * caller proceed; a later request can try the active interval again. */
  conn_mgr_handle_desired_state_granted(
      connection, conn_mgr_get_latency_for_le_connection(connection, NULL));
  bt_unlock();
}
