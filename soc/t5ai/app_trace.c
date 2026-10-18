/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/time.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/msgq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/app_trace.h>
#include <pbl/soc/t5ai/rtc.h>
#include "kernel/pebble_tasks.h"
#include "process_management/app_manager.h"
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(t5ai_app_trace, LOG_LEVEL_DEBUG);
#define BUCKETS 256u
#define PROBES  4u
#define HISTORY 64u
typedef struct {
  uint32_t pc, lr, event, count;
} Bucket;
typedef struct {
  uint32_t id, event, callback, wall, cpu, queued;
} Event;
// Keep diagnostics out of the kernel heap aperture. PSRAM is ready before App creation.
#define TRACE_STORAGE __attribute__((section(".t5ai_board_cache"), aligned(32)))
static Bucket s_buckets[BUCKETS] TRACE_STORAGE;
static Bucket s_dump_buckets[BUCKETS] TRACE_STORAGE;
static bool s_storage_ready;
static Event s_history[HISTORY] TRACE_STORAGE;
static uint32_t s_id, s_next, s_jitter, s_samples, s_overflow, s_events, s_waits;
static uint32_t s_event = 255u, s_callback, s_since, s_cpu, s_waiting, s_wait_since;
static uint32_t s_write, s_read, s_lost;
extern struct pbl_thread *pbl_cur;

