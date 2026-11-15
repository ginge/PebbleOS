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

#include "applib/platform.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/gpio/t5ai.h>
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
#include <pbl/soc/t5ai/devices.h>

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
#define CST92XX_INTERRUPT_PIN           42u
#define CST92XX_RESET_PIN               43u
#define CST92XX_DATA_REGISTER           0xd000u
#define CST92XX_REPORT_LENGTH           10u
#define CST92XX_REPORT_ACK              0xabu
#define CST92XX_TOUCH_STATUS            0x06u
#define CST92XX_NATIVE_SIZE             466u
#define CST92XX_POLL_MS                 20u
#define CST92XX_IDLE_POLL_MAX_MS        1000u

#ifdef CONFIG_T5AI_IO_PROFILE
static uint32_t s_queued_at;
#endif
static bool s_enabled;
static bool s_initialized;
static bool s_down;
static bool s_contact_active;
static bool s_palm_down;
static bool s_callback_scheduled;
static bool s_retry_pending;
static bool s_poll_scheduled;
static bool s_transfer_retry;
static uint16_t s_idle_poll_ms;
static uint32_t s_generation;
static uint32_t s_read_failures;
static uint8_t s_consecutive_read_failures;
static int16_t s_last_x;
static int16_t s_last_y;
static PBL_MUTEX_DEFINE(s_i2c_lock);
static struct pbl_delayed_work s_retry_timer;
static void *s_retry_context;
static const struct pbl_irq_spec s_interrupt =
    PBL_IRQ_SPEC(&T5AI_GPIO_INTC, CST92XX_INTERRUPT_PIN, PBL_IRQ_TYPE_EDGE_FALLING);
static const struct pbl_gpio s_interrupt_input = PBL_GPIO(&T5AI_GPIO, CST92XX_INTERRUPT_PIN, 0);
static const struct pbl_gpio s_reset = PBL_GPIO(&T5AI_GPIO, CST92XX_RESET_PIN, 0);

static void prv_process_pending_messages(void *context);
static void prv_watchdog_cb(void *context);
static struct pbl_delayed_work s_watchdog_timer;

// Caller holds s_i2c_lock, including across reset delays.
static void prv_reset(bool run) {
  (void)pbl_gpio_set(&s_reset, false);
  if (run) {
    pbl_sleep_ms(CST92XX_RESET_ASSERT_MS);
    (void)pbl_gpio_set(&s_reset, true);
    pbl_sleep_ms(CST92XX_RESET_SETTLE_MS);
  }
}

static bool prv_read_report(uint8_t report[CST92XX_REPORT_LENGTH]) {
  const uint8_t address[] = {
    (uint8_t)(CST92XX_DATA_REGISTER >> 8),
    (uint8_t)CST92XX_DATA_REGISTER,
  };
  T5_IO_BEGIN(read_at);
  const uint8_t ack[] = {address[0], address[1], CST92XX_REPORT_ACK};
  const bool result = pbl_i2c_write(I2C_CST92XX, address, sizeof(address)) == 0 &&
                      pbl_i2c_read(I2C_CST92XX, report, CST92XX_REPORT_LENGTH) == 0 &&
                      pbl_i2c_write(I2C_CST92XX, ack, sizeof(ack)) == 0;
  T5_IO_END(T5IO_TOUCH_READ, read_at, CST92XX_REPORT_LENGTH);
  return result;
}

static int16_t prv_map_coordinate(uint16_t coordinate, int16_t display_size) {
  return (int16_t)(coordinate * display_size / CST92XX_NATIVE_SIZE);
}

static void prv_report_sample(bool down, int16_t x, int16_t y) {
  t5ai_draw_profile_touch(down);
  pbl_touch_report(TOUCH, down, x, y);
}

static void prv_report_palm(int16_t x, int16_t y) {
  pbl_touch_report_gesture(TOUCH, PBL_INPUT_GES_PALM, x, y);
}

static uint32_t s_work_generation;
static void prv_touch_work(struct pbl_work *work) {
  const uint32_t generation = s_work_generation;
  prv_process_pending_messages((void *)(uintptr_t)generation);
}
static PBL_WORK_DEFINE(s_touch_work, prv_touch_work);

