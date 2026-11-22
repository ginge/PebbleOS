/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pbl/drivers/display.h>
#include <pbl/soc/t5ai/graphics_fast.h>

#include <applib/graphics/gcontext.h>
#include <process_state/app_state/app_state.h>

extern bool test_legacy;
#define STORAGE (280 * 280)
static uint8_t source[STORAGE], expected[STORAGE], actual[STORAGE];
static GBitmapDataRowInfoInternal source_rows[280], dest_rows[280];
static void compare(GBitmap src, GBitmap dst, GRect rect, GPoint offset, GCompOp mode) {
  for (unsigned i = 0; i < STORAGE; ++i)
    source[i] = i * 137u + 31u;
  memset(expected, 0xa5, STORAGE);
  memcpy(actual, expected, STORAGE);
  src.addr = source;
  dst.addr = expected;
  bitblt_bitmap_into_bitmap_tiled(&dst, &src, rect, offset, mode, GColorWhite);
  dst.addr = actual;
  t5ai_compositor_bitmap_into_bitmap_tiled(&dst, &src, rect, offset, mode, GColorWhite);
  assert(!memcmp(actual, expected, STORAGE));
}
static void compare_overlap(unsigned displacement) {
  uint8_t first[STORAGE + 64], second[STORAGE + 64];
  for (unsigned i = 0; i < sizeof first; ++i)
    first[i] = second[i] = i * 17u;
  GRect bounds = GRect(0, 0, 16, 12);
  GBitmap src = {.addr = first, .bounds = bounds, .row_size_bytes = 20};
  src.info.format = GBitmapFormat8Bit;
  src.info.version = GBITMAP_VERSION_CURRENT;
  GBitmap dst = src;
  dst.addr = first + displacement;
  bitblt_bitmap_into_bitmap_tiled(&dst, &src, bounds, GPointZero, GCompOpAssign, GColorWhite);
  src.addr = second;
  dst.addr = second + displacement;
  t5ai_compositor_bitmap_into_bitmap_tiled(&dst, &src, bounds, GPointZero, GCompOpAssign,
                                           GColorWhite);
  assert(!memcmp(first, second, sizeof first));
}
int main(void) {
  GRect bounds = GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  GBitmap src = {.bounds = bounds, .row_size_bytes = PBL_DISPLAY_WIDTH};
  src.info.version = GBITMAP_VERSION_CURRENT;
  GBitmap dst = src;
  for (unsigned sf = 0; sf < 2; ++sf)
    for (unsigned df = 0; df < 2; ++df)
      for (unsigned pad = 0; pad < 4; ++pad)
        for (unsigned pattern = 0; pattern < 3; ++pattern) {
          src.info.format = sf ? GBitmapFormat8BitCircular : GBitmapFormat8Bit;
          dst.info.format = df ? GBitmapFormat8BitCircular : GBitmapFormat8Bit;
          src.row_size_bytes = dst.row_size_bytes = PBL_DISPLAY_WIDTH + pad;
          src.data_row_infos = sf ? source_rows : NULL;
          dst.data_row_infos = df ? dest_rows : NULL;
          for (unsigned y = 0; y < PBL_DISPLAY_HEIGHT; ++y) {
            unsigned inset = y % 40, other = pattern == 0 ? inset : (pattern == 1 ? y % 23 : 0);
            source_rows[y] = (GBitmapDataRowInfoInternal){
              y * src.row_size_bytes, inset, PBL_DISPLAY_WIDTH - 1 - inset
            };
            // Non-monotonic offsets are legal; reserve distinct rows in reverse order.
            dest_rows[y] = (GBitmapDataRowInfoInternal){
              (PBL_DISPLAY_HEIGHT - 1 - y) * dst.row_size_bytes, other,
              PBL_DISPLAY_WIDTH - 1 - other
            };
          }
          for (unsigned mode = GCompOpAssign; mode <= GCompOpTintLuminance; ++mode) {
            compare(src, dst, bounds, GPointZero, mode);
            compare(src, dst, GRect(3, 5, 180, 190), GPoint(2, 1), mode);
          }
          // Circular display fallback with board mask and no per-bitmap row table.
          if (sf) {
            src.data_row_infos = NULL;
            compare(src, dst, bounds, GPointZero, GCompOpAssign);
          }
        }

  for (unsigned shift = 0; shift <= 32; ++shift)
    compare_overlap(shift);
  src.info.format = dst.info.format = GBitmapFormat8Bit;
  src.row_size_bytes = dst.row_size_bytes = 48;
  src.bounds = dst.bounds = GRect(0, 0, 37, 29);
  compare(src, dst, src.bounds, GPointZero, GCompOpAssign);
  src.bounds = dst.bounds = GRect(2, 1, 34, 27);
  compare(src, dst, GRect(2, 1, 30, 20), GPointZero, GCompOpAssign);
  src.bounds = dst.bounds = bounds;
  src.row_size_bytes = dst.row_size_bytes = PBL_DISPLAY_WIDTH;
#ifndef LEGACY_WIDE
  // Keep the normal differential run independent of the stock signed bit-31 shift.
  src.bounds = dst.bounds = GRect(0, 0, 16, 12);
#endif
  GRect legacy_bounds = src.bounds;
  src.info.version = GBITMAP_VERSION_0;
  compare(src, dst, legacy_bounds, GPointZero, GCompOpAssign);
  src.info.version = GBITMAP_VERSION_CURRENT;
  test_legacy = true;
  compare(src, dst, legacy_bounds, GPointZero, GCompOpAssign);
  test_legacy = false;
#ifdef BENCHMARK
  src.info.format = dst.info.format = GBitmapFormat8Bit;
  src.bounds = dst.bounds = bounds;
  src.row_size_bytes = dst.row_size_bytes = PBL_DISPLAY_WIDTH;
  src.addr = source;
  dst.addr = actual;
  const unsigned iterations = 20000;
  for (unsigned run = 0; run < 3; ++run) {
    clock_t begin = clock();
    for (unsigned i = 0; i < iterations; ++i)
      bitblt_bitmap_into_bitmap_tiled(&dst, &src, bounds, GPointZero, GCompOpAssign, GColorWhite);
    double stock = (double)(clock() - begin) / CLOCKS_PER_SEC;
    begin = clock();
    for (unsigned i = 0; i < iterations; ++i)
      t5ai_compositor_bitmap_into_bitmap_tiled(&dst, &src, bounds, GPointZero, GCompOpAssign,
                                               GColorWhite);
    double fast = (double)(clock() - begin) / CLOCKS_PER_SEC;
    printf("rectangular iterations=%u stock_ms=%.3f fast_ms=%.3f ratio=%.3f\n", iterations,
           stock * 1000, fast * 1000, fast / stock);
  }
#endif
  puts("complete compositor/stock differential passed");
  return 0;
}
