/* SPDX-License-Identifier: Apache-2.0 */
#include "applib/graphics/text.h"
#include "applib/ui/window.h"
#include "kernel/pebble_tasks.h"

#include <string.h>

#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>

#include <cmsis_core.h>

PBL_LOG_MODULE_DEFINE(t5ai_text_node_cache, LOG_LEVEL_DEBUG);
#define CACHE_ENTRIES 16u
#define CACHE_TEXT    96u
typedef struct {
  bool valid;
  char text[CACHE_TEXT];
  TextLayout result;
  int16_t spacing;
  TextLayoutFlowData flow;
} Entry;
static Entry s_cache[CACHE_ENTRIES] __attribute__((section(".t5ai_board_cache"), aligned(32)));
static struct pbl_thread *s_owner;
static bool s_active;
static unsigned s_replace;
static uint32_t s_hits, s_misses, s_bypass;
extern struct pbl_thread *pbl_cur;
void t5ai_cache_stock_window_render(Window *, GContext *);
void window_render(Window *window, GContext *ctx) {
  if ((__get_CONTROL() & 1u) || pebble_task_get_current() != PebbleTask_App) {
    t5ai_cache_stock_window_render(window, ctx);
    return;
  }
  // Only privileged App rendering owns this cache. Each render invalidates fonts/text/state.
  s_owner = pbl_cur;
  s_active = true;
  s_replace = 0u;
  for (unsigned i = 0; i < CACHE_ENTRIES; ++i)
    s_cache[i].valid = false;
  t5ai_cache_stock_window_render(window, ctx);
  s_active = false;
}
// Only text_node.c redirects its call here; that caller always supplies TextLayoutExtended.
GSize t5ai_text_node_cached_size(GContext *ctx, const char *text, GFont font, GRect box,
                                 GTextOverflowMode overflow, GTextAlignment alignment,
                                 GTextLayoutCacheRef layout) {
  if ((__get_CONTROL() & 1u) || !s_active || pbl_cur != s_owner || !layout || !text) {
    return graphics_text_layout_get_max_used_size(ctx, text, font, box, overflow, alignment,
                                                  layout);
  }
  const size_t length = strnlen(text, CACHE_TEXT);
  if (length == CACHE_TEXT) {
    ++s_bypass;
    return graphics_text_layout_get_max_used_size(ctx, text, font, box, overflow, alignment,
                                                  layout);
  }
  TextLayoutExtended *extended = (TextLayoutExtended *)layout;
  for (unsigned i = 0; i < CACHE_ENTRIES; ++i) {
    Entry *e = &s_cache[i];
    if (e->valid && e->result.font == font && e->result.overflow_mode == overflow &&
        e->result.alignment == alignment && memcmp(&e->result.box, &box, sizeof(box)) == 0 &&
        e->spacing == extended->line_spacing_delta &&
        memcmp(&e->flow, &extended->flow_data, sizeof(e->flow)) == 0 &&
        strcmp(e->text, text) == 0) {
      ++s_hits;
      // Reuse the existing stock TextLayout freshness path, preserving output fields/attributes.
      *(TextLayout *)layout = e->result;
      return graphics_text_layout_get_max_used_size(ctx, text, font, box, overflow, alignment,
                                                    layout);
    }
  }
  ++s_misses;
  const GSize size =
      graphics_text_layout_get_max_used_size(ctx, text, font, box, overflow, alignment, layout);
  Entry *e = &s_cache[s_replace++ % CACHE_ENTRIES];
  e->result = *(TextLayout *)layout;
  e->spacing = extended->line_spacing_delta;
  e->flow = extended->flow_data;
  memcpy(e->text, text, length + 1u);
  e->valid = true;
  return size;
}
#ifdef CONFIG_T5AI_DRAW_PROFILE
void t5ai_text_node_cache_dump(void) {
  PBL_LOG_DBG("T5LAYOUT hits=%lu misses=%lu bypass=%lu bytes=%lu", (unsigned long)s_hits,
              (unsigned long)s_misses, (unsigned long)s_bypass, (unsigned long)sizeof(s_cache));
}
#endif