static void prv_schedule(uint32_t generation) {
  pbl_irq_lock();
  if (s_enabled && generation == s_generation && !s_transfer_retry && !s_callback_scheduled) {
    s_callback_scheduled = true;
#ifdef CONFIG_T5AI_IO_PROFILE
    s_queued_at = T5AI_AON_RTC_COUNT_LOW;
#endif
    s_work_generation = generation;
    (void)pbl_work_submit_boosted(&pbl_sys_workq, &s_touch_work);
    s_retry_pending = false;
  }
  pbl_irq_unlock();
}

static void prv_interrupt_handler(void *arg) {
  prv_schedule(s_generation);
}

// Delayed work only queues the bus transfers and reset delays on KernelBG.
static void prv_retry_cb(void *context) {
  const uint32_t generation = (uint32_t)(uintptr_t)context;
  pbl_irq_lock();
  if (generation == s_generation) {
    s_poll_scheduled = false;
    s_transfer_retry = false;
  }
  pbl_irq_unlock();
  prv_schedule(generation);
}
static void prv_retry_work(struct pbl_work *work) {
  prv_retry_cb(s_retry_context);
}

static void prv_watchdog_cb(void *context) {
  pbl_irq_lock();
  const bool retry = s_enabled && !s_poll_scheduled &&
                     (s_retry_pending || pbl_gpio_get_raw(&s_interrupt_input) == 0);
  const uint32_t generation = s_generation;
  pbl_irq_unlock();
  if (retry) {
    prv_schedule(generation);
  }
}
static void prv_watchdog_work(struct pbl_work *work) {
  prv_watchdog_cb(nullptr);
  if (s_enabled)
    pbl_delayed_work_schedule_next(&s_watchdog_timer, pbl_ms_to_ticks(1000));
}

// Caller holds s_i2c_lock for report state, recovery and polling decisions.
static uint32_t prv_retry_failed_transfer(void) {
  uint32_t retry_ms = CST92XX_READ_RETRY_MS;
  pbl_irq_lock();
  s_transfer_retry = true;
  pbl_irq_unlock();
  s_idle_poll_ms = 0u;
  s_contact_active = false;
  ++s_read_failures;
  ++s_consecutive_read_failures;
  if (s_read_failures == 1u || (s_read_failures & CST92XX_FAILURE_LOG_MASK) == 0u) {
    PBL_LOG_ERR("CST92xx report transfer failed (%lu total)", (unsigned long)s_read_failures);
  }
  if (s_consecutive_read_failures >= CST92XX_RESET_FAILURE_THRESHOLD) {
    (void)pbl_intc_disable(&s_interrupt);
    prv_reset(true);
    s_consecutive_read_failures = 0u;
    (void)pbl_intc_enable(&s_interrupt);
    retry_ms = CST92XX_RESET_RETRY_MS;
  }
  return retry_ms;
}

