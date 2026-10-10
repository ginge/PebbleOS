/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/interrupt_profile.h>
#include <pbl/soc/t5ai/heap_trace.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/mcu/cache.h>
#include "system/passert.h"
#include <inttypes.h>
#include <stddef.h>
#include <string.h>
PBL_LOG_MODULE_DEFINE(t5ai_reboot_trace, LOG_LEVEL_DEBUG);
#define TRACE_MAGIC 0x52543001u
#define TRACE_COUNT 32u
typedef struct {
  uint32_t sequence, rtc_ms, ticks, event, value, checksum;
} Record;
typedef struct {
  uint32_t magic, next_sequence;
  Record records[TRACE_COUNT];
} Trace;
static Trace s_trace __attribute__((section(".t5ai_retained"), aligned(32)));
static Trace s_previous;
typedef struct {
  uint32_t pmu_live, pmu_boot, magic, next_sequence, cp_saved, ap_saved;
} BootSnapshot;
static BootSnapshot s_boot_early, s_boot_board;
static bool s_boot_captured;
// Read-only reset-reason words from the pinned BK7258 AON PMU map.
#define T5AI_TRACE_CP_SAVED (*(volatile uint32_t *)0x2809f718u)
#define T5AI_TRACE_AP_SAVED (*(volatile uint32_t *)0x2809f71cu)
#define T5AI_TRACE_PMU_LIVE (*(volatile uint32_t *)0x44000000u)
#define T5AI_TRACE_PMU_BOOT (*(volatile uint32_t *)0x440001e8u)
static BootSnapshot prv_snapshot(void) {
  const volatile Trace *trace = &s_trace;
  return (BootSnapshot){
    .pmu_live = T5AI_TRACE_PMU_LIVE,
    .pmu_boot = T5AI_TRACE_PMU_BOOT,
    .cp_saved = T5AI_TRACE_CP_SAVED,
    .ap_saved = T5AI_TRACE_AP_SAVED,
    .magic = trace->magic,
    .next_sequence = trace->next_sequence,
  };
}
void t5ai_reboot_trace_capture_boot(void) {
  if (s_boot_captured)
    return;
  s_boot_early = prv_snapshot();
  // Preserve the old ring before native cache setup and board initialization.
  const volatile unsigned char *src = (const volatile unsigned char *)&s_trace;
  unsigned char *dst = (unsigned char *)&s_previous;
  for (size_t i = 0; i < sizeof(s_previous) / sizeof(*dst); ++i)
    dst[i] = src[i];
  s_boot_captured = true;
}
static void prv_print_boot(const char *stage, const BootSnapshot *snapshot) {
  PBL_LOG_DBG("T5REBOOT saved stage=%s cp=%08" PRIx32 " ap=%08" PRIx32, stage, snapshot->cp_saved,
              snapshot->ap_saved);
  PBL_LOG_DBG(
      "T5REBOOT boot stage=%s r0=%08" PRIx32 " r7a=%08" PRIx32 " magic=%08" PRIx32 " next=%" PRIu32,
      stage, snapshot->pmu_live, snapshot->pmu_boot, snapshot->magic, snapshot->next_sequence);
}
static struct {
  uint32_t count, last_ms, max_gap;
  bool running;
} s_watchdog;
void t5ai_reboot_trace_watchdog(bool running) {
  uint32_t now = (uint32_t)(t5ai_rtc_get_raw_ticks() / (T5AI_AON_RTC_HZ / 1000u));
  pbl_irq_lock();
  if (running) {
    uint32_t gap = now - s_watchdog.last_ms;
    if (s_watchdog.running && gap > s_watchdog.max_gap)
      s_watchdog.max_gap = gap;
    s_watchdog.last_ms = now;
    s_watchdog.count++;
  }
  s_watchdog.running = running;
  pbl_irq_unlock();
}
static void prv_watchdog_dump(void) {
  pbl_irq_lock();
  uint32_t now = (uint32_t)(t5ai_rtc_get_raw_ticks() / (T5AI_AON_RTC_HZ / 1000u));
  uint32_t count = s_watchdog.count, age = now - s_watchdog.last_ms;
  uint32_t max_gap = s_watchdog.max_gap;
  bool running = s_watchdog.running;
  pbl_irq_unlock();
  PBL_LOG_DBG("T5REBOOT watchdog running=%u feeds=%" PRIu32 " age_ms=%" PRIu32
              " max_gap_ms=%" PRIu32,
              running, count, age, max_gap);
}
static bool s_live;
static bool s_started;
static struct pbl_thread s_thread;
PBL_THREAD_STACK_DEFINE(s_stack, 1536);
static uint32_t prv_checksum(const Record *record) {
  const uint8_t *bytes = (const uint8_t *)record;
  uint32_t sum = TRACE_MAGIC;
  for (size_t i = 0; i < offsetof(Record, checksum); ++i) {
    sum = (sum << 5) | (sum >> 27);
    sum ^= bytes[i];
  }
  return sum;
}
void t5ai_reboot_trace_event(T5RebootTraceEvent event, uint32_t value) {
  if (!s_live)
    return;
  pbl_irq_lock();
  Record record = {
    .sequence = s_trace.next_sequence,
    .rtc_ms = (uint32_t)(t5ai_rtc_get_raw_ticks() / (T5AI_AON_RTC_HZ / 1000u)),
    .ticks = pbl_uptime_ticks(),
    .event = event,
    .value = value,
  };
  record.checksum = prv_checksum(&record);
  Record *slot = &s_trace.records[(record.sequence - 1u) % TRACE_COUNT];
  *slot = record;
  dcache_flush(slot, sizeof(*slot));
  // Publish only after the complete record reaches RAM.
  s_trace.next_sequence++;
  dcache_flush(&s_trace, offsetof(Trace, records));
  pbl_irq_unlock();
}
static void prv_print(const Record *record, bool previous) {
  PBL_LOG_DBG("T5REBOOT previous=%u seq=%" PRIu32 " rtc_ms=%" PRIu32 " ticks=%" PRIu32
              " event=%" PRIu32 " value=%" PRIu32,
              previous, record->sequence, record->rtc_ms, record->ticks, record->event,
              record->value);
}
static void prv_dump_previous(void) {
  if (s_previous.magic != TRACE_MAGIC || !s_previous.next_sequence) {
    PBL_LOG_DBG("T5REBOOT previous ring unavailable");
    return;
  }
  const uint32_t end = s_previous.next_sequence;
  const uint32_t begin = end > TRACE_COUNT ? end - TRACE_COUNT : 1u;
  for (uint32_t seq = begin; seq < end; ++seq) {
    const Record *record = &s_previous.records[(seq - 1u) % TRACE_COUNT];
    if (record->sequence == seq && record->checksum == prv_checksum(record)) {
      prv_print(record, true);
    } else {
      PBL_LOG_WRN("T5REBOOT invalid previous record=%" PRIu32, seq);
    }
  }
}
static void prv_entry(void *arg) {
  (void)arg;
  prv_print_boot("soc", &s_boot_early);
  prv_print_boot("board", &s_boot_board);
  prv_dump_previous();
  uint32_t last_live = s_boot_board.pmu_live;
  uint32_t last_boot = s_boot_board.pmu_boot;
  uint32_t cursor = 1u;
  for (;;) {
    pbl_irq_lock();
    const uint32_t end = s_trace.next_sequence;
    const uint32_t begin = end > TRACE_COUNT ? end - TRACE_COUNT : 1u;
    pbl_irq_unlock();
    if (cursor < begin) {
      PBL_LOG_WRN("T5REBOOT live records lost=%" PRIu32, begin - cursor);
      cursor = begin;
    }
    while (cursor < end) {
      pbl_irq_lock();
      const Record record = s_trace.records[(cursor - 1u) % TRACE_COUNT];
      pbl_irq_unlock();
      if (record.sequence == cursor && record.checksum == prv_checksum(&record)) {
        prv_print(&record, false);
      } else {
        PBL_LOG_WRN("T5REBOOT live record overwritten=%" PRIu32, cursor);
      }
      ++cursor;
    }
    PBL_LOG_DBG("T5REBOOT drain rtc_ms=%" PRIu32 " ticks=%" PRIu32 " next=%" PRIu32,
                (uint32_t)(t5ai_rtc_get_raw_ticks() / (T5AI_AON_RTC_HZ / 1000u)),
                pbl_uptime_ticks(), cursor);
    const uint32_t live = T5AI_TRACE_PMU_LIVE;
    const uint32_t boot = T5AI_TRACE_PMU_BOOT;
    if (live != last_live || boot != last_boot) {
      PBL_LOG_DBG("T5REBOOT reason change r0=%08" PRIx32 " r7a=%08" PRIx32, live, boot);
      last_live = live;
      last_boot = boot;
    }
    prv_watchdog_dump();
    t5ai_heap_trace_dump();
    t5ai_interrupt_profile_dump();
    pbl_thread_sleep(PBL_MSEC(2000));
  }
}
void t5ai_reboot_trace_start(void) {
  if (s_started)
    return;
  s_started = true;
  t5ai_reboot_trace_capture_boot();
  s_boot_board = prv_snapshot();
  memset(&s_trace, 0, sizeof(s_trace));
  s_trace.magic = TRACE_MAGIC;
  s_trace.next_sequence = 1u;
  dcache_flush(&s_trace, sizeof(s_trace));
  s_live = true;
  const struct pbl_thread_attr attr = {
    .name = "T5Reboot",
    .entry = prv_entry,
    .prio = 1,
    .privileged = true,
    .stack = s_stack,
    .stack_size = sizeof(s_stack),
  };
  PBL_ASSERTN(pbl_thread_create(&s_thread, &attr) == 0);
}
