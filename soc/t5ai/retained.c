/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "system/rtc_registers.h"

#include <pbl/kernel/assert.h>
#include <pbl/soc/t5ai/cache.h>
#include <pbl/soc/t5ai/retained.h>

#include <cmsis_core.h>
#define T5AI_RETAINED_WORDS 32u
#define T5AI_RETAINED_MAGIC 0x50355452u
typedef struct {
  volatile uint32_t magic;
  volatile uint32_t values[T5AI_RETAINED_WORDS];
  volatile uint32_t checksum;
} T5AIRetainedState;
static T5AIRetainedState s_retained __attribute__((section(".t5ai_retained")));
static uint32_t prv_checksum(void) {
  uint32_t checksum = T5AI_RETAINED_MAGIC;
  for (unsigned i = 0; i < T5AI_RETAINED_WORDS; ++i) {
    checksum = (checksum << 5) | (checksum >> 27);
    checksum ^= s_retained.values[i];
  }
  return checksum;
}
void t5ai_retained_init(void) {
  if (s_retained.magic != T5AI_RETAINED_MAGIC || s_retained.checksum != prv_checksum()) {
    // Keep an explicit external-clock fault even when a clock commit was interrupted.
    // Cold/legacy stores have no fault signature and retain normal external-RTC fallback.
    s_retained.magic = 0;
    t5ai_cache_clean((const void *)&s_retained.magic, sizeof(s_retained.magic));
    for (unsigned i = 0; i < T5AI_RETAINED_WORDS; ++i) {
      if (i != T5AI_RTC_EXTERNAL_UNTRUSTED ||
          s_retained.values[i] != T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE) {
        s_retained.values[i] = 0;
      }
    }
    s_retained.checksum = prv_checksum();
    t5ai_cache_clean(&s_retained, sizeof(s_retained));
    s_retained.magic = T5AI_RETAINED_MAGIC;
    t5ai_cache_clean((const void *)&s_retained.magic, sizeof(s_retained.magic));
  }
}
uint32_t t5ai_retained_read(uint8_t index) {
  PBL_ASSERTN(index < T5AI_RETAINED_WORDS);
  return s_retained.values[index];
}
void t5ai_retained_write(uint8_t index, uint32_t value) {
  PBL_ASSERTN(index < T5AI_RETAINED_WORDS);
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  s_retained.values[index] = value;
  s_retained.checksum = prv_checksum();
  t5ai_cache_clean(&s_retained, sizeof(s_retained));
  __set_PRIMASK(mask);
}

uint32_t t5ai_boot_version_read(void) {
  return 0;
}

// Preserve the legacy 136-byte layout and checksum, including old-image readers.
// Reset before commit invalidates the entire store, using its existing fallback.
void t5ai_retained_write_clock(uint32_t epoch, uint64_t ticks) {
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  s_retained.magic = 0;
  t5ai_cache_clean((const void *)&s_retained.magic, sizeof(s_retained.magic));
  __DSB();
  s_retained.values[CURRENT_TIME_REGISTER] = epoch;
  s_retained.values[T5AI_RTC_BASE_TICKS_LOW] = (uint32_t)ticks;
  s_retained.values[T5AI_RTC_BASE_TICKS_HIGH] = (uint32_t)(ticks >> 32);
  s_retained.checksum = prv_checksum();
  t5ai_cache_clean(&s_retained, sizeof(s_retained));
  __DSB();
  s_retained.magic = T5AI_RETAINED_MAGIC;
  t5ai_cache_clean((const void *)&s_retained.magic, sizeof(s_retained.magic));
  __DSB();
  __set_PRIMASK(mask);
}
