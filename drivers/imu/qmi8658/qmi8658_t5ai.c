/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include "pbl/services/new_timer/new_timer.h"
#include "system/status_codes.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/imu/qmi8658.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/kernel.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/util/misc.h>

#ifdef CONFIG_T5AI_QMI8658_PROFILE
PBL_LOG_MODULE_DEFINE(driver_accel_qmi8658, LOG_LEVEL_DEBUG);
#else
PBL_LOG_MODULE_DEFINE(driver_accel_qmi8658, CONFIG_DRIVER_IMU_LOG_LEVEL);
#endif

#define QMI8658_DEFAULT_INTERVAL_US 8000u
#define QMI8658_ACCEL_SAMPLE_BYTES  6u
#define QMI8658_ACCEL_X_OFFSET      0u
#define QMI8658_ACCEL_Y_OFFSET      2u
#define QMI8658_ACCEL_Z_OFFSET      4u
#define QMI8658_ODR_125HZ           0x06u
#define QMI8658_ODR_62_5HZ          0x07u
#define QMI8658_ODR_31_25HZ         0x08u
#define QMI8658_ODR_LOW_POWER_21HZ  0x0du
#define QMI8658_ODR_LOW_POWER_11HZ  0x0eu
#define QMI8658_ODR_LOW_POWER_3HZ   0x0fu

#define QMI8658_WHO_AM_I 0x00u
#define QMI8658_CTRL1    0x02u
#define QMI8658_CTRL2    0x03u
#define QMI8658_CTRL7    0x08u
#define QMI8658_STATUS0  0x2eu
#define QMI8658_AX_L     0x35u
#define QMI8658_RESET    0x60u

#define QMI8658_ID              0x05u
#define QMI8658_RESET_COMMAND   0xb0u
#define QMI8658_RESET_SETTLE_MS 10u
// Preserve the qualified byte order/auto-increment setting, enable INT2 output.
// Later QMI8658 silicon uses bit 4 for INT2 enable (reserved in early C documentation).
#define QMI8658_CTRL1_INIT        0x70u
#define QMI8658_ACCEL_RANGE_4G    0x10u
#define QMI8658_ACCEL_ENABLE      0x01u
#define QMI8658_ACCEL_DATA_READY  0x01u
#define QMI8658_COUNTS_PER_G      8192
#define QMI8658_STARTUP_SETTLE_MS 100u
#define QMI8658_SHAKE_INTERVAL_US 32000u
#define QMI8658_SHAKE_HOLDOFF_US  500000u
#define QMI8658_SHAKE_MIN_MG      175u
#define QMI8658_SHAKE_MAX_MG      1200u

typedef struct {
  uint32_t interval_us;
  uint8_t odr;
} QMI8658Rate;

// Poll intervals approximate the sensor ODRs; preserve the existing rounded periods.
static const QMI8658Rate s_rates[] = {
  {QMI8658_DEFAULT_INTERVAL_US, QMI8658_ODR_125HZ},
  {16000u, QMI8658_ODR_62_5HZ},
  {32000u, QMI8658_ODR_31_25HZ},
  {48000u, QMI8658_ODR_LOW_POWER_21HZ},
  {91000u, QMI8658_ODR_LOW_POWER_11HZ},
  {333000u, QMI8658_ODR_LOW_POWER_3HZ},
};

#define QMI8658_CTRL8            0x09u
#define QMI8658_CTRL9            0x0au
#define QMI8658_FIFO_WTM         0x13u
#define QMI8658_FIFO_CTRL        0x14u
#define QMI8658_FIFO_COUNT       0x15u
#define QMI8658_FIFO_DATA        0x17u
#define QMI8658_STATUSINT        0x2du
#define QMI8658_TIMESTAMP        0x30u
#define QMI8658_CMD_RESET_FIFO   0x04u
#define QMI8658_CMD_REQUEST_FIFO 0x05u
#define QMI8658_CMD_DONE         0x80u
#define QMI8658_FIFO_STREAM      0x02u // 16 XYZ samples, stream mode
#define QMI8658_FIFO_OVERFLOW    0x20u
// At 100 kHz, six samples take 3.51 ms on the wire. Leave headroom for
// command/count/resume transfers within the shortest 8 ms sensor period.
#define QMI8658_BATCH_MAX     6u
#define QMI8658_WATERMARK_MAX 4u
#define QMI8658_CMD_ATTEMPTS  5u

#ifdef QMI8658_FIFO_SELF_TEST
#endif
// Serialize whole FIFO command/read/resume transactions against configuration.

static const struct pbl_qmi8658 *s_dev;
static bool prv_queue_work(void (*work)(void));
#include "qmi8658_profile.h"

// These helpers require s_dev->state->io_lock. The bus mutex still serializes other devices.
static bool prv_read(uint8_t reg, uint8_t *data, size_t length) {
  pbl_i2c_use((&s_dev->i2c));
  const uint32_t started = qmi_profile_io_start();
  const bool result = (pbl_i2c_read_register_block((&s_dev->i2c), reg, length, data) == 0);
  qmi_profile_io_end(started, length, result);
  pbl_i2c_release((&s_dev->i2c));
  return result;
}

