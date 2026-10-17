/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/time.h>
#include <inttypes.h>
#include <string.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/services/settings/settings_file.h>
#include <pbl/soc/t5ai/io_trace.h>

PBL_LOG_MODULE_DEFINE(t5ai_settings_sync_trace, LOG_LEVEL_DEBUG);

typedef struct {
  uint32_t count, total, max, bins[7];
} Timing;
static Timing s_io[T5IO_COUNT];
static Timing s_record[5];
static uint32_t s_owner, s_file, s_start, s_records, s_bytes, s_stage, s_stage_start;
static char s_name[24];
static uint32_t s_record_start, s_between_start;
static bool s_in_record;

static void prv_sample(Timing *t, uint32_t ticks) {
  unsigned bin = ticks == 0   ? 0
                 : ticks == 1 ? 1
                 : ticks < 5  ? 2
                 : ticks < 10 ? 3
                 : ticks < 20 ? 4
                 : ticks < 50 ? 5
                              : 6;
  __atomic_fetch_add(&t->count, 1u, __ATOMIC_RELAXED);
  __atomic_fetch_add(&t->total, ticks, __ATOMIC_RELAXED);
  __atomic_fetch_add(&t->bins[bin], 1u, __ATOMIC_RELAXED);
  uint32_t old = __atomic_load_n(&t->max, __ATOMIC_RELAXED);
  while (ticks > old && !__atomic_compare_exchange_n(&t->max, &old, ticks, false, __ATOMIC_RELAXED,
                                                     __ATOMIC_RELAXED)) {
  }
}

void t5ai_io_trace_sample(unsigned stage, uint32_t ticks) {
  if (stage < T5IO_COUNT) {
    prv_sample(&s_io[stage], ticks);
  }
}

static void prv_dump_timing(const char *kind, unsigned stage, const Timing *t) {
  PBL_LOG_SYNC_WRN(
      "T5SYNC timing kind=%s stage=%u count=%" PRIu32 " total_ticks=%" PRIu32 " max_ticks=%" PRIu32,
      kind, stage, __atomic_load_n(&t->count, __ATOMIC_RELAXED),
      __atomic_load_n(&t->total, __ATOMIC_RELAXED), __atomic_load_n(&t->max, __ATOMIC_RELAXED));
}

void t5ai_io_trace_dump(void) {
  for (unsigned i = 0; i < T5IO_COUNT; ++i) {
    prv_dump_timing("io", i, &s_io[i]);
  }
  uint32_t b[7];
  for (unsigned i = 0; i < 7; ++i) {
    b[i] = __atomic_load_n(&s_io[T5IO_WRITE_TOTAL].bins[i], __ATOMIC_RELAXED);
  }
  PBL_LOG_SYNC_WRN("T5SYNC write_hist t0=%" PRIu32 " t1=%" PRIu32 " t2_4=%" PRIu32 " t5_9=%" PRIu32
                   " t10_19=%" PRIu32 " t20_49=%" PRIu32 " t50plus=%" PRIu32,
                   b[0], b[1], b[2], b[3], b[4], b[5], b[6]);
  pbl_irq_lock();
  uint32_t owner = s_owner, file = s_file, start = s_start, records = s_records;
  uint32_t bytes = s_bytes, stage = s_stage, stage_start = s_stage_start;
  char name[sizeof(s_name)];
  memcpy(name, s_name, sizeof(name));
  pbl_irq_unlock();
  PBL_LOG_SYNC_WRN("T5SYNC file owner=%" PRIu32 " file=%" PRIu32 " start=%" PRIu32
                   " records=%" PRIu32 " bytes=%" PRIu32 " name=%s",
                   owner, file, start, records, bytes, name);
  PBL_LOG_SYNC_WRN("T5SYNC active stage=%" PRIu32 " start=%" PRIu32, stage, stage_start);
  for (unsigned i = 0; i < 5; ++i) {
    prv_dump_timing("record", i, &s_record[i]);
  }
}

static bool prv_ours(void) { return s_owner != 0 && pbl_thread_current()->id == s_owner; }

status_t __real_settings_file_mark_all_dirty(SettingsFile *);
status_t __wrap_settings_file_mark_all_dirty(SettingsFile *file) {
  // Do not corrupt an in-flight trace if another caller enters concurrently.
  pbl_irq_lock();
  if (s_owner) {
    pbl_irq_unlock();
    return __real_settings_file_mark_all_dirty(file);
  }
  s_owner = pbl_thread_current()->id;
  ++s_file;
  s_start = pbl_uptime_ticks();
  s_between_start = s_start;
  s_in_record = false;
  s_records = s_bytes = s_stage = 0;
  memset(s_record, 0, sizeof(s_record));
  memset(s_name, 0, sizeof(s_name));
  if (file->name) {
    strncpy(s_name, file->name, sizeof(s_name) - 1);
  }
  pbl_irq_unlock();
  status_t result = __real_settings_file_mark_all_dirty(file);
  t5ai_io_trace_dump();
  t5ai_flash_phase_trace_dump();
  PBL_LOG_SYNC_WRN("T5SYNC done file=%" PRIu32 " elapsed_ticks=%" PRIu32 " status=%ld", s_file,
                   (uint32_t)(pbl_uptime_ticks() - s_start), (long)result);
  pbl_irq_lock();
  s_owner = 0;
  pbl_irq_unlock();
  return result;
}

#define WRAP_WRITE(name, type, index)                         \
  void __real_##name(SettingsRawIter *, type);                \
  void __wrap_##name(SettingsRawIter *iter, type data) {      \
    if (!prv_ours()) {                                        \
      __real_##name(iter, data);                              \
      return;                                                 \
    }                                                         \
    uint32_t start = pbl_uptime_ticks();                      \
    uint32_t old_stage = s_stage, old_start = s_stage_start;  \
    s_stage = index + 1;                                      \
    s_stage_start = start;                                    \
    __real_##name(iter, data);                                \
    prv_sample(&s_record[index], pbl_uptime_ticks() - start); \
    s_stage = old_stage;                                      \
    s_stage_start = old_start;                                \
  }
void __real_settings_raw_iter_write_header(SettingsRawIter *, SettingsRecordHeader *);
void __wrap_settings_raw_iter_write_header(SettingsRawIter *iter, SettingsRecordHeader *hdr) {
  if (!prv_ours()) {
    __real_settings_raw_iter_write_header(iter, hdr);
    return;
  }
  uint32_t start = pbl_uptime_ticks();
  bool begins = (hdr->flags & SETTINGS_FLAG_WRITE_COMPLETE) != 0;
  if (begins && !s_in_record) {
    prv_sample(&s_record[4], start - s_between_start);
    s_record_start = start;
    s_in_record = true;
  }
  s_stage = 2;
  s_stage_start = start;
  __real_settings_raw_iter_write_header(iter, hdr);
  uint32_t end = pbl_uptime_ticks();
  prv_sample(&s_record[1], end - start);
  if (!begins && s_in_record) {
    prv_sample(&s_record[0], end - s_record_start);
    ++s_records;
    s_bytes += hdr->key_len + hdr->val_len;
    s_in_record = false;
    s_between_start = end;
  }
  s_stage = s_in_record ? 1 : 0;
  s_stage_start = s_in_record ? s_record_start : end;
}
WRAP_WRITE(settings_raw_iter_write_key, const uint8_t *, 2)
WRAP_WRITE(settings_raw_iter_write_val, const uint8_t *, 3)
