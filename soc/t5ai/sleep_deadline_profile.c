/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/drivers/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/sleep_deadline_profile.h>

// Diagnostic estimates only: repeating callbacks are rearmed inside the stock manager.
#define PROFILE_TIMER_RECORD_LIMIT      64u
#define PROFILE_EARLY_RECORD_LIMIT      8u
#define PROFILE_TIMER_REPEATING         (1u << 31)
#define PROFILE_TIMER_PERIOD_MASK       (PROFILE_TIMER_REPEATING - 1u)
#define PROFILE_MILLISECONDS_PER_SECOND 1000u
typedef struct {
  TimerID id;
  NewTimerCallback callback;
  uint32_t start_ms, period_flags;
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

static uint32_t prv_now_ms(void) {
  return (uint32_t)(t5ai_rtc_get_ticks() * PROFILE_MILLISECONDS_PER_SECOND / 1000u);
}

extern bool __real_new_timer_start(TimerID id, uint32_t ms, NewTimerCallback cb, void *context,
                                   uint32_t flags);
extern bool __real_new_timer_stop(TimerID id);
extern void __real_new_timer_delete(TimerID id);

static TimerRecord *prv_record(TimerID id, bool allocate) {
  TimerRecord *records = s_storage_ready ? s_records : s_early;
  const unsigned limit = s_storage_ready ? PROFILE_TIMER_RECORD_LIMIT : PROFILE_EARLY_RECORD_LIMIT;
  TimerRecord *free_record = NULL;
  for (unsigned i = 0; i < limit; ++i) {
    if (records[i].id == id)
      return &records[i];
    if (!records[i].id && !free_record)
      free_record = &records[i];
  }
  return allocate ? free_record : NULL;
}

bool __wrap_new_timer_start(TimerID id, uint32_t ms, NewTimerCallback cb, void *context,
                            uint32_t flags) {
  const uint32_t start = prv_now_ms();
  const bool result = __real_new_timer_start(id, ms, cb, context, flags);
  if (result) {
    pbl_irq_lock();
    TimerRecord *record = prv_record(id, true);
    if (record && ms <= PROFILE_TIMER_PERIOD_MASK)
      *record = (TimerRecord){
        .id = id,
        .callback = cb,
        .start_ms = start,
        .period_flags =
            (ms ? ms : 1u) | ((flags & TIMER_START_FLAG_REPEATING) ? PROFILE_TIMER_REPEATING : 0u)
      };
    else {
      if (record)
        *record = (TimerRecord){0};
      ++s_untracked;
    }
    pbl_irq_unlock();
  }
  return result;
}

bool __wrap_new_timer_stop(TimerID id) {
  const bool result = __real_new_timer_stop(id);
  pbl_irq_lock();
  TimerRecord *record = prv_record(id, false);
  if (record)
    record->period_flags = 0u;
  pbl_irq_unlock();
  return result;
}

void __wrap_new_timer_delete(TimerID id) {
  __real_new_timer_delete(id);
  pbl_irq_lock();
  TimerRecord *record = prv_record(id, false);
  if (record)
    *record = (TimerRecord){0};
  pbl_irq_unlock();
}

// Caller holds IRQ lock. No timer APIs, callbacks, logging or scheduler mutations.
unsigned t5ai_sleep_profile_timer_candidates(T5AISleepTimerCandidate *out) {
  const uint32_t now = prv_now_ms();
  unsigned count = 0u;
  TimerRecord *records = s_storage_ready ? s_records : s_early;
  const unsigned limit = s_storage_ready ? PROFILE_TIMER_RECORD_LIMIT : PROFILE_EARLY_RECORD_LIMIT;
  for (unsigned i = 0; i < limit; ++i) {
    const TimerRecord *record = &records[i];
    const uint32_t period = record->period_flags & PROFILE_TIMER_PERIOD_MASK;
    const bool repeating = (record->period_flags & PROFILE_TIMER_REPEATING) != 0u;
    if (!record->id || !period)
      continue;
    const uint32_t elapsed_ms = now - record->start_ms;
    if (!repeating && elapsed_ms >= period)
      continue;
    const uint32_t remaining = period - (repeating ? elapsed_ms % period : elapsed_ms);
    unsigned at = count;
    if (at == T5AI_SLEEP_PROFILE_CANDIDATES) {
      at--;
      if (out[at].remaining_ms <= remaining)
        continue;
    } else {
      ++count;
    }
    while (at && out[at - 1u].remaining_ms > remaining) {
      out[at] = out[at - 1u];
      --at;
    }
    out[at] = (T5AISleepTimerCandidate){
      .id = record->id,
      .remaining_ms = remaining,
      .period_ms = period,
      .callback = (uintptr_t)record->callback,
      .context = 0u
    };
  }
  return count;
}
