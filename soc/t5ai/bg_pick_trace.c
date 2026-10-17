/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/time.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/rtc.h>
#include <inttypes.h>

PBL_LOG_MODULE_DEFINE(t5ai_bg_pick_trace, LOG_LEVEL_DEBUG);

extern struct pbl_thread *g_task_threads[NumPebbleTask];
extern struct pbl_thread *pbl_cur;
struct pbl_thread *__real_sched_switch_in(void);
void __real_system_task_timer_init(void);

typedef struct {
  uint32_t picks, bg, app_higher, other_higher, equal, lower;
  uint32_t previous, gap_peak, ready_since, ready_peak;
  uint32_t bg_cpu, app_cpu, app_id;
  bool live, seen, ready;
} PickTrace;
static PickTrace s_trace;

static void prv_max(uint32_t *peak, uint32_t value) {
  if (value > *peak)
    *peak = value;
}

// PendSV holds the scheduler interrupt mask across this wrapper.
struct pbl_thread *__wrap_sched_switch_in(void) {
  struct pbl_thread *bg = g_task_threads[PebbleTask_KernelBackground];
  bool runnable = bg && (bg->backend.state == PBL_THREAD_READY ||
                         bg->backend.state == PBL_THREAD_RUNNING);
  struct pbl_thread *next = __real_sched_switch_in();
  if (!s_trace.live)
    return next;
  uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  if (s_trace.seen)
    prv_max(&s_trace.gap_peak, now - s_trace.previous);
  s_trace.seen = true;
  s_trace.previous = now;
  ++s_trace.picks;
  if (s_trace.ready)
    prv_max(&s_trace.ready_peak, now - s_trace.ready_since);
  if (!runnable || next == bg) {
    s_trace.ready = false;
  } else if (!s_trace.ready) {
    s_trace.ready = true;
    s_trace.ready_since = now;
  }
  if (runnable) {
    if (next == bg)
      ++s_trace.bg;
    else if (next->prio > bg->prio) {
      if (next == g_task_threads[PebbleTask_App])
        ++s_trace.app_higher;
      else
        ++s_trace.other_higher;
    } else if (next->prio == bg->prio)
      ++s_trace.equal;
    else
      ++s_trace.lower;
  }
  return next;
}

static uint32_t prv_cpu(struct pbl_thread *thread, uint32_t now) {
  if (!thread)
    return 0;
  return thread->backend.run_time +
         (thread == pbl_cur ? now - thread->backend.switched_in_at : 0);
}

static void prv_report(void *data) {
  (void)data;
  pbl_irq_lock();
  struct pbl_thread *bg = g_task_threads[PebbleTask_KernelBackground];
  struct pbl_thread *app = g_task_threads[PebbleTask_App];
  uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  if (s_trace.ready)
    prv_max(&s_trace.ready_peak, now - s_trace.ready_since);
  uint32_t ticks = pbl_uptime_ticks();
  uint32_t bg_cpu = prv_cpu(bg, ticks), app_cpu = prv_cpu(app, ticks);
  uint32_t bg_delta = bg_cpu - s_trace.bg_cpu;
  uint32_t app_delta = app && app->id == s_trace.app_id ? app_cpu - s_trace.app_cpu : 0;
  s_trace.bg_cpu = bg_cpu;
  s_trace.app_cpu = app_cpu;
  s_trace.app_id = app ? app->id : 0;
  uint32_t state = bg ? bg->backend.state : PBL_THREAD_DEAD;
  uint32_t bg_prio = bg ? bg->prio : 0, app_prio = app ? app->prio : 0;
  uint32_t callback = (uintptr_t)system_task_get_current_callback();
  uint32_t age = s_trace.ready ? now - s_trace.ready_since : 0;
  PickTrace snapshot = s_trace;
  char name[PBL_THREAD_NAME_LEN];
  for (unsigned i = 0; i < sizeof(name); ++i)
    name[i] = app ? app->name[i] : 0;
  name[sizeof(name) - 1] = 0;
  pbl_irq_unlock();
#ifdef CONFIG_T5AI_WDT_CAUSE_TRACE
  extern void t5ai_wdt_cause_trace_dump(void);
  t5ai_wdt_cause_trace_dump();
#endif
  // Durations are raw 32 kHz RTC ticks; counts and peaks are cumulative.
  PBL_LOG_DBG("T5PICK rtc=%" PRIu32 " picks=%" PRIu32 " bg=%" PRIu32
              " app_hi=%" PRIu32 " other_hi=%" PRIu32 " equal=%" PRIu32 " lower=%" PRIu32,
              now, snapshot.picks, snapshot.bg, snapshot.app_higher,
              snapshot.other_higher, snapshot.equal, snapshot.lower);
  PBL_LOG_DBG("T5PICK ready_age=%" PRIu32 " ready_peak=%" PRIu32 " gap_peak=%" PRIu32
              " bg_state=%" PRIu32 " bg_prio=%" PRIu32 " cb=%08" PRIx32 " bg_ticks=%" PRIu32,
              age, snapshot.ready_peak, snapshot.gap_peak, state, bg_prio, callback, bg_delta);
  PBL_LOG_DBG("T5PICK app=%" PRIu32 " name=%s app_prio=%" PRIu32 " app_ticks=%" PRIu32,
              snapshot.app_id, name, app_prio, app_delta);
}

void __wrap_system_task_timer_init(void) {
  __real_system_task_timer_init();
  s_trace.live = true;
  new_timer_start(new_timer_create(), 5000, prv_report, NULL, TIMER_START_FLAG_REPEATING);
}
