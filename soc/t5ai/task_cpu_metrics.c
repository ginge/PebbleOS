/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/time.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/kernel/assert.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(t5ai_task_cpu, LOG_LEVEL_DEBUG);

#define TASK_LIMIT 32u

typedef struct {
  uint32_t id, cpu, delta;
  char name[PBL_THREAD_NAME_LEN];
} TaskCpu;
static TaskCpu s_previous[TASK_LIMIT], s_current[TASK_LIMIT];
static uint32_t s_previous_count, s_previous_at, s_sequence;
static struct pbl_thread s_worker;
PBL_THREAD_STACK_DEFINE(s_stack, 4096);
extern struct pbl_thread *pbl_all_threads;
extern struct pbl_thread *pbl_cur;

static void prv_snapshot(bool report) {
  uint32_t count = 0, omitted = 0;
  pbl_irq_lock();
  uint32_t at = T5AI_AON_RTC_COUNT_LOW;
  uint32_t ticks = pbl_uptime_ticks();
  for (struct pbl_thread *t = pbl_all_threads; t; t = t->backend.all_next) {
    if (count == TASK_LIMIT) {
      ++omitted;
      continue;
    }
    TaskCpu *out = &s_current[count++];
    *out = (TaskCpu){.id = t->id, .cpu = t->backend.run_time};
    if (t == pbl_cur)
      out->cpu += ticks - t->backend.switched_in_at;
    memcpy(out->name, t->name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = 0;
  }
  pbl_irq_unlock();
  for (uint32_t i = 0; i < count; ++i) {
    for (uint32_t j = 0; j < s_previous_count; ++j) {
      if (s_current[i].id == s_previous[j].id) {
        s_current[i].delta = s_current[i].cpu - s_previous[j].cpu;
        break;
      }
    }
  }
  if (report) {
    uint32_t seq = ++s_sequence;
    PBL_LOG_DBG("TASKCPU seq=%lu dt_ticks=%lu hz=%lu count=%lu omitted=%lu", (unsigned long)seq,
                (unsigned long)(at - s_previous_at), (unsigned long)T5AI_AON_RTC_HZ,
                (unsigned long)count, (unsigned long)omitted);
    for (uint32_t i = 0; i < count; ++i) {
      TaskCpu *t = &s_current[i];
      PBL_LOG_DBG("TASKCPU seq=%lu id=%lu name=%s cpu_ms=%lu", (unsigned long)seq,
                  (unsigned long)t->id, t->name, (unsigned long)t->delta);
    }
    PBL_LOG_DBG("TASKCPU end=%lu", (unsigned long)seq);
  }
  memcpy(s_previous, s_current, count * sizeof(TaskCpu));
  s_previous_count = count;
  s_previous_at = at;
}

static void prv_entry(void *arg) {
  (void)arg;
  prv_snapshot(false);
  for (;;) {
    pbl_thread_sleep(PBL_MSEC(5000));
    prv_snapshot(true);
  }
}

void __real_system_task_timer_init(void);
void __wrap_system_task_timer_init(void) {
  __real_system_task_timer_init();
  const struct pbl_thread_attr attr = {
    .name = "TaskCPU",
    .entry = prv_entry,
    .prio = PBL_PRIO_MAX - 1,
    .privileged = true,
    .stack = s_stack,
    .stack_size = sizeof(s_stack)
  };
  PBL_ASSERTN(pbl_thread_create(&s_worker, &attr) == 0);
}
