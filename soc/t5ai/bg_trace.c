/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/time.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/app_trace.h>
#include "kernel/pebble_tasks.h"
#include <pbl/kernel/assert.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(t5ai_bg_trace, LOG_LEVEL_DEBUG);

#define RECORDS    64u
#define THREADS    32u
#define LONG_TICKS (T5AI_AON_RTC_HZ / 10u)

typedef struct {
  uint32_t kind, object, caller, owner, owner_lr;
} Operation;
typedef struct {
  uint32_t seq, at, span, state, callback, waitq, base, effective;
  Operation op;
} Record;
static struct pbl_thread *s_bg;
static bool s_live;
static uint32_t s_since, s_state, s_callback, s_waitq, s_written, s_read, s_lost;
static uint64_t s_totals[5];
static Operation s_op;
static Record s_ring[RECORDS];
static struct pbl_thread s_worker;
PBL_THREAD_STACK_DEFINE(s_stack, 4096);
extern struct pbl_thread *pbl_all_threads;
extern struct pbl_thread *pbl_cur;
extern struct pbl_thread *sched_switch_in(void);
static bool s_bg_runnable_on_pick;
static uint32_t s_pick_bg, s_pick_higher, s_pick_equal, s_pick_lower;

// Called only with scheduler interrupts locked. No allocation, UART or task locks.
static void prv_observe_boundary(bool boundary) {
  if (!s_live || !s_bg)
    return;
  uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  uint32_t state = s_bg->backend.state;
  uint32_t callback = (uintptr_t)system_task_get_current_callback();
  uint32_t waitq = (uintptr_t)s_bg->backend.waitq;
  if (!boundary && state == s_state && callback == s_callback && waitq == s_waitq)
    return;
  uint32_t span = now - s_since;
  if (s_state < 5u)
    s_totals[s_state] += span;
  // Idle poll waits are normal. Retain long active waits and all long ready spans.
  if (span >= LONG_TICKS && (s_callback || s_state == PBL_THREAD_READY)) {
    if (s_written - s_read == RECORDS) {
      ++s_read;
      ++s_lost;
    }
    s_ring[s_written % RECORDS] = (Record){
        .seq = s_written + 1u,
        .at = now,
        .span = span,
        .state = s_state,
        .callback = s_callback,
        .waitq = s_waitq,
        .base = s_bg->backend.base_prio,
        .effective = s_bg->prio,
        .op = s_op,
    };
    ++s_written;
  }
  s_since = now;
  s_state = state;
  s_callback = callback;
  s_waitq = waitq;
}

static void prv_observe(void) {
  prv_observe_boundary(false);
}

void __cyg_profile_func_enter(void *function, void *caller) {
  (void)caller;
  if (s_live && s_bg && function == (void *)sched_switch_in) {
    s_bg_runnable_on_pick = s_bg->backend.state == PBL_THREAD_READY ||
                            s_bg->backend.state == PBL_THREAD_RUNNING;
  }
  prv_observe();
}
void __cyg_profile_func_exit(void *function, void *caller) {
  (void)caller;
  if (s_live && s_bg && function == (void *)sched_switch_in && s_bg_runnable_on_pick) {
    if (pbl_cur == s_bg) {
      ++s_pick_bg;
    } else if (pbl_cur->prio > s_bg->prio) {
      ++s_pick_higher;
    } else if (pbl_cur->prio == s_bg->prio) {
      ++s_pick_equal;
    } else {
      ++s_pick_lower;
    }
  }
  prv_observe();
}

static bool prv_is_bg(void) {
  return s_live && pbl_cur == s_bg;
}
static Operation prv_begin(uint32_t kind, void *object, uintptr_t caller, struct pbl_mutex *mutex) {
  pbl_irq_lock();
  prv_observe();
  prv_observe_boundary(true);
  Operation previous = s_op;
  s_op = (Operation){.kind = kind, .object = (uintptr_t)object, .caller = caller};
  if (mutex) {
    s_op.owner = mutex->owner ? mutex->owner->id : 0u;
    s_op.owner_lr = mutex->lock_lr;
  }
  pbl_irq_unlock();
  return previous;
}
static void prv_end(Operation previous) {
  pbl_irq_lock();
  prv_observe_boundary(true);
  s_op = previous;
  pbl_irq_unlock();
}

