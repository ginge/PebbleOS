/* SPDX-License-Identifier: Apache-2.0 */

#include <inttypes.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/services/system_task.h>
#include <pbl/task_wdt/task_wdt.h>
#include <pbl/soc/t5ai/flash_ipc.h>

PBL_LOG_MODULE_DEFINE(t5ai_wdt_recovery_trace, LOG_LEVEL_DEBUG);

extern struct pbl_thread *g_task_threads[NumPebbleTask];
extern struct pbl_thread *pbl_cur;
static pbl_task_wdt_callback_t s_callback;
static void *s_data;
static uint32_t s_settings_active;
static uint32_t s_settings_start;

status_t __real_settings_blob_db_mark_all_dirty(void);
status_t __wrap_settings_blob_db_mark_all_dirty(void) {
  __atomic_store_n(&s_settings_start, pbl_uptime_ticks(), __ATOMIC_RELAXED);
  __atomic_store_n(&s_settings_active, 1u, __ATOMIC_RELAXED);
  status_t result = __real_settings_blob_db_mark_all_dirty();
  __atomic_store_n(&s_settings_active, 0u, __ATOMIC_RELAXED);
  return result;
}

static void prv_flash_dump(unsigned phase) {
  T5AIFlashDiagnostics flash;
  t5ai_flash_ipc_get_diagnostics(&flash);
  PBL_LOG_SYNC_WRN("T5REC flash phase=%u active=%" PRIu32 " start=%" PRIu32
                   " settings=%" PRIu32 " settings_start=%" PRIu32,
                   phase, flash.active_command, flash.active_tick,
                   __atomic_load_n(&s_settings_active, __ATOMIC_RELAXED),
                   __atomic_load_n(&s_settings_start, __ATOMIC_RELAXED));
  const T5AIFlashOperationDiagnostics *ops[] = {&flash.read, &flash.write, &flash.erase};
  for (unsigned i = 0; i < 3; ++i) {
    const T5AIFlashOperationDiagnostics *op = ops[i];
    PBL_LOG_SYNC_WRN("T5REC io phase=%u op=%u count=%" PRIu32 " max_ms=%" PRIu32
                     " last_ms=%" PRIu32 " offset=%" PRIu32 " len=%" PRIu32,
                     phase, i, op->count, op->max_ms, op->last_ms,
                     op->last_offset, op->last_length);
  }
}

typedef struct {
  uint32_t id, base, effective, held, state, waitq, cpu;
} Snapshot;

static Snapshot prv_snapshot(struct pbl_thread *thread) {
  if (!thread) {
    return (Snapshot){0};
  }
  return (Snapshot){thread->id, thread->backend.base_prio, thread->prio,
                    thread->backend.mutexes_held, thread->backend.state,
                    (uintptr_t)thread->backend.waitq, thread->backend.run_time};
}

static void prv_dump(unsigned phase) {
  pbl_irq_lock();
  Snapshot app = prv_snapshot(g_task_threads[PebbleTask_App]);
  Snapshot bg = prv_snapshot(g_task_threads[PebbleTask_KernelBackground]);
  pbl_tick_t ticks = pbl_uptime_ticks();
  pbl_irq_unlock();
  PBL_LOG_SYNC_WRN("T5REC clocks phase=%u ticks=%" PRIu32 " app_cpu=%" PRIu32 " bg_cpu=%" PRIu32,
                   phase, ticks, app.cpu, bg.cpu);
  PBL_LOG_SYNC_WRN("T5REC app phase=%u id=%" PRIu32 " base=%" PRIu32
                   " eff=%" PRIu32 " held=%" PRIu32 " state=%" PRIu32 " waitq=%08" PRIx32,
                   phase, app.id, app.base, app.effective, app.held, app.state, app.waitq);
  PBL_LOG_SYNC_WRN("T5REC bg phase=%u id=%" PRIu32 " base=%" PRIu32
                   " eff=%" PRIu32 " held=%" PRIu32 " state=%" PRIu32 " waitq=%08" PRIx32,
                   phase, bg.id, bg.base, bg.effective, bg.held, bg.state, bg.waitq);
}

extern void t5ai_io_trace_dump(void);

static void *prv_expired(int channel, void *data) {
  prv_dump(0);
  prv_flash_dump(0);
  t5ai_io_trace_dump();
  void *work = s_callback ? s_callback(channel, s_data) : NULL;
  prv_dump(1);
  return work;
}

int __real_pbl_task_wdt_add(struct pbl_thread *, uint32_t, pbl_task_wdt_callback_t, void *);
int __wrap_pbl_task_wdt_add(struct pbl_thread *thread, uint32_t timeout_ms,
                          pbl_task_wdt_callback_t callback, void *data) {
  if ((thread ? thread : pbl_cur) != g_task_threads[PebbleTask_KernelBackground]) {
    return __real_pbl_task_wdt_add(thread, timeout_ms, callback, data);
  }
  int channel = __real_pbl_task_wdt_add(thread, timeout_ms, prv_expired, NULL);
  if (channel >= 0) {
    s_callback = callback;
    s_data = data;
  }
  return channel;
}
