/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/irq.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/task_wdt/task_wdt.h>
#include <inttypes.h>

PBL_LOG_MODULE_DEFINE(t5ai_wdt_cause_trace, LOG_LEVEL_DEBUG);

#define RECORDS 8u

enum { Suspend = 1, Resume, FeedAll, FeedChannel, FeedThread };
typedef struct {
  uint32_t kind, rtc, arg, lr, thread, bg_state, callback;
} Record;
typedef struct {
  uint32_t count, reported;
  Record last;
} Feed;
static Record s_ring[RECORDS];
static uint32_t s_written, s_read, s_lost;
static Feed s_feeds[3];
static int s_bg_channel = -1;
extern struct pbl_thread *g_task_threads[NumPebbleTask];
extern struct pbl_thread *pbl_cur;

static void prv_record(uint32_t kind, uint32_t arg, uintptr_t lr) {
  pbl_irq_lock();
  struct pbl_thread *bg = g_task_threads[PebbleTask_KernelBackground];
  Record record = {
    .kind = kind, .rtc = T5AI_AON_RTC_COUNT_LOW, .arg = arg, .lr = lr,
    .thread = pbl_cur ? pbl_cur->id : 0,
    .bg_state = bg ? bg->backend.state : PBL_THREAD_DEAD,
    .callback = (uintptr_t)system_task_get_current_callback(),
  };
  if (kind >= FeedAll) {
    Feed *feed = &s_feeds[kind - FeedAll];
    ++feed->count;
    feed->last = record;
  } else {
    if (s_written - s_read == RECORDS) {
      ++s_read;
      ++s_lost;
    }
    s_ring[s_written++ % RECORDS] = record;
  }
  pbl_irq_unlock();
}

void __real_pbl_task_wdt_suspend(uint32_t timeout_ms);
void __wrap_pbl_task_wdt_suspend(uint32_t timeout_ms) {
  // Record before delegating: the caller may be starved as the real call returns.
  prv_record(Suspend, timeout_ms, (uintptr_t)__builtin_return_address(0));
  __real_pbl_task_wdt_suspend(timeout_ms);
}
void __real_pbl_task_wdt_resume(void);
void __wrap_pbl_task_wdt_resume(void) {
  prv_record(Resume, 0, (uintptr_t)__builtin_return_address(0));
  __real_pbl_task_wdt_resume();
}
void __real_pbl_task_wdt_feed_all(void);
void __wrap_pbl_task_wdt_feed_all(void) {
  prv_record(FeedAll, 0, (uintptr_t)__builtin_return_address(0));
  __real_pbl_task_wdt_feed_all();
}
int __real_pbl_task_wdt_add(struct pbl_thread *, uint32_t, pbl_task_wdt_callback_t, void *);
int __wrap_pbl_task_wdt_add(struct pbl_thread *thread, uint32_t timeout_ms,
                          pbl_task_wdt_callback_t callback, void *data) {
  int channel = __real_pbl_task_wdt_add(thread, timeout_ms, callback, data);
  if (channel >= 0 && (thread ? thread : pbl_cur) ==
                          g_task_threads[PebbleTask_KernelBackground]) {
    pbl_irq_lock();
    s_bg_channel = channel;
    pbl_irq_unlock();
  }
  return channel;
}
int __real_pbl_task_wdt_feed(int channel);
int __wrap_pbl_task_wdt_feed(int channel) {
  if (channel >= 0 && channel == s_bg_channel)
    prv_record(FeedChannel, channel, (uintptr_t)__builtin_return_address(0));
  return __real_pbl_task_wdt_feed(channel);
}
void __real_pbl_task_wdt_feed_thread(struct pbl_thread *thread);
void __wrap_pbl_task_wdt_feed_thread(struct pbl_thread *thread) {
  if (thread && thread == g_task_threads[PebbleTask_KernelBackground])
    prv_record(FeedThread, thread->id, (uintptr_t)__builtin_return_address(0));
  __real_pbl_task_wdt_feed_thread(thread);
}

void t5ai_wdt_cause_trace_dump(void) {
  // Drain a bounded number on the existing timer task, never in wrapped calls.
  for (unsigned i = 0; i < 4u; ++i) {
    pbl_irq_lock();
    if (s_read == s_written) {
      pbl_irq_unlock();
      break;
    }
    uint32_t seq = s_read + 1u;
    Record r = s_ring[s_read++ % RECORDS];
    pbl_irq_unlock();
    PBL_LOG_DBG("T5WDT seq=%" PRIu32 " kind=%" PRIu32 " rtc=%" PRIu32 " arg=%" PRIu32
                " lr=%08" PRIx32 " who=%" PRIu32 " cb=%08" PRIx32,
                seq, r.kind, r.rtc, r.arg, r.lr, (r.thread << 3u) | r.bg_state, r.callback);
  }
  for (unsigned i = 0; i < 3u; ++i) {
    pbl_irq_lock();
    Feed f = s_feeds[i];
    s_feeds[i].reported = f.count;
    pbl_irq_unlock();
    if (f.count == f.reported)
      continue;
    Record r = f.last;
    PBL_LOG_DBG("T5WDT feeds=%" PRIu32 " kind=%" PRIu32 " rtc=%" PRIu32 " arg=%" PRIu32
                " lr=%08" PRIx32 " who=%" PRIu32 " cb=%08" PRIx32,
                f.count, r.kind, r.rtc, r.arg, r.lr, (r.thread << 3u) | r.bg_state, r.callback);
  }
  pbl_irq_lock();
  uint32_t written = s_written, read = s_read, lost = s_lost;
  pbl_irq_unlock();
  PBL_LOG_DBG("T5WDT written=%" PRIu32 " read=%" PRIu32 " lost=%" PRIu32, written, read, lost);
}
