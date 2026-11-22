/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/draw_profile.h>

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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/i2c.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/touch/cst92xx_t5ai.h>
#include <pbl/input/input.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/util/misc.h>

PBL_LOG_MODULE_DEFINE(driver_touch_cst92xx, CONFIG_DRIVER_TOUCH_LOG_LEVEL);

#define CST92XX_RESET_ASSERT_MS         5u
#define CST92XX_RESET_SETTLE_MS         50u
#define CST92XX_READ_RETRY_MS           80u
#define CST92XX_RESET_RETRY_MS          1000u
#define CST92XX_RESET_FAILURE_THRESHOLD 3u
#define CST92XX_FAILURE_LOG_MASK        0x3fu
#define CST92XX_STATUS_OFFSET           0u
#define CST92XX_X_HIGH_OFFSET           1u
#define CST92XX_Y_HIGH_OFFSET           2u
#define CST92XX_XY_LOW_OFFSET           3u
#define CST92XX_COVER_OFFSET            4u
#define CST92XX_COVER_MASK              0x80u
#define CST92XX_SECOND_STATUS_OFFSET    7u
#define CST92XX_POINTS_OFFSET           5u
#define CST92XX_ACK_OFFSET              6u
#define CST92XX_POINTS_MASK             0x7fu
#define CST92XX_NIBBLE_MASK             0x0fu
#define CST92XX_NIBBLE_BITS             4u
#define CST92XX_DATA_REGISTER           0xd000u
#define CST92XX_REPORT_LENGTH           10u
#define CST92XX_REPORT_ACK              0xabu
#define CST92XX_TOUCH_STATUS            0x06u
#define CST92XX_POLL_MS                 20u
#define CST92XX_IDLE_POLL_MAX_MS        1000u

static const struct pbl_cst92xx_t5ai *prv_cst(const struct pbl_touch *touch) {
  return container_of(touch, const struct pbl_cst92xx_t5ai, touch);
}
static void prv_process_pending_messages(const struct pbl_cst92xx_t5ai *cst, uint32_t generation);

// Caller holds state->i2c_lock, including across reset delays.
static void prv_reset(const struct pbl_cst92xx_t5ai *cst, bool run) {
  (void)pbl_gpio_set(&cst->reset, false);
  if (run) {
    pbl_sleep_ms(CST92XX_RESET_ASSERT_MS);
    (void)pbl_gpio_set(&cst->reset, true);
    pbl_sleep_ms(CST92XX_RESET_SETTLE_MS);
  }
}

static bool prv_read_report(const struct pbl_cst92xx_t5ai *cst,
                            uint8_t report[CST92XX_REPORT_LENGTH]) {
  const uint8_t address[] = {
    (uint8_t)(CST92XX_DATA_REGISTER >> 8),
    (uint8_t)CST92XX_DATA_REGISTER,
  };
  T5_IO_BEGIN(read_at);
  const uint8_t ack[] = {address[0], address[1], CST92XX_REPORT_ACK};
  const bool result = pbl_i2c_write(&cst->i2c, address, sizeof(address)) == 0 &&
                      pbl_i2c_read(&cst->i2c, report, CST92XX_REPORT_LENGTH) == 0 &&
                      pbl_i2c_write(&cst->i2c, ack, sizeof(ack)) == 0;
  T5_IO_END(T5IO_TOUCH_READ, read_at, CST92XX_REPORT_LENGTH);
  return result;
}

static int16_t prv_map_coordinate(uint16_t coordinate, uint16_t display_size,
                                  uint16_t native_size) {
  return (int16_t)((uint32_t)coordinate * display_size / native_size);
}

static void prv_report_sample(const struct pbl_cst92xx_t5ai *cst, bool down, int16_t x, int16_t y) {
  t5ai_draw_profile_touch(down);
  pbl_touch_report(&cst->touch, down, x, y);
}

static void prv_report_palm(const struct pbl_cst92xx_t5ai *cst, int16_t x, int16_t y) {
  pbl_touch_report_gesture(&cst->touch, PBL_INPUT_GES_PALM, x, y);
}

static void prv_touch_work(struct pbl_work *work) {
  struct pbl_cst92xx_t5ai_state *state =
      container_of(work, struct pbl_cst92xx_t5ai_state, touch_work);
  const struct pbl_cst92xx_t5ai *cst = state->cst;
  const uint32_t generation = state->work_generation;
  prv_process_pending_messages(cst, generation);
}

