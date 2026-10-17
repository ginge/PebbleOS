/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/time.h>
#include <pbl/soc/t5ai/state_trace.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/mcu/cache.h>
#include <pbl/services/system_task.h>

#include "kernel/pebble_tasks.h"
#include "system/passert.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(t5ai_state_trace, LOG_LEVEL_DEBUG);

#define TRACE_MAGIC   0x53543101u
#define TRACE_SAMPLES 16u

typedef struct {
  uint32_t id;
  uint32_t cpu_ticks;
  uint32_t waitq;
  uint32_t scheduling;
} ThreadState;

typedef struct {
  uint32_t sequence;
  uint32_t ticks;
  uint32_t callback;
  uint32_t callback_since;
  ThreadState bg;
  ThreadState app;
  uint32_t checksum;
} Sample;

typedef struct {
  uint32_t magic;
  uint32_t next_sequence;
  uint32_t frozen;
  uint32_t reason;
  Sample samples[TRACE_SAMPLES];
} Trace;

static Trace s_trace __attribute__((section(".t5ai_retained"), aligned(32)));
static bool s_live;
static uint32_t s_callback;
static uint32_t s_callback_since;
static struct pbl_thread s_thread;
PBL_THREAD_STACK_DEFINE(s_stack, 1536);

static uint32_t prv_checksum(const Sample *sample) {
  const uint8_t *bytes = (const uint8_t *)sample;
  uint32_t checksum = TRACE_MAGIC;
  for (size_t i = 0; i < offsetof(Sample, checksum); i++) {
    checksum = (checksum << 5) | (checksum >> 27);
    checksum ^= bytes[i];
  }
  return checksum;
}

static ThreadState prv_thread_state(struct pbl_thread *thread, uint32_t now) {
  if (!thread) {
    return (ThreadState){.scheduling = UINT8_MAX};
  }
  uint32_t cpu_ticks = thread->backend.run_time;
  if (thread == pbl_thread_current()) {
    cpu_ticks += now - thread->backend.switched_in_at;
  }
  // Inspect the backend only with interrupts locked; never follow the wait queue.
  return (ThreadState){
    .id = thread->id,
    .cpu_ticks = cpu_ticks,
    .waitq = (uintptr_t)thread->backend.waitq,
    .scheduling = thread->backend.state | ((uint32_t)thread->backend.base_prio << 8) |
                  ((uint32_t)thread->prio << 16) | ((uint32_t)thread->backend.mutexes_held << 24),
  };
}

static void prv_sample(void) {
  pbl_irq_lock();
  if (!s_live || s_trace.frozen) {
    pbl_irq_unlock();
    return;
  }
  const uint32_t now = pbl_uptime_ticks();
  const uint32_t callback = (uintptr_t)system_task_get_current_callback();
  if (callback != s_callback || callback == 0) {
    s_callback = callback;
    s_callback_since = now;
  }
  Sample sample = {
    .sequence = s_trace.next_sequence,
    .ticks = now,
    .callback = callback,
    .callback_since = s_callback_since,
    .bg = prv_thread_state(pebble_task_get_thread(PebbleTask_KernelBackground), now),
    .app = prv_thread_state(pebble_task_get_thread(PebbleTask_App), now),
  };
  sample.checksum = prv_checksum(&sample);
  Sample *slot = &s_trace.samples[(sample.sequence - 1u) % TRACE_SAMPLES];
  *slot = sample;
  s_trace.next_sequence++;
  dcache_flush(slot, sizeof(*slot));
  dcache_flush(&s_trace, offsetof(Trace, samples));
  pbl_irq_unlock();
}

void t5ai_state_trace_freeze(uint32_t reason) {
  if (!s_live) {
    return;
  }
  pbl_irq_lock();
  if (!s_trace.frozen) {
    s_trace.reason = reason;
    s_trace.frozen = 1;
    dcache_flush(&s_trace, sizeof(s_trace));
  }
  pbl_irq_unlock();
}

static void prv_print_thread(const char *name, const ThreadState *state) {
  PBL_LOG_INFO("T5STATE %s id=%" PRIu32 " cpu_ticks=%" PRIu32
               " state=%u base=%u effective=%u waitq=%08" PRIx32,
               name, state->id, state->cpu_ticks, (unsigned)(state->scheduling & 0xffu),
               (unsigned)((state->scheduling >> 8) & 0xffu),
               (unsigned)((state->scheduling >> 16) & 0xffu), state->waitq);
}

static void prv_dump_previous(void) {
  if (s_trace.magic != TRACE_MAGIC || s_trace.next_sequence == 0 || s_trace.frozen > 1) {
    return;
  }
  const uint32_t end = s_trace.next_sequence;
  const uint32_t begin = end > TRACE_SAMPLES ? end - TRACE_SAMPLES : 1;
  PBL_LOG_INFO("T5STATE previous boot frozen=%" PRIu32 " reason=%" PRIu32 " tick_hz=%u",
               s_trace.frozen, s_trace.reason, PBL_TICK_HZ);
  for (uint32_t seq = begin; seq < end; seq++) {
    const Sample *sample = &s_trace.samples[(seq - 1u) % TRACE_SAMPLES];
    if (sample->sequence != seq || sample->checksum != prv_checksum(sample)) {
      PBL_LOG_WRN("T5STATE invalid sample=%" PRIu32, seq);
      continue;
    }
    PBL_LOG_INFO("T5STATE sample=%" PRIu32 " ticks=%" PRIu32 " callback=%08" PRIx32
                 " same_callback_ticks=%" PRIu32,
                 seq, sample->ticks, sample->callback, sample->ticks - sample->callback_since);
    prv_print_thread("BG", &sample->bg);
    prv_print_thread("APP", &sample->app);
  }
}

static void prv_entry(void *arg) {
  (void)arg;
  prv_dump_previous();
  memset(&s_trace, 0, sizeof(s_trace));
  s_trace.next_sequence = 1;
  s_trace.magic = TRACE_MAGIC;
  s_live = true;
  PBL_LOG_DBG("T5STATE recording samples=%u period_ms=1000", TRACE_SAMPLES);
  for (;;) {
    prv_sample();
    pbl_thread_sleep(PBL_MSEC(1000));
  }
}

void t5ai_state_trace_start(void) {
  const struct pbl_thread_attr attr = {
    .name = "T5State",
    .entry = prv_entry,
    .prio = PBL_PRIO_MAX - 1,
    .privileged = true,
    .stack = s_stack,
    .stack_size = sizeof(s_stack),
  };
  PBL_ASSERTN(pbl_thread_create(&s_thread, &attr) == 0);
}
