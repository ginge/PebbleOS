/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/delay.h>
#include <pbl/kernel/compiler.h>
#include <cmsis_core.h>

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

static uint32_t prv_timer_count(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  T5AI_TIMER0_READ_CONTROL &= ~(3u << 2);
  T5AI_TIMER0_READ_CONTROL |= 1u;
  while ((T5AI_TIMER0_READ_CONTROL & 1u) != 0) {
  }
  const uint32_t count = T5AI_TIMER0_READ_VALUE;
  if (primask == 0) {
    __enable_irq();
  }
  return count;
}

void PBL_NOINLINE delay_us(uint32_t us) {
  while (us != 0) {
    const uint32_t max_chunk = (UINT32_MAX / 2u) / T5AI_TIMER0_CYCLES_PER_US;
    const uint32_t chunk_us = us < max_chunk ? us : max_chunk;
    const uint32_t cycles = chunk_us * T5AI_TIMER0_CYCLES_PER_US;
    const uint32_t start = prv_timer_count();
    while ((uint32_t)(prv_timer_count() - start) < cycles) {
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
