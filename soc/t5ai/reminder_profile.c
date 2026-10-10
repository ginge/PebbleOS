/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/flash.h>
#include <pbl/drivers/flash/flash_impl.h>
#include <pbl/kernel/mutex.h>
#include <pbl/services/analytics/analytics.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/services/filesystem/pfs.h>
#include <pbl/services/settings/settings_file.h>

#include <pbl/services/blob_db/timeline_item_storage.h>

#include <string.h>

PBL_LOG_MODULE_DEFINE(t5ai_reminder_profile, LOG_LEVEL_DEBUG);

enum {
  CREATE,
  COPY,
  CLOSE_OLD,
  COMMIT,
  REOPEN,
  PHASE_COUNT
};
enum {
  READ,
  WRITE,
  ERASE,
  OP_COUNT
};
typedef struct {
  uint32_t calls, bytes, ticks;
} Operation;
typedef struct {
  uint32_t ticks, cpu, write_wait, mutex_wait, driver_write, analytics, flash_unlock;
  Operation ops[OP_COUNT];
} Phase;
static struct {
  struct pbl_thread *owner;
  struct pbl_mutex *db_mutex;
  const char *name;
  bool recording, complete, db_locked;
  status_t result;
  uint32_t wall_ms, kernel_ms, db_wait, db_since, db_hold;
  Phase phases[PHASE_COUNT];
  unsigned phase;
  uint32_t since, cpu_since, records, kept;
  int old_fd;
  bool in_write, in_read;
  uint32_t max_unlock;
  uintptr_t unlock_mutex;
  uint32_t max_wait, wait_owner;
  uintptr_t wait_mutex;
  SettingsFileRewriteFilterCallback filter;
  void *context;
} s_profile;
static uint32_t s_skipped;

static uint32_t prv_wall(void) {
  return (uint32_t)rtc_get_ticks();
}

static uint32_t prv_cpu(void) {
  pbl_irq_lock();
  struct pbl_thread *thread = pbl_thread_current();
  uint32_t ticks = thread->backend.run_time + pbl_uptime_ticks() - thread->backend.switched_in_at;
  pbl_irq_unlock();
  return ticks;
}

static bool prv_owner(void) {
  return s_profile.owner && s_profile.owner == pbl_thread_current();
}
static bool prv_active(void) {
  return prv_owner() && s_profile.recording;
}

static void prv_phase(unsigned phase) {
  uint32_t now = prv_wall();
  uint32_t cpu = prv_cpu();
  Phase *previous = &s_profile.phases[s_profile.phase];
  previous->ticks += now - s_profile.since;
  previous->cpu += cpu - s_profile.cpu_since;
  s_profile.phase = phase;
  s_profile.since = now;
  s_profile.cpu_since = cpu;
}

static bool prv_filter(void *key, size_t key_len, void *value, size_t value_len, void *context) {
  (void)context;
  s_profile.records++;
  bool keep =
      !s_profile.filter || s_profile.filter(key, key_len, value, value_len, s_profile.context);
  s_profile.kept += keep;
  return keep;
}

status_t __real_settings_file_rewrite_filtered(SettingsFile *, SettingsFileRewriteFilterCallback,
                                               void *);
status_t __wrap_settings_file_rewrite_filtered(SettingsFile *file,
                                               SettingsFileRewriteFilterCallback filter,
                                               void *context) {
  if (!prv_owner() || s_profile.recording || s_profile.complete ||
      strcmp(file->name, s_profile.name)) {
    return __real_settings_file_rewrite_filtered(file, filter, context);
  }
  s_profile.recording = true;
  s_profile.old_fd = file->iter.fd;
  s_profile.filter = filter;
  s_profile.context = context;
  s_profile.since = prv_wall();
  uint32_t wall_start = s_profile.since;
  uint32_t kernel_start = pbl_uptime_ticks();
  s_profile.cpu_since = prv_cpu();
  s_profile.owner = pbl_thread_current();
  status_t result = __real_settings_file_rewrite_filtered(file, prv_filter, NULL);
  prv_phase(s_profile.phase);
  s_profile.recording = false;
  s_profile.complete = true;
  s_profile.result = result;
  s_profile.wall_ms = (prv_wall() - wall_start) * (1000u / RTC_TICKS_HZ);
  s_profile.kernel_ms = pbl_ticks_to_ms(pbl_uptime_ticks() - kernel_start);
  return result;
}

