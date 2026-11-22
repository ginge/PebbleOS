/* SPDX-License-Identifier: Apache-2.0 */

#include "touch_queue.h"

#include "applib/ui/recognizer/recognizer_manager.h"
#include "kernel/pebble_tasks.h"
#include "process_management/app_manager.h"
#include "syscall/syscall.h"

#include <errno.h>
#include <stddef.h>

#include <pbl/kernel/assert.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/msgq.h>
#include <pbl/kernel/thread.h>
#include <pbl/soc/t5ai/core.h>
#include <pbl/soc/t5ai/rtc.h>

#ifdef CONFIG_T5AI_IO_PROFILE
#include <pbl/soc/t5ai/io_profile.h>
#else
#define T5_IO_ADD(kind, elapsed, amount) \
  do {                                   \
  } while (0)
#endif

static T5AITouchSample s_samples[T5AI_TOUCH_SAMPLE_CAPACITY];
static uint32_t s_next_token;
static uint32_t s_previous_move, s_last_move;
static const PebbleEvent *s_main_event;
static uint64_t s_main_ticks;
static TouchEvent s_last_touch;
static uint64_t s_sample_ticks;
static bool s_have_sample, s_delivering;
static uint32_t s_app_id;

// Envelopes must be expanded at their consumer boundary, never invoked.
static void prv_sample_marker(void *data) {
  PBL_ASSERTN(false);
}

static bool prv_envelope(const PebbleEvent *event) {
  return event->type == PEBBLE_CALLBACK_EVENT && event->callback.callback == prv_sample_marker;
}

// All sample ownership and motion-tail state is protected by the IRQ lock.
static T5AITouchSample *prv_find(uint32_t token) {
  if (token == 0u)
    return nullptr;
  for (size_t i = 0; i < T5AI_TOUCH_SAMPLE_CAPACITY; ++i) {
    if (s_samples[i].token == token)
      return &s_samples[i];
  }
  return nullptr;
}

static T5AITouchSample *prv_allocate(const PebbleEvent *event, uint64_t ticks,
                                     struct pbl_msgq *queue, uint32_t app_id) {
  for (size_t i = 0; i < T5AI_TOUCH_SAMPLE_CAPACITY; ++i) {
    if (s_samples[i].token != 0u)
      continue;
    do {
      ++s_next_token;
    } while (s_next_token == 0u || prv_find(s_next_token) != nullptr);
    s_samples[i] = (T5AITouchSample){
      .event = event->touch.event,
      .ticks = ticks,
      .queue = queue,
      .token = s_next_token,
      .app_id = app_id,
      .task_mask = event->task_mask,
      .app = queue != nullptr,
    };
    return &s_samples[i];
  }
  return nullptr;
}

static void prv_release(T5AITouchSample *sample) {
  if (sample->token == s_previous_move)
    s_previous_move = 0u;
  if (sample->token == s_last_move) {
    s_previous_move = 0u;
    s_last_move = 0u;
  }
  sample->token = 0u;
}

static PebbleEvent prv_enclose(const T5AITouchSample *sample) {
  return (PebbleEvent){
    .type = PEBBLE_CALLBACK_EVENT,
    .task_mask = sample->task_mask,
    .callback = {.callback = prv_sample_marker, .data = (void *)(uintptr_t)sample->token},
  };
}

static void prv_expand(PebbleEvent *event, const T5AITouchSample *sample) {
  *event = (PebbleEvent){
    .type = PEBBLE_TOUCH_EVENT,
    .task_mask = sample->task_mask,
    .touch = {.event = sample->event},
  };
}

static uint32_t prv_app_id(void) {
  const struct pbl_thread *thread = pebble_task_get_thread(PebbleTask_App);
  return thread ? pbl_thread_id(thread) : 0u;
}

static bool prv_target(struct pbl_msgq *queue) {
  const PebbleProcessMd *md = app_manager_get_current_app_md();
  return queue && queue == pebble_task_get_to_queue(PebbleTask_App) && md && !md->is_unprivileged &&
         prv_app_id() != 0u;
}

