/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

#include <pbl/soc/t5ai/graphics_fast.h>
#include <pbl/soc/t5ai/row_memory.h>
#ifdef CONFIG_T5AI_DRAW_PROFILE
#include <pbl/logging/logging.h>
PBL_LOG_MODULE_DEFINE(t5ai_compositor_fast, LOG_LEVEL_DEBUG);
static uint32_t s_copies, s_copy_bytes, s_clear_bytes;
#define COUNT(field, value) ((field) += (value))
#else
#define COUNT(field, value) ((void)0)
#endif

static bool prv_native(const GBitmap *bitmap, GSize size) {
  return bitmap->bounds.origin.x == 0 && bitmap->bounds.origin.y == 0 &&
         bitmap->bounds.size.w == size.w && bitmap->bounds.size.h == size.h &&
         (bitmap->info.format == GBitmapFormat8Bit ||
          (bitmap->info.format == GBitmapFormat8BitCircular &&
           (bitmap->data_row_infos ||
            (size.w == PBL_DISPLAY_WIDTH && size.h == PBL_DISPLAY_HEIGHT))));
}

static uintptr_t prv_end(const GBitmap *bitmap) {
  const unsigned height = bitmap->bounds.size.h;
  if (bitmap->info.format == GBitmapFormat8Bit) {
    return (uintptr_t)bitmap->addr + (height - 1u) * bitmap->row_size_bytes + bitmap->bounds.size.w;
  }
  if (!bitmap->data_row_infos) {
    return (uintptr_t)bitmap->addr + height * PBL_DISPLAY_WIDTH;
  }
  // Arbitrary circular row offsets need a full extent scan before any writes.
  uintptr_t end = (uintptr_t)bitmap->addr;
  for (unsigned y = 0; y < height; ++y) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(bitmap, y);
    const uintptr_t limit = (uintptr_t)row.data + row.max_x + 1u;
    if (limit > end) {
      end = limit;
    }
  }
  return end;
}

void t5ai_compositor_bitmap_into_bitmap_tiled(GBitmap *dest, const GBitmap *src, GRect rect,
                                              GPoint offset, GCompOp mode, GColor tint) {
  // Full assignment only. Legacy, tiling, clipping and blending stay stock.
  const uintptr_t a = (uintptr_t)dest->addr, b = (uintptr_t)src->addr;
  if (mode != GCompOpAssign || offset.x || offset.y || rect.origin.x || rect.origin.y ||
      rect.size.w <= 0 || rect.size.h <= 0 || !prv_native(src, rect.size) ||
      !prv_native(dest, rect.size) || gbitmap_get_format(src) != src->info.format ||
      !(prv_end(src) <= a || prv_end(dest) <= b)) {
    bitblt_bitmap_into_bitmap_tiled(dest, src, rect, offset, mode, tint);
    return;
  }
  COUNT(s_copies, 1u);
  for (unsigned y = 0; y < (unsigned)rect.size.h; ++y) {
    const GBitmapDataRowInfo d = gbitmap_get_data_row_info(dest, y);
    const GBitmapDataRowInfo s = gbitmap_get_data_row_info(src, y);
    const int first = MAX(MAX(d.min_x, s.min_x), 0);
    const int last = MIN(MIN(d.max_x, s.max_x), rect.size.w - 1);
    if (first <= last) {
      COUNT(s_copy_bytes, last - first + 1u);
      t5_row_copy(d.data + first, s.data + first, last - first + 1u);
    }
  }
}
// Only compositor.c's calls are redirected; preserve its full background clear.
void *__attribute__((optimize("O3", "no-tree-loop-distribute-patterns"))) t5ai_compositor_memset(
    void *dest, int value, size_t length) {
  COUNT(s_clear_bytes, length);
  t5_row_fill(dest, (uint8_t)value, length);
  return dest;
}

#ifdef CONFIG_T5AI_DRAW_PROFILE
void t5ai_compositor_fast_dump(void) {
  PBL_LOG_DBG("T5COMPOSITOR copies=%lu bytes=%lu clears=%lu", (unsigned long)s_copies,
              (unsigned long)s_copy_bytes, (unsigned long)s_clear_bytes);
}
#endif
