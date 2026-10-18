/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "input_port.h"

bool tuya_input_process(TuyaInputState *s, const TuyaInputSink *sink, bool touched, uint16_t x,
                        uint16_t y, bool back, bool select, uint32_t now_ms) {
  if (!s || !sink || (touched && (x >= TUYA_LOGICAL_SIZE || y >= TUYA_LOGICAL_SIZE))) {
    return false;
  }
  uint8_t buttons = (back ? 1u << BUTTON_ID_BACK : 0) | (select ? 1u << BUTTON_ID_SELECT : 0);
  TouchEvent event = {.non_navigational = sink->swipe_buttons};
  bool emit_touch = false;
  if (touched) {
    if (!s->touching) {
      s->start_x = x;
      s->start_y = y;
      s->started_ms = now_ms;
      event.type = TouchEvent_Touchdown;
      emit_touch = true;
    } else if (x != s->last_x || y != s->last_y) {
      event.type = TouchEvent_PositionUpdate;
      emit_touch = true;
    }
    s->last_x = x;
    s->last_y = y;
  } else if (s->touching) {
    event.type = TouchEvent_Liftoff;
    emit_touch = true;
    int dx = (int)s->last_x - s->start_x;
    int dy = (int)s->last_y - s->start_y;
    if (sink->swipe_buttons && (uint32_t)(now_ms - s->started_ms) <= 1000 && dx > -30 && dx < 30) {
      if (dy <= -40) {
        buttons |= 1u << BUTTON_ID_UP;
      }
      if (dy >= 40) {
        buttons |= 1u << BUTTON_ID_DOWN;
      }
    }
  }
  s->touching = touched;
  event.x = s->last_x;
  event.y = s->last_y;
  if (emit_touch && sink->touch) {
    sink->touch(sink->context, &event);
  }
  for (ButtonId id = BUTTON_ID_BACK; id < NUM_BUTTONS; ++id) {
    if (((buttons ^ s->buttons) & (1u << id)) && sink->button) {
      sink->button(sink->context, id, (buttons & (1u << id)) != 0);
    }
  }
  s->buttons = buttons;
  return true;
}
