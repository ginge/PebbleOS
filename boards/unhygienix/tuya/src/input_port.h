/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <assert.h>
#include "hardware_core.h"
#include "pbl/drivers/button_id.h"
#include "pbl/services/touch/touch_event.h"

typedef struct {
  void *context;
  void (*button)(void *context, ButtonId id, bool pressed);
  void (*touch)(void *context, const TouchEvent *event);
  bool swipe_buttons; /* Disable when using Pebble's touch navigation service. */
} TuyaInputSink;

typedef struct {
  uint8_t buttons;
  bool touching;
  uint16_t start_x, start_y, last_x, last_y;
  uint32_t started_ms;
} TuyaInputState;

bool tuya_input_process(TuyaInputState *state, const TuyaInputSink *sink, bool touched, uint16_t x,
                        uint16_t y, bool back, bool select, uint32_t now_ms);
