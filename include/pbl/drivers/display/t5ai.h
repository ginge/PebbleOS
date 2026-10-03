/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pbl/drivers/display/display.h>

#include <stdint.h>

void display_co5300_set_brightness(uint8_t level);
void display_co5300_update_cpu(NextRowCallback nrcb, UpdateCompleteCallback uccb);

// Durations use the 32 kHz AON clock, independent of CPU frequency.
typedef struct {
  uint32_t frames;
  uint32_t total_update_ticks;
  uint32_t last_update_ticks;
  uint32_t max_update_ticks;
  uint32_t last_interval_ticks;
  uint32_t dma_failures;
} T5AIDrawStats;

extern volatile T5AIDrawStats g_t5ai_draw_stats;