static bool prv_write(uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  pbl_i2c_use((&s_dev->i2c));
  const uint32_t started = qmi_profile_io_start();
  const bool result = (pbl_i2c_write_block((&s_dev->i2c), sizeof(data), data) == 0);
  qmi_profile_io_end(started, sizeof(data), result);
  pbl_i2c_release((&s_dev->i2c));
  return result;
}

static uint64_t prv_time_us(void) {
  time_t seconds;
  uint16_t milliseconds;
  rtc_get_time_ms(&seconds, &milliseconds);
  return ((uint64_t)seconds * 1000u + milliseconds) * 1000u;
}

static int16_t prv_to_mg(const uint8_t *raw) {
  const int16_t value = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
  return (int16_t)(((int32_t)value * 1000) / QMI8658_COUNTS_PER_G);
}

static void prv_decode(const uint8_t *raw, bool rotated, AccelDriverSample *sample) {
  sample->x = prv_to_mg(raw + QMI8658_ACCEL_Y_OFFSET);
  sample->y = -prv_to_mg(raw + QMI8658_ACCEL_X_OFFSET);
  sample->z = prv_to_mg(raw + QMI8658_ACCEL_Z_OFFSET);
  if (rotated) {
    sample->x = -sample->x;
    sample->y = -sample->y;
  }
}

// Remove gravity with a low-pass baseline before comparing motion on each axis.
static void prv_detect_shake(const AccelDriverSample *sample) {
  if (!s_dev->state->shake_enabled) {
    return;
  }
  const int32_t values[] = {sample->x, sample->y, sample->z};
  const uint64_t now = sample->timestamp_us;
  if (!s_dev->state->shake_baseline_valid || now < s_dev->state->shake_previous_us ||
      now - s_dev->state->shake_previous_us > QMI8658_SHAKE_HOLDOFF_US) {
    for (size_t i = 0; i < 3; ++i) {
      s_dev->state->shake_baseline[i] = values[i];
    }
    s_dev->state->shake_baseline_valid = true;
    s_dev->state->shake_holdoff_us = now;
    s_dev->state->shake_previous_us = now;
    return;
  }
  s_dev->state->shake_previous_us = now;
  const uint32_t threshold =
      s_dev->state->shake_sensitivity_high
          ? QMI8658_SHAKE_MIN_MG
          : QMI8658_SHAKE_MAX_MG - s_dev->state->shake_sensitivity_percent *
                                       (QMI8658_SHAKE_MAX_MG - QMI8658_SHAKE_MIN_MG) / 100u;
  int32_t peak = 0;
  size_t axis = 0;
  for (size_t i = 0; i < 3; ++i) {
    const int32_t delta = values[i] - s_dev->state->shake_baseline[i];
    if ((delta < 0 ? -delta : delta) > (peak < 0 ? -peak : peak)) {
      peak = delta;
      axis = i;
    }
    s_dev->state->shake_baseline[i] += delta / 8;
  }
  if (now >= s_dev->state->shake_holdoff_us && (uint32_t)(peak < 0 ? -peak : peak) >= threshold) {
    s_dev->state->shake_holdoff_us = now + QMI8658_SHAKE_HOLDOFF_US;
    pbl_accel_report_event(&s_dev->accel, &(const struct pbl_accel_event){
                                            .type = PBL_ACCEL_EVENT_SHAKE,
                                            .axis = (IMUCoordinateAxis)axis,
                                            .direction = peak < 0 ? -1 : 1
                                          });
  }
}

#ifdef QMI8658_FIFO_SELF_TEST
static void prv_self_test_start(void);

#endif

static void prv_deliver_sample(void);
static void prv_timer_callback(void *context);

static bool prv_wait_command(bool done, unsigned attempts, bool sleep) {
  for (unsigned i = 0; i < attempts; ++i) {
    uint8_t status;
    if (!prv_read(QMI8658_STATUSINT, &status, 1))
      return false;
    if (((status & QMI8658_CMD_DONE) != 0u) == done)
      return true;
    // Each read already sleeps on I2C completion. Only slow configuration
    // handshakes need an additional delay between attempts.
    if (sleep)
      pbl_sleep_ms(1);
  }
  return false;
}

static bool prv_command_begin(uint8_t command) {
  const bool slow = command == QMI8658_CMD_RESET_FIFO || s_dev->state->interval_us >= 48000u;
  unsigned attempts = slow ? 50u : s_dev->state->interval_us / 1000u;
  if (attempts < QMI8658_CMD_ATTEMPTS)
    attempts = QMI8658_CMD_ATTEMPTS;
  if (attempts > 50u)
    attempts = 50u;
  const uint32_t prepared = qmi_profile_io_start();
  const bool ready = s_dev->state->command_idle ||
                     (prv_write(QMI8658_CTRL9, 0u) && prv_wait_command(false, 50u, true));
  qmi_profile_fifo_phase(0u, prepared);
  if (!ready)
    return false;
  s_dev->state->command_idle = false;
  if (!prv_write(QMI8658_CTRL9, command))
    return false;
  const bool done = prv_wait_command(true, attempts, slow);
  const bool ack = prv_write(QMI8658_CTRL9, 0u);
  return done && ack;
}

