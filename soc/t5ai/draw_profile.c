/* SPDX-License-Identifier: Apache-2.0 */
#include "applib/graphics/text.h"
#include "applib/ui/window.h"
#include "kernel/kernel_heap.h"
#include "kernel/pebble_tasks.h"
#include "system/passert.h"

#include <string.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/thread.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/draw_profile.h>
#include <pbl/soc/t5ai/interrupt_profile.h>
#include <pbl/soc/t5ai/rtc.h>

#include <cmsis_core.h>

PBL_LOG_MODULE_DEFINE(t5ai_draw_profile, LOG_LEVEL_DEBUG);
#ifdef CONFIG_T5AI_DRAW_PROFILE_DETAIL
#define LIMIT 192u
#define DEPTH 64u
#else
#define LIMIT 16u
#define DEPTH 1u
#endif
typedef struct {
  uint32_t fn, count, wall, cpu;
} Counter;
typedef struct {
  uint32_t fn, wall, cpu;
} Span;
#define STORAGE __attribute__((section(".t5ai_board_cache"), aligned(32)))
static Counter s_counts[LIMIT] STORAGE, s_dump[LIMIT] STORAGE;
static Span s_spans[DEPTH];
static volatile uint32_t s_irq_depth, s_irq_app, s_irq_start;
static bool s_irq_from_app;
static uint32_t s_depth, s_lost, s_last, s_sequence, s_render_depth;
static volatile bool s_ready, s_frozen, s_storage_ready;
static volatile bool s_touch_down, s_touch_seen;
static volatile uint32_t s_touch_last;
bool t5ai_draw_profile_gesture_active(void) {
  return s_touch_down;
}
void t5ai_draw_profile_touch(bool down) {
#ifdef CONFIG_T5AI_IO_CPU_PROFILE
  extern void t5ai_io_profile_contact(bool down);
  t5ai_io_profile_contact(down);
#endif
  s_touch_down = down;
  s_touch_last = T5AI_AON_RTC_COUNT_LOW;
  if (down)
    s_touch_seen = true;
}
static struct pbl_thread s_worker;
PBL_THREAD_STACK_DEFINE(s_stack, 2048);
extern struct pbl_thread *pbl_cur;

