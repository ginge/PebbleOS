/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/display_profile.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/logging/logging.h>
#include "system/profiler.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(t5ai_display_profile, LOG_LEVEL_DEBUG);

#define PROFILE_FRAMES 16u
#define PEAK_HISTORY   128u
static uint32_t s_history[PEAK_HISTORY], s_history_head, s_history_count;
static uint32_t s_first_start, s_peak250, s_peak1000, s_peak_interval, s_local250, s_local1000;
static bool s_peak_started, s_peak_overflow;

static void prv_peak(uint32_t start) {
  if (!s_peak_started) {
    s_first_start = start;
    s_peak_started = true;
  }
  // Keep a half-open (start - 1 second, start] history, independent of UART loss.
  while (s_history_count && start - s_history[s_history_head] >= T5AI_AON_RTC_HZ) {
    s_history_head = (s_history_head + 1u) % PEAK_HISTORY;
    --s_history_count;
  }
  if (s_history_count == PEAK_HISTORY) {
    s_peak_overflow = true;
    s_history_head = (s_history_head + 1u) % PEAK_HISTORY;
    --s_history_count;
  }
  s_history[(s_history_head + s_history_count++) % PEAK_HISTORY] = start;
  uint32_t count250 = 0u;
  for (uint32_t i = 0; i < s_history_count; ++i) {
    count250 += start - s_history[(s_history_head + i) % PEAK_HISTORY] < T5AI_AON_RTC_HZ / 4u;
  }
  if (start - s_first_start >= T5AI_AON_RTC_HZ / 4u) {
    if (count250 > s_peak250)
      s_peak250 = count250;
    if (count250 > s_local250)
      s_local250 = count250;
  }
  if (start - s_first_start >= T5AI_AON_RTC_HZ) {
    if (s_history_count > s_peak1000)
      s_peak1000 = s_history_count;
    if (s_history_count > s_local1000)
      s_local1000 = s_history_count;
  }
}

typedef struct {
  uint32_t previous_count;
  uint32_t previous_cycles;
  uint32_t count;
  uint64_t us;
} Phase;

static Phase s_render, s_compositor, s_font;
static T5AITransferProfile s_transfer_window;
static uint32_t s_sequence, s_frame, s_previous_start, s_previous_hz;
static bool s_have_start;
static struct {
  uint32_t frames, intervals, min_ticks, max_ticks, over30, hz;
  uint64_t span_ticks, update_us;
  uint32_t update_max_us;
  bool invalid;
} s_window;

static uint64_t prv_cycles_us(uint32_t cycles, uint32_t hz) {
  return hz ? (uint64_t)cycles * 1000000u / hz : 0u;
}

static void prv_phase(Phase *phase, uint32_t count, uint32_t cycles, uint32_t hz) {
  const uint32_t calls = count - phase->previous_count;
  // A reset of the generic profiler invalidates this window.
  if (calls > 65536u) {
    s_window.invalid = true;
  } else {
    phase->count += calls;
    phase->us += prv_cycles_us(cycles - phase->previous_cycles, hz);
  }
  phase->previous_count = count;
  phase->previous_cycles = cycles;
}

static uint32_t prv_u32(uint64_t value) {
  if (value > UINT32_MAX) {
    s_window.invalid = true;
    return UINT32_MAX;
  }
  return value;
}

