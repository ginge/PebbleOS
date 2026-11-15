/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/timer.h>
#include <pbl/logging/logging.h>
#include <pbl/profiler/profiler.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/power.h>

#include <cmsis_core.h>

PBL_LOG_MODULE_DEFINE(soc_t5ai_power, LOG_LEVEL_DEBUG);

// BK7258 SYS register byte addresses from the pinned Tuya BSP.
#define T5AI_SYSTEM_CLOCK_MODE_ADDRESS       0x44010020u
#define T5AI_SYSTEM_CPU0_CLOCK_ADDRESS       0x44010010u
#define T5AI_SYSTEM_CPU1_CLOCK_ADDRESS       0x44010014u
#define T5AI_SYSTEM_ANALOG_CONTROL_9_ADDRESS 0x44010124u

#define T5AI_CORE_CLOCK_SOURCE_SHIFT 4u
#define T5AI_CORE_CLOCK_SOURCE_MASK  (3u << T5AI_CORE_CLOCK_SOURCE_SHIFT)
#define T5AI_CORE_CLOCK_DIVIDER_MASK 15u
#define T5AI_CPU_FULL_SPEED_ENABLE   (1u << 4u)
#define T5AI_CPU_HALF_SPEED_DIVIDER  2u

#define T5AI_CORE_SOURCE_XTAL_26MHZ  0u
#define T5AI_CORE_SOURCE_DPLL_320MHZ 2u
#define T5AI_CORE_SOURCE_DPLL_480MHZ 3u

#define T5AI_XTAL_CLOCK_HZ     26000000u
#define T5AI_DPLL_320_CLOCK_HZ 320000000u
#define T5AI_DPLL_480_CLOCK_HZ 480000000u

static bool s_initialized;

static uint32_t prv_clock_hz(uint32_t mode, uint32_t cpu) {
  uint32_t source_hz;
  switch ((mode & T5AI_CORE_CLOCK_SOURCE_MASK) >> T5AI_CORE_CLOCK_SOURCE_SHIFT) {
    case T5AI_CORE_SOURCE_XTAL_26MHZ:
      source_hz = T5AI_XTAL_CLOCK_HZ;
      break;
    case T5AI_CORE_SOURCE_DPLL_320MHZ:
      source_hz = T5AI_DPLL_320_CLOCK_HZ;
      break;
    case T5AI_CORE_SOURCE_DPLL_480MHZ:
      source_hz = T5AI_DPLL_480_CLOCK_HZ;
      break;
    default:
      // DCO frequency depends on calibration; do not invent a clock rate.
      return 0u;
  }
  // The divider field encodes divisor minus one.
  const uint32_t core_divider = (mode & T5AI_CORE_CLOCK_DIVIDER_MASK) + 1u;
  const uint32_t core_hz = source_hz / core_divider;
  return (cpu & T5AI_CPU_FULL_SPEED_ENABLE) ? core_hz : core_hz / T5AI_CPU_HALF_SPEED_DIVIDER;
}

static uint32_t prv_core_clock_hz(void) {
  return prv_clock_hz(*(volatile uint32_t *)T5AI_SYSTEM_CLOCK_MODE_ADDRESS,
                      *(volatile uint32_t *)T5AI_SYSTEM_CPU1_CLOCK_ADDRESS);
}

#ifdef CONFIG_T5AI_POWER_PROFILE
void t5ai_power_clock_profile(T5AIClockProfile *profile) {
  *profile = (T5AIClockProfile){
    .mode = *(volatile uint32_t *)T5AI_SYSTEM_CLOCK_MODE_ADDRESS,
    .cpu0 = *(volatile uint32_t *)T5AI_SYSTEM_CPU0_CLOCK_ADDRESS,
    .cpu1 = *(volatile uint32_t *)T5AI_SYSTEM_CPU1_CLOCK_ADDRESS,
    .ana9 = *(volatile uint32_t *)T5AI_SYSTEM_ANALOG_CONTROL_9_ADDRESS,
  };
  profile->cpu0_hz = prv_clock_hz(profile->mode, profile->cpu0);
  profile->ap_hz = prv_clock_hz(profile->mode, profile->cpu1);
}
#endif

void t5ai_power_init(void) {
  if (!s_initialized) {
    // This CP module vote elects shared CPU/bus clocks and voltage, including
    // the CPU0 running Bluetooth. Request it once, never on each idle entry.
    // Active work stays at 240 MHz. Low-power clocks belong to coordinated sleep.
    const status_t status = t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency_240MHz);
    if (status != S_SUCCESS) {
      PBL_LOG_ERR("Active CPU clock vote failed: %ld", (long)status);
    } else {
      s_initialized = true;
#ifdef CONFIG_PROFILER
      pbl_profiler_reset();
      pbl_profiler_start();
#endif
    }
  }
  t5ai_power_sync_tick_clock();
  PBL_LOG_DBG("Active CPU clock: %lu Hz", (unsigned long)SystemCoreClock);
}

void t5ai_power_update_clock(void) {
  const uint32_t actual_hz = prv_core_clock_hz();
  if (actual_hz != 0u) {
    SystemCoreClock = actual_hz;
  } else {
    PBL_LOG_ERR("Unsupported CPU clock source");
  }
  PBL_LOG_DBG("Active CPU clock: %lu Hz", (unsigned long)SystemCoreClock);
}

void t5ai_power_sync_tick_clock(void) {
  const uint32_t actual_hz = prv_core_clock_hz();
  if (actual_hz == 0u) {
    return;
  }
  SystemCoreClock = actual_hz;
  pbl_clockevent_set_hz(pbl_system_clockevent, actual_hz);
}

#ifdef CONFIG_T5AI_TICKLESS_WFI
uint32_t t5ai_power_wfi_clock_min_hz(void) {
  const uint32_t minimum = t5ai_cp_mailbox_wfi_clock_min_hz();
  return minimum != 0u && prv_core_clock_hz() >= minimum ? minimum : 0u;
}
#endif

uint32_t t5ai_power_clock_hz(void) {
  return prv_core_clock_hz();
}