static bool prv_app(void) {
  // Applib entry points also execute in unprivileged third-party apps.
  if (__get_CONTROL() & 1u)
    return false;
  if (!s_ready || s_frozen || pbl_cur != pebble_task_get_thread(PebbleTask_App))
    return false;
  if (!s_storage_ready) {
    if (__get_IPSR())
      return false;
    // CP PSRAM bring-up follows timer init; initialize on first privileged App use.
    memset(s_counts, 0, sizeof(s_counts));
    memset(s_dump, 0, sizeof(s_dump));
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
    extern void t5ai_display_profile_buffer_ready(void);
    t5ai_display_profile_buffer_ready();
#endif
    s_storage_ready = true;
  }
  return true;
}
// Outermost nesting avoids double-counting GPIO/mailbox/I2C/SysTick overlap.
uint32_t t5ai_interrupt_profile_irq_enter(void) {
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  if (s_irq_depth++ == 0u) {
    s_irq_start = now;
    s_irq_from_app = prv_app();
  }
  return now;
}
void t5ai_interrupt_profile_irq_exit(unsigned id, uint32_t start) {
  (void)id;
  (void)start;
  if (s_irq_depth && --s_irq_depth == 0u && s_irq_from_app)
    s_irq_app += T5AI_AON_RTC_COUNT_LOW - s_irq_start;
}
void __real_SysTick_Handler(void);
void __wrap_SysTick_Handler(void) {
  const uint32_t at = t5ai_interrupt_profile_irq_enter();
  __real_SysTick_Handler();
  t5ai_interrupt_profile_irq_exit(0, at);
}
static uint32_t prv_cpu(void) {
  pbl_irq_lock();
  const uint32_t ticks = (uint32_t)pbl_uptime_ticks();
  const uint32_t cpu = pbl_cur->backend.run_time + ticks - pbl_cur->backend.switched_in_at;
  pbl_irq_unlock();
  return cpu;
}
static void prv_add(uint32_t fn, uint32_t wall, uint32_t cpu) {
  const unsigned hash = ((fn >> 1) ^ (fn >> 9)) % (LIMIT - 8u);
  const unsigned probes = fn < 8u ? 1u : LIMIT - 8u;
  for (unsigned i = 0; i < probes; ++i) {
    Counter *c = &s_counts[fn < 8u ? fn : 8u + (hash + i) % (LIMIT - 8u)];
    if (!c->fn || c->fn == fn) {
      c->fn = fn;
      ++c->count;
      c->wall += wall;
      c->cpu += cpu;
      return;
    }
  }
  ++s_lost;
}
// These hooks only receive calls from the three selected translation units.
void __cyg_profile_func_enter(void *fn, void *caller) {
  (void)caller;
  if (!prv_app())
    return;
  if (s_depth < DEPTH)
    s_spans[s_depth] = (Span){(uintptr_t)fn, T5AI_AON_RTC_COUNT_LOW, prv_cpu()};
  else
    ++s_lost;
  ++s_depth;
}
void __cyg_profile_func_exit(void *fn, void *caller) {
  (void)caller;
  if (!prv_app() || !s_depth)
    return;
  if (--s_depth >= DEPTH)
    return;
  const Span *s = &s_spans[s_depth];
  if (s->fn != (uintptr_t)fn) {
    ++s_lost;
    return;
  }
  prv_add(s->fn, T5AI_AON_RTC_COUNT_LOW - s->wall, prv_cpu() - s->cpu);
  s_last = T5AI_AON_RTC_COUNT_LOW;
}

void __real_window_render(Window *, GContext *);
void __wrap_window_render(Window *window, GContext *ctx) {
  if (!prv_app()) {
    __real_window_render(window, ctx);
    return;
  }
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW, cpu = prv_cpu();
  const uint32_t irq = s_irq_app;
  ++s_render_depth;
  __real_window_render(window, ctx);
  --s_render_depth;
  prv_add(1u, T5AI_AON_RTC_COUNT_LOW - start, prv_cpu() - cpu);
  prv_add(4u, s_irq_app - irq, 0u);
  s_last = T5AI_AON_RTC_COUNT_LOW;
}
void __real_graphics_draw_text(GContext *, const char *, GFont, GRect, GTextOverflowMode,
                               GTextAlignment, GTextLayoutCacheRef);
