/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/button.h>

#include "board/board.h"

#include <pbl/drivers/gpio/t5ai_legacy.h>

#include <stdint.h>
#include <stdlib.h>

static bool s_rotated_180;

static bool prv_button_present(ButtonId id) {
  return BOARD_CONFIG_BUTTON.buttons[id].pin != UINT8_MAX;
}

void button_set_rotated(bool rotated) {
  s_rotated_180 = rotated;
}

bool button_is_pressed(ButtonId id) {
  if (s_rotated_180 && id == BUTTON_ID_UP) {
    id = BUTTON_ID_DOWN;
  } else if (s_rotated_180 && id == BUTTON_ID_DOWN) {
    id = BUTTON_ID_UP;
  }

  if (!prv_button_present(id)) {
    return false;
  }

  const ButtonConfig *button = &BOARD_CONFIG_BUTTON.buttons[id];
  const InputConfig input = { .pin = button->pin };
  const bool level = gpio_input_read(&input);
  return button->active_high ? level : !level;
}

uint8_t button_get_state_bits(void) {
  uint8_t state = 0u;
  for (ButtonId id = BUTTON_ID_BACK; id < NUM_BUTTONS; ++id) {
    if (button_is_pressed(id)) {
      state |= 1u << id;
    }
  }
  return state;
}

void button_init(void) {
  for (ButtonId id = BUTTON_ID_BACK; id < NUM_BUTTONS; ++id) {
    if (!prv_button_present(id)) {
      continue;
    }
    const ButtonConfig *button = &BOARD_CONFIG_BUTTON.buttons[id];
    const InputConfig input = { .pin = button->pin };
    gpio_input_init_pull_up_down(&input, (GPIOPuPd_TypeDef)button->pull);
  }
}
