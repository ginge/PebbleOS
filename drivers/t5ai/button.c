/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"

#include <stdint.h>
#include <stdlib.h>

#include <pbl/drivers/button.h>
#include <pbl/drivers/gpio.h>

static bool s_rotated_180;

static bool prv_button_present(ButtonId id) {
  return pbl_gpio_is_connected(&BOARD_CONFIG_BUTTON.buttons[id].gpio);
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
  return pbl_gpio_get(&button->gpio) > 0;
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
    (void)pbl_gpio_configure(&button->gpio, PBL_GPIO_INPUT);
  }
}