void __wrap_graphics_draw_text(GContext *ctx, const char *text, GFont font, GRect box,
                               GTextOverflowMode overflow, GTextAlignment alignment,
                               GTextLayoutCacheRef layout) {
  if (!prv_app()) {
    __real_graphics_draw_text(ctx, text, font, box, overflow, alignment, layout);
    return;
  }
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW, cpu = prv_cpu();
  ++s_render_depth;
  __real_graphics_draw_text(ctx, text, font, box, overflow, alignment, layout);
  --s_render_depth;
  prv_add(2u, T5AI_AON_RTC_COUNT_LOW - start, prv_cpu() - cpu);
}
int __real_pbl_mutex_lock_lr(struct pbl_mutex *, pbl_timeout_t, uintptr_t);
int __wrap_pbl_mutex_lock_lr(struct pbl_mutex *m, pbl_timeout_t timeout, uintptr_t lr) {
  if (!prv_app() || !s_render_depth)
    return __real_pbl_mutex_lock_lr(m, timeout, lr);
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  const int rc = __real_pbl_mutex_lock_lr(m, timeout, lr);
  prv_add(3u, T5AI_AON_RTC_COUNT_LOW - start, 0u);
  return rc;
}
struct pbl_waitq;
int __real_sched_block(struct pbl_waitq *, pbl_timeout_t);
int __wrap_sched_block(struct pbl_waitq *waitq, pbl_timeout_t timeout) {
  if (!prv_app() || !s_render_depth)
    return __real_sched_block(waitq, timeout);
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW, cpu = prv_cpu();
  const int rc = __real_sched_block(waitq, timeout);
  prv_add(5u, T5AI_AON_RTC_COUNT_LOW - start, prv_cpu() - cpu);
  return rc;
}
static void prv_entry(void *arg) {
  (void)arg;
  for (;;) {
    pbl_thread_sleep(PBL_MSEC(500));
    pbl_irq_lock();
    if (!s_last || s_depth || s_render_depth || !s_touch_seen || s_touch_down ||
        T5AI_AON_RTC_COUNT_LOW - s_touch_last < 2u * T5AI_AON_RTC_HZ) {
      pbl_irq_unlock();
      continue;
    }
    s_frozen = true;
    s_touch_seen = false;
    pbl_irq_unlock();
    // Only App writes; frozen gates it while copying outside the IRQ lock.
    memcpy(s_dump, s_counts, sizeof(s_dump));
    memset(s_counts, 0, sizeof(s_counts));
    const uint32_t lost = s_lost;
    s_lost = s_last = 0u;
    s_frozen = false;
    const uint32_t seq = ++s_sequence;
    PBL_LOG_DBG("T5FUNC begin=%lu lost=%lu tick_hz=%lu cpu_hz=%lu", (unsigned long)seq,
                (unsigned long)lost, (unsigned long)T5AI_AON_RTC_HZ, (unsigned long)PBL_TICK_HZ);
    for (unsigned i = 0; i < LIMIT; ++i) {
      Counter *c = &s_dump[i];
      if (!c->fn)
        continue;
      PBL_LOG_DBG("T5FUNC seq=%lu fn=%lx n=%lu wall=%lu cpu=%lu", (unsigned long)seq,
                  (unsigned long)c->fn, (unsigned long)c->count, (unsigned long)c->wall,
                  (unsigned long)c->cpu);
      pbl_thread_sleep(PBL_MSEC(2));
    }
    PBL_LOG_DBG("T5FUNC end=%lu", (unsigned long)seq);
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
    extern void t5ai_display_profile_dump(void);
    t5ai_display_profile_dump();
#endif
#ifdef CONFIG_T5AI_IO_PROFILE
    extern void t5ai_io_profile_dump(void);
    t5ai_io_profile_dump();
#endif
#ifdef CONFIG_T5AI_DISPLAY_DMA_IRQ
    extern void t5ai_display_dma_profile_dump(void);
    t5ai_display_dma_profile_dump();
#endif
    unsigned used = 0, free_bytes = 0, largest = 0;
    heap_calc_totals(kernel_heap_get(), &used, &free_bytes, &largest);
    PBL_LOG_DBG("T5HEAP used=%u free=%u largest=%u low=%lu", used, free_bytes, largest,
                (unsigned long)heap_get_minimum_headroom(kernel_heap_get()));
#ifdef CONFIG_T5AI_COMPOSITOR_FAST_PATH
    extern void t5ai_compositor_fast_dump(void);
    t5ai_compositor_fast_dump();
#endif
#ifdef CONFIG_T5AI_TEXT_NODE_CACHE
    extern void t5ai_text_node_cache_dump(void);
    t5ai_text_node_cache_dump();
#endif
  }
}
void __real_system_task_timer_init(void);
void __wrap_system_task_timer_init(void) {
  __real_system_task_timer_init();
  const struct pbl_thread_attr attr = {
    .name = "DrawProfile",
    .entry = prv_entry,
    .prio = PBL_PRIO_IDLE + 1,
    .privileged = true,
    .stack = s_stack,
    .stack_size = sizeof(s_stack)
  };
  PBL_ASSERTN(pbl_thread_create(&s_worker, &attr) == 0);
  s_ready = true;
}
