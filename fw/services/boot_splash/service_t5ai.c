/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/boot_splash.h"
#include <pbl/services/boot_splash/t5ai.h>

#include "board/display.h"
#include "board/splash.h"
#include <pbl/drivers/display/display.h>
#include <pbl/drivers/display/t5ai.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define T5AI_SPLASH_WHITE 0xffu
#define T5AI_SPLASH_BLACK 0xc0u
#define T5AI_SPLASH_TRACK 0xeau
#define T5AI_SPLASH_PROGRESS_WIDTH 80u
#define T5AI_SPLASH_INDICATOR_WIDTH 20u

static uint8_t s_t5ai_splash_row[PBL_DISPLAY_WIDTH];
static uint16_t s_t5ai_splash_y;
static bool s_splash_requested;

static bool prv_t5ai_splash_next_row(DisplayRow *row) {
  if (s_t5ai_splash_y >= PBL_DISPLAY_HEIGHT) {
    return false;
  }

  const uint16_t y = s_t5ai_splash_y++;
  memset(s_t5ai_splash_row, T5AI_SPLASH_WHITE, sizeof(s_t5ai_splash_row));

  const uint16_t logo_x0 = (PBL_DISPLAY_WIDTH - splash_width) / 2u;
  const uint16_t logo_y0 = (PBL_DISPLAY_HEIGHT - splash_height) / 2u;
  if (y >= logo_y0 && y < logo_y0 + splash_height) {
    const uint16_t logo_y = y - logo_y0;
    const uint16_t logo_stride = (splash_width + 7u) / 8u;
    for (uint16_t x = 0u; x < splash_width; ++x) {
      if (splash_bits[logo_y * logo_stride + x / 8u] & (1u << (x & 7u))) {
        s_t5ai_splash_row[logo_x0 + x] = T5AI_SPLASH_BLACK;
      }
    }
  }

  const uint16_t progress_y0 = logo_y0 + splash_height + 18u;
  if (y >= progress_y0 && y < progress_y0 + 4u) {
    const uint16_t progress_x0 = (PBL_DISPLAY_WIDTH - T5AI_SPLASH_PROGRESS_WIDTH) / 2u;
    memset(s_t5ai_splash_row + progress_x0, T5AI_SPLASH_TRACK, T5AI_SPLASH_PROGRESS_WIDTH);
    const uint16_t indicator_x0 = progress_x0 +
                                  (T5AI_SPLASH_PROGRESS_WIDTH - T5AI_SPLASH_INDICATOR_WIDTH) / 2u;
    memset(s_t5ai_splash_row + indicator_x0, T5AI_SPLASH_BLACK,
           T5AI_SPLASH_INDICATOR_WIDTH);
  }

  row->address = y;
  row->data = s_t5ai_splash_row;
  return true;
}

static void prv_t5ai_splash_complete(void) {
}

void boot_splash_start(void) {
  // The stock early call precedes timer services required by CP transactions.
  s_splash_requested = true;
}

void t5ai_boot_splash_render(void) {
  if (!s_splash_requested) {
    return;
  }
  s_splash_requested = false;
  display_init();

  // T5AI's 67,600-byte framebuffer does not fit in the remaining internal
  // kernel heap. The logo is compiled into the AP image, so render it one
  // bounded row at a time through the synchronous CPU aperture transport.
  // GDMA is retained for normal compositor updates after startup.
  s_t5ai_splash_y = 0u;
  display_co5300_update_cpu(prv_t5ai_splash_next_row, prv_t5ai_splash_complete);
}

void boot_splash_stop(void) {
  s_splash_requested = false;
  // The static splash remains in panel GRAM until the compositor's first
  // normal frame replaces it.
}
