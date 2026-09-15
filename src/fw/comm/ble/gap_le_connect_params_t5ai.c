/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "gap_le_connect_params.h"
#include "gap_le_connection.h"

#include "comm/bluetooth_analytics.h"
#include "comm/bt_conn_mgr.h"
#include "comm/bt_lock.h"
#include "pbl/services/analytics/analytics.h"
#include "pbl/services/new_timer/new_timer.h"

extern void conn_mgr_handle_desired_state_granted(GAPLEConnection *hdl,
                                                  ResponseTimeState granted_state);

/* The BK7258 controller owns the link policy. Android already establishes the
 * link at latency zero, and issuing an LE update while service discovery and
 * flash traffic share the CP transport can stall the host command path. Treat
 * the controller's established parameters as satisfying Pebble's logical
 * responsiveness request until CP link-policy support is available. */
void gap_le_connect_params_request(GAPLEConnection *connection,
                                   ResponseTimeState desired_state) {
  new_timer_stop(connection->param_update_info.watchdog_timer);
  connection->param_update_info.attempts = 0;
  connection->param_update_info.is_request_pending = false;
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
  if (event->status != HciStatusCode_Success) {
    bt_unlock();
    return;
  }

  GAPLEConnection *connection = gap_le_connection_by_addr(&event->dev_address);
  if (connection != NULL) {
    connection->conn_params = event->conn_params;
    connection->param_update_info.is_request_pending = false;
    if (!connection->local_is_master) {
      bluetooth_analytics_handle_connection_params_update(&event->conn_params);
      PBL_ANALYTICS_ADD(ble_conn_param_update_count, 1);
    }
    conn_mgr_handle_desired_state_granted(
        connection, conn_mgr_get_latency_for_le_connection(connection, NULL));
  }
  bt_unlock();
}