static bool prv_command_finish(void) {
  s_dev->state->command_idle = prv_wait_command(false, 50u, false);
  return s_dev->state->command_idle;
}

static bool prv_command(uint8_t command) {
  const bool done = prv_command_begin(command);
  const bool idle = prv_command_finish();
  return done && idle;
}

static void prv_mask_irq(void) {
  (void)pbl_intc_disable(&s_dev->int2);
  s_dev->state->irq_armed = false;
}

static void prv_arm_irq(void) {
  // A pending edge is cleared by the HAL on enable; finish_poll checks the
  // level to catch a watermark that rose while delivery owned the raw slot.
  if (s_dev->state->active && !s_dev->state->recovery_required && !s_dev->state->irq_armed) {
    s_dev->state->irq_armed = true;
    (void)pbl_intc_enable(&s_dev->int2);
  }
}

static void prv_finish_poll(void) {
  pbl_irq_lock();
  s_dev->state->poll_pending = false;
  const uint32_t elapsed_ms = (uint32_t)rtc_get_ticks() - s_dev->state->last_acquire_ms;
  // Watermark deassertion is asynchronous. A high pin immediately after
  // resume is not evidence of a fresh batch; allow one conversion to settle.
  const bool catchup = s_dev->state->active && !s_dev->state->recovery_required &&
                       s_dev->state->watermark > 1u &&
                       elapsed_ms >= (s_dev->state->interval_us + 999u) / 1000u &&
                       s_dev->state->catchup_available && (pbl_gpio_get_raw(&s_dev->int2_gpio) > 0);
  if (catchup) {
    // At most one immediate catchup per real edge/watchdog pass. A stuck-high
    // pad cannot create an unbounded acquisition/delivery loop.
    s_dev->state->catchup_available = false;
    s_dev->state->watermark_seen = false;
    s_dev->state->irq_started = 0u;
    s_dev->state->poll_pending = true;
    prv_mask_irq();
    qmi_profile_catchup();
  } else {
    prv_arm_irq();
  }
  pbl_irq_unlock();
  if (catchup)
    pbl_sem_give(&s_dev->state->poll_signal);
}

static void prv_schedule(void) {
  pbl_irq_lock();
  const bool schedule = s_dev->state->initialized &&
                        (s_dev->state->active || s_dev->state->recovery_required) &&
                        !s_dev->state->poll_pending;
  if (schedule) {
    s_dev->state->poll_pending = true;
    prv_mask_irq();
  }
  pbl_irq_unlock();
  if (schedule) {
    pbl_sem_give(&s_dev->state->poll_signal);
  }
}

static void prv_int2_handler(void *arg) {
  qmi_profile_irq();
  s_dev->state->irq_started = qmi_profile_io_start();
  s_dev->state->watermark_seen = s_dev->state->watermark > 1u;
  s_dev->state->catchup_available = true;
  s_dev->state->watchdog_passes = 0u;
  prv_schedule();
}

static void prv_timer_callback(void *context) {
  (void)context;
  qmi_profile_watchdog();
  // No bus work on NewTimers. A persistent high pad generates at most one
  // worker pass per watchdog period, including after transfer failures.
  const uint32_t deadline_ms =
      (2u * s_dev->state->watermark * s_dev->state->interval_us + 999u) / 1000u;
  if ((uint32_t)rtc_get_ticks() - s_dev->state->last_acquire_ms >= deadline_ms ||
      s_dev->state->recovery_required) {
    if (++s_dev->state->watchdog_passes >= 3u) {
      s_dev->state->recovery_required = true;
      s_dev->state->watchdog_passes = 0u;
    }
    s_dev->state->catchup_available = true;
    prv_schedule();
  }
}

static uint8_t prv_watermark(uint32_t count) {
  // Software shake must see every sample without added batching latency.
  if (s_dev->state->shake_enabled || count < 2u) {
    return 1u;
  }
  return count > QMI8658_WATERMARK_MAX ? QMI8658_WATERMARK_MAX : count;
}