void t5ai_display_profile_frame(uint32_t start, uint32_t elapsed, uint32_t hz,
                                uint32_t dma_failures, const T5AITransferProfile *transfer) {
#define ADD(field) s_transfer_window.field += transfer->field
  ADD(rows);
  ADD(panel_rows);
  ADD(bytes);
  ADD(dma_submits);
  ADD(dma_frames);
  ADD(cpu_frames);
  ADD(convert_cycles);
  ADD(copy_cycles);
  ADD(clean_cycles);
  ADD(next_cycles);
  ADD(command_cycles);
  ADD(cpu_cycles);
  ADD(wait_cycles);
  ADD(kick_cycles);
  ADD(observed_cycles);
  ADD(lock_cycles);
#undef ADD
  if (transfer->wait_max_cycles > s_transfer_window.wait_max_cycles)
    s_transfer_window.wait_max_cycles = transfer->wait_max_cycles;
  prv_peak(start);
  if (s_have_start) {
    const uint32_t interval = start - s_previous_start;
    if (interval && (!s_peak_interval || interval < s_peak_interval)) {
      s_peak_interval = interval;
    }
  }
  // Sparse anchors cross-check the hardware timebase against host receive time.
  // Peak counters above include EVERY update, even if UART records are dropped.
  // A cumulative counter makes dropped UART records visible to the reporter.
  ++s_frame;
  if ((s_frame & 15u) == 1u) {
    PBL_LOG_DBG("T5CADENCE frame=%lu tick=%lu elapsed=%lu hz=%lu dma_fail=%lu",
                (unsigned long)s_frame, (unsigned long)start, (unsigned long)elapsed,
                (unsigned long)hz, (unsigned long)dma_failures);
  }
  if (!s_window.frames) {
    s_window.hz = hz;
    s_window.min_ticks = UINT32_MAX;
  }
  s_window.invalid |= !hz || hz != s_window.hz || (s_have_start && hz != s_previous_hz);
  if (s_have_start) {
    const uint32_t interval = start - s_previous_start;
    ++s_window.intervals;
    s_window.span_ticks += interval;
    if (interval < s_window.min_ticks) {
      s_window.min_ticks = interval;
    }
    if (interval > s_window.max_ticks) {
      s_window.max_ticks = interval;
    }
    s_window.over30 += (uint64_t)interval * 30u > T5AI_AON_RTC_HZ;
  }
  s_have_start = true;
  s_previous_start = start;
  s_previous_hz = hz;
  const uint32_t update_us = prv_u32((uint64_t)elapsed * 1000000u / T5AI_AON_RTC_HZ);
  s_window.update_us += update_us;
  if (update_us > s_window.update_max_us) {
    s_window.update_max_us = update_us;
  }
  prv_phase(&s_render, PROFILER_NODE_GET_COUNT(render_app),
            PROFILER_NODE_GET_TOTAL_CYCLES(render_app), hz);
  prv_phase(&s_compositor, PROFILER_NODE_GET_COUNT(compositor),
            PROFILER_NODE_GET_TOTAL_CYCLES(compositor), hz);
  prv_phase(&s_font, PROFILER_NODE_GET_COUNT(text_render_flash),
            PROFILER_NODE_GET_TOTAL_CYCLES(text_render_flash), hz);
  if (++s_window.frames != PROFILE_FRAMES) {
    return;
  }

  // Hashed logging permits seven 32-bit arguments per record.
  const uint32_t update = prv_u32(s_window.update_us);
  const uint32_t render = prv_u32(s_render.us);
  const uint32_t compositor = prv_u32(s_compositor.us);
  const uint32_t font = prv_u32(s_font.us);
  ++s_sequence;
  PBL_LOG_DBG("T5FRAME seq=%lu frames=%lu intervals=%lu span_lo=%lu span_hi=%lu min=%lu max=%lu",
              (unsigned long)s_sequence, (unsigned long)s_window.frames,
              (unsigned long)s_window.intervals, (unsigned long)(uint32_t)s_window.span_ticks,
              (unsigned long)(s_window.span_ticks >> 32), (unsigned long)s_window.min_ticks,
              (unsigned long)s_window.max_ticks);
  PBL_LOG_DBG(
      "T5PEAK seq=%lu count250=%lu count1000=%lu min_ticks=%lu overflow=%lu local250=%lu local1000=%lu",
      (unsigned long)s_sequence, (unsigned long)s_peak250, (unsigned long)s_peak1000,
      (unsigned long)s_peak_interval, (unsigned long)s_peak_overflow, (unsigned long)s_local250,
      (unsigned long)s_local1000);
  PBL_LOG_DBG(
      "T5DRAW seq=%lu update_us=%lu update_max_us=%lu over30=%lu dma_fail=%lu invalid=%lu hz=%lu",
      (unsigned long)s_sequence, (unsigned long)update, (unsigned long)s_window.update_max_us,
      (unsigned long)s_window.over30, (unsigned long)dma_failures, (unsigned long)s_window.invalid,
      (unsigned long)hz);
  PBL_LOG_DBG(
      "T5PHASE seq=%lu render_n=%lu render_us=%lu comp_n=%lu comp_us=%lu font_n=%lu font_us=%lu",
      (unsigned long)s_sequence, (unsigned long)s_render.count, (unsigned long)render,
      (unsigned long)s_compositor.count, (unsigned long)compositor, (unsigned long)s_font.count,
      (unsigned long)font);
  const T5AITransferProfile *t = &s_transfer_window;
#define US(field) (unsigned long)prv_u32(hz ? t->field * 1000000u / hz : 0u)
  PBL_LOG_DBG(
      "T5XFER seq=%lu rows=%lu panel_rows=%lu bytes=%lu dma_n=%lu dma_frames=%lu cpu_frames=%lu",
      (unsigned long)s_sequence, (unsigned long)t->rows, (unsigned long)t->panel_rows,
      (unsigned long)t->bytes, (unsigned long)t->dma_submits, (unsigned long)t->dma_frames,
      (unsigned long)t->cpu_frames);
  PBL_LOG_DBG(
      "T5PREP seq=%lu convert_us=%lu copy_us=%lu clean_us=%lu next_us=%lu command_us=%lu cpu_us=%lu",
      (unsigned long)s_sequence, US(convert_cycles), US(copy_cycles), US(clean_cycles),
      US(next_cycles), US(command_cycles), US(cpu_cycles));
  PBL_LOG_DBG("T5DMA seq=%lu wait_us=%lu kick_us=%lu observed_us=%lu wait_max_us=%lu lock_us=%lu",
              (unsigned long)s_sequence, US(wait_cycles), US(kick_cycles), US(observed_cycles),
              (unsigned long)prv_cycles_us(t->wait_max_cycles, hz), US(lock_cycles));
#undef US
  memset(&s_transfer_window, 0, sizeof(s_transfer_window));
  s_local250 = s_local1000 = 0u;
  memset(&s_window, 0, sizeof(s_window));
  s_render.count = s_compositor.count = s_font.count = 0u;
  s_render.us = s_compositor.us = s_font.us = 0u;
}
