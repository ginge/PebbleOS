/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>

#include <pbl/kernel/assert.h>
#include <pbl/kernel/compiler.h>
#include <pbl/kernel/mmio.h>
#include <pbl/soc/t5ai/core.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/soc/t5ai/rtc.h>

/* BK7258 timer0 free-running counter setup from the pinned T5AI BSP. */
#define T5AI_TIMER0_BASE         0x44810000u
#define T5AI_TIMER0_CONTROL      (*(volatile uint32_t *)(T5AI_TIMER0_BASE + 0x08u))
#define T5AI_TIMER0_PERIOD       (*(volatile uint32_t *)(T5AI_TIMER0_BASE + 0x10u))
#define T5AI_TIMER0_START        (*(volatile uint32_t *)(T5AI_TIMER0_BASE + 0x1cu))
#define T5AI_TIMER0_READ_CONTROL (*(volatile uint32_t *)(T5AI_TIMER0_BASE + 0x20u))
#define T5AI_TIMER0_READ_VALUE   (*(volatile uint32_t *)(T5AI_TIMER0_BASE + 0x24u))

#define T5AI_SYSTEM_BASE          0x44010000u
#define T5AI_SYSTEM_TIMER0_CLOCK  (*(volatile uint32_t *)(T5AI_SYSTEM_BASE + 0x20u))
#define T5AI_SYSTEM_TIMER0_ENABLE (*(volatile uint32_t *)(T5AI_SYSTEM_BASE + 0x30u))

#define T5AI_TIMER0_HZ            26000000u
#define T5AI_TIMER0_CYCLES_PER_US (T5AI_TIMER0_HZ / 1000000u)

#define T5AI_TIMER0_LATCH_TIMEOUT_TICKS (T5AI_AON_RTC_HZ / 1000u)
#define T5AI_TIMER0_POLL_LIMIT          65536u

static uint32_t prv_timer_count(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  T5AI_TIMER0_READ_CONTROL &= ~(3u << 2);
  T5AI_TIMER0_READ_CONTROL |= 1u;
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  unsigned remaining = T5AI_TIMER0_POLL_LIMIT;
  bool ready;
  // Keep the completion observation: the asynchronous latch can be busy again
  // by the next read. A later status must not invalidate a completed poll.
  while (!(ready = (pbl_readl((uintptr_t)&T5AI_TIMER0_READ_CONTROL) & 1u) == 0u) &&
         --remaining != 0u &&
         (uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) < T5AI_TIMER0_LATCH_TIMEOUT_TICKS) {
  }
  const uint32_t count = T5AI_TIMER0_READ_VALUE;
  if (primask == 0) {
    __enable_irq();
  }
  // Never return a short delay after a failed latch. Restore the caller's mask
  // before entering the always-enabled fatal path.
  PBL_ASSERTN(ready);
  return count;
}

void PBL_NOINLINE delay_us(uint32_t us) {
  while (us != 0) {
    const uint32_t max_chunk = (UINT32_MAX / 2u) / T5AI_TIMER0_CYCLES_PER_US;
    const uint32_t chunk_us = us < max_chunk ? us : max_chunk;
    const uint32_t cycles = chunk_us * T5AI_TIMER0_CYCLES_PER_US;
    const uint32_t start = prv_timer_count();
    const uint32_t aon_start = T5AI_AON_RTC_COUNT_LOW;
    const uint32_t aon_limit = ((uint64_t)chunk_us * T5AI_AON_RTC_HZ + 999999u) / 1000000u +
                               T5AI_TIMER0_LATCH_TIMEOUT_TICKS;
    unsigned remaining = T5AI_TIMER0_POLL_LIMIT;
    uint32_t previous = start;
    for (;;) {
      const uint32_t now = prv_timer_count();
      if ((uint32_t)(now - start) >= cycles) {
        break;
      }
      if (now != previous) {
        remaining = T5AI_TIMER0_POLL_LIMIT;
      }
      previous = now;
      // Also fail safely if the timer stops advancing after successful latches.
      PBL_ASSERTN(--remaining != 0u && (uint32_t)(T5AI_AON_RTC_COUNT_LOW - aon_start) < aon_limit);
    }
    us -= chunk_us;
  }
}

void delay_init(void) {
  T5AI_SYSTEM_TIMER0_ENABLE |= 1u << 4;
  T5AI_SYSTEM_TIMER0_CLOCK |= 1u << 20;
  T5AI_TIMER0_CONTROL |= 1u;
  T5AI_TIMER0_PERIOD = UINT32_MAX;
  T5AI_TIMER0_START |= 1u;
}