static bool prv_configure_locked(void) {
  prv_mask_irq();
  s_dev->state->watermark_seen = false;
  s_dev->state->irq_started = 0u;
  ++s_dev->state->generation;
  s_dev->state->counter_valid = false;
  s_dev->state->startup_us = prv_time_us();
  s_dev->state->last_sample_valid = false;
  s_dev->state->shake_baseline_valid = false;
  const bool active = s_dev->state->num_samples != 0u || s_dev->state->shake_enabled;
  const uint8_t watermark = prv_watermark(s_dev->state->num_samples);
  bool ok = prv_write(QMI8658_CTRL7, 0u) && prv_write(QMI8658_FIFO_CTRL, 0u);
  if (ok && watermark > 1u) {
    // Low-power ODRs slow command processing too. Reset at the normal clock,
    // then select the requested ODR; never silently retain a faster rate.
    ok = prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_125HZ) &&
         prv_command(QMI8658_CMD_RESET_FIFO) && prv_write(QMI8658_FIFO_WTM, watermark) &&
         prv_write(QMI8658_FIFO_CTRL, QMI8658_FIFO_STREAM);
  }
  ok = ok && prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | s_dev->state->odr);
  if (ok && active) {
    ok = prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE);
  }
  s_dev->state->watermark = watermark;
  s_dev->state->active = active;
  s_dev->state->recovery_required = !ok;
  if (!ok)
    PBL_LOG_WRN("QMI8658 configuration failed interval=%lu batch=%lu",
                (unsigned long)s_dev->state->interval_us, (unsigned long)s_dev->state->num_samples);
  if (ok && active && !s_dev->state->poll_pending) {
    prv_arm_irq();
  }
  return ok;
}

static void prv_update_timer(void) {
  if (s_dev->state->active || s_dev->state->recovery_required) {
    uint32_t period_ms = (2u * s_dev->state->watermark * s_dev->state->interval_us + 999u) / 1000u;
    if (period_ms < 100u) {
      period_ms = 100u;
    }
    (void)new_timer_start(s_dev->state->timer, period_ms, prv_timer_callback, NULL,
                          TIMER_START_FLAG_REPEATING);
  } else {
    (void)new_timer_stop(s_dev->state->timer);
  }
}

// Called only under the accelerometer device lock (offload callback or public API). No I2C.
static void prv_process_pending(void) {
  if (!s_dev->state->active || s_dev->state->sample_generation != s_dev->state->generation ||
      !s_dev->state->pending_count) {
    s_dev->state->pending_count = 0u;
    return;
  }
  const uint64_t latest =
      s_dev->state->pending_first_us +
      (uint64_t)(s_dev->state->pending_count - 1u) * s_dev->state->pending_interval_us;
  if (latest < s_dev->state->startup_us) {
    s_dev->state->startup_us = latest;
    s_dev->state->shake_baseline_valid = false;
  }
  unsigned first = 0u;
  for (unsigned i = 0; i < s_dev->state->pending_count; ++i) {
    AccelDriverSample sample = {
      .timestamp_us =
          s_dev->state->pending_first_us + (uint64_t)i * s_dev->state->pending_interval_us,
    };
    if (sample.timestamp_us < s_dev->state->startup_us ||
        sample.timestamp_us - s_dev->state->startup_us < QMI8658_STARTUP_SETTLE_MS * 1000u) {
      first = i + 1u;
      continue;
    }
    prv_decode(s_dev->state->raw + i * QMI8658_ACCEL_SAMPLE_BYTES, s_dev->state->pending_rotated,
               &sample);
    s_dev->state->last_sample = sample;
    s_dev->state->last_sample_valid = true;
    prv_detect_shake(&sample);
  }
  const unsigned count = s_dev->state->pending_count - first;
  if (count && s_dev->state->num_samples) {
    const int8_t rotate = s_dev->state->pending_rotated ? -1 : 1;
    const AccelRawBatch batch = {
      .data = s_dev->state->raw + first * QMI8658_ACCEL_SAMPLE_BYTES,
      .num_samples = count,
      .stride = QMI8658_ACCEL_SAMPLE_BYTES,
      .axis =
          {{.offset = QMI8658_ACCEL_Y_OFFSET, .sign = rotate},
           {.offset = QMI8658_ACCEL_X_OFFSET, .sign = -rotate},
           {.offset = QMI8658_ACCEL_Z_OFFSET, .sign = 1}},
      .scale_num = 1000,
      .scale_den = QMI8658_COUNTS_PER_G,
      .first_timestamp_us =
          s_dev->state->pending_first_us + (uint64_t)first * s_dev->state->pending_interval_us,
      .sampling_interval_us = s_dev->state->pending_interval_us,
    };
    pbl_accel_report_samples(&s_dev->accel, &batch);
  }
  qmi_profile_delivery(count, prv_time_us(), s_dev->state->pending_first_us);
  s_dev->state->pending_count = 0u;
}

static void prv_deliver_sample(void) {
  prv_process_pending();
  prv_finish_poll();
}

static uint32_t prv_counter(const uint8_t *raw) {
  return raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16);
}

