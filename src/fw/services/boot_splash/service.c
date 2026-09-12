/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/boot_splash.h"

#if defined(CONFIG_SOC_T5AI)

#include "board/display.h"
#include "board/splash.h"
#include <pbl/drivers/display/display.h>

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
  // T5AI's 67,600-byte framebuffer does not fit in the remaining internal
  // kernel heap. The logo is compiled into the AP image, so render it one
  // bounded row at a time through the synchronous native display transport.
  s_t5ai_splash_y = 0u;
  display_update(prv_t5ai_splash_next_row, prv_t5ai_splash_complete);
}

void boot_splash_stop(void) {
  // The static splash remains in panel GRAM until the compositor's first
  // normal frame replaces it.
}

#elif defined(CONFIG_PBLBOOT) || defined(CONFIG_QEMU)

#include "board/display.h"
#include "board/splash.h"
#include <pbl/drivers/display/display.h>
#include "kernel/pbl_malloc.h"
#include "kernel/util/sleep.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

// Platform-specific colors: PebbleOS uses ARGB2222 (GColor8), not raw RGB332
#ifdef CONFIG_QEMU
// QEMU display natively renders ARGB2222
#define SPLASH_COLOR_WHITE  0xFF  // A=3, R=3, G=3, B=3
#define SPLASH_COLOR_BLACK  0xC0  // A=3, R=0, G=0, B=0
#define SPLASH_COLOR_LGRAY  0xEA  // A=3, R=2, G=2, B=2
// QEMU boot splash always uses 8bpp framebuffer regardless of platform bit depth
#define BOOT_SPLASH_FB_BYTES (PBL_DISPLAY_WIDTH * PBL_DISPLAY_HEIGHT)
#else
// Hardware displays: the compositor handles GColor8 → native conversion,
// but boot_splash writes raw pixels before the compositor is initialized.
// On obelix/getafix the LCDC accepts RGB332 directly.
#define SPLASH_COLOR_WHITE  0xFF
#define SPLASH_COLOR_BLACK  0x00
#define SPLASH_COLOR_LGRAY  0xB6
#define BOOT_SPLASH_FB_BYTES DISPLAY_FRAMEBUFFER_BYTES
#endif

// Progress bar configuration
#define PROGRESS_BAR_WIDTH      80
#define PROGRESS_BAR_HEIGHT     4
#define PROGRESS_INDICATOR_WIDTH 20
#define PROGRESS_FRAME_DELAY_MS 100
#define PROGRESS_TOTAL_FRAMES   20
#define BOOT_SPLASH_TASK_STACK_SIZE 512
#define BOOT_SPLASH_TASK_PRIORITY   (configMAX_PRIORITIES - 2)

// Boot splash state
static TaskHandle_t s_boot_splash_task;
static volatile bool s_boot_splash_running;
static uint8_t *s_boot_splash_fb;

// Draw a filled rectangle
static void prv_draw_filled_rect(uint8_t *fb, int16_t x0, int16_t y0,
                                 int16_t width, int16_t height, uint8_t color) {
  for (int16_t y = y0; y < y0 + height; y++) {
    for (int16_t x = x0; x < x0 + width; x++) {
      if (x >= 0 && x < PBL_DISPLAY_WIDTH && y >= 0 && y < PBL_DISPLAY_HEIGHT) {
        fb[(uint32_t)y * PBL_DISPLAY_WIDTH + x] = color;
      }
    }
  }
}

// Draw progress bar with animated indicator
static void prv_draw_progress_bar(uint8_t *fb, int16_t center_x, int16_t center_y,
                                  uint16_t frame) {
  const uint8_t COLOR_TRACK = SPLASH_COLOR_LGRAY;
  const uint8_t COLOR_INDICATOR = SPLASH_COLOR_BLACK;

  int16_t bar_x0 = center_x - PROGRESS_BAR_WIDTH / 2;
  int16_t bar_y0 = center_y - PROGRESS_BAR_HEIGHT / 2;

  // Draw track (background)
  prv_draw_filled_rect(fb, bar_x0, bar_y0, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, COLOR_TRACK);

  // Calculate indicator position (ping-pong animation)
  int32_t max_travel = PROGRESS_BAR_WIDTH - PROGRESS_INDICATOR_WIDTH;
  int32_t half_frames = PROGRESS_TOTAL_FRAMES / 2;
  int32_t cycle_frame = frame % PROGRESS_TOTAL_FRAMES;
  int32_t indicator_offset;

  if (cycle_frame < half_frames) {
    // Moving right
    indicator_offset = (cycle_frame * max_travel) / half_frames;
  } else {
    // Moving left
    indicator_offset = max_travel - ((cycle_frame - half_frames) * max_travel) / half_frames;
  }

  // Draw indicator
  prv_draw_filled_rect(fb, bar_x0 + indicator_offset, bar_y0,
                       PROGRESS_INDICATOR_WIDTH, PROGRESS_BAR_HEIGHT, COLOR_INDICATOR);
}

