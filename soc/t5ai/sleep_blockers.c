/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/irq.h>
#include <pbl/soc/t5ai/sleep.h>

static uint16_t s_owners[T5AISleepOwner_Count];
static T5AISleepBlockers s_blockers;

static void prv_advance_epoch(void) {
  if (s_blockers.epoch == UINT32_MAX)
    s_blockers.faulted = true;
  else
    ++s_blockers.epoch;
}

void t5ai_sleep_block(T5AISleepOwner owner) {
  pbl_irq_lock();
  if ((unsigned)owner >= T5AISleepOwner_Count || s_owners[owner] == UINT16_MAX) {
    s_blockers.faulted = true;
  } else {
    ++s_owners[owner];
    s_blockers.owners |= 1u << owner;
    prv_advance_epoch();
  }
  pbl_irq_unlock();
}

void t5ai_sleep_unblock(T5AISleepOwner owner) {
  pbl_irq_lock();
  if ((unsigned)owner >= T5AISleepOwner_Count || s_owners[owner] == 0u) {
    s_blockers.faulted = true;
  } else {
    if (--s_owners[owner] == 0u)
      s_blockers.owners &= ~(1u << owner);
    prv_advance_epoch();
  }
  pbl_irq_unlock();
}

T5AISleepBlockers t5ai_sleep_blockers(void) {
  pbl_irq_lock();
  const T5AISleepBlockers state = s_blockers;
  pbl_irq_unlock();
  return state;
}

bool t5ai_sleep_blockers_match(uint32_t epoch) {
  return !s_blockers.faulted && s_blockers.owners == 0u && s_blockers.epoch == epoch;
}

void t5ai_sleep_fault(void) {
  pbl_irq_lock();
  s_blockers.faulted = true;
  pbl_irq_unlock();
}