static void prv_timestamp(uint32_t counter, unsigned count, uint64_t now) {
  const uint32_t delta = (counter - s_dev->state->last_counter) & 0xffffffu;
  const uint64_t span = (uint64_t)(count - 1u) * s_dev->state->interval_us;
  uint64_t latest = now;
  if (s_dev->state->counter_valid && delta && delta < 0x800000u &&
      now >= s_dev->state->last_counter_us) {
    const uint64_t predicted =
        s_dev->state->last_counter_us + (uint64_t)delta * s_dev->state->interval_us;
    // Re-anchor after clock changes, reset or long drift; never fabricate a
    // future sample. Low-power ODR periods retain the legacy rounded values.
    if (predicted <= now && now - predicted <= s_dev->state->interval_us) {
      latest = predicted;
    }
    qmi_profile_gap(delta > count ? delta - count : 0u);
  }
  s_dev->state->pending_first_us = latest >= span ? latest - span : 0u;
  s_dev->state->last_counter = counter;
  s_dev->state->last_counter_us = latest;
  s_dev->state->counter_valid = true;
}

// One bounded pass, with no callbacks while the hardware lock is held.
static void prv_acquire_sample(void) {
  qmi_profile_acquire_start();
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  bool ok = s_dev->state->initialized && (s_dev->state->active || s_dev->state->recovery_required);
  unsigned count = 0u;
  uint32_t counter = 0u;
  uint64_t now = 0u;
  if (ok && s_dev->state->recovery_required) {
    ok = prv_configure_locked();
    s_dev->state->startup_us = prv_time_us();
    qmi_profile_recovery();
    // Do not read startup data in the recovery pass.
    pbl_mutex_unlock(&s_dev->state->io_lock);
    prv_update_timer();
    qmi_profile_acquire_end(0u);
    prv_finish_poll();
    return;
  }
  if (ok && s_dev->state->watermark == 1u) {
    // STATUS0, STATUS1, 24-bit sample count, temperature, XYZ in one transfer.
    uint8_t data[13];
    ok = prv_read(QMI8658_STATUS0, data, sizeof(data));
    now = prv_time_us();
    if (ok && (data[0] & QMI8658_ACCEL_DATA_READY)) {
      for (unsigned i = 0; i < QMI8658_ACCEL_SAMPLE_BYTES; ++i) {
        s_dev->state->raw[i] = data[7u + i];
      }
      counter = prv_counter(data + 2u);
      count = 1u;
    }
  } else if (ok) {
    uint8_t status[2] = {0};
    bool watermark = s_dev->state->watermark_seen;
    s_dev->state->watermark_seen = false;
#ifdef QMI8658_FIFO_SELF_TEST
    if (s_dev->state->test_precheck)
      watermark = false;
#endif
    // INT2 already establishes watermark. Only watchdog passes need a status
    // precheck; another transfer here consumes the next-conversion budget.
    if (!watermark)
      ok = prv_read(QMI8658_FIFO_COUNT, status, sizeof(status));
    if (ok && (watermark || (status[1] & 0x40u))) {
      now = prv_time_us();
      const uint32_t frozen = qmi_profile_io_start();
      if (s_dev->state->irq_started)
        qmi_profile_irq_latency(s_dev->state->irq_started);
      s_dev->state->irq_started = 0u;
      ok = prv_command_begin(QMI8658_CMD_REQUEST_FIFO);
      qmi_profile_fifo_phase(1u, frozen);
      uint32_t stage = qmi_profile_io_start();
      const bool idle = prv_command_finish();
      qmi_profile_fifo_phase(2u, stage);
      ok = ok && idle;
      stage = qmi_profile_io_start();
      // The count is stable in read mode. Re-read it to include any samples
      // arriving during the command handshake; never read output registers.
      ok = ok && prv_read(QMI8658_FIFO_COUNT, status, sizeof(status));
      qmi_profile_fifo_phase(3u, stage);
      if (ok) {
        // QST Rev A section 8.4: count is in 16-bit words, not bytes.
        const unsigned bytes = 2u * (status[0] | ((status[1] & 3u) << 8));
        count = bytes / QMI8658_ACCEL_SAMPLE_BYTES;
        qmi_profile_fifo_count(count);
        if ((status[1] & QMI8658_FIFO_OVERFLOW) || bytes % QMI8658_ACCEL_SAMPLE_BYTES ||
            count > QMI8658_BATCH_MAX) {
          qmi_profile_drop(count);
          ok = false;
        } else if (count) {
          stage = qmi_profile_io_start();
          ok = prv_read(QMI8658_FIFO_DATA, s_dev->state->raw, bytes);
          qmi_profile_fifo_phase(4u, stage);
        }
      }
      // Always exit read mode, even on NACK, malformed count or overflow.
      stage = qmi_profile_io_start();
      const bool resumed = prv_write(QMI8658_FIFO_CTRL, QMI8658_FIFO_STREAM);
      qmi_profile_fifo_phase(5u, stage);
      qmi_profile_fifo_end(frozen);
      // CTRL9R must complete before consuming data or clearing read mode.
      ok = ok && resumed;
    }
  }
  if (!ok && s_dev->state->active) {
    s_dev->state->recovery_required = true;
    count = 0u;
    s_dev->state->counter_valid = false;
  }
  if (ok && count && s_dev->state->watermark == 1u && s_dev->state->counter_valid &&
      counter == s_dev->state->last_counter) {
    count = 0u;
  }
  if (ok && count) {
    s_dev->state->last_acquire_ms = rtc_get_ticks();
    if (s_dev->state->watermark == 1u) {
      prv_timestamp(counter, count, now);
    } else {
      // Revision 7c leaves TIMESTAMP frozen while FIFO is enabled. Like the
      // other FIFO drivers, anchor the newest sample to the host read time.
      const uint64_t span = (uint64_t)(count - 1u) * s_dev->state->interval_us;
      s_dev->state->pending_first_us = now >= span ? now - span : 0u;
      s_dev->state->counter_valid = false;
    }
    s_dev->state->pending_count = count;
    s_dev->state->pending_interval_us = s_dev->state->interval_us;
    s_dev->state->pending_rotated = s_dev->state->rotated;
    s_dev->state->sample_generation = s_dev->state->generation;
  }
  pbl_mutex_unlock(&s_dev->state->io_lock);
  qmi_profile_acquire_end(count);
  if (!count || !prv_queue_work(prv_deliver_sample)) {
    if (count) {
      qmi_profile_drop(count);
    }
    s_dev->state->pending_count = 0u;
    prv_finish_poll();
  }
}

