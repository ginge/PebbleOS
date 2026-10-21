/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/irq.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/io_profile.h>
PBL_LOG_MODULE_DEFINE(t5ai_io_profile, LOG_LEVEL_DEBUG);
typedef struct {
  uint32_t calls, ticks, maximum, amount;
} Sample;
static Sample s_samples[T5IO_COUNT];
void t5ai_io_profile_add(unsigned kind, uint32_t elapsed, uint32_t amount) {
  if (kind >= T5IO_COUNT)
    return;
  pbl_irq_lock();
  Sample *sample = &s_samples[kind];
  ++sample->calls;
  sample->ticks += elapsed;
  sample->amount += amount;
  if (elapsed > sample->maximum)
    sample->maximum = elapsed;
  pbl_irq_unlock();
}
#ifdef CONFIG_T5AI_IO_CPU_PROFILE
static void prv_cpu_dump(void);
#endif
void t5ai_io_profile_dump(void) {
#ifdef CONFIG_T5AI_IO_CPU_PROFILE
  prv_cpu_dump();
#endif
  Sample copy[T5IO_COUNT];
  pbl_irq_lock();
  for (unsigned i = 0; i < T5IO_COUNT; ++i) {
    copy[i] = s_samples[i];
    s_samples[i] = (Sample){0};
  }
  pbl_irq_unlock();
  for (unsigned i = 0; i < T5IO_COUNT; ++i) {
    const Sample *s = &copy[i];
    PBL_LOG_DBG("T5IO kind=%u n=%lu ticks=%lu max=%lu amount=%lu", i, (unsigned long)s->calls,
                (unsigned long)s->ticks, (unsigned long)s->maximum, (unsigned long)s->amount);
  }
}

#ifdef CONFIG_T5AI_IO_CPU_PROFILE
/* This code is appended inside default-off IO profiling. */
#include "kernel/pebble_tasks.h"

#include <pbl/kernel/thread.h>
#include <pbl/kernel/time.h>
extern struct pbl_thread *sched_idle_thread(void);
extern struct pbl_thread *pbl_cur;
typedef struct {
  uint32_t wall, idle, app, main, app_id;
} CpuSample;
static CpuSample s_cpu_first, s_cpu_last;
static bool s_cpu_started, s_cpu_finished;
static uint32_t prv_runtime(struct pbl_thread *thread, uint32_t now) {
  if (!thread)
    return 0u;
  return thread->backend.run_time + (thread == pbl_cur ? now - thread->backend.switched_in_at : 0u);
}
static CpuSample prv_cpu_snapshot(void) {
  const uint32_t now = (uint32_t)pbl_uptime_ticks();
  return (CpuSample){
    now, prv_runtime(sched_idle_thread(), now),
    prv_runtime(pebble_task_get_thread(PebbleTask_App), now),
    prv_runtime(pebble_task_get_thread(PebbleTask_KernelMain), now),
    pebble_task_get_thread(PebbleTask_App) ? pebble_task_get_thread(PebbleTask_App)->id : 0u
  };
}
void t5ai_io_profile_contact(bool down) {
  pbl_irq_lock();
  if (down && !s_cpu_started) {
    s_cpu_first = prv_cpu_snapshot();
    s_cpu_started = true;
    s_cpu_finished = false;
  } else if (!down && s_cpu_started) {
    s_cpu_last = prv_cpu_snapshot();
    s_cpu_finished = true;
  }
  pbl_irq_unlock();
}
static void prv_cpu_dump(void) {
  pbl_irq_lock();
  const bool ready = s_cpu_finished;
  const CpuSample first = s_cpu_first, last = s_cpu_last;
  s_cpu_started = s_cpu_finished = false;
  pbl_irq_unlock();
  if (!ready)
    return;
  PBL_LOG_DBG("T5CPU wall=%lu idle=%lu app=%lu main=%lu hz=%lu app_valid=%u",
              (unsigned long)(last.wall - first.wall), (unsigned long)(last.idle - first.idle),
              (unsigned long)(last.app - first.app), (unsigned long)(last.main - first.main),
              (unsigned long)PBL_TICK_HZ, first.app_id == last.app_id);
}

#endif
