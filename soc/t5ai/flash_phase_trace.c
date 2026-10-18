/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/time.h>
#include <inttypes.h>
#include <string.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/services/filesystem/pfs.h>
#include <pbl/soc/t5ai/io_trace.h>
#include <pbl/soc/t5ai/rtc.h>

PBL_LOG_MODULE_DEFINE(t5ai_flash_phase_trace, LOG_LEVEL_DEBUG);
enum {
  TEMP_OPEN,
  OPEN,
  CLOSE,
  PREFS_LOCK,
  ALERTS_LOCK,
  MUTEX_LOCK,
  MUTEX_UNLOCK,
  FLASH_READ,
  FLASH_WRITE,
  FLASH_ERASE,
  PREP_COUNT
};
typedef struct {
  uint32_t count, ticks, aon, max_aon;
} Phase;
static Phase s_phases[PREP_COUNT];
static uint32_t s_owner, s_start, s_aon_start;
static uintptr_t s_wait_mutex;
static uint32_t s_wait_owner, s_wait_max;
static bool prv_ours(void) {
  return s_owner != 0 && s_owner == pbl_thread_current()->id;
}
void t5ai_flash_phase_trace_begin(void) {
  pbl_irq_lock();
  if (!s_owner) {
    memset(s_phases, 0, sizeof(s_phases));
    s_wait_mutex = s_wait_owner = s_wait_max = 0;
    s_owner = pbl_thread_current()->id;
    s_start = pbl_uptime_ticks();
    s_aon_start = T5AI_AON_RTC_COUNT_LOW;
  }
  pbl_irq_unlock();
}
static void prv_record(unsigned stage, uint32_t ticks, uint32_t aon) {
  if (!prv_ours())
    return;
  Phase *p = &s_phases[stage];
  uint32_t elapsed = T5AI_AON_RTC_COUNT_LOW - aon;
  pbl_irq_lock();
  ++p->count;
  p->ticks += pbl_uptime_ticks() - ticks;
  p->aon += elapsed;
  if (elapsed > p->max_aon)
    p->max_aon = elapsed;
  pbl_irq_unlock();
}
void t5ai_flash_phase_trace_dump(void) {
  for (unsigned i = 0; i < PREP_COUNT; ++i) {
    pbl_irq_lock();
    Phase p = s_phases[i];
    pbl_irq_unlock();
    PBL_LOG_SYNC_WRN("T5PREP stage=%u count=%" PRIu32 " ticks=%" PRIu32 " aon=%" PRIu32
                     " max_aon=%" PRIu32,
                     i, p.count, p.ticks, p.aon, p.max_aon);
  }
  PBL_LOG_SYNC_WRN("T5PREP mutex=%" PRIxPTR " owner=%" PRIu32 " wait_aon=%" PRIu32, s_wait_mutex,
                   s_wait_owner, s_wait_max);
}
void t5ai_flash_phase_trace_end(void) {
  if (!prv_ours())
    return;
  uint32_t ticks = pbl_uptime_ticks() - s_start;
  uint32_t aon = T5AI_AON_RTC_COUNT_LOW - s_aon_start;
  t5ai_flash_phase_trace_dump();
  PBL_LOG_SYNC_WRN("T5PREP done ticks=%" PRIu32 " aon=%" PRIu32, ticks, aon);
  s_owner = 0;
}

int __real_pfs_open(const char *, uint8_t, uint8_t, size_t);
int __wrap_pfs_open(const char *name, uint8_t flags, uint8_t type, size_t size) {
  uint32_t ticks = pbl_uptime_ticks(), aon = T5AI_AON_RTC_COUNT_LOW;
  int result = __real_pfs_open(name, flags, type, size);
  prv_record(flags & OP_FLAG_OVERWRITE ? TEMP_OPEN : OPEN, ticks, aon);
  return result;
}
status_t __real_pfs_close(int);
status_t __wrap_pfs_close(int fd) {
  uint32_t ticks = pbl_uptime_ticks(), aon = T5AI_AON_RTC_COUNT_LOW;
  status_t result = __real_pfs_close(fd);
  prv_record(CLOSE, ticks, aon);
  return result;
}
int __real_pbl_mutex_lock_lr(struct pbl_mutex *, pbl_timeout_t, uintptr_t);
int __wrap_pbl_mutex_lock_lr(struct pbl_mutex *mutex, pbl_timeout_t timeout, uintptr_t lr) {
  uint32_t ticks = pbl_uptime_ticks(), aon = T5AI_AON_RTC_COUNT_LOW, owner = 0;
  if (prv_ours()) {
    pbl_irq_lock();
    if (mutex->owner)
      owner = mutex->owner->id;
    pbl_irq_unlock();
  }
  int result = __real_pbl_mutex_lock_lr(mutex, timeout, lr);
  uint32_t elapsed = T5AI_AON_RTC_COUNT_LOW - aon;
  if (prv_ours() && elapsed > s_wait_max) {
    s_wait_max = elapsed;
    s_wait_mutex = (uintptr_t)mutex;
    s_wait_owner = owner;
  }
  prv_record(MUTEX_LOCK, ticks, aon);
  return result;
}
#define WRAP_VOID0(name, stage)                                        \
  void __real_##name(void);                                            \
  void __wrap_##name(void) {                                           \
    uint32_t ticks = pbl_uptime_ticks(), aon = T5AI_AON_RTC_COUNT_LOW; \
    __real_##name();                                                   \
    prv_record(stage, ticks, aon);                                     \
  }
WRAP_VOID0(prefs_private_lock, PREFS_LOCK)
WRAP_VOID0(alerts_preferences_lock, ALERTS_LOCK)
void __real_pbl_mutex_unlock(struct pbl_mutex *);
void __wrap_pbl_mutex_unlock(struct pbl_mutex *mutex) {
  uint32_t ticks = pbl_uptime_ticks(), aon = T5AI_AON_RTC_COUNT_LOW;
  __real_pbl_mutex_unlock(mutex);
  prv_record(MUTEX_UNLOCK, ticks, aon);
}
#define WRAP_FLASH(name, qualifier, stage)                                         \
  void __real_##name(qualifier uint8_t *, uint32_t, uint32_t);                     \
  void __wrap_##name(qualifier uint8_t *buffer, uint32_t address, uint32_t size) { \
    uint32_t ticks = pbl_uptime_ticks(), aon = T5AI_AON_RTC_COUNT_LOW;             \
    __real_##name(buffer, address, size);                                          \
    prv_record(stage, ticks, aon);                                                 \
  }
WRAP_FLASH(flash_read_bytes, , FLASH_READ)
WRAP_FLASH(flash_write_bytes, const, FLASH_WRITE)
void __real_flash_erase_sector_blocking(uint32_t);
void __wrap_flash_erase_sector_blocking(uint32_t address) {
  uint32_t ticks = pbl_uptime_ticks(), aon = T5AI_AON_RTC_COUNT_LOW;
  __real_flash_erase_sector_blocking(address);
  prv_record(FLASH_ERASE, ticks, aon);
}
