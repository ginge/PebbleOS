/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/graphics_fast.h>
#include <pbl/soc/t5ai/power.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/text_node_cache.h>

#include <applib/fonts/fonts.h>
#include <applib/graphics/graphics_private.h>

PBL_LOG_MODULE_DEFINE(t5ai_graphics_bench, LOG_LEVEL_DEBUG);
#define HEIGHT 4u
#define BYTES  (PBL_DISPLAY_WIDTH * HEIGHT)
// Private PSRAM scratch, never the live framebuffer or CP-owned storage.
static uint8_t s_src[BYTES] __attribute__((section(".t5ai_board_cache"), aligned(32)));
static uint8_t s_dst[BYTES] __attribute__((section(".t5ai_board_cache"), aligned(32)));
void t5ai_stock_graphics_private_plot_pixel(GBitmap *, GRect *, int, int, uint16_t, GColor);

static uint32_t prv_copy(GBitmap *dst, GBitmap *src, bool fast) {
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  for (unsigned i = 0; i < 1024u; ++i) {
    if (fast) {
      t5ai_compositor_bitmap_into_bitmap_tiled(dst, src, dst->bounds, GPointZero, GCompOpAssign,
                                               GColorWhite);
    } else {
      bitblt_bitmap_into_bitmap_tiled(dst, src, dst->bounds, GPointZero, GCompOpAssign,
                                      GColorWhite);
    }
  }
  return T5AI_AON_RTC_COUNT_LOW - start;
}

static uint32_t prv_clear(bool fast) {
  typedef void *(*Fill)(void *, int, size_t);
  // Keep actual linked calls on both sides, including when the compiler knows the buffer.
  static Fill volatile stock_fill = memset, fast_fill = t5ai_compositor_memset;
  Fill fill = fast ? fast_fill : stock_fill;
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  for (unsigned i = 0; i < 1024u; ++i) {
    fill(s_dst, 0xa5, BYTES);
  }
  return T5AI_AON_RTC_COUNT_LOW - start;
}

static bool prv_clear_ok(void) {
  for (unsigned i = 0; i < BYTES; ++i) {
    if (s_dst[i] != 0xa5) {
      return false;
    }
  }
  return true;
}

static uint32_t prv_pixels(GBitmap *bitmap, bool fast) {
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  for (unsigned i = 0; i < 100000u; ++i) {
    const int x = i % PBL_DISPLAY_WIDTH, y = (i / PBL_DISPLAY_WIDTH) % HEIGHT;
    if (fast) {
      graphics_private_plot_pixel(bitmap, &bitmap->bounds, x, y, i % 4, (GColor){.argb = i});
    } else {
      t5ai_stock_graphics_private_plot_pixel(bitmap, &bitmap->bounds, x, y, i % 4,
                                             (GColor){.argb = i});
    }
  }
  return T5AI_AON_RTC_COUNT_LOW - start;
}

static uint32_t prv_text(GContext *ctx, GFont font, bool fast, bool misses, GSize *size) {
  static const char repeat[] = "Repeated timeline measurement with wrapping and several words";
  char unique[80];
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  for (unsigned i = 0; i < 128u; ++i) {
    // Twenty keys exceed the sixteen-entry cache. Keep construction outside layout semantics.
    memcpy(unique, repeat, sizeof(repeat));
    unique[0] = 'A' + i % 20u;
    const char *text = misses ? unique : repeat;
    TextLayoutExtended layout = {};
    const GRect box = GRect(0, 0, PBL_DISPLAY_WIDTH, 160);
    if (fast) {
      *size = t5ai_text_node_cached_size(ctx, text, font, box, GTextOverflowModeWordWrap,
                                         GTextAlignmentLeft, (TextLayout *)&layout);
    } else {
      *size =
          graphics_text_layout_get_max_used_size(ctx, text, font, box, GTextOverflowModeWordWrap,
                                                 GTextAlignmentLeft, (TextLayout *)&layout);
    }
  }
  return T5AI_AON_RTC_COUNT_LOW - start;
}

