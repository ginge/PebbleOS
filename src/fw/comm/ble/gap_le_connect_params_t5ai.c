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

#define T5AI_PARAM_UPDATE_DELAY_MS 1000u
#define T5AI_PARAM_UPDATE_WATCHDOG_MS 5000u
#define T5AI_SUPERVISION_TIMEOUT_10MS 2000u

extern void conn_mgr_handle_desired_state_granted(GAPLEConnection *hdl,
                                                  ResponseTimeState granted_state);

static void prv_timer_callback(void *context);

static void prv_grant_current_state(GAPLEConnection *connection) {
  conn_mgr_handle_desired_state_granted(connection,
                                        conn_mgr_get_latency_for_le_connection(connection, NULL));
}

static void prv_timer_system_task_callback(void *context) {
  bt_lock();
  GAPLEConnection *connection = context;
  if (!gap_le_connection_is_valid(connection)) {
    bt_unlock();
    return;
  }

  if (connection->param_update_info.is_request_pending) {
    PBL_LOG_WRN("T5AI supervision-timeout update timed out; retaining central parameters");
    connection->param_update_info.is_request_pending = false;
    prv_grant_current_state(connection);
    bt_unlock();
    return;
  }

  const BleConnectionParamsUpdateReq request = {
      .interval_min_1_25ms = connection->conn_params.conn_interval_1_25ms,
      .interval_max_1_25ms = connection->conn_params.conn_interval_1_25ms,
      .slave_latency_events = connection->conn_params.slave_latency_events,
      .supervision_timeout_10ms = T5AI_SUPERVISION_TIMEOUT_10MS,
  };
  const bool accepted = bt_driver_le_connection_parameter_update(&connection->device, &request);
  PBL_LOG_INFO(
      "T5AI supervision-timeout update: interval=%u latency=%u "
      "timeout=%u accepted=%u",
      request.interval_min_1_25ms, request.slave_latency_events, request.supervision_timeout_10ms,
      accepted);
  if (accepted) {
    connection->param_update_info.is_request_pending = true;
    new_timer_start(connection->param_update_info.watchdog_timer, T5AI_PARAM_UPDATE_WATCHDOG_MS,
                    prv_timer_callback, connection, 0);
  } else {
    prv_grant_current_state(connection);
  }
  bt_unlock();
}

static void prv_timer_callback(void *context) {
  system_task_add_callback(prv_timer_system_task_callback, context);
}

/* Keep the unstable interval/latency transitions disabled. Preserve the
 * central-established parameters, but raise a short supervision timeout so a
 * transient controller stall during initial synchronization does not tear the
 * link down. */
void gap_le_connect_params_request(GAPLEConnection *connection, ResponseTimeState desired_state) {
  if (connection->param_update_info.is_request_pending) {
    return;
  }
  new_timer_stop(connection->param_update_info.watchdog_timer);
  connection->param_update_info.attempts = 0;

  if (connection->conn_params.supervision_timeout_10ms >= T5AI_SUPERVISION_TIMEOUT_10MS) {
    PBL_LOG_INFO(
        "T5AI retaining central connection parameters: interval=%u latency=%u "
        "timeout=%u desired=%u",
        connection->conn_params.conn_interval_1_25ms, connection->conn_params.slave_latency_events,
        connection->conn_params.supervision_timeout_10ms, (unsigned)desired_state);
    conn_mgr_handle_desired_state_granted(connection, desired_state);
    return;
  }

  new_timer_start(connection->param_update_info.watchdog_timer, T5AI_PARAM_UPDATE_DELAY_MS,
                  prv_timer_callback, connection, 0);
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
  /* A rejected controller request must not leave the response-time state
   * pending forever. Keep using the established latency-zero link and let the
   * caller proceed; a later connection can retry the timeout extension. */
  prv_grant_current_state(connection);
  bt_unlock();
}
