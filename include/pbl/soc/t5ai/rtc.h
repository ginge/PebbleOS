/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

// Always-on hardware counter; frequency is independent of Pebble's RTC tick scale.
#define T5AI_AON_RTC_BASE 0x44000200u
#define T5AI_AON_RTC_COUNT_LOW (*(volatile uint32_t *)(T5AI_AON_RTC_BASE + 0x0cu))
#define T5AI_AON_RTC_COUNT_HIGH (*(volatile uint32_t *)(T5AI_AON_RTC_BASE + 0x28u))
#define T5AI_AON_RTC_HZ 32000u

//! Returns the unscaled 32 kHz always-on counter for hardware timebase validation.
uint64_t t5ai_rtc_get_raw_ticks(void);
