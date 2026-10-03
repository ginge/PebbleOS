/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/rtc.h>
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/system_task.h>

#include "process_management/app_manager.h"
#include "process_management/pebble_process_md.h"
#include "process_management/process_loader.h"
#include "process_state/app_state/app_state.h"
#include "system/passert.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

PBL_LOG_MODULE_DEFINE(t5ai_app_load_trace, LOG_LEVEL_DEBUG);

#define TRACE_CAPACITY 8u

typedef struct {
  uint32_t sequence;
  uint32_t request, begin, loader_begin, loader_end;
  uint32_t task_begin, task_ready, frame_ready, display_done, focused;
  char name[32];
  bool failed, reported;
} AppLoadTrace;

static AppLoadTrace s_trace[TRACE_CAPACITY];
static unsigned s_sequence;
static uint32_t s_request;
static bool s_request_pending;
static TimerID s_dump_timer = TIMER_INVALID_ID;
static void (*s_display_callback)(void);
static unsigned s_display_sequence;

static uint32_t prv_now(void) {
  return (uint32_t)rtc_get_ticks();
}

static AppLoadTrace *prv_current(void) {
  return s_sequence ? &s_trace[(s_sequence - 1u) % TRACE_CAPACITY] : NULL;
}

static void prv_request(void) {
  if (!s_request_pending) {
    s_request = prv_now();
    s_request_pending = true;
  }
}

static void prv_dump(void *context) {
  (void)context;
  for (unsigned i = 0; i < TRACE_CAPACITY; ++i) {
    taskENTER_CRITICAL();
    AppLoadTrace entry = s_trace[i];
    const uint32_t end = entry.focused ? entry.focused : entry.display_done;
    const bool ready =
        entry.sequence && !entry.reported && (entry.failed || (end && prv_now() - end >= 2000u));
    if (ready) {
      s_trace[i].reported = true;
    }
    taskEXIT_CRITICAL();
    if (!ready) {
      continue;
    }
    PBL_LOG_DBG("APPLOAD seq=%lu app=%s request=%lu begin=%lu failed=%u",
                (unsigned long)entry.sequence, entry.name, (unsigned long)entry.request,
                (unsigned long)entry.begin, entry.failed);
    PBL_LOG_DBG("APPLOAD seq=%lu pre_load=%lu loader_start=%lu loader_ms=%lu",
                (unsigned long)entry.sequence, (unsigned long)(entry.begin - entry.request),
                (unsigned long)(entry.loader_begin - entry.begin),
                (unsigned long)(entry.loader_end - entry.loader_begin));
    PBL_LOG_DBG("APPLOAD seq=%lu task_start=%lu state_init=%lu frame_ready=%lu",
                (unsigned long)entry.sequence,
                (unsigned long)(entry.task_begin ? entry.task_begin - entry.begin : 0),
                (unsigned long)(entry.task_ready ? entry.task_ready - entry.task_begin : 0),
                (unsigned long)(entry.frame_ready ? entry.frame_ready - entry.begin : 0));
    PBL_LOG_DBG(
        "APPLOAD seq=%lu init_draw=%lu display=%lu focused=%lu", (unsigned long)entry.sequence,
        (unsigned long)(entry.frame_ready && entry.task_ready ? entry.frame_ready - entry.task_ready
                                                              : 0),
        (unsigned long)(entry.display_done ? entry.display_done - entry.begin : 0),
        (unsigned long)(entry.focused ? entry.focused - entry.begin : 0));
  }
}

static void prv_queue_dump(void *context) {
  (void)context;
  (void)system_task_add_callback(prv_dump, NULL);
}

void __real_app_manager_close_current_app(bool gracefully);
void __wrap_app_manager_close_current_app(bool gracefully) {
  prv_request();
  __real_app_manager_close_current_app(gracefully);
}