static void prv_schedule(const struct pbl_cst92xx_t5ai *cst, uint32_t generation) {
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  pbl_irq_lock();
  if (state->enabled && generation == state->generation && !state->transfer_retry &&
      !state->callback_scheduled) {
    state->callback_scheduled = true;
#ifdef CONFIG_T5AI_IO_PROFILE
    state->queued_at = T5AI_AON_RTC_COUNT_LOW;
#endif
    state->work_generation = generation;
    (void)pbl_work_submit_boosted(&pbl_sys_workq, &state->touch_work);
    state->retry_pending = false;
  }
  pbl_irq_unlock();
}

static void prv_interrupt_handler(void *arg) {
  const struct pbl_cst92xx_t5ai *cst = arg;
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  prv_schedule(cst, state->generation);
}

// Delayed work only queues the bus transfers and reset delays on KernelBG.
static void prv_retry_work(struct pbl_work *work) {
  struct pbl_cst92xx_t5ai_state *state =
      container_of(work, struct pbl_cst92xx_t5ai_state, retry_timer.work);
  const uint32_t generation = state->retry_generation;
  pbl_irq_lock();
  if (generation == state->generation) {
    state->poll_scheduled = false;
    state->transfer_retry = false;
  }
  pbl_irq_unlock();
  prv_schedule(state->cst, generation);
}
static void prv_watchdog_work(struct pbl_work *work) {
  struct pbl_cst92xx_t5ai_state *state =
      container_of(work, struct pbl_cst92xx_t5ai_state, watchdog_timer.work);
  const struct pbl_cst92xx_t5ai *cst = state->cst;
  pbl_irq_lock();
  const bool retry = state->enabled && !state->poll_scheduled &&
                     (state->retry_pending || pbl_gpio_get_raw(&cst->interrupt_input) == 0);
  const uint32_t generation = state->generation;
  pbl_irq_unlock();
  if (retry) {
    prv_schedule(cst, generation);
  }
  if (state->enabled)
    pbl_delayed_work_schedule_next(&state->watchdog_timer, pbl_ms_to_ticks(1000));
}

// Caller holds state->i2c_lock for report state, recovery and polling decisions.
static uint32_t prv_retry_failed_transfer(const struct pbl_cst92xx_t5ai *cst) {
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  uint32_t retry_ms = CST92XX_READ_RETRY_MS;
  pbl_irq_lock();
  state->transfer_retry = true;
  pbl_irq_unlock();
  state->idle_poll_ms = 0u;
  state->contact_active = false;
  ++state->read_failures;
  ++state->consecutive_read_failures;
  if (state->read_failures == 1u || (state->read_failures & CST92XX_FAILURE_LOG_MASK) == 0u) {
    PBL_LOG_ERR("CST92xx report transfer failed (%lu total)", (unsigned long)state->read_failures);
  }
  if (state->consecutive_read_failures >= CST92XX_RESET_FAILURE_THRESHOLD) {
    (void)pbl_intc_disable(&cst->interrupt);
    prv_reset(cst, true);
    state->consecutive_read_failures = 0u;
    (void)pbl_intc_enable(&cst->interrupt);
    retry_ms = CST92XX_RESET_RETRY_MS;
  }
  return retry_ms;
}

static bool prv_decode_report(const struct pbl_cst92xx_t5ai *cst,
                              const uint8_t report[CST92XX_REPORT_LENGTH], bool *palm) {
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  const uint8_t points = report[CST92XX_POINTS_OFFSET] & CST92XX_POINTS_MASK;
  const uint8_t status = report[CST92XX_STATUS_OFFSET] & CST92XX_NIBBLE_MASK;
  // An ACK echo or empty buffer is not a fresh liftoff report.
  const bool framed = report[CST92XX_ACK_OFFSET] == CST92XX_REPORT_ACK &&
                      report[CST92XX_STATUS_OFFSET] != CST92XX_REPORT_ACK;
  if (!framed) {
    return state->down;
  }
  const bool any_down =
      points != 0u && (status == CST92XX_TOUCH_STATUS ||
                       (points > 1u && (report[CST92XX_SECOND_STATUS_OFFSET] &
                                        CST92XX_NIBBLE_MASK) == CST92XX_TOUCH_STATUS));
  if (any_down && (report[CST92XX_COVER_OFFSET] & CST92XX_COVER_MASK)) {
    *palm = !state->palm_down;
    state->palm_down = true;
  } else if (!any_down) {
    state->palm_down = false;
  }
  bool down = false;
  state->contact_active = any_down;
  if (!state->palm_down && points != 0u && status == CST92XX_TOUCH_STATUS) {
    const uint16_t native_x = ((uint16_t)report[CST92XX_X_HIGH_OFFSET] << CST92XX_NIBBLE_BITS) |
                              (report[CST92XX_XY_LOW_OFFSET] >> CST92XX_NIBBLE_BITS);
    const uint16_t native_y = ((uint16_t)report[CST92XX_Y_HIGH_OFFSET] << CST92XX_NIBBLE_BITS) |
                              (report[CST92XX_XY_LOW_OFFSET] & CST92XX_NIBBLE_MASK);
    if (native_x < cst->native_width && native_y < cst->native_height) {
      // The native touch class applies board orientation after scaling.
      state->last_x = prv_map_coordinate(native_x, cst->touch.props.size_x, cst->native_width);
      state->last_y = prv_map_coordinate(native_y, cst->touch.props.size_y, cst->native_height);
      down = true;
    } else {
      state->contact_active = false;
      PBL_LOG_WRN("CST92xx coordinate out of range: %u,%u", (unsigned)native_x, (unsigned)native_y);
    }
  }
  return down;
}