// Called once inside the privileged App render/cache scope.
void t5ai_graphics_bench(GContext *ctx) {
  PBL_LOG_DBG("T5GBENCH begin hz=%lu tick_hz=%lu width=%u height=%u",
              (unsigned long)t5ai_power_clock_hz(), (unsigned long)T5AI_AON_RTC_HZ,
              PBL_DISPLAY_WIDTH, HEIGHT);
  GBitmap src = {
    .addr = s_src,
    .bounds = GRect(0, 0, PBL_DISPLAY_WIDTH, HEIGHT),
    .row_size_bytes = PBL_DISPLAY_WIDTH
  };
  src.info.format = GBitmapFormat8Bit;
  src.info.version = GBITMAP_VERSION_CURRENT;
  GBitmap dst = src;
  dst.addr = s_dst;
  for (unsigned i = 0; i < BYTES; ++i) {
    s_src[i] = i * 137u + 31u;
  }
  // Warm real fonts before recording layout samples. Do not use a fixture font on hardware.
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GSize expected = {}, actual = {};
  (void)prv_text(ctx, font, false, false, &expected);
  for (unsigned sample = 0; sample < 3u; ++sample) {
    uint32_t stock, fast;
    memset(s_dst, 0xa5, BYTES);
    bool copy_ok;
    if (sample & 1u) {
      fast = prv_copy(&dst, &src, true);
      copy_ok = memcmp(s_src, s_dst, BYTES) == 0;
      memset(s_dst, 0xa5, BYTES);
      stock = prv_copy(&dst, &src, false);
    } else {
      stock = prv_copy(&dst, &src, false);
      copy_ok = memcmp(s_src, s_dst, BYTES) == 0;
      memset(s_dst, 0xa5, BYTES);
      fast = prv_copy(&dst, &src, true);
    }
    copy_ok = copy_ok && memcmp(s_src, s_dst, BYTES) == 0;
    PBL_LOG_DBG("T5GBENCH kind=copy sample=%u n=1024 stock=%lu fast=%lu ok=%u", sample,
                (unsigned long)stock, (unsigned long)fast, copy_ok);
    bool clear_ok;
    if (sample & 1u) {
      fast = prv_clear(true);
      clear_ok = prv_clear_ok();
      stock = prv_clear(false);
    } else {
      stock = prv_clear(false);
      clear_ok = prv_clear_ok();
      fast = prv_clear(true);
    }
    clear_ok = clear_ok && prv_clear_ok();
    PBL_LOG_DBG("T5GBENCH kind=clear sample=%u n=1024 stock=%lu fast=%lu ok=%u", sample,
                (unsigned long)stock, (unsigned long)fast, clear_ok);
    // Both paths receive identical initial bytes; compare final bytes without another buffer.
    memset(s_dst, 0xa5, BYTES);
    const uint32_t first = prv_pixels(&dst, (sample & 1u) != 0u);
    memcpy(s_src, s_dst, BYTES);
    memset(s_dst, 0xa5, BYTES);
    const uint32_t second = prv_pixels(&dst, (sample & 1u) == 0u);
    stock = sample & 1u ? second : first;
    fast = sample & 1u ? first : second;
    const bool pixel_ok = memcmp(s_src, s_dst, BYTES) == 0;
    PBL_LOG_DBG("T5GBENCH kind=pixel sample=%u n=100000 stock=%lu fast=%lu ok=%u", sample,
                (unsigned long)stock, (unsigned long)fast, pixel_ok);
    for (unsigned misses = 0; misses < 2u; ++misses) {
      stock = prv_text(ctx, font, false, misses, &expected);
      fast = prv_text(ctx, font, true, misses, &actual);
      PBL_LOG_DBG("T5GBENCH kind=text sample=%u misses=%u n=128 stock=%lu fast=%lu ok=%u", sample,
                  misses, (unsigned long)stock, (unsigned long)fast,
                  expected.w == actual.w && expected.h == actual.h);
    }
  }
  extern void t5ai_text_node_cache_dump(void);
  t5ai_text_node_cache_dump();
  PBL_LOG_DBG("T5GBENCH end hz=%lu", (unsigned long)t5ai_power_clock_hz());
}
