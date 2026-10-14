#include <pebble.h>

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;
static AppTimer *s_work_timer;
static volatile uint32_t s_work_result = 0x12345678u;
static uint32_t s_bursts;
static bool s_focused = true;
static bool s_started;
static bool s_pixel;
static uint32_t s_sample_start;
static uint32_t s_frames;
static char s_fps[16] = "--.-";

static uint32_t prv_now_ms(void) {
  time_t seconds;
  uint16_t milliseconds;
  time_ms(&seconds, &milliseconds);
  return (uint32_t)seconds * 1000u + milliseconds;
}

static void prv_cpu_burst(void *context) {
  s_work_timer = NULL;
  if (!s_focused) {
    return;
  }
  uint32_t start = prv_now_ms();
  uint32_t value = s_work_result;
  uint32_t batches = 0;
  do {
    for (uint32_t i = 0; i < 4096; ++i) {
      value ^= value << 13;
      value ^= value >> 17;
      value ^= value << 5;
      value = value * 1664525u + 1013904223u;
    }
    ++batches;
  } while (prv_now_ms() - start < 750u);
  s_work_result = value;
  uint32_t elapsed = prv_now_ms() - start;
  ++s_bursts;
  APP_LOG(APP_LOG_LEVEL_INFO, "CPU burst %lu: %lu ms, %lu batches, checksum %08lx",
          (unsigned long)s_bursts, (unsigned long)elapsed, (unsigned long)batches,
          (unsigned long)s_work_result);
  layer_mark_dirty(s_layer);
  // Keep start-to-start cadence; avoid catch-up bursts if the clock jumps.
  s_work_timer = app_timer_register(elapsed < 5000u ? 5000u - elapsed : 5000u,
                                   prv_cpu_burst, NULL);
}

static void prv_request_frame(void *context) {
  s_timer = NULL;
  if (s_focused) {
    layer_mark_dirty(s_layer);
  }
}

static void prv_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_fps, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
                     GRect(0, (bounds.size.h - 70) / 2, bounds.size.w, 54),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, "FPS", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, (bounds.size.h - 70) / 2 + 50, bounds.size.w, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Change one central pixel each frame so identical text still reaches the display.
  s_pixel = !s_pixel;
  graphics_context_set_stroke_color(ctx, s_pixel ? GColorWhite : GColorBlack);
  graphics_draw_pixel(ctx, GPoint(bounds.size.w / 2, bounds.size.h / 2 + 60));

  if (!s_focused) {
    return;
  }
  uint32_t now = prv_now_ms();
  if (!s_started) {
    s_started = true;
    s_sample_start = now;
    s_frames = 0;
  } else {
    ++s_frames;
    uint32_t elapsed = now - s_sample_start;
    if (elapsed >= 1000) {
      uint32_t tenths = (s_frames * 10000u + elapsed / 2) / elapsed;
      snprintf(s_fps, sizeof(s_fps), "%lu.%lu", (unsigned long)(tenths / 10),
               (unsigned long)(tenths % 10));
      s_sample_start = now;
      s_frames = 0;
    }
  }
  // One pending request at a time; count draws, never timer firings.
  if (!s_timer) {
    s_timer = app_timer_register(1, prv_request_frame, NULL);
  }
}

static void prv_focus_changed(bool focused) {
  s_focused = focused;
  s_started = false;
  snprintf(s_fps, sizeof(s_fps), "--.-");
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  if (s_work_timer) {
    app_timer_cancel(s_work_timer);
    s_work_timer = NULL;
  }
  if (focused) {
    layer_mark_dirty(s_layer);
    s_work_timer = app_timer_register(5000, prv_cpu_burst, NULL);
  }
}

int main(void) {
  s_window = window_create();
  if (!s_window) {
    return 1;
  }
  Layer *root = window_get_root_layer(s_window);
  s_layer = layer_create(layer_get_bounds(root));
  if (!s_layer) {
    window_destroy(s_window);
    return 1;
  }
  layer_set_update_proc(s_layer, prv_draw);
  layer_add_child(root, s_layer);
  window_set_background_color(s_window, GColorBlack);
  app_focus_service_subscribe(prv_focus_changed);
  window_stack_push(s_window, false);
  if (!s_work_timer) {
    s_work_timer = app_timer_register(5000, prv_cpu_burst, NULL);
  }
  app_event_loop();
  app_focus_service_unsubscribe();
  if (s_timer) {
    app_timer_cancel(s_timer);
  }
  if (s_work_timer) {
    app_timer_cancel(s_work_timer);
  }
  layer_destroy(s_layer);
  window_destroy(s_window);
  return 0;
}
