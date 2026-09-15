/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/debounced_button.h>

#include "board/board.h"
#include <pbl/drivers/button.h>
#include <pbl/drivers/exti.h>
#include <pbl/drivers/rtc.h>
#include <pbl/logging/logging.h>
#include <pbl/services/system_task.h>
#include "kernel/events.h"
#include "pbl/services/new_timer/new_timer.h"
#include "system/bootbits.h"
#include "system/passert.h"
#include "system/reset.h"
#include "util/bitset.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>
#include <stdlib.h>

PBL_LOG_MODULE_DEFINE(driver_debounced_button_t5ai, LOG_LEVEL_INFO);

#define DEBOUNCE_PERIOD_MS 2u
#define NUM_DEBOUNCE_SAMPLES 20u
#define RESET_BUTTONS ((1u << BUTTON_ID_SELECT) | (1u << BUTTON_ID_BACK))
#define RESET_THRESHOLD_SAMPLES (5000u / DEBOUNCE_PERIOD_MS)

static TimerID s_debounce_timer = TIMER_INVALID_ID;
static uint32_t s_sample_counts[NUM_BUTTONS];
static uint32_t s_debounced_state;
static uint32_t s_hard_reset_samples;

// Temporary T5AI latency probe. Keep all measurement at this driver boundary:
// no shared input, event-loop, or compositor code is changed by the experiment.
static volatile bool s_trace_active;
static volatile RtcTicks s_trace_irq_rtc;
static volatile TickType_t s_trace_irq_freertos;
static RtcTicks s_trace_callback_rtc;
static TickType_t s_trace_callback_freertos;
static RtcTicks s_trace_first_sample_rtc;
static bool s_trace_callback_seen;
static bool s_trace_first_sample_seen;

static bool prv_button_present(ButtonId id) {
  return BOARD_CONFIG_BUTTON.buttons[id].pin != UINT8_MAX;
}

static void prv_sample_buttons(void *context) {
  (void)context;
  bool sampling_required = false;

  if (s_trace_active && !s_trace_first_sample_seen) {
    s_trace_first_sample_rtc = rtc_get_ticks();
    s_trace_first_sample_seen = true;
  }

  for (ButtonId id = BUTTON_ID_BACK; id < NUM_BUTTONS; ++id) {
    if (!prv_button_present(id)) {
      continue;
    }

    const bool pressed = button_is_pressed(id);
    const bool debounced = bitset32_get(&s_debounced_state, id);
    if (pressed == debounced) {
      s_sample_counts[id] = 0u;
      continue;
    }

    sampling_required = true;
    if (++s_sample_counts[id] < NUM_DEBOUNCE_SAMPLES) {
      continue;
    }

    s_sample_counts[id] = 0u;
    bitset32_update(&s_debounced_state, id, pressed);
    PebbleEvent event = {
      .type = pressed ? PEBBLE_BUTTON_DOWN_EVENT : PEBBLE_BUTTON_UP_EVENT,
      .button.button_id = id,
    };

    if (s_trace_active) {
      const RtcTicks event_rtc = rtc_get_ticks();
      const TickType_t event_freertos = xTaskGetTickCount();
      const uint32_t callback_rtc_ms = s_trace_callback_seen ?
          (uint32_t)(s_trace_callback_rtc - s_trace_irq_rtc) : UINT32_MAX;
      const uint32_t callback_freertos_ticks = s_trace_callback_seen ?
          (uint32_t)(s_trace_callback_freertos - s_trace_irq_freertos) : UINT32_MAX;
      const uint32_t first_sample_ms = s_trace_first_sample_seen ?
          (uint32_t)(s_trace_first_sample_rtc - s_trace_irq_rtc) : UINT32_MAX;
      PBL_LOG_ERR(
          "BTNPERF id=%u down=%u cb_rtc=%lu cb_ft=%lu first=%lu event_rtc=%lu event_ft=%lu",
          (unsigned)id, (unsigned)pressed,
          (unsigned long)callback_rtc_ms, (unsigned long)callback_freertos_ticks,
          (unsigned long)first_sample_ms,
          (unsigned long)(event_rtc - s_trace_irq_rtc),
          (unsigned long)(event_freertos - s_trace_irq_freertos));
      s_trace_active = false;
    }
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
        .code = force_prf ? RebootReasonCode_PrfResetButtonsHeld :
                            RebootReasonCode_ResetButtonsHeld,
      };
      reboot_reason_set(&reason);
      system_hard_reset();
    }
  } else {
    s_hard_reset_samples = 0u;
  }
#endif

  if (!sampling_required) {
    (void)new_timer_stop(s_debounce_timer);
  }
}

static void prv_restart_debounce_timer(void *context) {
  (void)context;
  if (s_trace_active && !s_trace_callback_seen) {
    s_trace_callback_rtc = rtc_get_ticks();
    s_trace_callback_freertos = xTaskGetTickCount();
    s_trace_callback_seen = true;
  }
  PBL_ASSERTN(new_timer_start(s_debounce_timer, DEBOUNCE_PERIOD_MS,
                              prv_sample_buttons, NULL,
                              TIMER_START_FLAG_REPEATING));
}

static void prv_button_interrupt_handler(bool *should_context_switch) {
  if (!s_trace_active) {
    s_trace_irq_rtc = rtc_get_ticks();
    s_trace_irq_freertos = xTaskGetTickCountFromISR();
    s_trace_callback_seen = false;
    s_trace_first_sample_seen = false;
    s_trace_active = true;
  }
  PBL_ASSERTN(system_task_add_callback_from_isr(
      prv_restart_debounce_timer, NULL, should_context_switch));
}

void debounced_button_init(void) {
  button_init();

  s_debounce_timer = new_timer_create();
  PBL_ASSERTN(s_debounce_timer != TIMER_INVALID_ID);

  for (ButtonId id = BUTTON_ID_BACK; id < NUM_BUTTONS; ++id) {
    if (!prv_button_present(id)) {
      continue;
    }
    const ButtonConfig *button = &BOARD_CONFIG_BUTTON.buttons[id];
    const ExtiConfig exti = {
      .pin = button->pin,
      .pull = button->pull,
    };
    exti_configure_pin(exti, ExtiTrigger_RisingFalling,
                       prv_button_interrupt_handler);
    exti_enable(exti);
  }

  if (button_get_state_bits() != 0u) {
    prv_restart_debounce_timer(NULL);
  }
}

void command_put_raw_button_event(const char *button_index,
                                  const char *is_button_down_event) {
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