static bool prv_decode_report(const uint8_t report[CST92XX_REPORT_LENGTH], bool *palm) {
  const uint8_t points = report[CST92XX_POINTS_OFFSET] & CST92XX_POINTS_MASK;
  const uint8_t status = report[CST92XX_STATUS_OFFSET] & CST92XX_NIBBLE_MASK;
  // An ACK echo or empty buffer is not a fresh liftoff report.
  const bool framed = report[CST92XX_ACK_OFFSET] == CST92XX_REPORT_ACK &&
                      report[CST92XX_STATUS_OFFSET] != CST92XX_REPORT_ACK;
  if (!framed) {
    return s_down;
  }
  const bool any_down =
      points != 0u && (status == CST92XX_TOUCH_STATUS ||
                       (points > 1u && (report[CST92XX_SECOND_STATUS_OFFSET] &
                                        CST92XX_NIBBLE_MASK) == CST92XX_TOUCH_STATUS));
  if (any_down && (report[CST92XX_COVER_OFFSET] & CST92XX_COVER_MASK)) {
    *palm = !s_palm_down;
    s_palm_down = true;
  } else if (!any_down) {
    s_palm_down = false;
  }
  bool down = false;
  s_contact_active = any_down;
  if (!s_palm_down && points != 0u && status == CST92XX_TOUCH_STATUS) {
    const uint16_t native_x = ((uint16_t)report[CST92XX_X_HIGH_OFFSET] << CST92XX_NIBBLE_BITS) |
                              (report[CST92XX_XY_LOW_OFFSET] >> CST92XX_NIBBLE_BITS);
    const uint16_t native_y = ((uint16_t)report[CST92XX_Y_HIGH_OFFSET] << CST92XX_NIBBLE_BITS) |
                              (report[CST92XX_XY_LOW_OFFSET] & CST92XX_NIBBLE_MASK);
    if (native_x < CST92XX_NATIVE_SIZE && native_y < CST92XX_NATIVE_SIZE) {
      // The panel is mounted with both touch axes mirrored.
      s_last_x = PBL_DISPLAY_WIDTH - 1 - prv_map_coordinate(native_x, PBL_DISPLAY_WIDTH);
      s_last_y = PBL_DISPLAY_HEIGHT - 1 - prv_map_coordinate(native_y, PBL_DISPLAY_HEIGHT);
      down = true;
    } else {
      s_contact_active = false;
      PBL_LOG_WRN("CST92xx coordinate out of range: %u,%u", (unsigned)native_x, (unsigned)native_y);
    }
  }
  return down;
}

static uint32_t prv_poll_delay(void) {
  uint32_t retry_ms = 0u;
  // Keep recovery polling during suppressed contact, including partial multi-finger release.
  if (s_contact_active || s_palm_down) {
    s_idle_poll_ms = 0u;
    retry_ms = CST92XX_POLL_MS;
  } else if (pbl_gpio_get_raw(&s_interrupt_input) == 0) {
    // Empty reports on a stuck INT must not keep the AP and bus busy at 50 Hz.
    uint32_t next_ms = s_idle_poll_ms ? s_idle_poll_ms * 2u : CST92XX_POLL_MS;
    s_idle_poll_ms = next_ms < CST92XX_IDLE_POLL_MAX_MS ? next_ms : CST92XX_IDLE_POLL_MAX_MS;
    retry_ms = s_idle_poll_ms;
  } else {
    s_idle_poll_ms = 0u;
  }
  return retry_ms;
}

static bool prv_generation_current(uint32_t generation) {
  pbl_irq_lock();
  const bool current = s_enabled && generation == s_generation;
  pbl_irq_unlock();
  return current;
}

static void prv_process_pending_messages(void *context) {
  const uint32_t generation = (uint32_t)(uintptr_t)context;
  T5_IO_BEGIN(callback_at);
#ifdef CONFIG_T5AI_IO_PROFILE
  const uint32_t queued_at = s_queued_at;
#endif
  T5_IO_BEGIN(lock_at);
  pbl_mutex_lock(&s_i2c_lock, PBL_FOREVER);
  T5_IO_END(T5IO_TOUCH_LOCK, lock_at, 0u);
  pbl_irq_lock();
  if (!s_enabled || generation != s_generation) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&s_i2c_lock);
    return;
  }
  T5_IO_ADD(T5IO_TOUCH_QUEUE, callback_at - queued_at, 0u);
  s_callback_scheduled = false;
  // An IRQ queued during a failed transfer must not bypass its retry delay.
  if (s_transfer_retry) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&s_i2c_lock);
    return;
  }
  s_poll_scheduled = false;
  pbl_irq_unlock();
  (void)pbl_delayed_work_cancel(&s_retry_timer);

  uint8_t report[CST92XX_REPORT_LENGTH] = {0};
  bool down;
  bool palm = false;
  uint32_t retry_ms;
  if (prv_read_report(report)) {
    s_consecutive_read_failures = 0u;
    down = prv_decode_report(report, &palm);
    retry_ms = prv_poll_delay();
  } else {
    down = false;
    retry_ms = prv_retry_failed_transfer();
  }
  s_down = down;

  if (retry_ms) {
    pbl_irq_lock();
    s_poll_scheduled = true;
    pbl_irq_unlock();
    s_retry_context = context;
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq_high, &s_retry_timer, PBL_MSEC(retry_ms));
  }
  const int16_t x = s_last_x;
  const int16_t y = s_last_y;
  pbl_mutex_unlock(&s_i2c_lock);
  // Input listeners take service locks; never invoke them under the hardware lock.
  // Palm listeners may disable/re-enable touch; recheck before the raw sample.
  if (palm && prv_generation_current(generation)) {
    prv_report_palm(x, y);
  }
  if (prv_generation_current(generation)) {
    prv_report_sample(down, x, y);
  }
  T5_IO_END(T5IO_TOUCH_CALLBACK, callback_at, 0u);
}