bool __real_app_manager_launch_new_app(const AppLaunchConfig *config);
bool __wrap_app_manager_launch_new_app(const AppLaunchConfig *config) {
  prv_request();
  const bool accepted = __real_app_manager_launch_new_app(config);
  if (!accepted) {
    s_request_pending = false;
  }
  return accepted;
}

void __real_process_manager_init_context(ProcessContext *context, const PebbleProcessMd *md,
                                         const void *args);
void __wrap_process_manager_init_context(ProcessContext *context, const PebbleProcessMd *md,
                                         const void *args) {
  if (context == app_manager_get_task_context()) {
    const uint32_t now = prv_now();
    taskENTER_CRITICAL();
    AppLoadTrace *entry = &s_trace[s_sequence++ % TRACE_CAPACITY];
    *entry = (AppLoadTrace){
        .sequence = s_sequence, .begin = now, .request = s_request_pending ? s_request : now};
    strncpy(entry->name, process_metadata_get_name(md), sizeof(entry->name) - 1u);
    s_request_pending = false;
    taskEXIT_CRITICAL();
    if (s_dump_timer == TIMER_INVALID_ID) {
      s_dump_timer = new_timer_create();
      PBL_ASSERTN(
          new_timer_start(s_dump_timer, 5000u, prv_queue_dump, NULL, TIMER_START_FLAG_REPEATING));
    }
  }
  __real_process_manager_init_context(context, md, args);
}

void *__real_process_loader_load(const PebbleProcessMd *md, PebbleTask task,
                                 MemorySegment *destination);
void *__wrap_process_loader_load(const PebbleProcessMd *md, PebbleTask task,
                                 MemorySegment *destination) {
  AppLoadTrace *entry = task == PebbleTask_App ? prv_current() : NULL;
  if (entry) {
    entry->loader_begin = prv_now();
  }
  void *result = __real_process_loader_load(md, task, destination);
  if (entry) {
    entry->loader_end = prv_now();
    entry->failed = result == NULL;
  }
  return result;
}

void __real_app_state_init(void);
void __wrap_app_state_init(void) {
  AppLoadTrace *entry = prv_current();
  if (entry) {
    entry->task_begin = prv_now();
  }
  __real_app_state_init();
  if (entry) {
    entry->task_ready = prv_now();
  }
}

void __real_compositor_app_render_ready(void);
void __wrap_compositor_app_render_ready(void) {
  AppLoadTrace *entry = prv_current();
  if (entry && entry->task_ready && !entry->frame_ready) {
    entry->frame_ready = prv_now();
  }
  __real_compositor_app_render_ready();
}

static void prv_display_done(void) {
  AppLoadTrace *entry = &s_trace[(s_display_sequence - 1u) % TRACE_CAPACITY];
  if (entry->sequence == s_display_sequence && !entry->display_done) {
    entry->display_done = prv_now();
  }
  void (*callback)(void) = s_display_callback;
  s_display_callback = NULL;
  if (callback) {
    callback();
  }
}

void __real_compositor_display_update(void (*callback)(void));
void __wrap_compositor_display_update(void (*callback)(void)) {
  AppLoadTrace *entry = prv_current();
  if (entry && entry->frame_ready && !entry->display_done) {
    s_display_sequence = entry->sequence;
    s_display_callback = callback;
    __real_compositor_display_update(prv_display_done);
  } else {
    __real_compositor_display_update(callback);
  }
}

bool __real_process_manager_send_event_to_process(PebbleTask task, PebbleEvent *event);
bool __wrap_process_manager_send_event_to_process(PebbleTask task, PebbleEvent *event) {
  AppLoadTrace *entry = prv_current();
  if (task == PebbleTask_App && entry && entry->frame_ready && !entry->focused &&
      event->type == PEBBLE_APP_DID_CHANGE_FOCUS_EVENT && event->app_focus.in_focus) {
    entry->focused = prv_now();
  }
  return __real_process_manager_send_event_to_process(task, event);
}
