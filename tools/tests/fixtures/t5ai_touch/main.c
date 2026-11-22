/* SPDX-License-Identifier: Apache-2.0 */
#include "model.h"
#include "soc/t5ai/touch_queue.c"
static unsigned owned(void) {
  unsigned count = 0;
  for (unsigned i = 0; i < T5AI_TOUCH_SAMPLE_CAPACITY; ++i)
    count += s_samples[i].token != 0;
  return count;
}
static PebbleEvent touch(TouchEventType type, int x, bool gate) {
  return (PebbleEvent){
    .type = PEBBLE_TOUCH_EVENT,
    .task_mask = 0x8000u,
    .touch = {.event = {.type = type, .x = x, .y = 0, .non_navigational = gate}}
  };
}
static void submit(TouchEventType type, int x, uint64_t ticks) {
  task = PebbleTask_SysWorkq;
  clock_ms = ticks;
  PebbleEvent e = touch(type, x, false);
  event_put(&e);
  assert(e.type == PEBBLE_TOUCH_EVENT && e.task_mask == 0x8000u);
}
static void forward(bool gate) {
  task = PebbleTask_KernelMain;
  clock_ms += 1000;
  PebbleEvent e;
  assert(event_take_timeout(&e, PBL_NO_WAIT));
  assert(e.type == PEBBLE_TOUCH_EVENT);
  e.touch.event.non_navigational = gate;
  assert(pbl_msgq_put(&app_queue, &e, PBL_NO_WAIT) == 0);
}
static PebbleEvent receive(uint64_t expected) {
  task = PebbleTask_App;
  clock_ms += 1000;
  PebbleEvent e;
  assert(pbl_msgq_get(&app_queue, &e, PBL_NO_WAIT) == 0);
  assert(e.type == PEBBLE_TOUCH_EVENT && e.task_mask == 0x8000u);
  // Other application queues must not invalidate the event delivery context.
  PebbleEvent side_storage[1], side_event = {.type = PEBBLE_NULL_EVENT};
  struct pbl_msgq side_queue;
  pbl_msgq_init(&side_queue, side_storage, sizeof(PebbleEvent), 1);
  assert(!pbl_msgq_put(&side_queue, &side_event, PBL_NO_WAIT));
  assert(!pbl_msgq_get(&side_queue, &side_event, PBL_NO_WAIT));
  recognizer_manager_handle_touch_event(&e.touch.event, pan);
  assert(delivered_ticks == expected && t5ai_touch_sample_ticks() == clock_ms);
  return e;
}
static void refill_at_dequeue(void) {
  submit(TouchEvent_PositionUpdate, 30, 30);
  forward(false);
  task = PebbleTask_App;
}
static void cb(const Recognizer *r, RecognizerEvent ev) {
}
int main(void) {
  setup(8);
  // Acquisition timestamps survive Main backlog without touching source event bytes.
  submit(TouchEvent_Touchdown, 0, 100);
  submit(TouchEvent_PositionUpdate, 20, 120);
  submit(TouchEvent_PositionUpdate, 40, 140);
  submit(TouchEvent_PositionUpdate, 60, 160);
  forward(false);
  forward(false);
  forward(false);
  forward(false);
  assert(!callback_tokens && owned() == 3 && pbl_msgq_num_used(&app_queue) == 3);
  alignas(void *) uint8_t pan_storage[PAN_RECOGNIZER_STATIC_SIZE];
  pan = pan_recognizer_init_static(pan_storage, cb, NULL, PanAxis_Horizontal);
  assert(pan);
  assert(receive(100).touch.event.type == TouchEvent_Touchdown);
  assert(receive(140).touch.event.x == 40);
  assert(receive(160).touch.event.x == 60);
  GPoint velocity = pan_recognizer_get_velocity(pan);
  assert(velocity.x == 1000 && velocity.y == 0);
  assert(!owned());
  pan = NULL;
  // Repeated bursts wrap both the public queue and the independent owned pool.
  for (unsigned pass = 0; pass < 100; ++pass) {
    uint64_t base = 1000 + pass * 10;
    submit(TouchEvent_PositionUpdate, 10, base);
    forward(false);
    submit(TouchEvent_PositionUpdate, 20, base + 1);
    forward(false);
    submit(TouchEvent_PositionUpdate, 30, base + 2);
    forward(false);
    assert(receive(base + 1).touch.event.x == 20);
    assert(receive(base + 2).touch.event.x == 30);
    assert(!owned());
  }
  // The explicit ownership transfer occurs after dequeue: a producer can
  // update still-owned samples before that transfer without accessing queue storage.
  setup(8);
  submit(TouchEvent_PositionUpdate, 10, 10);
  forward(false);
  submit(TouchEvent_PositionUpdate, 20, 20);
  forward(false);
  task = PebbleTask_App;
  on_unlock = refill_at_dequeue;
  assert(receive(20).touch.event.x == 20);
  assert(receive(30).touch.event.x == 30 && !owned());
  // Touchdown and liftoff remain FIFO barriers between separate gestures.
  setup(8);
  submit(TouchEvent_Touchdown, 0, 0);
  forward(false);
  submit(TouchEvent_PositionUpdate, 1, 1);
  forward(false);
  submit(TouchEvent_Liftoff, 2, 2);
  forward(false);
  submit(TouchEvent_Touchdown, 3, 3);
  forward(false);
  assert(receive(0).touch.event.type == TouchEvent_Touchdown);
  assert(receive(1).touch.event.type == TouchEvent_PositionUpdate);
  assert(receive(2).touch.event.type == TouchEvent_Liftoff);
  assert(receive(3).touch.event.type == TouchEvent_Touchdown && !owned());
  // Full-width acquisition time survives delays longer than the old stamp modulus.
  setup(8);
  submit(TouchEvent_PositionUpdate, 1, (1ull << 40) + 123);
  forward(false);
  clock_ms += (1ull << 24);
  assert(receive((1ull << 40) + 123).touch.event.x == 1 && !owned());
  // Transition, navigation and unrelated-event barriers are never coalesced across.
  setup(8);
  submit(TouchEvent_PositionUpdate, 1, 1);
  forward(false);
  submit(TouchEvent_PositionUpdate, 2, 2);
  forward(false);
  task = PebbleTask_KernelMain;
  PebbleEvent other = {.type = PEBBLE_TICK_EVENT};
  assert(pbl_msgq_put(&app_queue, &other, PBL_NO_WAIT) == 0);
  submit(TouchEvent_PositionUpdate, 3, 3);
  forward(false);
  submit(TouchEvent_PositionUpdate, 4, 4);
  forward(true);
  submit(TouchEvent_Liftoff, 5, 5);
  forward(true);
  assert(pbl_msgq_num_used(&app_queue) == 6);
  assert(receive(1).touch.event.x == 1);
  assert(receive(2).touch.event.x == 2);
  task = PebbleTask_App;
  assert(pbl_msgq_get(&app_queue, &other, PBL_NO_WAIT) == 0 && other.type == PEBBLE_TICK_EVENT);
  assert(receive(3).touch.event.x == 3);
  assert(receive(4).touch.event.non_navigational);
  assert(receive(5).touch.event.type == TouchEvent_Liftoff && !owned());
  // Full public queues can coalesce only eligible motion; failed barriers release ownership.
  setup(2);
  submit(TouchEvent_PositionUpdate, 1, 1);
  forward(false);
  submit(TouchEvent_PositionUpdate, 2, 2);
  forward(false);
  submit(TouchEvent_PositionUpdate, 3, 3);
  forward(false);
  task = PebbleTask_KernelMain;
  PebbleEvent e = touch(TouchEvent_Liftoff, 4, false);
  assert(pbl_msgq_put(&app_queue, &e, PBL_NO_WAIT) == -EBUSY && owned() == 2);
  e = touch(TouchEvent_PositionUpdate, 4, false);
  assert(pbl_msgq_put(&app_queue, &e, PBL_NO_WAIT) == -EBUSY && owned() == 2);
  assert(receive(2).touch.event.x == 2);
  assert(receive(3).touch.event.x == 3);
  assert(!owned());
  // App generation changes invalidate queued old samples; new motion forms a fresh tail.
  setup(8);
  submit(TouchEvent_PositionUpdate, 1, 1);
  forward(false);
  submit(TouchEvent_PositionUpdate, 2, 2);
  forward(false);
  ++app_thread.id;
  submit(TouchEvent_PositionUpdate, 3, 3);
  forward(false);
  task = PebbleTask_App;
  assert(pbl_msgq_get(&app_queue, &e, PBL_NO_WAIT) == 0 && e.type == PEBBLE_NULL_EVENT);
  assert(pbl_msgq_get(&app_queue, &e, PBL_NO_WAIT) == 0 && e.type == PEBBLE_NULL_EVENT);
  assert(receive(3).touch.event.x == 3 && !owned());
  // A stale sample cannot expose a privileged callback to a new unprivileged app.
  setup(8);
  submit(TouchEvent_PositionUpdate, 1, 1);
  forward(false);
  ++app_thread.id;
  metadata.is_unprivileged = true;
  task = PebbleTask_App;
  assert(!pbl_msgq_get(&app_queue, &e, PBL_NO_WAIT) && e.type == PEBBLE_NULL_EVENT && !owned());
  // The normal queue-cleanup consumer releases all old-generation tokens.
  setup(8);
  submit(TouchEvent_PositionUpdate, 1, 1);
  forward(false);
  ++app_thread.id;
  task = PebbleTask_KernelMain;
  assert(pbl_msgq_get(&app_queue, &e, PBL_NO_WAIT) == 0);
  pbl_msgq_purge(&app_queue);
  assert(!owned());
  // Unprivileged apps receive stock touch events and timing remains syscall-based.
  setup(8);
  metadata.is_unprivileged = true;
  submit(TouchEvent_Touchdown, 1, 44);
  forward(false);
  assert(!owned());
  task = PebbleTask_App;
  assert(pbl_msgq_get(&app_queue, &e, PBL_NO_WAIT) == 0);
  recognizer_manager_handle_touch_event(&e.touch.event, NULL);
  assert(delivered_ticks == clock_ms);
  control = 1;
  clock_ms = 222;
  assert(t5ai_touch_sample_ticks() == 222);
  // Duplicate, forged and failed input publications never steal or leak a record.
  control = 0;
  setup(8);
  task = PebbleTask_SysWorkq;
  submit(TouchEvent_Touchdown, 1, 1);
  PebbleEvent token;
  assert(!pbl_msgq_peek(&input_queue, &token));
  assert(pbl_msgq_put(&input_queue, &token, PBL_NO_WAIT) == -EINVAL && owned() == 1);
  forward(false);
  receive(1);
  assert(!owned());
  token.callback.data = (void *)12345;
  assert(pbl_msgq_put(&input_queue, &token, PBL_NO_WAIT) == -EINVAL);
  setup(8);
  PebbleEvent scratch[1];
  struct pbl_msgq full;
  pbl_msgq_init(&full, scratch, sizeof(PebbleEvent), 1);
  e = (PebbleEvent){.type = PEBBLE_NULL_EVENT};
  assert(!pbl_msgq_put(&full, &e, PBL_NO_WAIT));
  e = touch(TouchEvent_Touchdown, 1, false);
  pbl_irq_lock();
  T5AITouchSample *pending = prv_allocate(&e, 1, NULL, 0);
  token = prv_enclose(pending);
  pbl_irq_unlock();
  assert(pbl_msgq_put(&full, &token, PBL_NO_WAIT) == -EBUSY && !owned());
  // A bounded pool rejects further app allocation without losing a stored transition.
  control = 0;
  setup(128);
  task = PebbleTask_KernelMain;
  for (unsigned i = 0; i < T5AI_TOUCH_SAMPLE_CAPACITY; ++i) {
    e = touch(TouchEvent_Touchdown, i, false);
    assert(!pbl_msgq_put(&app_queue, &e, PBL_NO_WAIT));
  }
  e = touch(TouchEvent_Liftoff, 100, false);
  assert(pbl_msgq_put(&app_queue, &e, PBL_NO_WAIT) == -ENOMEM);
  for (unsigned i = 0; i < T5AI_TOUCH_SAMPLE_CAPACITY; ++i)
    assert(!pbl_msgq_get(&app_queue, &e, PBL_NO_WAIT));
  assert(!owned() && !irq_depth);
  return 0;
}