static void prv_poll_entry(void *context) {
  (void)context;
  while (true) {
    if (pbl_sem_take(&s_dev->state->poll_signal, PBL_FOREVER) == 0) {
      prv_acquire_sample();
    }
  }
}

static int prv_init(const struct pbl_accel *accel) {
  if (s_dev && &s_dev->accel != accel)
    return -ENOTSUP;
  s_dev = container_of(accel, const struct pbl_qmi8658, accel);
  s_dev->state->watermark = 1;
  s_dev->state->odr = QMI8658_ODR_125HZ;
  s_dev->state->initialized = false;
  s_dev->state->timer = TIMER_INVALID_ID;
  s_dev->state->interval_us = s_dev->state->requested_interval_us = QMI8658_DEFAULT_INTERVAL_US;
  s_dev->state->shake_sensitivity_percent = 50;
  pbl_mutex_init(&s_dev->state->io_lock);
  pbl_sem_init(&s_dev->state->poll_signal, 0, 1);
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
#ifdef CONFIG_T5AI_ACCEL_DISABLED
  const bool disabled = prv_write(QMI8658_CTRL7, 0u);
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return disabled ? 0 : -EIO;
#endif
  bool ok = prv_write(QMI8658_RESET, QMI8658_RESET_COMMAND);
  pbl_sleep_ms(QMI8658_RESET_SETTLE_MS);
  uint8_t id[2] = {0};
  ok = ok && prv_read(QMI8658_WHO_AM_I, id, 1u) && prv_read(QMI8658_WHO_AM_I + 1u, id + 1u, 1u) &&
       id[0] == QMI8658_ID;
  ok = ok && prv_write(QMI8658_CTRL1, QMI8658_CTRL1_INIT) && prv_write(QMI8658_CTRL8, 0x80u) &&
       prv_write(QMI8658_CTRL7, 0u);
  pbl_mutex_unlock(&s_dev->state->io_lock);
  if (!ok) {
    PBL_LOG_ERR("QMI8658 initialization failed id=%02x rev=%02x", id[0], id[1]);
    return -EIO;
  }
  PBL_LOG_DBG("QMI8658 interrupt acquisition id=%02x rev=%02x", id[0], id[1]);
  s_dev->state->timer = new_timer_create();
  if (s_dev->state->timer == TIMER_INVALID_ID) {
    return -ENOMEM;
  }
  int ret = pbl_gpio_configure(&s_dev->int2_gpio, PBL_GPIO_INPUT);
  if (ret == 0)
    ret = pbl_intc_request(&s_dev->int2, prv_int2_handler, (void *)s_dev);
  if (ret != 0) {
    new_timer_delete(s_dev->state->timer);
    return ret;
  }
  const struct pbl_thread_attr attr = {
    .name = "QMI8658",
    .entry = prv_poll_entry,
    .prio = PBL_PRIO_MAX - 1,
    .privileged = true,
    .stack = s_dev->state->poll_stack,
    .stack_size = sizeof(s_dev->state->poll_stack),
  };
  if (pbl_thread_create(&s_dev->state->poll_thread, &attr) != 0) {
    new_timer_delete(s_dev->state->timer);
    (void)pbl_intc_free(&s_dev->int2);
    return -EIO;
  }
  s_dev->state->initialized = true;
#ifdef QMI8658_FIFO_SELF_TEST
  prv_self_test_start();
#endif
  return 0;
}

static int prv_set_rotated(const struct pbl_accel *accel, bool rotated) {
  if (s_dev->state->rotated == rotated) {
    return 0;
  }
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  prv_process_pending();
  const bool previous = s_dev->state->rotated;
  int ret = 0;
  s_dev->state->rotated = rotated;
  if (s_dev->state->initialized) {
    if (!prv_configure_locked()) {
      s_dev->state->rotated = previous;
      (void)prv_configure_locked();
      ret = -EIO;
    }
    s_dev->state->startup_us = prv_time_us();
    prv_update_timer();
  }
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return ret;
}