// Boot splash task - runs until stopped
static void prv_boot_splash_task(void *param) {
  uint8_t *fb = s_boot_splash_fb;
  uint16_t frame = 0;

  // Get splash logo dimensions
  const uint16_t logo_width = splash_width;
  const uint16_t logo_height = splash_height;
  const uint8_t *logo_data = splash_bits;

  // Calculate logo position (centered)
  uint16_t logo_x0 = (PBL_DISPLAY_WIDTH - logo_width) / 2;
  uint16_t logo_y0 = (PBL_DISPLAY_HEIGHT - logo_height) / 2;

  // Calculate progress bar center position (below centered logo)
  int16_t progress_cx = PBL_DISPLAY_WIDTH / 2;
  int16_t progress_cy = logo_y0 + logo_height + 20;

  // Animation loop - runs until s_boot_splash_running is cleared
  while (s_boot_splash_running) {
    // Clear framebuffer to white
    memset(fb, SPLASH_COLOR_WHITE, BOOT_SPLASH_FB_BYTES);

    // Draw logo
    for (uint16_t y = 0U; y < logo_height; y++) {
      for (uint16_t x = 0U; x < logo_width; x++) {
        if (logo_data[y * (logo_width / 8) + x / 8] & (0x1U << (x & 7))) {
          fb[(y + logo_y0) * PBL_DISPLAY_WIDTH + (x + logo_x0)] = SPLASH_COLOR_BLACK;
        }
      }
    }

    // Draw progress bar
    prv_draw_progress_bar(fb, progress_cx, progress_cy, frame);

    // Send frame to display
    display_update_boot_frame(fb);

    frame++;

    // Wait for next frame using short delays to allow quick stop
    for (uint8_t i = 0; i < (PROGRESS_FRAME_DELAY_MS / 10) && s_boot_splash_running; i++) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  // Cleanup
  kernel_free(fb);
  s_boot_splash_fb = NULL;
  s_boot_splash_task = NULL;
  vTaskDelete(NULL);
}

void boot_splash_start(void) {
  // Initialize the display
  display_init();

  // Allocate framebuffer for boot splash
  s_boot_splash_fb = kernel_malloc(BOOT_SPLASH_FB_BYTES);
  if (!s_boot_splash_fb) {
    return;
  }

  // Start the boot splash task
  s_boot_splash_running = true;
  xTaskCreate(prv_boot_splash_task,
              "BootSplash",
              BOOT_SPLASH_TASK_STACK_SIZE,
              NULL,
              BOOT_SPLASH_TASK_PRIORITY,
              &s_boot_splash_task);
}

void boot_splash_stop(void) {
  if (s_boot_splash_running && s_boot_splash_task != NULL) {
    s_boot_splash_running = false;
    // Wait for the splash task to finish (max ~10ms delay due to short sleep intervals)
    while (s_boot_splash_task != NULL) {
      psleep(10);
    }

    // Draw final frame with logo only (no progress bar)
    uint8_t *fb = kernel_malloc(BOOT_SPLASH_FB_BYTES);
    if (fb) {
      memset(fb, SPLASH_COLOR_WHITE, BOOT_SPLASH_FB_BYTES);

      // Get splash logo dimensions
      const uint16_t logo_width = splash_width;
      const uint16_t logo_height = splash_height;
      const uint8_t *logo_data = splash_bits;

      // Calculate logo position (centered, same as animation)
      uint16_t logo_x0 = (PBL_DISPLAY_WIDTH - logo_width) / 2;
      uint16_t logo_y0 = (PBL_DISPLAY_HEIGHT - logo_height) / 2;

      // Draw logo
      for (uint16_t y = 0U; y < logo_height; y++) {
        for (uint16_t x = 0U; x < logo_width; x++) {
          if (logo_data[y * (logo_width / 8) + x / 8] & (0x1U << (x & 7))) {
            fb[(y + logo_y0) * PBL_DISPLAY_WIDTH + (x + logo_x0)] = SPLASH_COLOR_BLACK;
          }
        }
      }

      display_update_boot_frame(fb);
      kernel_free(fb);
    }
  }
}

#else // Everything else

void boot_splash_start(void) {
  // No-op on platforms where the bootloader handles the splash
}

void boot_splash_stop(void) {
  // No-op on platforms where the bootloader handles the splash
}

#endif
