/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/interrupt_profile.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/logging/logging.h>
#include <cmsis_core.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

PBL_LOG_MODULE_DEFINE(t5ai_interrupt_profile, LOG_LEVEL_DEBUG);

#define CLOCK_MODE (*(volatile uint32_t *)0x44010020u)
#define CPU1_CLOCK (*(volatile uint32_t *)0x44010014u)

typedef struct {
  uint32_t ticks, gaps_1_5ms, gaps_3ms, gap_max, body_max;
  uint32_t masks, mask_total, mask_max;
  uint32_t clock_changes, reload_changes, mode, cpu1, reload;
} Profile;
typedef struct {
  uint32_t count, total, maximum;
} IRQProfile;
static IRQProfile s_irqs[4];
static Profile s_profile;
static bool s_enabled, s_mask_active, s_tick_seen;
static uint32_t s_mask_start, s_tick_previous;

void __real_arch_irq_disable(void);
void __real_arch_irq_enable(void);
void __real_SysTick_Handler(void);

static uint32_t prv_now(void) {
  return T5AI_AON_RTC_COUNT_LOW;
}
static void prv_max(uint32_t *maximum, uint32_t value) {
  if (value > *maximum) {
    *maximum = value;
  }
}

uint32_t t5ai_interrupt_profile_irq_enter(void) {
  return prv_now();
}
void t5ai_interrupt_profile_irq_exit(unsigned id, uint32_t start) {
  if (!s_enabled || id >= 4) {
    return;
  }
  const uint32_t elapsed = prv_now() - start;
  ++s_irqs[id].count;
  s_irqs[id].total += elapsed;
  prv_max(&s_irqs[id].maximum, elapsed);
}

void __wrap_arch_irq_disable(void) {
  __real_arch_irq_disable();
  if (s_enabled && !s_mask_active) {
    s_mask_start = prv_now();
    s_mask_active = true;
  }
}

void __wrap_arch_irq_enable(void) {
  if (s_enabled && s_mask_active) {
    const uint32_t elapsed = prv_now() - s_mask_start;
    ++s_profile.masks;
    s_profile.mask_total += elapsed;
    prv_max(&s_profile.mask_max, elapsed);
    s_mask_active = false;
  }
  __real_arch_irq_enable();
}

void __wrap_SysTick_Handler(void) {
  const uint32_t entry = prv_now();
  if (s_enabled) {
    if (s_tick_seen) {
      const uint32_t gap = entry - s_tick_previous;
      prv_max(&s_profile.gap_max, gap);
      s_profile.gaps_1_5ms += gap > T5AI_AON_RTC_HZ * 3u / 2000u;
      s_profile.gaps_3ms += gap > T5AI_AON_RTC_HZ * 3u / 1000u;
    }
    s_tick_seen = true;
    s_tick_previous = entry;
    ++s_profile.ticks;
    const uint32_t mode = CLOCK_MODE;
    // Only the speed bit affects the CPU1 divider; reset/halt bits are unrelated.
    const uint32_t cpu1 = CPU1_CLOCK & (1u << 4u);
    const uint32_t reload = SysTick->LOAD;
    s_profile.clock_changes += (mode & 0x3fu) != (s_profile.mode & 0x3fu) || cpu1 != s_profile.cpu1;
    s_profile.reload_changes += reload != s_profile.reload;
    s_profile.mode = mode;
    s_profile.cpu1 = cpu1;
    s_profile.reload = reload;
  }
  __real_SysTick_Handler();
  if (s_enabled) {
    prv_max(&s_profile.body_max, prv_now() - entry);
  }
}

void t5ai_interrupt_profile_start(void) {
  if (s_enabled) {
    return;
  }
  s_profile.mode = CLOCK_MODE;
  s_profile.cpu1 = CPU1_CLOCK & (1u << 4u);
  s_profile.reload = SysTick->LOAD;
  s_enabled = true;
}

void t5ai_interrupt_profile_dump(void) {
  pbl_irq_lock();
  const Profile p = s_profile;
  const IRQProfile irqs[] = {s_irqs[0], s_irqs[1], s_irqs[2], s_irqs[3]};
  const uint32_t now = prv_now();
  pbl_irq_unlock();
  for (unsigned id = 0; id < 4; ++id) {
    PBL_LOG_DBG("T5IRQ source=%u count=%" PRIu32 " total=%" PRIu32 " max=%" PRIu32, id, irqs[id].count,
                irqs[id].total, irqs[id].maximum);
  }
  // Durations are raw 32 kHz ticks; totals are cumulative modulo 2^32.
  PBL_LOG_DBG("T5IRQ rtc=%" PRIu32 " tick=%" PRIu32 " gap_max=%" PRIu32 " gap_gt1p5=%" PRIu32
              " gap_gt3=%" PRIu32 " body_max=%" PRIu32,
              now, p.ticks, p.gap_max, p.gaps_1_5ms, p.gaps_3ms, p.body_max);
  PBL_LOG_DBG("T5IRQ mask_count=%" PRIu32 " mask_total=%" PRIu32 " mask_max=%" PRIu32, p.masks,
              p.mask_total, p.mask_max);
  PBL_LOG_DBG("T5IRQ clock_changes=%" PRIu32 " reload_changes=%" PRIu32 " mode=%08" PRIx32
              " cpu1_speed=%" PRIu32 " reload=%" PRIu32,
              p.clock_changes, p.reload_changes, p.mode, p.cpu1 >> 4u, p.reload);
}