static int prv_set_sampling_interval(const struct pbl_accel *accel, uint32_t interval_us,
                                     uint32_t *actual_us) {
  int ret = 0;
  const uint32_t requested = interval_us;
  if (s_dev->state->shake_enabled && interval_us > QMI8658_SHAKE_INTERVAL_US) {
    interval_us = QMI8658_SHAKE_INTERVAL_US;
  }
  const QMI8658Rate *rate = &s_rates[0];
  for (size_t i = 1; i < sizeof(s_rates) / sizeof(s_rates[0]); ++i) {
    if (s_rates[i].interval_us <= interval_us) {
      rate = &s_rates[i];
    }
  }
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  if (s_dev->state->initialized && s_dev->state->odr != rate->odr) {
    prv_process_pending();
    const uint32_t old_interval = s_dev->state->interval_us;
    const uint8_t old_odr = s_dev->state->odr;
    s_dev->state->interval_us = rate->interval_us;
    s_dev->state->odr = rate->odr;
    if (!prv_configure_locked()) {
      ret = -EIO;
      s_dev->state->interval_us = old_interval;
      s_dev->state->odr = old_odr;
      (void)prv_configure_locked();
    }
    s_dev->state->startup_us = prv_time_us();
    prv_update_timer();
  }
  if (ret == 0)
    s_dev->state->requested_interval_us = requested;
  pbl_mutex_unlock(&s_dev->state->io_lock);
  *actual_us = s_dev->state->interval_us;
  return ret;
}

static uint32_t prv_get_sampling_interval(const struct pbl_accel *accel) {
  return s_dev->state->interval_us;
}

static int prv_set_num_samples(const struct pbl_accel *accel, uint32_t count) {
  if (count > QMI8658_BATCH_MAX) {
    return -EINVAL;
  }
  if (!s_dev->state->initialized || count == s_dev->state->num_samples) {
    return s_dev->state->initialized ? 0 : -ENODEV;
  }
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  prv_process_pending();
  const bool was_active = s_dev->state->active;
  int ret = 0;
  const uint32_t previous = s_dev->state->num_samples;
  s_dev->state->num_samples = count;
  if (!prv_configure_locked()) {
    ret = -EIO;
    s_dev->state->num_samples = previous;
    (void)prv_configure_locked();
  }
  if (!was_active && s_dev->state->active) {
    s_dev->state->startup_us = prv_time_us();
  }
  prv_update_timer();
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return ret;
}

static uint32_t prv_get_max_num_samples(const struct pbl_accel *accel) {
  return QMI8658_BATCH_MAX;
}

static int prv_peek(const struct pbl_accel *accel, AccelDriverSample *data) {
  if (!data)
    return -EINVAL;
  if (!s_dev->state->initialized)
    return -ENODEV;
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  prv_process_pending();
  if (s_dev->state->active) {
    // Reading output registers in FIFO mode destroys FIFO contents.
    if (!s_dev->state->last_sample_valid) {
      pbl_mutex_unlock(&s_dev->state->io_lock);
      return -EIO;
    }
    *data = s_dev->state->last_sample;
    pbl_mutex_unlock(&s_dev->state->io_lock);
    return 0;
  }
  bool ok = prv_write(QMI8658_FIFO_CTRL, 0u) &&
            prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_125HZ) &&
            prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE);
  if (ok) {
    pbl_sleep_ms(QMI8658_STARTUP_SETTLE_MS);
    uint8_t status, raw[QMI8658_ACCEL_SAMPLE_BYTES];
    ok = prv_read(QMI8658_STATUS0, &status, 1) && (status & QMI8658_ACCEL_DATA_READY) &&
         prv_read(QMI8658_AX_L, raw, sizeof(raw));
    if (ok) {
      prv_decode(raw, s_dev->state->rotated, data);
      data->timestamp_us = prv_time_us();
      s_dev->state->last_sample = *data;
      s_dev->state->last_sample_valid = true;
    }
  }
  const bool disabled = prv_write(QMI8658_CTRL7, 0u) &&
                        prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | s_dev->state->odr);
  if (!disabled) {
    s_dev->state->recovery_required = true;
    prv_update_timer();
  }
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return ok && disabled ? 0 : -EIO;
}