static uint32_t prv_poll_delay(const struct pbl_cst92xx_t5ai *cst) {
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  uint32_t retry_ms = 0u;
  // Keep recovery polling during suppressed contact, including partial multi-finger release.
  if (state->contact_active || state->palm_down) {
    state->idle_poll_ms = 0u;
    retry_ms = CST92XX_POLL_MS;
  } else if (pbl_gpio_get_raw(&cst->interrupt_input) == 0) {
    // Empty reports on a stuck INT must not keep the AP and bus busy at 50 Hz.
    uint32_t next_ms = state->idle_poll_ms ? state->idle_poll_ms * 2u : CST92XX_POLL_MS;
    state->idle_poll_ms = next_ms < CST92XX_IDLE_POLL_MAX_MS ? next_ms : CST92XX_IDLE_POLL_MAX_MS;
    retry_ms = state->idle_poll_ms;
  } else {
    state->idle_poll_ms = 0u;
  }
  return retry_ms;
}

static bool prv_generation_current(const struct pbl_cst92xx_t5ai *cst, uint32_t generation) {
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  pbl_irq_lock();
  const bool current = state->enabled && generation == state->generation;
  pbl_irq_unlock();
  return current;
}

static void prv_process_pending_messages(const struct pbl_cst92xx_t5ai *cst, uint32_t generation) {
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  T5_IO_BEGIN(callback_at);
#ifdef CONFIG_T5AI_IO_PROFILE
  const uint32_t queued_at = state->queued_at;
#endif
  T5_IO_BEGIN(lock_at);
  pbl_mutex_lock(&state->i2c_lock, PBL_FOREVER);
  T5_IO_END(T5IO_TOUCH_LOCK, lock_at, 0u);
  pbl_irq_lock();
  if (!state->enabled || generation != state->generation) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&state->i2c_lock);
    return;
  }
  T5_IO_ADD(T5IO_TOUCH_QUEUE, callback_at - queued_at, 0u);
  state->callback_scheduled = false;
  // An IRQ queued during a failed transfer must not bypass its retry delay.
  if (state->transfer_retry) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&state->i2c_lock);
    return;
  }
  state->poll_scheduled = false;
  pbl_irq_unlock();
  (void)pbl_delayed_work_cancel(&state->retry_timer);

  uint8_t report[CST92XX_REPORT_LENGTH] = {0};
  bool down;
  bool palm = false;
  uint32_t retry_ms;
  if (prv_read_report(cst, report)) {
    state->consecutive_read_failures = 0u;
    down = prv_decode_report(cst, report, &palm);
    retry_ms = prv_poll_delay(cst);
  } else {
    down = false;
    retry_ms = prv_retry_failed_transfer(cst);
  }
  state->down = down;

  if (retry_ms) {
    pbl_irq_lock();
    state->poll_scheduled = true;
    pbl_irq_unlock();
    state->retry_generation = generation;
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq_high, &state->retry_timer, PBL_MSEC(retry_ms));
  }
  const int16_t x = state->last_x;
  const int16_t y = state->last_y;
  pbl_mutex_unlock(&state->i2c_lock);
  // Input listeners take service locks; never invoke them under the hardware lock.
  // Palm listeners may disable/re-enable touch; recheck before the raw sample.
  if (palm && prv_generation_current(cst, generation)) {
    prv_report_palm(cst, x, y);
  }
  if (prv_generation_current(cst, generation)) {
    prv_report_sample(cst, down, x, y);
  }
  T5_IO_END(T5IO_TOUCH_CALLBACK, callback_at, 0u);
}

