/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"

#include <errno.h>
#include <string.h>

#include <pbl/drivers/display/t5ai.h>
#include <pbl/drivers/panel/co5300.h>
#include <pbl/kernel/delay.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/rtc.h>
#define PANEL_WIDTH                  466u
#define PANEL_HEIGHT                 466u
#define DISPLAY_COLOR_COUNT          64u
#define DISPLAY_COLOR_RED_SHIFT      4u
#define DISPLAY_COLOR_GREEN_SHIFT    2u
#define DISPLAY_COLOR_COMPONENT_MASK 3u
#define DISPLAY_RGB565_GREEN_STEP    21u
#define DISPLAY_RGB565_RED_SHIFT     11u
#define DISPLAY_RGB565_GREEN_SHIFT   5u
#define DISPLAY_COLOR_MASK           63u
#ifdef CONFIG_T5AI_DISPLAY_RECOVERY
static unsigned s_retries;
#endif
#ifdef CONFIG_T5AI_DISPLAY_FAULT_TEST
static bool s_injected;
#endif
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
static bool s_disabled, s_reprepare, s_full_redraw, s_sleep_complete, s_enable_pending;
#endif
static uint16_t s_rgb565[64];
static uint16_t s_panel_source_x[PANEL_WIDTH];
#define DISPLAY_BATCH_ROWS  8u
#define DISPLAY_BATCH_BYTES (DISPLAY_BATCH_ROWS * 2u * PANEL_WIDTH * 2u)
static uint8_t s_pixels[DISPLAY_BATCH_BYTES]
    __attribute__((section(".t5ai_display_dma"), aligned(T5AI_SHARED_ALIGNMENT)));
PBL_LOG_MODULE_DEFINE(driver_display_co5300, LOG_LEVEL_DEBUG);
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
volatile T5AIDrawStats g_t5ai_draw_stats;
#endif
static void prv_init_conversion_tables(void) {
  // Rounded expansion of 2-bit red/blue to 5 bits: 0, 10, 21, 31.
  static const uint8_t red_blue[4] = {0u, 10u, 21u, 31u};
  for (unsigned framebuffer_pixel = 0u; framebuffer_pixel < DISPLAY_COLOR_COUNT;
       ++framebuffer_pixel) {
    const uint16_t red =
        red_blue[(framebuffer_pixel >> DISPLAY_COLOR_RED_SHIFT) & DISPLAY_COLOR_COMPONENT_MASK];
    const uint16_t green =
        ((framebuffer_pixel >> DISPLAY_COLOR_GREEN_SHIFT) & DISPLAY_COLOR_COMPONENT_MASK) *
        DISPLAY_RGB565_GREEN_STEP;
    const uint16_t blue = red_blue[framebuffer_pixel & DISPLAY_COLOR_COMPONENT_MASK];
    const uint16_t rgb565 =
        (red << DISPLAY_RGB565_RED_SHIFT) | (green << DISPLAY_RGB565_GREEN_SHIFT) | blue;
    // Store in the byte order consumed by the panel so the conversion loop
    // can emit one aligned halfword instead of two byte stores.
    s_rgb565[framebuffer_pixel] = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
  }
  for (unsigned x = 0u; x < PANEL_WIDTH; ++x) {
    s_panel_source_x[x] = x * PBL_DISPLAY_WIDTH / PANEL_WIDTH;
  }
}

static void __attribute__((section(".t5ai_ramfunc"), noinline, optimize("O3"))) prv_convert_row(
    uint8_t *destination, const uint8_t *source, bool reverse, bool invert_colors) {
  uint16_t *output = (uint16_t *)destination;
  unsigned previous_source_x = PBL_DISPLAY_WIDTH;
  uint16_t converted_pixel = 0u;
  for (unsigned x = 0u; x < PANEL_WIDTH; ++x) {
    unsigned source_x = s_panel_source_x[x];
    if (reverse) {
      source_x = PBL_DISPLAY_WIDTH - 1u - source_x;
    }
    // Scaling 260 pixels to 466 means most logical pixels are emitted twice.
    // The framebuffer is in PSRAM, so avoid rereading and reconverting a pixel
    // while the nearest-neighbour mapping still selects the same source x.
    if (source_x != previous_source_x) {
      unsigned framebuffer_pixel = source[source_x] & DISPLAY_COLOR_MASK;
      if (invert_colors) {
        framebuffer_pixel ^= DISPLAY_COLOR_MASK;
      }
      converted_pixel = s_rgb565[framebuffer_pixel];
      previous_source_x = source_x;
    }
    output[x] = converted_pixel;
  }
}

