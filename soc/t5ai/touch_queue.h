/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "kernel/events.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pbl/kernel/msgq.h>

// Internal queue metadata occupies unused touch-union bytes, never TouchEvent or its ABI.
#define T5_TOUCH_TIME_MASK 0x7fffffu
static_assert(offsetof(PebbleEvent, task_mask) >= sizeof(PebbleTouchEvent) + 3u,
              "No room for board-local touch timestamp");
static inline uint8_t *t5_touch_meta(PebbleEvent *e) {
  return (uint8_t *)e + sizeof(PebbleTouchEvent);
}
static inline void t5_touch_stamp(PebbleEvent *e, uint64_t ticks) {
  uint8_t *p = t5_touch_meta(e);
  p[0] = ticks;
  p[1] = ticks >> 8;
  p[2] = ((ticks >> 16) & 0x7fu) | 0x80u;
}
static inline bool t5_touch_has_stamp(const PebbleEvent *e) {
  return ((const uint8_t *)e)[sizeof(PebbleTouchEvent) + 2u] & 0x80u;
}
static inline uint64_t t5_touch_time(const PebbleEvent *e, uint64_t now) {
  const uint8_t *p = (const uint8_t *)e + sizeof(PebbleTouchEvent);
  const uint32_t stamp = p[0] | (uint32_t)p[1] << 8 | (uint32_t)(p[2] & 0x7fu) << 16;
  const uint32_t age = ((uint32_t)now - stamp) & T5_TOUCH_TIME_MASK;
  return now - age;
}
static inline bool t5_touch_move(const PebbleEvent *e) {
  return e->type == PEBBLE_TOUCH_EVENT && e->touch.event.type == TouchEvent_PositionUpdate;
}
// Caller holds the kernel IRQ lock; retain both most-recent acquisition samples for velocity.
static inline bool t5_touch_combine(struct pbl_msgq *q, const PebbleEvent *e) {
  if (!t5_touch_move(e) || q->backend.count < 2u)
    return false;
  const uint32_t last = (q->backend.tail + q->max_msgs - 1u) % q->max_msgs;
  const uint32_t previous = (last + q->max_msgs - 1u) % q->max_msgs;
  PebbleEvent *a = (PebbleEvent *)((uint8_t *)q->buf + previous * q->msg_size);
  PebbleEvent *b = (PebbleEvent *)((uint8_t *)q->buf + last * q->msg_size);
  if (!t5_touch_move(a) || !t5_touch_move(b) ||
      a->touch.event.non_navigational != e->touch.event.non_navigational ||
      b->touch.event.non_navigational != e->touch.event.non_navigational)
    return false;
  *a = *b;
  *b = *e;
  return true;
}
