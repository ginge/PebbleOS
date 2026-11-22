/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <applib/graphics/graphics_private.h>
void t5ai_stock_graphics_private_plot_pixel(GBitmap *, GRect *, int, int, uint16_t, GColor);
static uint8_t expected[128], actual[128];
static GBitmapDataRowInfoInternal rows[8];
static GBitmap bitmap = {.row_size_bytes = 16, .bounds = {{0, 0}, {12, 8}}, .data_row_infos = rows};
static void check(GRect clip, int x, int y, uint16_t opacity, uint8_t color, uint8_t dest) {
  memset(expected, dest, sizeof(expected));
  memcpy(actual, expected, sizeof(actual));
  bitmap.addr = expected;
  t5ai_stock_graphics_private_plot_pixel(&bitmap, &clip, x, y, opacity, (GColor){.argb = color});
  bitmap.addr = actual;
  graphics_private_plot_pixel(&bitmap, &clip, x, y, opacity, (GColor){.argb = color});
  if (memcmp(actual, expected, sizeof(actual))) {
    fprintf(stderr, "mismatch format=%u x=%d y=%d opacity=%u color=%u dest=%u\n",
            bitmap.info.format, x, y, opacity, color, dest);
    __builtin_abort();
  }
}
static uint32_t state = 42;
static uint32_t random_value(void) {
  state = state * 1664525u + 1013904223u;
  return state;
}
int main(void) {
  GRect full = {{0, 0}, {16, 8}};
  for (unsigned i = 0; i < 8; ++i)
    rows[i] = (GBitmapDataRowInfoInternal){i * 16, i % 4, 15 - i % 4};
  for (unsigned fmt = 0; fmt < 6; ++fmt) {
    bitmap.info.format = fmt;
    for (unsigned c = 0; c < 256; ++c)
      for (unsigned d = 0; d < 256; ++d)
        for (unsigned a = 0; a < 4; ++a)
          check(full, 6, 3, a, c, d);
    for (unsigned a = 0; a < 65536; ++a)
      check(full, 6, 3, a, 0x91, 0x67);
    for (int x = -3; x < 20; ++x)
      for (int y = -3; y < 11; ++y)
        for (unsigned a = 0; a < 4; ++a) {
          check(full, x, y, a, 0x56, 0x89);
          check((GRect){{12, 7}, {-10, -6}}, x, y, a, 0x56, 0x89);
          check((GRect){{2, 1}, {0, 6}}, x, y, a, 0x56, 0x89);
        }
    for (unsigned i = 0; i < 100000; ++i) {
      // Rows remain valid whenever the clip admits a point.
      int x = (int)(random_value() % 25) - 4, y = random_value() % 8;
      GRect clip = {
        {(int16_t)(random_value() % 20 - 3), 0},
        {(int16_t)(random_value() % 31 - 15), 8}
      };
      bitmap.bounds.origin.x = random_value() % 4;
      bitmap.bounds.size.w = random_value() % 12;
      check(clip, x, y, random_value(), random_value(), random_value());
    }
    bitmap.bounds = (GRect){{0, 0}, {12, 8}};
    // Clip endpoint overflow and int-to-GPoint narrowing use the stock fallback.
    check((GRect){{32760, 0}, {20, 8}}, 0, 3, 0, 0x23, 0x47);
    check(full, 65542, 3, 0, 0x23, 0x47);
    check(full, 6, 65539, 0, 0x23, 0x47);
    if (fmt == GBitmapFormat8BitCircular) {
      rows[3].min_x = 32768;
      check(full, 6, 3, 1, 0x23, 0x47);
      rows[3].max_x = 65535;
      check(full, 6, 3, 1, 0x23, 0x47);
      rows[3] = (GBitmapDataRowInfoInternal){48, 3, 12};
    }
  }
  // Display-backed circular rows have no explicit table in production.
  bitmap.info.format = GBitmapFormat8BitCircular;
  bitmap.data_row_infos = NULL;
  bitmap.row_size_bytes = PBL_DISPLAY_WIDTH;
  bitmap.bounds = GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  static uint8_t implicit_expected[PBL_DISPLAY_WIDTH * PBL_DISPLAY_HEIGHT];
  static uint8_t implicit_actual[PBL_DISPLAY_WIDTH * PBL_DISPLAY_HEIGHT];
  memset(implicit_expected, 0xa5, sizeof implicit_expected);
  memcpy(implicit_actual, implicit_expected, sizeof implicit_actual);
  GRect implicit_clip = bitmap.bounds;
  bitmap.addr = implicit_expected;
  t5ai_stock_graphics_private_plot_pixel(&bitmap, &implicit_clip, 8, 7, 1, GColorWhite);
  bitmap.addr = implicit_actual;
  graphics_private_plot_pixel(&bitmap, &implicit_clip, 8, 7, 1, GColorWhite);
  assert(!memcmp(implicit_expected, implicit_actual, sizeof implicit_actual));
  // Exercise real display-sized storage, including offsets beyond 64 KiB.
  static uint8_t large_expected[260 * 260], large_actual[260 * 260];
  static GBitmapDataRowInfoInternal large_rows[260];
  GRect screen = {{0, 0}, {260, 260}};
  bitmap.bounds = screen;
  bitmap.row_size_bytes = 260;
  bitmap.data_row_infos = large_rows;
  for (unsigned y = 0; y < 260; ++y)
    large_rows[y] = (GBitmapDataRowInfoInternal){y * 260, y % 40, 259 - y % 40};
  for (unsigned format = 0; format < 2; ++format) {
    bitmap.info.format = format ? GBitmapFormat8BitCircular : GBitmapFormat8Bit;
    for (unsigned i = 0; i < sizeof(large_expected); ++i)
      large_expected[i] = i;
    memcpy(large_actual, large_expected, sizeof(large_actual));
    for (unsigned i = 0; i < 100000; ++i) {
      int x = (int)(random_value() % 270) - 5, y = random_value() % 260;
      uint16_t opacity = random_value();
      GColor color = {.argb = random_value()};
      bitmap.addr = large_expected;
      t5ai_stock_graphics_private_plot_pixel(&bitmap, &screen, x, y, opacity, color);
      bitmap.addr = large_actual;
      graphics_private_plot_pixel(&bitmap, &screen, x, y, opacity, color);
    }
    assert(!memcmp(large_expected, large_actual, sizeof(large_actual)));
  }

#ifdef BENCHMARK
  bitmap.info.format = GBitmapFormat8Bit;
  bitmap.row_size_bytes = 260;
  const unsigned iterations = 5000000;
  for (unsigned run = 0; run < 3; ++run) {
    memset(large_expected, 0xa5, sizeof large_expected);
    memcpy(large_actual, large_expected, sizeof large_actual);
    bitmap.addr = large_expected;
    clock_t begin = clock();
    for (unsigned i = 0; i < iterations; ++i)
      t5ai_stock_graphics_private_plot_pixel(&bitmap, &screen, i % 260, (i / 260) % 260, i % 4,
                                             (GColor){.argb = i});
    double stock = (double)(clock() - begin) / CLOCKS_PER_SEC;
    bitmap.addr = large_actual;
    begin = clock();
    for (unsigned i = 0; i < iterations; ++i)
      graphics_private_plot_pixel(&bitmap, &screen, i % 260, (i / 260) % 260, i % 4,
                                  (GColor){.argb = i});
    double fast = (double)(clock() - begin) / CLOCKS_PER_SEC;
    assert(!memcmp(large_expected, large_actual, sizeof large_actual));
    printf("pixel iterations=%u stock_ms=%.3f fast_ms=%.3f ratio=%.3f\n", iterations, stock * 1000,
           fast * 1000, fast / stock);
  }
#endif
  puts("pixel equivalence: all formats, colour pairs, alpha values and geometry passed");
  return 0;
}