static void prv_dump(void) {
  if (!s_profile.complete)
    return;
  uint32_t wall_ms = s_profile.wall_ms, kernel_ms = s_profile.kernel_ms;
  status_t result = s_profile.result;
  PBL_LOG_DBG("T5DB name=%s db_wait=%lu db_hold=%lu skipped=%lu", s_profile.name,
              (unsigned long)s_profile.db_wait, (unsigned long)s_profile.db_hold,
              (unsigned long)s_skipped);
  PBL_LOG_DBG("T5DB profile result=%ld records=%lu kept=%lu wall=%lu kernel=%lu", (long)result,
              (unsigned long)s_profile.records, (unsigned long)s_profile.kept,
              (unsigned long)wall_ms, (unsigned long)kernel_ms);
  for (unsigned i = 0; i < PHASE_COUNT; i++) {
    Phase *p = &s_profile.phases[i];
    PBL_LOG_DBG("T5DB phase=%u wall=%lu cpu=%lu R=%lu/%lu/%lu", i,
                (unsigned long)(p->ticks * (1000u / RTC_TICKS_HZ)),
                (unsigned long)pbl_ticks_to_ms(p->cpu), (unsigned long)p->ops[READ].calls,
                (unsigned long)p->ops[READ].bytes,
                (unsigned long)(p->ops[READ].ticks * (1000u / RTC_TICKS_HZ)));
    PBL_LOG_DBG("T5DB phase=%u W=%lu/%lu/%lu E=%lu/%lu", i, (unsigned long)p->ops[WRITE].calls,
                (unsigned long)p->ops[WRITE].bytes,
                (unsigned long)(p->ops[WRITE].ticks * (1000u / RTC_TICKS_HZ)),
                (unsigned long)p->ops[ERASE].calls,
                (unsigned long)(p->ops[ERASE].ticks * (1000u / RTC_TICKS_HZ)));
    PBL_LOG_DBG("T5DB phase=%u write_lock=%lu driver=%lu analytics=%lu unlock=%lu mutex_wait=%lu",
                i, (unsigned long)p->write_wait, (unsigned long)p->driver_write,
                (unsigned long)p->analytics, (unsigned long)p->flash_unlock,
                (unsigned long)p->mutex_wait);
  }
  PBL_LOG_DBG("T5DB wait mutex=%lx owner=%lu ms=%lu", (unsigned long)s_profile.wait_mutex,
              (unsigned long)s_profile.wait_owner, (unsigned long)s_profile.max_wait);
  PBL_LOG_DBG("T5DB unlock mutex=%lx ms=%lu", (unsigned long)s_profile.unlock_mutex,
              (unsigned long)s_profile.max_unlock);
}

int __real_pfs_open(const char *, uint8_t, uint8_t, size_t);
int __wrap_pfs_open(const char *name, uint8_t flags, uint8_t type, size_t size) {
  bool active = prv_active();
  if (active) {
    prv_phase((flags & OP_FLAG_OVERWRITE) ? CREATE : REOPEN);
  }
  int result = __real_pfs_open(name, flags, type, size);
  if (active && (flags & OP_FLAG_OVERWRITE)) {
    prv_phase(COPY);
  }
  return result;
}

status_t __real_pfs_close(int);
status_t __wrap_pfs_close(int fd) {
  if (prv_active()) {
    prv_phase(fd == s_profile.old_fd ? CLOSE_OLD : COMMIT);
  }
  return __real_pfs_close(fd);
}

static void prv_record(unsigned operation, uint32_t bytes, uint32_t since) {
  Operation *op = &s_profile.phases[s_profile.phase].ops[operation];
  op->calls++;
  op->bytes += bytes;
  op->ticks += prv_wall() - since;
}

void __real_flash_read_bytes(uint8_t *, uint32_t, uint32_t);
void __wrap_flash_read_bytes(uint8_t *buffer, uint32_t address, uint32_t size) {
  bool active = prv_active();
  uint32_t since = active ? prv_wall() : 0;
  if (active)
    s_profile.in_read = true;
  __real_flash_read_bytes(buffer, address, size);
  if (active)
    s_profile.in_read = false;
  if (active)
    prv_record(READ, size, since);
}

void __real_flash_write_bytes(const uint8_t *, uint32_t, uint32_t);
void __wrap_flash_write_bytes(const uint8_t *buffer, uint32_t address, uint32_t size) {
  bool active = prv_active();
  uint32_t since = active ? prv_wall() : 0;
  if (active)
    s_profile.in_write = true;
  __real_flash_write_bytes(buffer, address, size);
  if (active)
    s_profile.in_write = false;
  if (active)
    prv_record(WRITE, size, since);
}

