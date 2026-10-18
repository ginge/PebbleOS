/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <clar.h>
#include <comm/ble/gap_le_connect_params.h>
#include <comm/ble/gap_le_connection.h>
#include <comm/bt_conn_mgr.h>
#include <fake_new_timer.h>
#include <fake_rtc.h>
#include <fake_system_task.h>

extern void gap_le_connect_params_setup_connection(GAPLEConnection *connection, TimerID timer);
extern void gap_le_connect_params_cleanup_by_connection(GAPLEConnection *connection);
extern enum pbl_bt_response_time_state gap_le_connect_params_get_actual_state(
    GAPLEConnection *connection);

static GAPLEConnection s_connection;
static enum pbl_bt_response_time_state s_desired;
static struct pbl_bt_conn_params_update_req s_request;
static unsigned s_requests, s_grants, s_failures;
static bool s_accept, s_valid;

bool gap_le_connection_is_valid(const GAPLEConnection *connection) {
  return s_valid && connection == &s_connection;
}

GAPLEConnection *gap_le_connection_by_addr(const struct pbl_bt_addr *addr) {
  return s_valid ? &s_connection : nullptr;
}

enum pbl_bt_response_time_state conn_mgr_get_latency_for_le_connection(GAPLEConnection *connection,
                                                                       uint16_t *duration) {
  return s_desired;
}

void conn_mgr_handle_desired_state_granted(GAPLEConnection *connection,
                                           enum pbl_bt_response_time_state state) {
  cl_assert_equal_i(state, s_desired);
  ++s_grants;
}

bool pbl_bt_le_connection_parameter_update(const struct pbl_bt_device_internal *device,
                                           const struct pbl_bt_conn_params_update_req *request) {
  s_request = *request;
  ++s_requests;
  return s_accept;
}

void bluetooth_analytics_handle_param_update_failed(void) {
  ++s_failures;
}

static void prv_expire(void) {
  cl_assert(stub_new_timer_fire(s_connection.param_update_info.watchdog_timer));
  fake_system_task_callbacks_invoke_pending();
}

void test_gap_le_connect_params__initialize(void) {
  s_connection = (GAPLEConnection){
    .conn_params =
        {.conn_interval_1_25ms = 24, .slave_latency_events = 0, .supervision_timeout_10ms = 500},
  };
  s_desired = PBL_BT_RESPONSE_TIME_MIN;
  s_requests = s_grants = s_failures = 0;
  s_accept = s_valid = true;
  fake_rtc_init(2 * RTC_TICKS_HZ, 0);
  gap_le_connect_params_setup_connection(&s_connection, new_timer_create());
}

void test_gap_le_connect_params__cleanup(void) {
  fake_system_task_callbacks_invoke_pending();
  gap_le_connect_params_cleanup_by_connection(&s_connection);
  stub_new_timer_cleanup();
}

void test_gap_le_connect_params__request_waits_for_actual_completion(void) {
  gap_le_connect_params_request(&s_connection, s_desired);
  cl_assert_equal_i(s_requests, 1);
  cl_assert_equal_i(s_grants, 0);
  cl_assert(s_connection.param_update_info.is_request_pending);
  cl_assert_equal_i(s_request.interval_min_1_25ms, 12);
  cl_assert_equal_i(s_request.slave_latency_events, 0);
  cl_assert_equal_i(s_request.supervision_timeout_10ms, 600);
  struct pbl_bt_conn_update_complete_event event = {
    .status = PBL_BT_HCI_STATUS_SUCCESS,
    .conn_params =
        {.conn_interval_1_25ms = 12, .slave_latency_events = 0, .supervision_timeout_10ms = 600},
  };
  pbl_bt_handle_le_conn_params_update_event(&event);
  cl_assert_equal_i(s_grants, 1);
  cl_assert_equal_i(gap_le_connect_params_get_actual_state(&s_connection), s_desired);
  cl_assert(!new_timer_scheduled(s_connection.param_update_info.watchdog_timer, nullptr));
}