static int prv_touch_init(const struct pbl_touch *touch) {
  if (s_initialized) {
    return 0;
  }
  int ret = pbl_device_init(&T5AI_GPIO_INTC.dev);
  if (ret != 0) {
    return ret;
  }
  pbl_delayed_work_init(&s_retry_timer, prv_retry_work);
  pbl_delayed_work_init(&s_watchdog_timer, prv_watchdog_work);
  pbl_mutex_lock(&s_i2c_lock, PBL_FOREVER);
  (void)pbl_gpio_configure(&s_reset, PBL_GPIO_OUTPUT_INACTIVE);
  prv_reset(true);
  uint8_t report[CST92XX_REPORT_LENGTH] = {0};
  bool detected = false;
  for (unsigned attempt = 0; attempt < 3u; ++attempt) {
    if (prv_read_report(report)) {
      detected = true;
      break;
    }
    if (attempt + 1u < 3u)
      prv_reset(true);
  }
  if (!detected)
    PBL_LOG_ERR("CST92xx not detected at 0x5a");
  prv_reset(false);
  pbl_mutex_unlock(&s_i2c_lock);
  PBL_ASSERTN(pbl_gpio_configure(&s_interrupt_input, PBL_GPIO_INPUT) == 0);
  PBL_ASSERTN(pbl_intc_request(&s_interrupt, prv_interrupt_handler, nullptr) == 0);
  s_initialized = true;
  return 0;
}

static void prv_set_enabled(bool enabled) {
  pbl_mutex_lock(&s_i2c_lock, PBL_FOREVER);
  if (s_enabled == enabled) {
    pbl_mutex_unlock(&s_i2c_lock);
    return;
  }
  (void)pbl_intc_disable(&s_interrupt);
  pbl_irq_lock();
  s_enabled = false;
  ++s_generation;
  s_callback_scheduled = false;
  s_retry_pending = false;
  s_poll_scheduled = false;
  s_transfer_retry = false;
  s_idle_poll_ms = 0u;
  pbl_irq_unlock();
  (void)pbl_delayed_work_cancel(&s_retry_timer);
  s_consecutive_read_failures = 0u;
  s_down = false;
  s_contact_active = false;
  s_palm_down = false;
  if (enabled) {
    prv_reset(true);
    pbl_irq_lock();
    s_enabled = true;
    pbl_irq_unlock();
    (void)pbl_intc_enable(&s_interrupt);
    (void)pbl_delayed_work_schedule(&pbl_sys_workq_high, &s_watchdog_timer, PBL_MSEC(1000));
    // Read once on enable even if INT was already asserted before IRQ unmasking.
    prv_schedule(s_generation);
  } else {
    (void)pbl_delayed_work_cancel(&s_watchdog_timer);
    prv_reset(false);
  }
  pbl_mutex_unlock(&s_i2c_lock);
}

static int prv_touch_enable(const struct pbl_touch *touch) {
  prv_set_enabled(true);
  return 0;
}
static int prv_touch_disable(const struct pbl_touch *touch) {
  prv_set_enabled(false);
  return 0;
}
const struct pbl_touch_ops pbl_touch_cst92xx_t5ai_ops = {
  .init = prv_touch_init,
  .enable = prv_touch_enable,
  .disable = prv_touch_disable,
};