static int prv_touch_init(const struct pbl_touch *touch) {
  const struct pbl_cst92xx_t5ai *cst = prv_cst(touch);
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  if (state->initialized) {
    return 0;
  }
  int ret = pbl_device_init(&cst->interrupt.intc->dev);
  if (ret != 0) {
    return ret;
  }
  if (!cst->native_width || !cst->native_height || !touch->props.size_x || !touch->props.size_y)
    return -EINVAL;
  state->cst = cst;
  // Preserve initial logical (0,0), including idle/error reports before first contact.
  state->last_x = touch->props.inverted_x ? touch->props.size_x - 1u : 0u;
  state->last_y = touch->props.inverted_y ? touch->props.size_y - 1u : 0u;
  pbl_work_init(&state->touch_work, prv_touch_work);
  pbl_delayed_work_init(&state->retry_timer, prv_retry_work);
  pbl_delayed_work_init(&state->watchdog_timer, prv_watchdog_work);
  pbl_mutex_lock(&state->i2c_lock, PBL_FOREVER);
  (void)pbl_gpio_configure(&cst->reset, PBL_GPIO_OUTPUT_INACTIVE);
  prv_reset(cst, true);
  uint8_t report[CST92XX_REPORT_LENGTH] = {0};
  bool detected = false;
  for (unsigned attempt = 0; attempt < 3u; ++attempt) {
    if (prv_read_report(cst, report)) {
      detected = true;
      break;
    }
    if (attempt + 1u < 3u)
      prv_reset(cst, true);
  }
  if (!detected)
    PBL_LOG_ERR("CST92xx not detected at 0x%x", (unsigned)cst->i2c.addr);
  prv_reset(cst, false);
  pbl_mutex_unlock(&state->i2c_lock);
  PBL_ASSERTN(pbl_gpio_configure(&cst->interrupt_input, PBL_GPIO_INPUT) == 0);
  PBL_ASSERTN(pbl_intc_request(&cst->interrupt, prv_interrupt_handler, (void *)cst) == 0);
  state->initialized = true;
  return 0;
}

static void prv_set_enabled(const struct pbl_cst92xx_t5ai *cst, bool enabled) {
  struct pbl_cst92xx_t5ai_state *state = cst->state;
  pbl_mutex_lock(&state->i2c_lock, PBL_FOREVER);
  if (state->enabled == enabled) {
    pbl_mutex_unlock(&state->i2c_lock);
    return;
  }
  (void)pbl_intc_disable(&cst->interrupt);
  pbl_irq_lock();
  state->enabled = false;
  ++state->generation;
  state->callback_scheduled = false;
  state->retry_pending = false;
  state->poll_scheduled = false;
  state->transfer_retry = false;
  state->idle_poll_ms = 0u;
  pbl_irq_unlock();
  (void)pbl_delayed_work_cancel(&state->retry_timer);
  state->consecutive_read_failures = 0u;
  state->down = false;
  state->contact_active = false;
  state->palm_down = false;
  if (enabled) {
    prv_reset(cst, true);
    pbl_irq_lock();
    state->enabled = true;
    pbl_irq_unlock();
    (void)pbl_intc_enable(&cst->interrupt);
    (void)pbl_delayed_work_schedule(&pbl_sys_workq_high, &state->watchdog_timer, PBL_MSEC(1000));
    // Read once on enable even if INT was already asserted before IRQ unmasking.
    prv_schedule(cst, state->generation);
  } else {
    (void)pbl_delayed_work_cancel(&state->watchdog_timer);
    prv_reset(cst, false);
  }
  pbl_mutex_unlock(&state->i2c_lock);
}

static int prv_touch_enable(const struct pbl_touch *touch) {
  const struct pbl_cst92xx_t5ai *cst = prv_cst(touch);
  prv_set_enabled(cst, true);
  return 0;
}
static int prv_touch_disable(const struct pbl_touch *touch) {
  const struct pbl_cst92xx_t5ai *cst = prv_cst(touch);
  prv_set_enabled(cst, false);
  return 0;
}
const struct pbl_touch_ops pbl_touch_cst92xx_t5ai_ops = {
  .init = prv_touch_init,
  .enable = prv_touch_enable,
  .disable = prv_touch_disable,
};
