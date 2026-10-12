/* SPDX-License-Identifier: Apache-2.0 */

#include "applib/graphics/graphics_private.h"

void t5ai_stock_graphics_private_plot_pixel(GBitmap *framebuffer, GRect *clip_box, int x, int y,
                                            uint16_t opacity, GColor color);

// Keep uncommon clip geometry and bitmap formats on the stock implementation.
void __attribute__((optimize("O3"))) graphics_private_plot_pixel(GBitmap *framebuffer,
                                                                 GRect *clip_box, int x, int y,
                                                                 uint16_t opacity, GColor color) {
  const int16_t right = grect_get_max_x(clip_box);
  const int16_t bottom = grect_get_max_y(clip_box);
  if (right < clip_box->origin.x || bottom < clip_box->origin.y || x != (int16_t)x ||
      y != (int16_t)y) {
    t5ai_stock_graphics_private_plot_pixel(framebuffer, clip_box, x, y, opacity, color);
    return;
  }
  if (x < clip_box->origin.x || x >= right || y < clip_box->origin.y || y >= bottom) {
    return;
  }
  if (framebuffer->info.format != GBitmapFormat8Bit &&
      framebuffer->info.format != GBitmapFormat8BitCircular) {
    t5ai_stock_graphics_private_plot_pixel(framebuffer, clip_box, x, y, opacity, color);
    return;
  }

  // Alpha assignment intentionally keeps the stock two-bit truncation.
  color.a = (uint8_t)(MAX_PLOT_BRIGHTNESS - opacity);
  if (color.a == 0u) {
    return;
  }

  uint8_t *row;
  if (framebuffer->info.format == GBitmapFormat8BitCircular) {
    const GBitmapDataRowInfoInternal *info = &framebuffer->data_row_infos[(uint16_t)y];
    const int16_t min_x = info->min_x;
    const int16_t max_x = info->max_x;
    if (x < min_x || x > max_x) {
      return;
    }
    row = (uint8_t *)framebuffer->addr + info->offset;
  } else {
    const int16_t last_x = grect_get_max_x(&framebuffer->bounds) - 1;
    if (x < 0 || x > last_x) {
      return;
    }
    row = (uint8_t *)framebuffer->addr + (uint16_t)y * framebuffer->row_size_bytes;
  }
  if (color.a == 3u) {
    row[x] = color.argb;
  } else {
    row[x] = gcolor_alpha_blend(color, (GColor){.argb = row[x]}).argb;
  }
}