void __real_event_put(PebbleEvent *);
void __wrap_event_put(PebbleEvent *event) {
  const PebbleProcessMd *md = app_manager_get_current_app_md();
  if ((__get_CONTROL() & 1u) || event->type != PEBBLE_TOUCH_EVENT || !md || md->is_unprivileged) {
    __real_event_put(event);
    return;
  }
  const uint64_t ticks = t5ai_rtc_get_ticks();
  pbl_irq_lock();
  T5AITouchSample *sample = prv_allocate(event, ticks, nullptr, 0u);
  PebbleEvent envelope = sample ? prv_enclose(sample) : (PebbleEvent){};
  pbl_irq_unlock();
  // The void event API already fails stop on sustained queue exhaustion.
  PBL_ASSERTN(sample != nullptr);
  __real_event_put(&envelope);
}

static bool prv_move(const T5AITouchSample *sample, struct pbl_msgq *queue, uint32_t app_id,
                     bool non_navigational) {
  return sample && sample->app && sample->queue == queue && sample->app_id == app_id &&
         sample->event.type == TouchEvent_PositionUpdate &&
         sample->event.non_navigational == non_navigational;
}

int __real_pbl_msgq_put(struct pbl_msgq *, const void *, pbl_timeout_t);
int __wrap_pbl_msgq_put(struct pbl_msgq *queue, const void *message, pbl_timeout_t timeout) {
  // msg_size is public queue configuration; no backing storage or backend state is accessed.
  if (!queue || queue->msg_size != sizeof(PebbleEvent))
    return __real_pbl_msgq_put(queue, message, timeout);
  const PebbleEvent *event = message;
  if (prv_envelope(event)) {
    const uint32_t token = (uintptr_t)event->callback.data;
    pbl_irq_lock();
    T5AITouchSample *sample = prv_find(token);
    const bool unpublished = sample && !sample->app && sample->queue == nullptr;
    if (unpublished)
      sample->queue = queue;
    pbl_irq_unlock();
    if (!unpublished)
      return -EINVAL;
    const int rc = __real_pbl_msgq_put(queue, message, timeout);
    if (rc != 0) {
      pbl_irq_lock();
      sample = prv_find(token);
      if (sample)
        prv_release(sample);
      pbl_irq_unlock();
    }
    return rc;
  }
  if (queue != pebble_task_get_to_queue(PebbleTask_App))
    return __real_pbl_msgq_put(queue, message, timeout);

  pbl_irq_lock();
  if (!pbl_timeout_is_no_wait(timeout) || !prv_target(queue) || event->type != PEBBLE_TOUCH_EVENT) {
    s_previous_move = s_last_move = 0u;
    pbl_irq_unlock();
    return __real_pbl_msgq_put(queue, message, timeout);
  }
  const uint32_t app_id = prv_app_id();
  const uint64_t ticks = pebble_task_get_current() == PebbleTask_KernelMain && event == s_main_event
                             ? s_main_ticks
                             : t5ai_rtc_get_ticks();
  // A transition attempt is a barrier even when the public queue rejects it.
  if (event->touch.event.type != TouchEvent_PositionUpdate)
    s_previous_move = s_last_move = 0u;
  T5AITouchSample *previous = prv_find(s_previous_move);
  T5AITouchSample *last = prv_find(s_last_move);
  if (event->touch.event.type == TouchEvent_PositionUpdate &&
      prv_move(previous, queue, app_id, event->touch.event.non_navigational) &&
      prv_move(last, queue, app_id, event->touch.event.non_navigational)) {
    previous->event = last->event;
    previous->ticks = last->ticks;
    previous->task_mask = last->task_mask;
    last->event = event->touch.event;
    last->ticks = ticks;
    last->task_mask = event->task_mask;
    T5_IO_ADD(T5IO_COALESCED, 0u, 1u);
    pbl_irq_unlock();
    return 0;
  }
  T5AITouchSample *sample = prv_allocate(event, ticks, queue, app_id);
  if (!sample) {
    pbl_irq_unlock();
    return -ENOMEM;
  }
  PebbleEvent envelope = prv_enclose(sample);
  // Nonblocking put and ownership publication are atomic with respect to consumers.
  const int rc = __real_pbl_msgq_put(queue, &envelope, timeout);
  if (rc != 0) {
    prv_release(sample);
  } else if (event->touch.event.type == TouchEvent_PositionUpdate) {
    s_previous_move = s_last_move;
    s_last_move = sample->token;
  } else {
    s_previous_move = s_last_move = 0u;
  }
  pbl_irq_unlock();
  return rc;
}