int __real_sched_block(struct pbl_waitq *, pbl_timeout_t);
int __wrap_sched_block(struct pbl_waitq *waitq, pbl_timeout_t timeout) {
  if (!prv_is_bg() || s_op.kind)
    return __real_sched_block(waitq, timeout);
  Operation previous = prv_begin(1u, waitq, (uintptr_t)__builtin_return_address(0), NULL);
  int rc = __real_sched_block(waitq, timeout);
  prv_end(previous);
  return rc;
}
int __real_pbl_mutex_lock_lr(struct pbl_mutex *, pbl_timeout_t, uintptr_t);
int __wrap_pbl_mutex_lock_lr(struct pbl_mutex *mutex, pbl_timeout_t timeout, uintptr_t lr) {
  if (!prv_is_bg())
    return __real_pbl_mutex_lock_lr(mutex, timeout, lr);
  Operation previous = prv_begin(2u, mutex, lr, mutex);
  int rc = __real_pbl_mutex_lock_lr(mutex, timeout, lr);
  prv_end(previous);
  return rc;
}
void __real_pbl_mutex_unlock(struct pbl_mutex *);
void __wrap_pbl_mutex_unlock(struct pbl_mutex *mutex) {
  if (!prv_is_bg()) {
    __real_pbl_mutex_unlock(mutex);
    return;
  }
  Operation previous = prv_begin(3u, mutex, (uintptr_t)__builtin_return_address(0), mutex);
  __real_pbl_mutex_unlock(mutex);
  prv_end(previous);
}

