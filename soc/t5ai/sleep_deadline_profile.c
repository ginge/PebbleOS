/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/time.h>
#include <pbl/kernel/timer.h>
#include <pbl/soc/t5ai/sleep_deadline_profile.h>

#define PROFILE_TIMER_RECORD_LIMIT 64u
#define PROFILE_EARLY_RECORD_LIMIT 8u

typedef struct {
  struct pbl_timer *timer;
  uint64_t expires;
  pbl_tick_t period;
  uintptr_t callback;
} TimerRecord;
static TimerRecord s_early[PROFILE_EARLY_RECORD_LIMIT];
static TimerRecord s_records[PROFILE_TIMER_RECORD_LIMIT]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));
static bool s_storage_ready;
static unsigned s_untracked;

void t5ai_sleep_profile_timers_ready(void) {
  pbl_irq_lock();
  if (!s_storage_ready) {
    memset(s_records, 0, sizeof(s_records));
    memcpy(s_records, s_early, sizeof(s_early));
    s_storage_ready = true;
  }
  pbl_irq_unlock();
}

unsigned t5ai_sleep_profile_timers_untracked(void) {
  return s_untracked;
}

static TimerRecord *prv_record(struct pbl_timer *timer, bool allocate) {
  TimerRecord *records = s_storage_ready ? s_records : s_early;
  const unsigned limit = s_storage_ready ? PROFILE_TIMER_RECORD_LIMIT : PROFILE_EARLY_RECORD_LIMIT;
  const uint64_t now = pbl_uptime_ticks();
  TimerRecord *available = nullptr;
  for (unsigned i = 0; i < limit; ++i) {
    if (records[i].timer == timer)
      return &records[i];
    if ((!records[i].timer || records[i].expires <= now) && !available)
      available = &records[i];
  }
  return allocate ? available : nullptr;
}

void __real_pbl_timer_start(struct pbl_timer *, pbl_timeout_t);
void __wrap_pbl_timer_start(struct pbl_timer *timer, pbl_timeout_t delay) {
  pbl_irq_lock();
  __real_pbl_timer_start(timer, delay);
  TimerRecord *record = prv_record(timer, true);
  if (record) {
    *record = (TimerRecord){
      .timer = timer,
      .expires = pbl_uptime_ticks() + pbl_timer_remaining(timer),
      .period = delay.ticks,
      .callback = (uintptr_t)timer->fn,
    };
  } else {
    ++s_untracked;
  }
  pbl_irq_unlock();
}

void __real_pbl_timer_stop(struct pbl_timer *);
void __wrap_pbl_timer_stop(struct pbl_timer *timer) {
  pbl_irq_lock();
  __real_pbl_timer_stop(timer);
  TimerRecord *record = prv_record(timer, false);
  if (record)
    *record = (TimerRecord){0};
  pbl_irq_unlock();
}

// Caller holds IRQ lock. Expired timers may have been freed: never dereference them.
unsigned t5ai_sleep_profile_timer_candidates(T5AISleepTimerCandidate *out) {
  const uint64_t now = pbl_uptime_ticks();
  unsigned count = 0;
  const TimerRecord *records = s_storage_ready ? s_records : s_early;
  const unsigned limit = s_storage_ready ? PROFILE_TIMER_RECORD_LIMIT : PROFILE_EARLY_RECORD_LIMIT;
  for (unsigned i = 0; i < limit; ++i) {
    const TimerRecord *record = &records[i];
    if (!record->timer || record->expires <= now)
      continue;
    const uint32_t remaining = pbl_ticks_to_ms(record->expires - now);
    unsigned at = count;
    if (at == T5AI_SLEEP_PROFILE_CANDIDATES) {
      --at;
      if (out[at].remaining_ms <= remaining)
        continue;
    } else {
      ++count;
    }
    while (at && out[at - 1].remaining_ms > remaining) {
      out[at] = out[at - 1];
      --at;
    }
    out[at] = (T5AISleepTimerCandidate){
      .id = (uint32_t)(uintptr_t)record->timer,
      .remaining_ms = remaining,
      .period_ms = pbl_ticks_to_ms(record->period),
      .callback = record->callback,
      .context = (uintptr_t)record->timer,
    };
  }
  return count;
}
