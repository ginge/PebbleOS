/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include "kernel/events.h"
#include "pbl/util/bitops.h"
#include "system/bootbits.h"
#include "system/passert.h"
#include "system/reset.h"

#include <stdint.h>
#include <stdlib.h>

#include <pbl/drivers/button.h>
#include <pbl/drivers/debounced_button.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/intc.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>

#define RESET_HOLD_MS 5000u

#define DEBOUNCE_PERIOD_MS      2u
#define NUM_DEBOUNCE_SAMPLES    20u
#define RESET_BUTTONS           ((1u << BUTTON_ID_SELECT) | (1u << BUTTON_ID_BACK))
#define RESET_THRESHOLD_SAMPLES (RESET_HOLD_MS / DEBOUNCE_PERIOD_MS)

static PBL_SEM_DEFINE(s_button_signal, 0, 1);
static struct pbl_thread s_button_thread;
PBL_THREAD_STACK_DEFINE(s_button_stack, 1536);
static uint32_t s_sample_counts[NUM_BUTTONS];
static uint32_t s_debounced_state;
static uint32_t s_hard_reset_samples;

static bool prv_button_present(ButtonId id) {
  return pbl_gpio_is_connected(&BOARD_CONFIG_BUTTON.buttons[id].gpio);
}

static bool prv_sample_buttons(void) {
  bool sampling_required = false;

  for (ButtonId id = BUTTON_ID_BACK; id < NUM_BUTTONS; ++id) {
    if (!prv_button_present(id)) {
      continue;
    }

    const bool pressed = button_is_pressed(id);
    const bool debounced = pbl_bitset32_get(&s_debounced_state, id);
    if (pressed == debounced) {
      s_sample_counts[id] = 0u;
      continue;
    }

    sampling_required = true;
    if (++s_sample_counts[id] < NUM_DEBOUNCE_SAMPLES) {
      continue;
    }

    s_sample_counts[id] = 0u;
    pbl_bitset32_update(&s_debounced_state, id, pressed);
    PebbleEvent event = {
      .type = pressed ? PEBBLE_BUTTON_DOWN_EVENT : PEBBLE_BUTTON_UP_EVENT,
      .button.button_id = id,
    };

    event_put(&event);
  }

#if !defined(CONFIG_MFG)
  if ((s_debounced_state & RESET_BUTTONS) == RESET_BUTTONS) {
    sampling_required = true;
    if (++s_hard_reset_samples > RESET_THRESHOLD_SAMPLES) {
      const bool force_prf = (s_debounced_state & (1u << BUTTON_ID_UP)) != 0u;
      if (force_prf) {
        boot_bit_set(BOOT_BIT_FORCE_PRF);
      }
      RebootReason reason = {
        .code =
            force_prf ? RebootReasonCode_PrfResetButtonsHeld : RebootReasonCode_ResetButtonsHeld,
      };
      reboot_reason_set(&reason);
      system_hard_reset();
    }
  } else {
    s_hard_reset_samples = 0u;
  }
#endif

  return sampling_required;
}

static void prv_button_interrupt_handler(void *arg) {
  // Coalesce edges without a droppable work queue or a KernelBG dependency.
  pbl_sem_give(&s_button_signal);
}

static void prv_button_entry(void *context) {
  (void)context;
  while (true) {
    if (pbl_sem_take(&s_button_signal, PBL_FOREVER) != 0) {
      continue;
    }
    do {
      pbl_thread_sleep(PBL_MSEC(DEBOUNCE_PERIOD_MS));
    } while (prv_sample_buttons());
  }
}

void debounced_button_init(void) {
  button_init();

  // Match timer-service urgency without claiming an unqualified hardware timer.
  const struct pbl_thread_attr attr = {
    .name = "Buttons",
    .entry = prv_button_entry,
    .prio = PBL_PRIO_MAX,
    .privileged = true,
    .stack = s_button_stack,
    .stack_size = sizeof(s_button_stack),
  };
  PBL_ASSERTN(pbl_thread_create(&s_button_thread, &attr) == 0);

  for (ButtonId id = BUTTON_ID_BACK; id < NUM_BUTTONS; ++id) {
    if (!prv_button_present(id)) {
      continue;
    }
    const ButtonConfig *button = &BOARD_CONFIG_BUTTON.buttons[id];
    struct pbl_irq_spec irq;
    PBL_ASSERTN(pbl_gpio_to_irq(&button->gpio, PBL_IRQ_TYPE_EDGE_BOTH, &irq) == 0);
    PBL_ASSERTN(pbl_intc_request(&irq, prv_button_interrupt_handler, nullptr) == 0);
    PBL_ASSERTN(pbl_intc_enable(&irq) == 0);
  }

  if (button_get_state_bits() != 0u) {
    pbl_sem_give(&s_button_signal);
  }
}

void command_put_raw_button_event(const char *button_index, const char *is_button_down_event) {
  const int id = atoi(button_index);
  const int is_down = atoi(is_button_down_event);
  if (id < 0 || id >= NUM_BUTTONS || (is_down != 0 && is_down != 1)) {
    return;
  }

  PebbleEvent event = {
    .type = is_down ? PEBBLE_BUTTON_DOWN_EVENT : PEBBLE_BUTTON_UP_EVENT,
    .button.button_id = id,
  };
  event_put(&event);
}
