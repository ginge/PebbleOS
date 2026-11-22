/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pbl/kernel/thread.h>
#include <pbl/soc/t5ai/text_node_cache.h>

#include <applib/graphics/gcontext.h>
#include <applib/graphics/graphics_private.h>
#include <applib/graphics/perimeter.h>
#include <applib/graphics/text.h>
#include <applib/graphics/text_render.h>
#include <kernel/pebble_tasks.h>

[[noreturn]] void pbl_panic_at(const char *file, int line, const char *fmt, ...) {
  abort();
}
[[noreturn]] void util_assertion_failed(const char *file, int line) {
  abort();
}
void *applib_malloc(size_t size) {
  return malloc(size);
}
void applib_free(void *ptr) {
  free(ptr);
}
GSize graphics_context_get_framebuffer_size(GContext *ctx) {
  return GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
}
bool app_state_get_text_perimeter_debugging_enabled(void) {
  return false;
}
void graphics_private_draw_horizontal_line_prepared(GContext *ctx, GBitmap *framebuffer,
                                                    GRect *clip_box, int16_t y, Fixed_S16_3 x1,
                                                    Fixed_S16_3 x2, GColor color) {
  assert(false);
}
static struct pbl_thread app, other;
struct pbl_thread *pbl_cur = &app;
static unsigned control, height_calls;
static PebbleTask task = PebbleTask_App;
unsigned test_control(void) {
  return control;
}
PebbleTask pebble_task_get_current(void) {
  return task;
}
uint8_t fonts_get_font_height(GFont font) {
  ++height_calls;
  return font->max_height;
}
void render_glyph(GContext *ctx, uint32_t cp, FontInfo *font, GRect cursor) {
  assert(false);
}
static void (*render_body)(void);
void window_render(Window *w, GContext *ctx) {
  render_body();
}
static GContext context;
static FontInfo fonts[2] = {{.max_height = 10}, {.max_height = 14}};
static GRect box = {{0, 0}, {80, 100}};
static bool eligible = true;
static GTextOverflowMode overflow = GTextOverflowModeWordWrap;
static GTextAlignment alignment = GTextAlignmentLeft;
static TextLayoutFlowData flow;
static GRangeHorizontal perimeter(const GPerimeter *p, const GSize *size, GRangeVertical range,
                                  uint16_t inset) {
  return (GRangeHorizontal){inset, size->w - 2 * inset};
}
static const GPerimeter boundary = {.callback = perimeter};
static void query(const char *text, unsigned f, int spacing, unsigned inset) {
  TextLayoutExtended expected = {.line_spacing_delta = spacing, .flow_data = flow},
                     actual = expected;
  expected.flow_data.perimeter.inset = actual.flow_data.perimeter.inset = inset;
  GSize stock = graphics_text_layout_get_max_used_size(&context, text, &fonts[f], box, overflow,
                                                       alignment, (TextLayout *)&expected);
  GSize cached = t5ai_text_node_cached_size(&context, text, &fonts[f], box, overflow, alignment,
                                            (TextLayout *)&actual);
  assert(stock.w == cached.w && stock.h == cached.h);
  assert(!memcmp(&expected, &actual, sizeof expected));
}
static void check_hit_and_bypass(void) {
  TextLayoutExtended first = {0}, second = {0};
  unsigned before = height_calls;
  t5ai_text_node_cached_size(&context, "unique render freshness", &fonts[0], box,
                             GTextOverflowModeWordWrap, GTextAlignmentLeft, (TextLayout *)&first);
  assert(height_calls > before);
  before = height_calls;
  t5ai_text_node_cached_size(&context, "unique render freshness", &fonts[0], box,
                             GTextOverflowModeWordWrap, GTextAlignmentLeft, (TextLayout *)&second);
  assert(eligible ? height_calls == before : height_calls > before);
  assert(!memcmp(&first, &second, sizeof first));
  if (eligible) {
    struct pbl_thread *owner = pbl_cur;
    pbl_cur = owner == &app ? &other : &app;
    TextLayoutExtended third = {0};
    before = height_calls;
    t5ai_text_node_cached_size(&context, "unique render freshness", &fonts[0], box,
                               GTextOverflowModeWordWrap, GTextAlignmentLeft, (TextLayout *)&third);
    assert(height_calls > before);
    pbl_cur = owner;
  }
}
static void exercise(void) {
  check_hit_and_bypass();
  query("hello world", 0, 0, 0);
  query("hello world", 0, 0, 0);
  query("hello world", 1, 0, 0);
  query("hello world", 0, 2, 0);
  query("hello world", 0, 0, 1);
  query("hello\nworld", 0, 0, 0);
  query("مرحبا שלום hello", 0, 0, 0);
  box.origin.y = 5;
  query("hello world", 0, 0, 0);
  box.origin.y = 0;
  char mutable[] = "hello world";
  query(mutable, 0, 0, 0);
  mutable[0] = 'j';
  query(mutable, 0, 0, 0);
  for (unsigned mode = GTextOverflowModeWordWrap; mode <= GTextOverflowModeFill; ++mode)
    for (unsigned align = GTextAlignmentLeft; align <= GTextAlignmentRight; ++align) {
      overflow = mode;
      alignment = align;
      query("hello world with multiple lines", 0, 0, 0);
    }
  overflow = GTextOverflowModeWordWrap;
  alignment = GTextAlignmentLeft;
  flow.perimeter.impl = &boundary;
  query("flow with multiple words", 0, 0, 3);
  flow.paging.page_on_screen = (GRangeVertical){0, 50};
  query("flow with multiple words", 0, 0, 3);
  flow.paging.origin_on_screen.y = 10;
  query("flow with multiple words", 0, 0, 3);
  flow = (TextLayoutFlowData){};
  for (unsigned i = 0; i < 20; ++i) {
    char unique[24];
    snprintf(unique, sizeof unique, "eviction %u", i);
    query(unique, 0, 0, 0);
  }
  query("hello world", 0, 0, 0);
  char text[120];
  memset(text, 'x', 119);
  text[119] = 0;
  query(text, 0, 0, 0);
}
#ifdef BENCHMARK
static void benchmark(void) {
  const unsigned iterations = 20000;
  const char *text = "Repeated timeline measurement with wrapping and several words";
  char unique[20][80];
  for (unsigned i = 0; i < 20; ++i)
    snprintf(unique[i], sizeof unique[i],
             "Unique timeline layout %u with wrapping and several words", i);
  for (unsigned misses = 0; misses < 2; ++misses)
    for (unsigned run = 0; run < 3; ++run) {
      GSize stock = {0}, cached = {0};
      clock_t begin = clock();
      for (unsigned i = 0; i < iterations; ++i) {
        TextLayoutExtended l = {0};
        stock = graphics_text_layout_get_max_used_size(&context, misses ? unique[i % 20] : text,
                                                       &fonts[0], box, GTextOverflowModeWordWrap,
                                                       GTextAlignmentLeft, (TextLayout *)&l);
      }
      double original = (double)(clock() - begin) / CLOCKS_PER_SEC;
      begin = clock();
      for (unsigned i = 0; i < iterations; ++i) {
        TextLayoutExtended l = {0};
        cached = t5ai_text_node_cached_size(&context, misses ? unique[i % 20] : text, &fonts[0],
                                            box, GTextOverflowModeWordWrap, GTextAlignmentLeft,
                                            (TextLayout *)&l);
      }
      double fast = (double)(clock() - begin) / CLOCKS_PER_SEC;
      assert(stock.w == cached.w && stock.h == cached.h);
      printf("text_cache misses=%u iterations=%u stock_ms=%.3f cache_ms=%.3f ratio=%.3f\n", misses,
             iterations, original * 1000, fast * 1000, fast / original);
    }
}
#endif
int main(void) {
  render_body = exercise;
  t5ai_text_node_cache_window_render(NULL, &context);
  t5ai_text_node_cache_window_render(NULL, &context);
  eligible = false;
  control = 1;
  t5ai_text_node_cache_window_render(NULL, &context);
  control = 0;
  eligible = true;
  pbl_cur = &other;
  t5ai_text_node_cache_window_render(NULL, &context);
  pbl_cur = &app;
  eligible = false;
  task = PebbleTask_KernelMain;
  t5ai_text_node_cache_window_render(NULL, &context);
  task = PebbleTask_App;
  eligible = true;
#ifdef BENCHMARK
  render_body = benchmark;
  t5ai_text_node_cache_window_render(NULL, &context);
#endif
  puts("complete text-cache/layout differential passed");
  return 0;
}