typedef struct {
  uint32_t id, cpu, delta, state, base, effective, waitq;
  char name[PBL_THREAD_NAME_LEN];
} Thread;
static Thread s_previous[THREADS];
static uint32_t s_previous_count;
static uint64_t s_previous_totals[5];
static void prv_snapshot(void) {
  Thread threads[THREADS];
  uint32_t count = 0u, now, state, callback, waitq, age, lost;
  uint64_t totals[5];
  Operation op;
  pbl_irq_lock();
  prv_observe();
  now = T5AI_AON_RTC_COUNT_LOW;
  state = s_state;
  callback = s_callback;
  age = now - s_since;
  waitq = s_waitq;
  lost = s_lost;
  op = s_op;
  if (op.kind == 2u && state == PBL_THREAD_BLOCKED) {
    struct pbl_mutex *m = (void *)(uintptr_t)op.object;
    op.owner = m->owner ? m->owner->id : 0u;
    op.owner_lr = m->lock_lr;
  }
  memcpy(totals, s_totals, sizeof(totals));
  if (state < 5u)
    totals[state] += age;
  uint32_t ticks = pbl_uptime_ticks();
  for (struct pbl_thread *t = pbl_all_threads; t && count < THREADS; t = t->backend.all_next) {
    Thread *out = &threads[count++];
    *out = (Thread){.id = t->id,
                    .cpu = t->backend.run_time,
                    .state = t->backend.state,
                    .base = t->backend.base_prio,
                    .effective = t->prio,
                    .waitq = (uintptr_t)t->backend.waitq};
    if (t == pbl_cur)
      out->cpu += ticks - t->backend.switched_in_at;
    memcpy(out->name, t->name, sizeof(out->name));
  }
  pbl_irq_unlock();
  for (uint32_t i = 0; i < count; ++i) {
    for (uint32_t j = 0; j < s_previous_count; ++j) {
      if (threads[i].id == s_previous[j].id) {
        threads[i].delta = threads[i].cpu - s_previous[j].cpu;
        break;
      }
    }
  }
  uint32_t ready = (totals[PBL_THREAD_READY] - s_previous_totals[PBL_THREAD_READY]) / 32u;
  uint32_t blocked = (totals[PBL_THREAD_BLOCKED] - s_previous_totals[PBL_THREAD_BLOCKED]) / 32u;
  if (callback || ready >= 100u) {
    PBL_LOG_DBG(
        "T5BG now=%lu cb=%08lx state=%lu age_ms=%lu ready_ms=%lu blocked_ms=%lu run_ms=%lu",
        (unsigned long)now, (unsigned long)callback, (unsigned long)state,
        (unsigned long)(age / 32u), (unsigned long)ready, (unsigned long)blocked,
        (unsigned long)((totals[PBL_THREAD_RUNNING] - s_previous_totals[PBL_THREAD_RUNNING]) /
                        32u));
    PBL_LOG_DBG(
        "T5BG waitq=%08lx op=%lu object=%08lx caller=%08lx owner=%lu owner_lr=%08lx lost=%lu",
        (unsigned long)waitq, (unsigned long)op.kind, (unsigned long)op.object,
        (unsigned long)op.caller, (unsigned long)op.owner, (unsigned long)op.owner_lr,
        (unsigned long)lost);
    PBL_LOG_DBG("T5BG picks_bg=%lu picks_higher=%lu picks_equal=%lu picks_lower=%lu",
                (unsigned long)s_pick_bg, (unsigned long)s_pick_higher,
                (unsigned long)s_pick_equal, (unsigned long)s_pick_lower);
    // Print every thread once per diagnostic window; IDs survive name truncation.
    for (uint32_t i = 0; i < count; ++i) {
      Thread *t = &threads[i];
      if (!t->delta && t->id != s_bg->id && t->id != op.owner)
        continue;
      PBL_LOG_DBG(
          "T5BG task=%lu name=%s cpu_ticks=%lu state=%lu base=%lu effective=%lu waitq=%08lx",
          (unsigned long)t->id, t->name, (unsigned long)t->delta, (unsigned long)t->state,
          (unsigned long)t->base, (unsigned long)t->effective, (unsigned long)t->waitq);
    }
  }
  memcpy(s_previous, threads, count * sizeof(Thread));
  s_previous_count = count;
  memcpy(s_previous_totals, totals, sizeof(totals));
}
static void prv_entry(void *arg) {
  (void)arg;
  pbl_irq_lock();
  s_bg = pebble_task_get_thread(PebbleTask_KernelBackground);
  PBL_ASSERTN(s_bg);
  s_since = T5AI_AON_RTC_COUNT_LOW;
  s_state = s_bg->backend.state;
  s_callback = (uintptr_t)system_task_get_current_callback();
  s_waitq = (uintptr_t)s_bg->backend.waitq;
  s_live = true;
  pbl_irq_unlock();
  PBL_LOG_DBG(
      "T5BG start hz=32000 period_ms=1000 records=64 states=ready0/run1/block2 op=wait1/lock2/unlock3");
  for (;;) {
    prv_snapshot();
    t5ai_app_trace_dump();
    for (unsigned i = 0; i < 8u; ++i) {
      Record record;
      bool have;
      pbl_irq_lock();
      have = s_read != s_written;
      if (have)
        record = s_ring[s_read++ % RECORDS];
      pbl_irq_unlock();
      if (!have)
        break;
      PBL_LOG_DBG("T5BG segment=%lu at=%lu ms=%lu state=%lu cb=%08lx waitq=%08lx base=%lu",
                  (unsigned long)record.seq, (unsigned long)record.at,
                  (unsigned long)(record.span / 32u), (unsigned long)record.state,
                  (unsigned long)record.callback, (unsigned long)record.waitq,
                  (unsigned long)record.base);
      PBL_LOG_DBG(
          "T5BG segment=%lu op=%lu object=%08lx caller=%08lx owner=%lu owner_lr=%08lx effective=%lu",
          (unsigned long)record.seq, (unsigned long)record.op.kind, (unsigned long)record.op.object,
          (unsigned long)record.op.caller, (unsigned long)record.op.owner,
          (unsigned long)record.op.owner_lr, (unsigned long)record.effective);
    }
    pbl_thread_sleep(PBL_MSEC(1000));
  }
}
void __real_system_task_timer_init(void);
void __wrap_system_task_timer_init(void) {
  __real_system_task_timer_init();
  const struct pbl_thread_attr attr = {.name = "BGTrace",
                                       .entry = prv_entry,
                                       .prio = PBL_PRIO_MAX - 1,
                                       .privileged = true,
                                       .stack = s_stack,
                                       .stack_size = sizeof(s_stack)};
  PBL_ASSERTN(pbl_thread_create(&s_worker, &attr) == 0);
}
