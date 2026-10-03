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
#include "pbl/services/system_task.h"

#define T5AI_FAST_INTERVAL_1_25MS 12u
#define T5AI_IDLE_INTERVAL_1_25MS 24u

extern void conn_mgr_handle_desired_state_granted(GAPLEConnection *hdl,
                                                  ResponseTimeState granted_state);

static void prv_grant_current_state(GAPLEConnection *connection) {
  conn_mgr_handle_desired_state_granted(connection,
                                        conn_mgr_get_latency_for_le_connection(connection, NULL));
}

/* CPU0's peripheral-side L2CAP parameter-update path can stop servicing the
 * live link when the central does not answer the request, ending in HCI
 * supervision timeout 0x08. Keep the controller-negotiated parameters and
 * grant the policy request locally until CPU0 exposes a reliable update ABI.
 * This deliberately favours connection stability over idle power. */
void gap_le_connect_params_request(GAPLEConnection *connection, ResponseTimeState desired_state) {
  new_timer_stop(connection->param_update_info.watchdog_timer);
  connection->param_update_info.is_request_pending = false;
  connection->param_update_info.attempts = 0;
  PBL_LOG_INFO("T5AI retaining controller connection parameters: interval=%u latency=%u "
               "timeout=%u desired=%u",
               connection->conn_params.conn_interval_1_25ms,
               connection->conn_params.slave_latency_events,
               connection->conn_params.supervision_timeout_10ms, (unsigned)desired_state);
  conn_mgr_handle_desired_state_granted(connection, desired_state);
}

void gap_le_connect_params_setup_connection(GAPLEConnection *connection, TimerID timer) {
  connection->param_update_info.watchdog_timer = timer;
}

void gap_le_connect_params_cleanup_by_connection(GAPLEConnection *connection) {
  new_timer_delete(connection->param_update_info.watchdog_timer);
}

ResponseTimeState gap_le_connect_params_get_actual_state(GAPLEConnection *connection) {
  return connection->conn_params.slave_latency_events == 0u &&
                 connection->conn_params.conn_interval_1_25ms <= T5AI_FAST_INTERVAL_1_25MS
             ? ResponseTimeMin
             : ResponseTimeMax;
}

void gap_le_connect_params_re_evaluate(GAPLEConnection *connection) {
  conn_mgr_handle_desired_state_granted(connection,
                                        conn_mgr_get_latency_for_le_connection(connection, NULL));
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
  new_timer_stop(connection->param_update_info.watchdog_timer);
  if (event->status == HciStatusCode_Success) {
    connection->conn_params = event->conn_params;
    if (!connection->local_is_master) {
      bluetooth_analytics_handle_connection_params_update(&event->conn_params);
      PBL_ANALYTICS_ADD(ble_conn_param_update_count, 1);
    }
  }
  prv_grant_current_state(connection);
  bt_unlock();
}
