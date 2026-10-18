/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/services/activity/activity.h>
#include <pbl/services/app_inbox_service.h>
#include <pbl/services/app_message/app_message_sender.h>
#include <pbl/services/app_outbox_service.h>
#include <pbl/services/blob_db/api.h>
#include <pbl/services/bluetooth/bluetooth_persistent_storage.h>
#include <pbl/services/shared_prf_storage/shared_prf_storage.h>
#include <pbl/services/system_task.h>

#include <clar.h>
#include <popups/notifications/notification_window.h>
#include <popups/timeline/peek.h>
#include <shell/normal/watchface.h>
#include <shell/prefs.h>
#include <shell/shell_event_loop.h>

static bool s_prefs_initialized;
static bool s_migrated;
static bool s_setup_complete;
static unsigned s_enqueues;
static unsigned s_compactions;
static unsigned s_unfaithful;
static SystemTaskEventCallback s_callback;
static void *s_context;

void shell_prefs_init(void) {
  s_prefs_initialized = true;
}

bool shell_prefs_get_settings_dbs_compacted_v1(void) {
  cl_assert(s_prefs_initialized);
  return s_migrated;
}

void shell_prefs_set_settings_dbs_compacted_v1(bool done) {
  cl_assert(done);
  cl_assert_equal_i(s_compactions, 1);
  s_migrated = done;
}

bool system_task_add_callback(SystemTaskEventCallback callback, void *context) {
  cl_assert(s_prefs_initialized);
  cl_assert(!s_callback);
  s_callback = callback;
  s_context = context;
  ++s_enqueues;
  return true;
}

void blob_db_compact_growable_dbs(void) {
  cl_assert(!s_migrated);
  ++s_compactions;
}

bool shared_prf_storage_get_getting_started_complete(void) {
  return s_setup_complete;
}

void shared_prf_storage_set_getting_started_complete(bool complete) {
  cl_assert(complete);
  if (!s_setup_complete) {
    cl_assert_equal_i(s_unfaithful, 1);
  }
  s_setup_complete = complete;
}

void bt_persistent_storage_set_unfaithful(bool unfaithful) {
  cl_assert(unfaithful);
  cl_assert(!s_setup_complete);
  ++s_unfaithful;
}

void notification_window_service_init(void) {
}

void app_inbox_service_init(void) {
}

void app_outbox_service_init(void) {
}

void app_message_sender_init(void) {
}

void watchface_init(void) {
}

void timeline_peek_init(void) {
}

void workout_service_init(void) {
}

bool activity_prefs_tracking_is_enabled(void) {
  return false;
}

bool activity_start_tracking(bool test_mode) {
  cl_assert(false);
  return false;
}

void test_shell_event_loop_init__initialize(void) {
  s_prefs_initialized = false;
  s_migrated = false;
  s_setup_complete = false;
  s_enqueues = 0;
  s_compactions = 0;
  s_unfaithful = 0;
  s_callback = nullptr;
  s_context = nullptr;
}

static void prv_run_migration(void) {
  cl_assert_equal_i(s_enqueues, 1);
  cl_assert_equal_i(s_compactions, 0);
  cl_assert(!s_migrated);
  cl_assert(s_callback);
  s_callback(s_context);
  s_callback = nullptr;
  cl_assert(s_migrated);
  cl_assert_equal_i(s_compactions, 1);
}

void test_shell_event_loop_init__first_boot_and_repeat(void) {
  shell_event_loop_init();
  cl_assert(s_setup_complete);
  cl_assert_equal_i(s_unfaithful, 1);
  prv_run_migration();
  shell_event_loop_init();
  cl_assert_equal_i(s_enqueues, 1);
  cl_assert_equal_i(s_compactions, 1);
  cl_assert_equal_i(s_unfaithful, 1);
}

void test_shell_event_loop_init__existing_setup_still_migrates(void) {
  s_setup_complete = true;
  shell_event_loop_init();
  cl_assert_equal_i(s_unfaithful, 0);
  prv_run_migration();
}

void test_shell_event_loop_init__setup_runs_with_migration_already_done(void) {
  s_migrated = true;
  shell_event_loop_init();
  cl_assert(s_setup_complete);
  cl_assert_equal_i(s_unfaithful, 1);
  cl_assert_equal_i(s_enqueues, 0);
  cl_assert_equal_i(s_compactions, 0);
}

void test_shell_event_loop_init__completed_boot_does_not_repeat_work(void) {
  s_migrated = true;
  s_setup_complete = true;
  shell_event_loop_init();
  cl_assert(s_setup_complete);
  cl_assert_equal_i(s_unfaithful, 0);
  cl_assert_equal_i(s_enqueues, 0);
  cl_assert_equal_i(s_compactions, 0);
}