void __real_flash_erase_sector_blocking(uint32_t);
void __wrap_flash_erase_sector_blocking(uint32_t address) {
  bool active = prv_active();
  uint32_t since = active ? prv_wall() : 0;
  __real_flash_erase_sector_blocking(address);
  if (active)
    prv_record(ERASE, 0, since);
}

int __real_pbl_mutex_lock_lr(struct pbl_mutex *, pbl_timeout_t, uintptr_t);
int __wrap_pbl_mutex_lock_lr(struct pbl_mutex *mutex, pbl_timeout_t timeout, uintptr_t lr) {
  bool active = prv_active();
  uint32_t since = active ? prv_wall() : 0;
  uint32_t owner = 0;
  if (active) {
    pbl_irq_lock();
    if (mutex->owner)
      owner = mutex->owner->id;
    pbl_irq_unlock();
  }
  bool db = prv_owner() && mutex == s_profile.db_mutex;
  uint32_t db_since = db ? prv_wall() : 0;
  int result = __real_pbl_mutex_lock_lr(mutex, timeout, lr);
  if (db && result == 0) {
    s_profile.db_wait += prv_wall() - db_since;
    s_profile.db_since = prv_wall();
    s_profile.db_locked = true;
  }
  if (active) {
    uint32_t elapsed = prv_wall() - since;
    s_profile.phases[s_profile.phase].mutex_wait += elapsed;
    if (s_profile.in_write)
      s_profile.phases[s_profile.phase].write_wait += elapsed;
    if (elapsed > s_profile.max_wait) {
      s_profile.max_wait = elapsed;
      s_profile.wait_mutex = (uintptr_t)mutex;
      s_profile.wait_owner = owner;
    }
  }
  return result;
}

int __real_flash_impl_write_page_begin(const void *, FlashAddress, size_t);
int __wrap_flash_impl_write_page_begin(const void *buffer, FlashAddress address, size_t size) {
  bool active = prv_active();
  uint32_t since = active ? prv_wall() : 0;
  int result = __real_flash_impl_write_page_begin(buffer, address, size);
  if (active)
    s_profile.phases[s_profile.phase].driver_write += prv_wall() - since;
  return result;
}

void __real_sys_pbl_analytics_add(enum pbl_analytics_key, int32_t);
void __wrap_sys_pbl_analytics_add(enum pbl_analytics_key key, int32_t amount) {
  bool active = prv_active();
  uint32_t since = active ? prv_wall() : 0;
  __real_sys_pbl_analytics_add(key, amount);
  if (active)
    s_profile.phases[s_profile.phase].analytics += prv_wall() - since;
}

void __real_pbl_mutex_unlock(struct pbl_mutex *);
void __wrap_pbl_mutex_unlock(struct pbl_mutex *mutex) {
  bool active = prv_active() && (s_profile.in_write || s_profile.in_read);
  uint32_t since = active ? prv_wall() : 0;
  if (prv_owner() && mutex == s_profile.db_mutex && s_profile.db_locked) {
    s_profile.db_hold += prv_wall() - s_profile.db_since;
    s_profile.db_locked = false;
  }
  __real_pbl_mutex_unlock(mutex);
  if (active) {
    uint32_t elapsed = prv_wall() - since;
    s_profile.phases[s_profile.phase].flash_unlock += elapsed;
    if (elapsed > s_profile.max_unlock) {
      s_profile.max_unlock = elapsed;
      s_profile.unlock_mutex = (uintptr_t)mutex;
    }
  }
}

// Own the bounded slot through printing; simultaneous/nested rewrites pass through.
static status_t prv_storage(TimelineItemStorage *storage,
                            status_t (*operation)(TimelineItemStorage *)) {
  pbl_irq_lock();
  bool claim =
      !s_profile.owner && (!strcmp(storage->name, "pindb") || !strcmp(storage->name, "reminderdb"));
  if (claim) {
    memset(&s_profile, 0, sizeof(s_profile));
    s_profile.owner = pbl_thread_current();
    s_profile.db_mutex = &storage->mutex;
    s_profile.name = storage->name;
  } else if (s_profile.owner) {
    s_skipped++;
  }
  pbl_irq_unlock();
  status_t result = operation(storage);
  if (claim) {
    // The shared storage operation has returned after releasing its DB mutex.
    prv_dump();
    pbl_irq_lock();
    s_profile.owner = NULL;
    pbl_irq_unlock();
  }
  return result;
}
status_t __real_timeline_item_storage_flush(TimelineItemStorage *);
status_t __wrap_timeline_item_storage_flush(TimelineItemStorage *storage) {
  return prv_storage(storage, __real_timeline_item_storage_flush);
}
