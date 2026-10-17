# SPDX-License-Identifier: Apache-2.0
"""Exercise the real T5-only timing wrappers with deterministic blocking calls."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUB = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int status_t;
typedef uint32_t FlashAddress;
typedef int pbl_timeout_t;
struct pbl_mutex { struct pbl_thread *owner; };
enum pbl_analytics_key { METRIC };
struct pbl_thread { uint32_t id; struct { uint32_t run_time, switched_in_at; } backend; };
struct pbl_thread *pbl_thread_current(void);
uint32_t pbl_uptime_ticks(void);
uint64_t rtc_get_ticks(void);
#define RTC_TICKS_HZ 1000u
uint32_t pbl_ticks_to_ms(uint32_t ticks);
void pbl_irq_lock(void);
void pbl_irq_unlock(void);
#define LOG_LEVEL_DEBUG 4
#define PBL_LOG_MODULE_DEFINE(...)
void test_log(const char *, ...);
#define PBL_LOG_DBG(...) test_log(__VA_ARGS__)
#define OP_FLAG_OVERWRITE 8
#define OP_FLAG_READ 1
#define OP_FLAG_WRITE 2
typedef bool (*SettingsFileRewriteFilterCallback)(void *, size_t, void *, size_t, void *);
typedef struct { char *name; struct { int fd; } iter; } SettingsFile;
typedef struct { SettingsFile file; struct pbl_mutex mutex; char *name; } TimelineItemStorage;
'''
HARNESS = r'''
#include "stub.h"
#include <assert.h>
#include <stdarg.h>
#include "soc/t5ai/reminder_profile.c"
static struct pbl_thread owner, other;
static struct pbl_thread *current = &owner;
static uint32_t now;
static unsigned logs, filters, real_calls;
static bool fail_open, db_held;
static const char *db_name = "reminderdb";
struct pbl_thread *pbl_thread_current(void) { return current; }
uint32_t pbl_uptime_ticks(void) { return now; }
uint64_t rtc_get_ticks(void) { return (uint64_t)now * 3; }
uint32_t pbl_ticks_to_ms(uint32_t t) { return t; }
void pbl_irq_lock(void) {}
void pbl_irq_unlock(void) {}
void test_log(const char *format, ...) { (void)format; assert(!db_held); logs++; }
static void wait(unsigned n) { now += n; current->backend.switched_in_at += n; }
int __real_pfs_open(const char *name, uint8_t flags, uint8_t type, size_t size) {
  assert(!strcmp(name, db_name) && type == 3 && size == 40960);
  wait(flags & OP_FLAG_OVERWRITE ? 1000 : 400);
  current->backend.run_time += 25;
  return fail_open ? -1 : 22;
}
status_t __real_pfs_close(int fd) { wait(fd == 11 ? 10 : 300); return 0; }
void __real_flash_read_bytes(uint8_t *out, uint32_t address, uint32_t size) {
  assert(address == 100 && size == 7); memset(out, 0, size); wait(2000);
}
void __real_flash_write_bytes(const uint8_t *in, uint32_t address, uint32_t size) {
  assert(in[0] == 0 && address == 100 && size == 7); struct pbl_mutex mutex = {.owner = &other};
  other.id = 42;
  assert(__wrap_pbl_mutex_lock_lr(&mutex, 123, 456) == 0);
  __wrap_sys_pbl_analytics_add(METRIC, 7);
  assert(__wrap_flash_impl_write_page_begin(in, address, size) == 7);
  __wrap_pbl_mutex_unlock(&mutex);
}
void __real_flash_erase_sector_blocking(uint32_t address) { assert(address == 100); wait(250); }
int __real_pbl_mutex_lock_lr(struct pbl_mutex *mutex, pbl_timeout_t timeout, uintptr_t lr) {
  if (mutex == s_profile.db_mutex) { wait(50); db_held = true; return 0; }
  assert(mutex->owner == &other && timeout == 123 && lr == 456); wait(300); return 0;
}
void __real_sys_pbl_analytics_add(enum pbl_analytics_key key, int32_t amount) {
  assert(key == METRIC && amount == 7); wait(100);
}
int __real_flash_impl_write_page_begin(const void *buf, FlashAddress addr, size_t len) {
  assert(buf && addr == 100 && len == 7); wait(100); return 7;
}
void __real_pbl_mutex_unlock(struct pbl_mutex *mutex) {
  assert(mutex); if (mutex == s_profile.db_mutex) { db_held = false; return; } wait(400);
}
static bool filter(void *key, size_t kl, void *value, size_t vl, void *context) {
  assert(key == value && kl == 7 && vl == 7 && context == &filters); filters++; current->backend.run_time += 35; return filters % 2;
}
status_t __real_settings_file_rewrite_filtered(SettingsFile *file,
                                               SettingsFileRewriteFilterCallback cb, void *ctx) {
  real_calls++;
  if (strcmp(file->name, db_name)) return -9;
  assert(__wrap_pfs_open(file->name, OP_FLAG_OVERWRITE, 3, 40960) == (fail_open ? -1 : 22));
  if (fail_open) return -1;
  uint8_t buffer[7];
  __wrap_flash_read_bytes(buffer, 100, sizeof(buffer));
  assert(cb(buffer, 7, buffer, 7, ctx));
  assert(!cb(buffer, 7, buffer, 7, ctx));
  __wrap_flash_write_bytes(buffer, 100, sizeof(buffer));
  // Another task's operations must not inflate the owner's flash totals.
  current = &other;
  __wrap_flash_read_bytes(buffer, 100, sizeof(buffer));
  current = &owner;
  owner.backend.switched_in_at += 2000; // Scheduler excludes time spent on other task.
  __wrap_pfs_close(11);
  __wrap_pfs_close(22);
  __wrap_flash_erase_sector_blocking(100);
  __wrap_pfs_open(file->name, OP_FLAG_READ | OP_FLAG_WRITE, 3, 40960);
  return 0;
}
status_t __real_timeline_item_storage_flush(TimelineItemStorage *storage) {
  unsigned before = logs;
  __wrap_pbl_mutex_lock_lr(&storage->mutex, 123, 456);
  status_t result = __wrap_settings_file_rewrite_filtered(&storage->file, filter, &filters);
  assert(logs == before); // No summaries under DB lock, even on failure.
  __wrap_pbl_mutex_unlock(&storage->mutex);
  return result;
}
int main(void) {
  TimelineItemStorage storage = {.name = "reminderdb", .file = {.name = "reminderdb", .iter.fd = 11}};
  assert(__wrap_timeline_item_storage_flush(&storage) == 0);
  assert(!s_profile.owner && logs == 19 && filters == 2 && real_calls == 1);
  assert(s_profile.records == 2 && s_profile.kept == 1);
  assert(s_profile.phases[CREATE].ticks == 3000);
  assert(s_profile.phases[COPY].ticks == 14700);
  assert(s_profile.phases[COPY].ops[READ].calls == 1);
  assert(s_profile.phases[COPY].ops[READ].bytes == 7);
  assert(s_profile.phases[COPY].ops[READ].ticks == 6000);
  assert(s_profile.phases[COPY].ops[WRITE].ticks == 2700);
  assert(s_profile.phases[COPY].flash_unlock == 1200 && s_profile.max_unlock == 1200);
  assert(s_profile.phases[COPY].write_wait == 900);
  assert(s_profile.phases[COPY].analytics == 300);
  assert(s_profile.phases[COPY].driver_write == 300);
  assert(s_profile.max_wait == 900 && s_profile.wait_owner == 42);
  assert(s_profile.phases[COPY].cpu == 70);
  assert(s_profile.phases[COPY].mutex_wait == 900);
  assert(s_profile.phases[CLOSE_OLD].ticks == 30);
  assert(s_profile.phases[COMMIT].ticks == 1650);
  assert(s_profile.phases[COMMIT].ops[ERASE].ticks == 750);
  assert(s_profile.phases[REOPEN].ticks == 1200);
  assert(s_profile.phases[CREATE].cpu == 25);
  assert(s_profile.db_wait == 150 && s_profile.db_hold == 20580);
  storage.name = storage.file.name = "pindb"; db_name = "pindb";
  assert(__wrap_timeline_item_storage_flush(&storage) == 0);
  assert(logs == 38 && real_calls == 2);
  fail_open = true;
  assert(__wrap_timeline_item_storage_flush(&storage) == -1);
  assert(!s_profile.owner && logs == 57 && s_profile.records == 0);
  assert(s_profile.phases[CREATE].ticks == 3000);
  return 0;
}
'''


class ReminderProfileTest(unittest.TestCase):
    def test_phases_accounting_filter_failure_and_task_isolation(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "stub.h").write_text(STUB)
            for header in ("kernel/mutex", "drivers/flash/flash_impl", "services/analytics/analytics", "drivers/rtc", "drivers/flash", "kernel/irq", "kernel/time", "kernel/sched", "kernel/thread",
                           "logging/logging", "services/filesystem/pfs",
                           "services/settings/settings_file", "services/blob_db/timeline_item_storage"):
                path = directory / "pbl" / (header + ".h")
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "stub.h"\n')
            (directory / "test.c").write_text(HARNESS)
            binary = directory / "test"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(directory), "-I", str(ROOT),
                            str(directory / "test.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