static int prv_set_shake_detection(const struct pbl_accel *accel, bool on) {
  if (!s_dev->state->initialized || on == s_dev->state->shake_enabled) {
    return s_dev->state->initialized ? 0 : -ENODEV;
  }
  int ret = 0;
  const uint32_t requested = s_dev->state->requested_interval_us;
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  prv_process_pending();
  const bool was_active = s_dev->state->active;
  s_dev->state->shake_enabled = on;
  if (!prv_configure_locked()) {
    ret = -EIO;
    s_dev->state->shake_enabled = !on;
    (void)prv_configure_locked();
  }
  if (!was_active && s_dev->state->active) {
    s_dev->state->startup_us = prv_time_us();
  }
  prv_update_timer();
  pbl_mutex_unlock(&s_dev->state->io_lock);
  uint32_t actual;
  const int interval_ret = prv_set_sampling_interval(accel, requested, &actual);
  if (ret == 0 && interval_ret != 0) {
    // The class publishes the detection state only after the entire operation succeeds.
    (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
    s_dev->state->shake_enabled = !on;
    (void)prv_configure_locked();
    prv_update_timer();
    pbl_mutex_unlock(&s_dev->state->io_lock);
  }
  return ret != 0 ? ret : interval_ret;
}

static int prv_set_shake_sensitivity_high(const struct pbl_accel *accel, bool sensitivity_high) {
  s_dev->state->shake_sensitivity_high = sensitivity_high;
  return 0;
}

static int prv_set_shake_sensitivity_percent(const struct pbl_accel *accel, uint8_t percent) {
  s_dev->state->shake_sensitivity_percent = percent > 100u ? 100u : percent;
  return 0;
}
#ifdef QMI8658_FIFO_SELF_TEST
static void prv_self_test_schedule(void *context);
static void prv_self_test_step(void) {
  PBL_LOG_DBG("QMITEST phase=%u", s_dev->state->test_phase);
  PBL_LOG_DBG("QMITEST previous valid=%u x=%d y=%d z=%d", s_dev->state->last_sample_valid,
              s_dev->state->last_sample.x, s_dev->state->last_sample.y,
              s_dev->state->last_sample.z);
  switch (s_dev->state->test_phase++) {
    case 0:
      s_dev->state->test_interval = s_dev->state->requested_interval_us;
      s_dev->state->test_count = s_dev->state->num_samples;
      s_dev->state->test_shake = s_dev->state->shake_enabled;
      s_dev->state->test_rotated = s_dev->state->rotated;
      pbl_accel_set_shake_detection(&s_dev->accel, false);
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 8000u, nullptr);
      pbl_accel_set_num_samples(&s_dev->accel, 2u);
      s_dev->state->test_precheck = true;
      break;
    case 1:
      s_dev->state->test_precheck = false;
      break;
    case 2:
      pbl_accel_set_num_samples(&s_dev->accel, 4u);
      break;
    case 3:
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 16000u, nullptr);
      break;
    case 4:
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 32000u, nullptr);
      break;
    case 5:
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 91000u, nullptr);
      pbl_accel_set_num_samples(&s_dev->accel, 2u);
      break;
    case 6:
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 333000u, nullptr);
      break;
    default:
      pbl_accel_set_rotated(&s_dev->accel, s_dev->state->test_rotated);
      pbl_accel_set_shake_detection(&s_dev->accel, s_dev->state->test_shake);
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, s_dev->state->test_interval, nullptr);
      pbl_accel_set_num_samples(&s_dev->accel, s_dev->state->test_count);
      (void)new_timer_stop(s_dev->state->test_timer);
      PBL_LOG_DBG("QMITEST restored");
      s_dev->state->test_pending = false;
      return;
  }
  PBL_LOG_DBG("QMITEST state interval=%lu batch=%lu shake=%u rotated=%u active=%u",
              (unsigned long)s_dev->state->interval_us, (unsigned long)s_dev->state->num_samples,
              s_dev->state->shake_enabled, s_dev->state->rotated, s_dev->state->active);
  (void)new_timer_start(s_dev->state->test_timer, 15000u, prv_self_test_schedule, NULL,
                        TIMER_START_FLAG_REPEATING);
  s_dev->state->test_pending = false;
}
static void prv_self_test_schedule(void *context) {
  (void)context;
  if (!s_dev->state->test_pending) {
    s_dev->state->test_pending = true;
    if (!prv_queue_work(prv_self_test_step)) {
      s_dev->state->test_pending = false;
    }
  }
}
static void prv_self_test_start(void) {
  s_dev->state->test_timer = new_timer_create();
  if (s_dev->state->test_timer != TIMER_INVALID_ID) {
    (void)new_timer_start(s_dev->state->test_timer, 45000u, prv_self_test_schedule, NULL, 0);
  }
}
#endif

static void prv_work_callback(void *data) {
  void (*work)(void) = data;
  pbl_accel_lock(&s_dev->accel);
  work();
  pbl_accel_unlock(&s_dev->accel);
}
static bool prv_queue_work(void (*work)(void)) {
  return new_timer_add_work_callback(prv_work_callback, work);
}
const struct pbl_accel_ops pbl_qmi8658_ops = {
  .init = prv_init,
  .set_rotated = prv_set_rotated,
  .set_sampling_interval = prv_set_sampling_interval,
  .get_sampling_interval = prv_get_sampling_interval,
  .set_num_samples = prv_set_num_samples,
  .get_max_num_samples = prv_get_max_num_samples,
  .peek = prv_peek,
  .set_shake_detection = prv_set_shake_detection,
  .set_shake_sensitivity_high = prv_set_shake_sensitivity_high,
  .set_shake_sensitivity_percent = prv_set_shake_sensitivity_percent,
};