static uint32_t prv_cpu(struct pbl_thread *t) {
  return t->backend.run_time + (t == pbl_cur ? pbl_uptime_ticks() - t->backend.switched_in_at : 0u);
}
static void prv_identity(struct pbl_thread *t, uint32_t now) {
  if (s_id == t->id)
    return;
  s_id = t->id;
  s_event = 255u;
  s_callback = 0u;
  s_since = now;
  s_cpu = prv_cpu(t);
  s_waiting = 0u;
}
// Armv8-M places the core frame first, also with an extended FP frame.
static bool prv_frame_valid(uintptr_t sp, uint32_t er) {
  uint32_t bytes = (er & 16u ? 8u : 26u) * 4u;
  return (er & 0xffffff8du) == 0xffffff8du && !(sp & 3u) && sp >= 0x28010000u &&
         sp <= 0x28066000u - bytes;
}
void t5ai_app_trace_tick(const uint32_t *frame, uint32_t er) {
  if (!s_storage_ready)
    return;
  uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  if (s_next && (int32_t)(now - s_next) < 0)
    return;
  s_jitter = (s_jitter + 1u) % 7u;
  s_next = now + (7u + s_jitter) * 32u;
  struct pbl_thread *t = pebble_task_get_thread(PebbleTask_App);
  if (!t || t != pbl_cur || !prv_frame_valid((uintptr_t)frame, er))
    return;
  if ((frame[7] & 0x1ffu) || !(frame[7] & (1u << 24)))
    return;
  prv_identity(t, now);
  uint32_t pc = frame[6] & ~15u;
  uint32_t event = s_waiting ? 254u : s_event;
  uint32_t lr = frame[5] & ~1u;
  ++s_samples;
  uint32_t slot = ((pc >> 4) ^ (lr >> 1) ^ (event * 17u)) % BUCKETS;
  for (unsigned i = 0; i < PROBES; ++i) {
    Bucket *b = &s_buckets[(slot + i) % BUCKETS];
    if (!b->count || (b->pc == pc && b->lr == lr && b->event == event)) {
      b->pc = pc;
      b->lr = lr;
      b->event = event;
      ++b->count;
      return;
    }
  }
  ++s_overflow;
}
int __real_pbl_msgq_get(struct pbl_msgq *, void *, pbl_timeout_t);
int __wrap_pbl_msgq_get(struct pbl_msgq *q, void *msg, pbl_timeout_t timeout) {
  struct pbl_thread *t = pebble_task_get_thread(PebbleTask_App);
  if (!s_storage_ready || !t || t != pbl_cur ||
      q != app_manager_get_task_context()->to_process_event_queue)
    return __real_pbl_msgq_get(q, msg, timeout);
  pbl_irq_lock();
  uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  prv_identity(t, now);
  uint32_t cpu = prv_cpu(t);
  if (!s_waiting && now - s_since >= 3200u) {
    if (s_write - s_read == HISTORY) {
      ++s_read;
      ++s_lost;
    }
    s_history[s_write++ % HISTORY] =
        (Event){s_id, s_event, s_callback, now - s_since, cpu - s_cpu, q->backend.count};
  }
  s_waiting = 1u;
  s_wait_since = now;
  pbl_irq_unlock();
  int rc = __real_pbl_msgq_get(q, msg, timeout);
  pbl_irq_lock();
  now = T5AI_AON_RTC_COUNT_LOW;
  s_waits += now - s_wait_since;
  if (!rc) {
    const PebbleEvent *e = msg;
    s_event = e->type;
    s_callback = e->type == PEBBLE_CALLBACK_EVENT ? (uintptr_t)e->callback.callback : 0u;
    ++s_events;
  }
  s_waiting = 0u;
  s_since = now;
  s_cpu = prv_cpu(t);
  pbl_irq_unlock();
  return rc;
}
void t5ai_app_trace_dump(void) {
  if (!s_storage_ready) {
    if (!pebble_task_get_thread(PebbleTask_App))
      return;
    pbl_irq_lock();
    memset(s_buckets, 0, sizeof(s_buckets));
    s_storage_ready = true;
    pbl_irq_unlock();
  }
  static uint32_t next;
  uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  if (next && (int32_t)(now - next) < 0)
    return;
  next = now + 5u * T5AI_AON_RTC_HZ;
  Bucket *buckets = s_dump_buckets;
  uint32_t id, event, callback, age, waiting, samples, overflow, events, waits, lost;
  pbl_irq_lock();
  memcpy(buckets, s_buckets, sizeof(s_buckets));
  memset(s_buckets, 0, sizeof(s_buckets));
  id = s_id;
  event = s_event;
  callback = s_callback;
  age = (now - s_since) / 32u;
  waiting = s_waiting;
  samples = s_samples;
  overflow = s_overflow;
  events = s_events;
  waits = s_waits / 32u;
  lost = s_lost;
  s_samples = s_overflow = s_events = s_waits = 0u;
  pbl_irq_unlock();
  PBL_LOG_DBG("T5APP id=%lu event=%lu cb=%08lx age_ms=%lu waiting=%lu samples=%lu overflow=%lu",
              (unsigned long)id, (unsigned long)event, (unsigned long)callback, (unsigned long)age,
              (unsigned long)waiting, (unsigned long)samples, (unsigned long)overflow);
  PBL_LOG_DBG("T5APP now=%lu events=%lu receive_ms=%lu history_lost=%lu", (unsigned long)now,
              (unsigned long)events, (unsigned long)waits, (unsigned long)lost);
  for (unsigned n = 0; n < 12u; ++n) {
    unsigned best = 0u;
    for (unsigned i = 1; i < BUCKETS; ++i)
      if (buckets[i].count > buckets[best].count)
        best = i;
    Bucket *b = &buckets[best];
    if (!b->count)
      break;
    PBL_LOG_DBG("T5APP pc=%08lx lr=%08lx event=%lu count=%lu", (unsigned long)b->pc,
                (unsigned long)b->lr, (unsigned long)b->event, (unsigned long)b->count);
    b->count = 0u;
  }
  for (unsigned i = 0; i < HISTORY; ++i) {
    Event e;
    pbl_irq_lock();
    bool have = s_read != s_write;
    if (have)
      e = s_history[s_read++ % HISTORY];
    pbl_irq_unlock();
    if (!have)
      break;
    PBL_LOG_DBG("T5APP long id=%lu event=%lu cb=%08lx wall_ms=%lu cpu_ticks=%lu queued=%lu",
                (unsigned long)e.id, (unsigned long)e.event, (unsigned long)e.callback,
                (unsigned long)(e.wall / 32u), (unsigned long)e.cpu, (unsigned long)e.queued);
  }
}
