/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#include <pbl/drivers/display.h>

// Raw CO5300 level (1..255). Tune on hardware.
#define T5AI_AMOLED_MIN_BRIGHTNESS 35u

extern const struct pbl_display_ops pbl_display_co5300_ops;

// Durations use the 32 kHz AON clock, independent of CPU frequency.
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
typedef struct {
  uint32_t frames;
  uint32_t total_update_ticks;
  uint32_t last_update_ticks;
  uint32_t max_update_ticks;
  uint32_t last_interval_ticks;
  uint32_t dma_failures;
} T5AIDrawStats;

extern volatile T5AIDrawStats g_t5ai_draw_stats;
#endif