int __real_pbl_msgq_get(struct pbl_msgq *, void *, pbl_timeout_t);
int __wrap_pbl_msgq_get(struct pbl_msgq *queue, void *message, pbl_timeout_t timeout) {
  const int rc = __real_pbl_msgq_get(queue, message, timeout);
  if (rc != 0 || !queue || queue->msg_size != sizeof(PebbleEvent))
    return rc;
  PebbleEvent *event = message;
  const bool app_task = pebble_task_get_current() == PebbleTask_App;
  if (app_task && queue == pebble_task_get_to_queue(PebbleTask_App))
    s_have_sample = false;
  if (!prv_envelope(event))
    return rc;
  pbl_irq_lock();
  T5AITouchSample *sample = prv_find((uintptr_t)event->callback.data);
  const bool valid = sample && sample->queue == queue;
  // KernelMain envelopes stay intact until event_take_timeout finishes callback tracking.
  if (!valid || !sample->app) {
    pbl_irq_unlock();
    PBL_ASSERTN(valid);
    return rc;
  }
  const T5AITouchSample delivered = *sample;
  prv_release(sample);
  pbl_irq_unlock();
  prv_expand(event, &delivered);
  if (app_task && delivered.app_id != pbl_thread_id(pbl_thread_current())) {
    // A normal empty event preserves dequeue/cleanup semantics without exposing
    // an old sample or a privileged callback to a replacement app.
    *event = (PebbleEvent){.type = PEBBLE_NULL_EVENT};
    return 0;
  }
  if (app_task && prv_target(queue)) {
    s_last_touch = delivered.event;
    s_sample_ticks = delivered.ticks;
    s_app_id = delivered.app_id;
    s_have_sample = true;
    T5_IO_ADD(T5IO_APP_QUEUE,
              (uint32_t)(t5ai_rtc_get_ticks() - s_sample_ticks) * (T5AI_AON_RTC_HZ / 1000u), 0u);
  }
  return rc;
}

bool __real_event_take_timeout(PebbleEvent *, pbl_timeout_t);
bool __wrap_event_take_timeout(PebbleEvent *event, pbl_timeout_t timeout) {
  s_main_event = nullptr;
  const bool taken = __real_event_take_timeout(event, timeout);
  if (!taken || !prv_envelope(event))
    return taken;
  pbl_irq_lock();
  T5AITouchSample *sample = prv_find((uintptr_t)event->callback.data);
  const bool valid = sample && !sample->app;
  T5AITouchSample delivered = sample ? *sample : (T5AITouchSample){};
  if (valid)
    prv_release(sample);
  pbl_irq_unlock();
  PBL_ASSERTN(valid);
  prv_expand(event, &delivered);
  s_main_event = event;
  s_main_ticks = delivered.ticks;
  return true;
}

void __real_recognizer_manager_handle_touch_event(const TouchEvent *, void *);
void __wrap_recognizer_manager_handle_touch_event(const TouchEvent *event, void *context) {
  if (__get_CONTROL() & 1u) {
    __real_recognizer_manager_handle_touch_event(event, context);
    return;
  }
  const bool matched = s_have_sample && pbl_thread_id(pbl_thread_current()) == s_app_id &&
                       pebble_task_get_current() == PebbleTask_App &&
                       event->type == s_last_touch.type && event->x == s_last_touch.x &&
                       event->y == s_last_touch.y &&
                       event->non_navigational == s_last_touch.non_navigational;
  s_delivering = matched;
  __real_recognizer_manager_handle_touch_event(event, context);
  s_delivering = false;
}

uint64_t t5ai_touch_sample_ticks(void) {
  if (__get_CONTROL() & 1u)
    return sys_get_ticks();
  return s_delivering ? s_sample_ticks : sys_get_ticks();
}