static int prv_window(uint16_t y0, uint16_t y1) {
  // CO5300 RASET requires an even start row and an even row count.
  if ((y0 & 1u) || ((y1 - y0 + 1u) & 1u))
    return -EINVAL;
  const uint8_t commands[][8] = {
    {0x02, 0, 0x2a, 0, 0, 6, 1, 0xd7},
    {0x02, 0, 0x2b, 0, y0 >> 8, y0, y1 >> 8, y1},
  };
  for (unsigned i = 0; i < 2; ++i) {
    const struct pbl_spi_xfer xfer = {.tx_buf = commands[i], .len = 8};
    const int ret = pbl_spi_transfer(&CO5300.spi, &xfer, 1);
    if (ret != 0)
      return ret;
  }
  return 0;
}
static int prv_send(uint16_t y0, uint16_t y1, const void *pixels, size_t length) {
  int ret = prv_window(y0, y1);
  if (ret != 0)
    return ret;
  const uint8_t command[] = {0x32, 0, 0x2c, 0};
  const struct pbl_spi_xfer xfers[] = {
    {.tx_buf = command, .len = sizeof(command)},
    {.tx_buf = pixels, .len = length, .tx_nbits = 4},
  };
  return pbl_spi_transfer(&CO5300.spi, xfers, 2);
}
static int prv_init(const struct pbl_display *disp) {
  (void)disp;
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  s_disabled = s_reprepare = s_full_redraw = s_sleep_complete = s_enable_pending = false;
#endif
  prv_init_conversion_tables();
  int ret = pbl_panel_prepare(&CO5300.panel);
  return ret != 0 ? ret : pbl_panel_enable(&CO5300.panel);
}
static int prv_enable(const struct pbl_display *disp) {
  (void)disp;
  pbl_mutex_lock(&co5300_lock, PBL_FOREVER);
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  int ret = 0;
  if (s_reprepare) {
    ret = pbl_panel_prepare(&CO5300.panel);
    if (ret == 0)
      s_full_redraw = s_enable_pending = true;
  } else if (!s_enable_pending) {
    ret = pbl_panel_enable(&CO5300.panel);
  }
  if (ret == 0)
    s_disabled = s_reprepare = s_sleep_complete = false;
#else
  const int ret = pbl_panel_enable(&CO5300.panel);
#endif
  pbl_mutex_unlock(&co5300_lock);
  return ret;
}
static int prv_disable(const struct pbl_display *disp) {
  (void)disp;
  pbl_mutex_lock(&co5300_lock, PBL_FOREVER);
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  int ret = 0;
  if (!s_sleep_complete) {
    s_disabled = s_reprepare = true;
    s_enable_pending = false;
    ret = pbl_panel_disable(&CO5300.panel);
    if (ret == 0)
      ret = pbl_panel_unprepare(&CO5300.panel);
    s_sleep_complete = ret == 0;
  }
#else
  const int ret = pbl_panel_disable(&CO5300.panel);
#endif
  pbl_mutex_unlock(&co5300_lock);
  return ret;
}
static int prv_rotated(const struct pbl_display *disp, bool rotated) {
  (void)disp;
  (void)rotated;
  return 0;
}
static int prv_update(const struct pbl_display *disp, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1) {
  if (!buf->data || buf->pitch < disp->width)
    return -EINVAL;
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  const uint32_t start = t5ai_rtc_get_raw_ticks();
#endif
  pbl_mutex_lock(&co5300_lock, PBL_FOREVER);
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  if (s_disabled) {
    pbl_mutex_unlock(&co5300_lock);
    return -EHOSTDOWN;
  }
  if (s_full_redraw) {
    y0 = 0u;
    y1 = disp->height - 1u;
  }
#endif
  int ret = 0;
#ifdef CONFIG_T5AI_DISPLAY_RECOVERY
retry:
#endif
  const uint16_t logical_first = disp->state->rotated ? disp->height - 1u - y1 : y0;
  const uint16_t logical_end = disp->state->rotated ? (unsigned)disp->height - y0 : y1 + 1u;
  const uint16_t first = ((logical_first * PANEL_HEIGHT + disp->height - 1u) / disp->height) & ~1u;
  const uint16_t end =
      (((logical_end * PANEL_HEIGHT + disp->height - 1u) / disp->height) + 1u) & ~1u;
  // Align physical windows, resending adjacent rows from the native framebuffer as needed.
  for (uint16_t y = first; y < end; y += DISPLAY_BATCH_ROWS * 2u) {
    const uint16_t batch_end =
        y + DISPLAY_BATCH_ROWS * 2u < end ? y + DISPLAY_BATCH_ROWS * 2u : end;
    uint16_t previous = UINT16_MAX;
    for (uint16_t row = y; row < batch_end; ++row) {
      uint16_t source_y = row * disp->height / PANEL_HEIGHT;
      if (disp->state->rotated)
        source_y = disp->height - 1u - source_y;
      uint8_t *pixels = s_pixels + (row - y) * PANEL_WIDTH * 2u;
      if (source_y == previous) {
        memcpy(pixels, pixels - PANEL_WIDTH * 2u, PANEL_WIDTH * 2u);
      } else {
        prv_convert_row(pixels, buf->data + source_y * buf->pitch, disp->state->rotated, false);
      }
      previous = source_y;
    }
    ret = prv_send(y, batch_end - 1u, s_pixels, (batch_end - y) * PANEL_WIDTH * 2u);
#ifdef CONFIG_T5AI_DISPLAY_FAULT_TEST
    if (ret == 0 && !s_injected && t5ai_rtc_get_raw_ticks() > 8u * T5AI_AON_RTC_HZ) {
      s_injected = true;
      ret = -EIO;
    }
#endif
    if (ret != 0)
      break;
  }
#ifdef CONFIG_T5AI_DISPLAY_RECOVERY
  if (ret != 0 && s_retries < 3u) {
    ++s_retries;
    pbl_sleep_ms(1000);
    goto retry;
  }
#endif
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  // Reset loses GRAM; show the panel only after a successful full redraw.
  if (ret == 0 && s_enable_pending) {
    ret = pbl_panel_enable(&CO5300.panel);
    if (ret == 0)
      s_enable_pending = false;
  }
  if (ret == 0)
    s_full_redraw = false;
#endif
  pbl_mutex_unlock(&co5300_lock);
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  g_t5ai_draw_stats.frames++;
  g_t5ai_draw_stats.last_update_ticks = (uint32_t)t5ai_rtc_get_raw_ticks() - start;
  g_t5ai_draw_stats.total_update_ticks += g_t5ai_draw_stats.last_update_ticks;
  if (g_t5ai_draw_stats.last_update_ticks > g_t5ai_draw_stats.max_update_ticks)
    g_t5ai_draw_stats.max_update_ticks = g_t5ai_draw_stats.last_update_ticks;
#endif
  pbl_display_complete(disp, ret);
  return 0;
}
static int prv_clear(const struct pbl_display *disp) {
  (void)disp;
  pbl_mutex_lock(&co5300_lock, PBL_FOREVER);
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  if (s_disabled) {
    pbl_mutex_unlock(&co5300_lock);
    return -EHOSTDOWN;
  }
#endif
  memset(s_pixels, 0, sizeof(s_pixels));
  int ret = 0;
  for (uint16_t y = 0; y < PANEL_HEIGHT; y += DISPLAY_BATCH_ROWS * 2u) {
    const uint16_t end =
        y + DISPLAY_BATCH_ROWS * 2u < PANEL_HEIGHT ? y + DISPLAY_BATCH_ROWS * 2u : PANEL_HEIGHT;
    ret = prv_send(y, end - 1u, s_pixels, (end - y) * PANEL_WIDTH * 2u);
    if (ret != 0)
      break;
  }
  pbl_mutex_unlock(&co5300_lock);
  return ret;
}
const struct pbl_display_ops pbl_display_co5300_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .set_rotated = prv_rotated,
  .update = prv_update,
  .clear = prv_clear,
};