void test_gap_le_connect_params__rejected_requests_are_bounded_without_false_grant(void) {
  s_accept = false;
  gap_le_connect_params_request(&s_connection, s_desired);
  for (unsigned i = 0; i < 3; ++i) {
    prv_expire();
  }
  cl_assert_equal_i(s_requests, 3);
  cl_assert_equal_i(s_grants, 0);
  cl_assert_equal_i(s_failures, 1);
  cl_assert(!new_timer_scheduled(s_connection.param_update_info.watchdog_timer, nullptr));
}

void test_gap_le_connect_params__initial_pause_defers_request(void) {
  fake_rtc_set_ticks(0);
  gap_le_connect_params_request(&s_connection, s_desired);
  cl_assert_equal_i(s_requests, 0);
  cl_assert_equal_i(stub_new_timer_timeout(s_connection.param_update_info.watchdog_timer), 1000);
  fake_rtc_increment_ticks(RTC_TICKS_HZ);
  prv_expire();
  cl_assert_equal_i(s_requests, 1);
  cl_assert_equal_i(s_grants, 0);
}

void test_gap_le_connect_params__idle_request_preserves_latency_and_timeout(void) {
  s_desired = PBL_BT_RESPONSE_TIME_MAX;
  gap_le_connect_params_request(&s_connection, s_desired);
  cl_assert_equal_i(s_request.interval_min_1_25ms, 24);
  cl_assert_equal_i(s_request.interval_max_1_25ms, 36);
  cl_assert_equal_i(s_request.slave_latency_events, 30);
  cl_assert_equal_i(s_request.supervision_timeout_10ms, 600);
  cl_assert_equal_i(s_grants, 0);
}

void test_gap_le_connect_params__failed_completion_does_not_change_actual_state(void) {
  gap_le_connect_params_request(&s_connection, s_desired);
  struct pbl_bt_conn_update_complete_event event = {
    .status = 1,
    .conn_params = {.conn_interval_1_25ms = 12},
  };
  pbl_bt_handle_le_conn_params_update_event(&event);
  cl_assert_equal_i(s_grants, 0);
  cl_assert_equal_i(s_connection.conn_params.conn_interval_1_25ms, 24);
  cl_assert_equal_i(gap_le_connect_params_get_actual_state(&s_connection),
                    PBL_BT_RESPONSE_TIME_INVALID);
}

void test_gap_le_connect_params__queued_retry_ignores_closed_connection(void) {
  gap_le_connect_params_request(&s_connection, s_desired);
  cl_assert(stub_new_timer_fire(s_connection.param_update_info.watchdog_timer));
  s_valid = false;
  fake_system_task_callbacks_invoke_pending();
  cl_assert_equal_i(s_requests, 1);
  cl_assert_equal_i(s_grants, 0);
}

void test_gap_le_connect_params__idle_completion_grants_only_matching_latency(void) {
  s_desired = PBL_BT_RESPONSE_TIME_MAX;
  gap_le_connect_params_request(&s_connection, s_desired);
  struct pbl_bt_conn_update_complete_event event = {
    .status = PBL_BT_HCI_STATUS_SUCCESS,
    .conn_params =
        {.conn_interval_1_25ms = 36, .slave_latency_events = 0, .supervision_timeout_10ms = 600},
  };
  pbl_bt_handle_le_conn_params_update_event(&event);
  cl_assert_equal_i(s_grants, 0);
  event.conn_params.slave_latency_events = 30;
  pbl_bt_handle_le_conn_params_update_event(&event);
  cl_assert_equal_i(s_grants, 1);
  cl_assert_equal_i(gap_le_connect_params_get_actual_state(&s_connection), s_desired);
}

void test_gap_le_connect_params__unanswered_requests_are_bounded_without_false_grant(void) {
  gap_le_connect_params_request(&s_connection, s_desired);
  for (unsigned i = 0; i < 3; ++i) {
    prv_expire();
  }
  cl_assert_equal_i(s_requests, 3);
  cl_assert_equal_i(s_grants, 0);
  cl_assert_equal_i(s_failures, 1);
  cl_assert(!new_timer_scheduled(s_connection.param_update_info.watchdog_timer, nullptr));
}
