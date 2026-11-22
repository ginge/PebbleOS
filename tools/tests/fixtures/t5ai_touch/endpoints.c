/* SPDX-License-Identifier: Apache-2.0 */
#include "model.h"

#include <stdio.h>
#include <stdlib.h>

#include <kernel.h>
struct pbl_msgq input_queue, app_queue;
static PebbleEvent input_storage[128], app_storage[128];
PebbleProcessMd metadata;
struct pbl_thread app_thread, main_thread;
struct pbl_thread *pbl_cur;
PebbleTask task;
uint64_t clock_ms, delivered_ticks;
unsigned control, irq_depth, callback_tokens;
void (*on_unlock)(void);
Recognizer *pan;
void setup(unsigned capacity) {
  assert(!irq_depth);
  metadata = (PebbleProcessMd){0};
  app_thread.id = 11;
  main_thread.id = 22;
  task = PebbleTask_SysWorkq;
  pbl_cur = &app_thread;
  clock_ms = 0;
  control = 0;
  callback_tokens = 0;
  pbl_msgq_init(&input_queue, input_storage, sizeof(PebbleEvent), 128);
  pbl_msgq_init(&app_queue, app_storage, sizeof(PebbleEvent), capacity);
}
unsigned test_control(void) {
  return control;
}
struct pbl_thread *pbl_thread_current(void) {
  return task == PebbleTask_App ? &app_thread : &main_thread;
}
struct pbl_thread *pebble_task_get_thread(PebbleTask t) {
  assert(t == PebbleTask_App);
  return &app_thread;
}
PebbleTask pebble_task_get_current(void) {
  return task;
}
struct pbl_msgq *pebble_task_get_to_queue(PebbleTask t) {
  return t == PebbleTask_App ? &app_queue : NULL;
}
const PebbleProcessMd *app_manager_get_current_app_md(void) {
  return &metadata;
}
uint64_t t5ai_rtc_get_raw_ticks(void) {
  return clock_ms * (T5AI_AON_RTC_HZ / 1000u);
}
uint64_t sys_get_ticks(void) {
  return clock_ms;
}
void pbl_irq_lock(void) {
  ++irq_depth;
}
void pbl_irq_unlock(void) {
  assert(irq_depth);
  --irq_depth;
}
unsigned int arch_irq_lock(void) {
  return irq_depth++;
}
void arch_irq_unlock(unsigned int key) {
  assert(irq_depth == key + 1);
  irq_depth = key;
  if (!key && on_unlock) {
    void (*fn)(void) = on_unlock;
    on_unlock = NULL;
    fn();
  }
}
bool arch_in_isr(void) {
  return false;
}
struct pbl_thread *waitq_pop(struct pbl_waitq *q) {
  assert(!q->head);
  return NULL;
}
int sched_block(struct pbl_waitq *q, pbl_timeout_t t, unsigned key) {
  assert(false);
  return -1;
}
void sched_wake(struct pbl_thread *t, int rc) {
  assert(false);
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  fprintf(stderr, "fatal %s:%d\n", file, line);
  abort();
}
[[noreturn]] void pbl_panic_at(const char *file, int line, const char *fmt, ...) {
  pbl_panic_bare_at(file, line);
}
void event_put(PebbleEvent *event) {
  assert(pbl_msgq_put(&input_queue, event, PBL_NO_WAIT) == 0);
  if (event->type == PEBBLE_CALLBACK_EVENT)
    ++callback_tokens;
}
bool event_take_timeout(PebbleEvent *event, pbl_timeout_t timeout) {
  bool have = pbl_msgq_get(&input_queue, event, timeout) == 0;
  if (have && event->type == PEBBLE_CALLBACK_EVENT) {
    assert(callback_tokens);
    --callback_tokens;
  }
  return have;
}
void recognizer_manager_handle_touch_event(const TouchEvent *event, void *context) {
  delivered_ticks = t5ai_touch_sample_ticks();
  if (context)
    recognizer_handle_touch_event(context, event);
}
void recognizer_manager_handle_state_change(RecognizerManager *manager, Recognizer *changed) {
}
