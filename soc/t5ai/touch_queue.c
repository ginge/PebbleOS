/* SPDX-License-Identifier: Apache-2.0 */
#include "touch_queue.h"

#include "applib/ui/recognizer/recognizer_manager.h"
#include "kernel/events.h"
#include "kernel/pebble_tasks.h"
#include "process_management/app_manager.h"
#include "syscall/syscall.h"

#include <stddef.h>
#include <string.h>

#include <pbl/drivers/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/msgq.h>
#include <pbl/kernel/thread.h>

#include <cmsis_core.h>

#ifdef CONFIG_T5AI_IO_PROFILE
#include <pbl/soc/t5ai/io_profile.h>
#else
#define T5_IO_BEGIN(name)
#define T5_IO_END(kind, name, amount) \
  do {                                \
  } while (0)
#define T5_IO_ADD(kind, elapsed, amount) \
  do {                                   \
  } while (0)
#endif

static TouchEvent s_last_touch;
static RtcTicks s_sample_ticks;
static bool s_have_sample, s_delivering;
static uint32_t s_app_id;
extern struct pbl_thread *pbl_cur;
static bool prv_target(struct pbl_msgq *q) {
  const PebbleProcessMd *md = app_manager_get_current_app_md();
  return q && q->msg_size == sizeof(PebbleEvent) && md && !md->is_unprivileged &&
         q == pebble_task_get_to_queue(PebbleTask_App);
}
void __real_event_put(PebbleEvent *);
void __wrap_event_put(PebbleEvent *event) {
  // Stamp the sample before KernelMain queues it onward; preserve Main backlog in its age.
  if (!(__get_CONTROL() & 1u) && event->type == PEBBLE_TOUCH_EVENT) {
    const PebbleProcessMd *md = app_manager_get_current_app_md();
    if (md && !md->is_unprivileged)
      t5_touch_stamp(event, rtc_get_ticks());
  }
  __real_event_put(event);
}
int __real_pbl_msgq_put(struct pbl_msgq *, const void *, pbl_timeout_t);
int __wrap_pbl_msgq_put(struct pbl_msgq *q, const void *message, pbl_timeout_t timeout) {
  if (!pbl_timeout_is_no_wait(timeout) || !prv_target(q) ||
      ((const PebbleEvent *)message)->type != PEBBLE_TOUCH_EVENT)
    return __real_pbl_msgq_put(q, message, timeout);
  PebbleEvent event = *(const PebbleEvent *)message;
  if (!t5_touch_has_stamp(&event))
    t5_touch_stamp(&event, rtc_get_ticks());
  pbl_irq_lock();
  const bool combined = t5_touch_combine(q, &event);
  if (combined)
    T5_IO_ADD(T5IO_COALESCED, 0u, 1u);
  const int result = combined ? 0 : __real_pbl_msgq_put(q, &event, timeout);
  pbl_irq_unlock();
  return result;
}
int __real_pbl_msgq_get(struct pbl_msgq *, void *, pbl_timeout_t);
int __wrap_pbl_msgq_get(struct pbl_msgq *q, void *message, pbl_timeout_t timeout) {
  const int rc = __real_pbl_msgq_get(q, message, timeout);
  if (!rc && prv_target(q) && pebble_task_get_current() == PebbleTask_App) {
    const PebbleEvent *e = message;
    s_have_sample = e->type == PEBBLE_TOUCH_EVENT && t5_touch_has_stamp(e);
    if (s_have_sample) {
      s_last_touch = e->touch.event;
      const RtcTicks now = rtc_get_ticks();
      s_sample_ticks = t5_touch_time(e, now);
      T5_IO_ADD(T5IO_APP_QUEUE, (uint32_t)(now - s_sample_ticks) * 32u, 0u);
      s_app_id = pbl_cur->id;
    }
  }
  return rc;
}
void __real_recognizer_manager_handle_touch_event(const TouchEvent *, void *);
void __wrap_recognizer_manager_handle_touch_event(const TouchEvent *event, void *context) {
  if (__get_CONTROL() & 1u) {
    __real_recognizer_manager_handle_touch_event(event, context);
    return;
  }
  const bool matched =
      s_have_sample && pbl_cur->id == s_app_id && pebble_task_get_current() == PebbleTask_App &&
      event->type == s_last_touch.type && event->x == s_last_touch.x &&
      event->y == s_last_touch.y && event->non_navigational == s_last_touch.non_navigational;
  s_delivering = matched;
  __real_recognizer_manager_handle_touch_event(event, context);
  s_delivering = false;
}
RtcTicks t5ai_touch_sample_ticks(void) {
  // The pan recognizer also runs in unprivileged watchapps.
  if (__get_CONTROL() & 1u)
    return sys_get_ticks();
  return s_delivering ? s_sample_ticks : sys_get_ticks();
}
