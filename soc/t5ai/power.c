/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/power.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/logging/logging.h>
#include "system/profiler.h"
#include <cmsis_core.h>

#include <stdbool.h>
#include <stdint.h>

PBL_LOG_MODULE_DEFINE(soc_t5ai_power, LOG_LEVEL_DEBUG);

static bool s_initialized;

static uint32_t prv_core_clock_hz(void) {
  // The CP arbitrates all module votes, so an accepted 240 MHz vote is a
  // minimum, not necessarily the elected clock. Read the CPU1 clock tree.
  const uint32_t mode = *(volatile uint32_t *)0x44010020u;
  const uint32_t cpu1 = *(volatile uint32_t *)0x44010014u;
  uint32_t source_hz;
  switch ((mode >> 4u) & 3u) {
    case 0u:
      source_hz = 26000000u;
      break;
    case 2u:
      source_hz = 320000000u;
      break;
    case 3u:
      source_hz = 480000000u;
      break;
    default:
      // DCO frequency depends on calibration; do not invent a clock rate.
      return 0u;
  }
  const uint32_t core_hz = source_hz / ((mode & 15u) + 1u);
  return (cpu1 & (1u << 4u)) ? core_hz : core_hz / 2u;
}

void t5ai_power_init(void) {
  if (!s_initialized) {
    // Like Round 2, elect the active clock once. Scheduler WFI handles idle
    // without repeatedly changing CP0/Bluetooth clocks, bus clocks or voltage.
    const status_t status = t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency_240MHz);
    if (status != S_SUCCESS) {
      PBL_LOG_ERR("Active CPU clock vote failed: %ld", (long)status);
    } else {
      s_initialized = true;
      PROFILER_INIT;
      PROFILER_START;
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
  const uint32_t reload = actual_hz / CONFIG_KERNEL_TICK_HZ - 1u;
  if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) && SysTick->LOAD != reload) {
    SysTick->LOAD = reload;
    // Discard the residual period calibrated against the previous clock.
    SysTick->VAL = 0u;
  }
}
