/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include <pbl/drivers/accel.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/exti.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/kernel.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include "kernel/util/sleep.h"
#include "pbl/services/new_timer/new_timer.h"
#include "system/status_codes.h"
#include <pbl/logging/logging.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

static TimerID s_timer = TIMER_INVALID_ID;
static uint32_t s_interval_us = QMI8658_DEFAULT_INTERVAL_US;
static uint32_t s_requested_interval_us = QMI8658_DEFAULT_INTERVAL_US;
static uint32_t s_num_samples;
static uint8_t s_watermark = 1u;
static uint8_t s_odr = QMI8658_ODR_125HZ;
static bool s_shake_enabled;
static bool s_active;
static bool s_initialized;
static bool s_rotated;
static bool s_shake_sensitivity_high;
static uint8_t s_shake_sensitivity_percent = 50u;
static bool s_shake_baseline_valid;
static int32_t s_shake_baseline[3];
static uint64_t s_shake_previous_us;
static uint64_t s_shake_holdoff_us;
static uint64_t s_startup_us;
static AccelDriverSample s_last_sample;
static bool s_last_sample_valid;
static bool s_poll_pending;
static bool s_irq_armed;
static bool s_catchup_available;
static bool s_watermark_seen;
static uint32_t s_irq_started;
#ifdef QMI8658_FIFO_SELF_TEST
static bool s_test_precheck;
#endif
static uint32_t s_last_acquire_ms;
static unsigned s_watchdog_passes;
static uint32_t s_generation;
static uint32_t s_sample_generation;
static uint8_t s_raw[QMI8658_BATCH_MAX * QMI8658_ACCEL_SAMPLE_BYTES];
static uint8_t s_pending_count;
static uint64_t s_pending_first_us;
static uint32_t s_pending_interval_us;
static bool s_pending_rotated;
static uint32_t s_last_counter;
static uint64_t s_last_counter_us;
static bool s_counter_valid;
static PBL_SEM_DEFINE(s_poll_signal, 0, 1);
// Serialize whole FIFO command/read/resume transactions against configuration.
static PBL_MUTEX_DEFINE(s_io_lock);
static struct pbl_thread s_poll_thread;
PBL_THREAD_STACK_DEFINE(s_poll_stack, 1536);

#include "qmi8658_profile.h"

// These helpers require s_io_lock. The bus mutex still serializes other devices.
static bool prv_read(uint8_t reg, uint8_t *data, size_t length) {
  i2c_use(I2C_QMI8658);
  const uint32_t started = qmi_profile_io_start();
  const bool result = i2c_read_register_block(I2C_QMI8658, reg, length, data);
  qmi_profile_io_end(started, length, result);
  i2c_release(I2C_QMI8658);
  return result;
}

static bool prv_write(uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  i2c_use(I2C_QMI8658);
  const uint32_t started = qmi_profile_io_start();
  const bool result = i2c_write_block(I2C_QMI8658, sizeof(data), data);
  qmi_profile_io_end(started, sizeof(data), result);
  i2c_release(I2C_QMI8658);
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
  if (!s_shake_enabled) {
    return;
  }
  const int32_t values[] = {sample->x, sample->y, sample->z};
  const uint64_t now = sample->timestamp_us;
  if (!s_shake_baseline_valid || now < s_shake_previous_us ||
      now - s_shake_previous_us > QMI8658_SHAKE_HOLDOFF_US) {
    for (size_t i = 0; i < 3; ++i) {
      s_shake_baseline[i] = values[i];
    }
    s_shake_baseline_valid = true;
    s_shake_holdoff_us = now;
    s_shake_previous_us = now;
    return;
  }
  s_shake_previous_us = now;
  const uint32_t threshold =
      s_shake_sensitivity_high
          ? QMI8658_SHAKE_MIN_MG
          : QMI8658_SHAKE_MAX_MG -
                s_shake_sensitivity_percent * (QMI8658_SHAKE_MAX_MG - QMI8658_SHAKE_MIN_MG) / 100u;
  int32_t peak = 0;
  size_t axis = 0;
  for (size_t i = 0; i < 3; ++i) {
    const int32_t delta = values[i] - s_shake_baseline[i];
    if ((delta < 0 ? -delta : delta) > (peak < 0 ? -peak : peak)) {
      peak = delta;
      axis = i;
    }
    s_shake_baseline[i] += delta / 8;
  }
  if (now >= s_shake_holdoff_us && (uint32_t)(peak < 0 ? -peak : peak) >= threshold) {
    s_shake_holdoff_us = now + QMI8658_SHAKE_HOLDOFF_US;
    accel_cb_shake_detected((IMUCoordinateAxis)axis, peak < 0 ? -1 : 1);
  }
}

#ifdef QMI8658_FIFO_SELF_TEST
static void prv_self_test_start(void);

#endif

static void prv_deliver_sample(void);
static void prv_timer_callback(void *context);

static bool s_command_idle;

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
      psleep(1);
  }
  return false;
}

static bool prv_command_begin(uint8_t command) {
  const bool slow = command == QMI8658_CMD_RESET_FIFO || s_interval_us >= 48000u;
  unsigned attempts = slow ? 50u : s_interval_us / 1000u;
  if (attempts < QMI8658_CMD_ATTEMPTS)
    attempts = QMI8658_CMD_ATTEMPTS;
  if (attempts > 50u)
    attempts = 50u;
  const uint32_t prepared = qmi_profile_io_start();
  const bool ready =
      s_command_idle || (prv_write(QMI8658_CTRL9, 0u) && prv_wait_command(false, 50u, true));
  qmi_profile_fifo_phase(0u, prepared);
  if (!ready)
    return false;
  s_command_idle = false;
  if (!prv_write(QMI8658_CTRL9, command))
    return false;
  const bool done = prv_wait_command(true, attempts, slow);
  const bool ack = prv_write(QMI8658_CTRL9, 0u);
  return done && ack;
}

static bool prv_command_finish(void) {
  s_command_idle = prv_wait_command(false, 50u, false);
  return s_command_idle;
}

static bool prv_command(uint8_t command) {
  const bool done = prv_command_begin(command);
  const bool idle = prv_command_finish();
  return done && idle;
}

static bool s_recovery_required;

static void prv_mask_irq(void) {
  exti_disable(BOARD_QMI8658_INT2);
  s_irq_armed = false;
}

static void prv_arm_irq(void) {
  // A pending edge is cleared by the HAL on enable; finish_poll checks the
  // level to catch a watermark that rose while delivery owned the raw slot.
  if (s_active && !s_recovery_required && !s_irq_armed) {
    s_irq_armed = true;
    exti_enable(BOARD_QMI8658_INT2);
  }
}

static void prv_finish_poll(void) {
  pbl_irq_lock();
  s_poll_pending = false;
  const InputConfig input = {.pin = BOARD_QMI8658_INT2.pin};
  const uint32_t elapsed_ms = (uint32_t)rtc_get_ticks() - s_last_acquire_ms;
  // Watermark deassertion is asynchronous. A high pin immediately after
  // resume is not evidence of a fresh batch; allow one conversion to settle.
  const bool catchup = s_active && !s_recovery_required && s_watermark > 1u &&
                       elapsed_ms >= (s_interval_us + 999u) / 1000u && s_catchup_available &&
                       gpio_input_read(&input);
  if (catchup) {
    // At most one immediate catchup per real edge/watchdog pass. A stuck-high
    // pad cannot create an unbounded acquisition/delivery loop.
    s_catchup_available = false;
    s_watermark_seen = false;
    s_irq_started = 0u;
    s_poll_pending = true;
    prv_mask_irq();
    qmi_profile_catchup();
  } else {
    prv_arm_irq();
  }
  pbl_irq_unlock();
  if (catchup)
    pbl_sem_give(&s_poll_signal);
}

static void prv_schedule(void) {
  pbl_irq_lock();
  const bool schedule = s_initialized && (s_active || s_recovery_required) && !s_poll_pending;
  if (schedule) {
    s_poll_pending = true;
    prv_mask_irq();
  }
  pbl_irq_unlock();
  if (schedule) {
    pbl_sem_give(&s_poll_signal);
  }
}

static void prv_int2_handler(void) {
  qmi_profile_irq();
  s_irq_started = qmi_profile_io_start();
  s_watermark_seen = s_watermark > 1u;
  s_catchup_available = true;
  s_watchdog_passes = 0u;
  prv_schedule();
}

static void prv_timer_callback(void *context) {
  (void)context;
  qmi_profile_watchdog();
  // No bus work on NewTimers. A persistent high pad generates at most one
  // worker pass per watchdog period, including after transfer failures.
  const uint32_t deadline_ms = (2u * s_watermark * s_interval_us + 999u) / 1000u;
  if ((uint32_t)rtc_get_ticks() - s_last_acquire_ms >= deadline_ms || s_recovery_required) {
    if (++s_watchdog_passes >= 3u) {
      s_recovery_required = true;
      s_watchdog_passes = 0u;
    }
    s_catchup_available = true;
    prv_schedule();
  }
}

static uint8_t prv_watermark(uint32_t count) {
  // Software shake must see every sample without added batching latency.
  if (s_shake_enabled || count < 2u) {
    return 1u;
  }
  return count > QMI8658_WATERMARK_MAX ? QMI8658_WATERMARK_MAX : count;
}

static bool prv_configure_locked(void) {
  prv_mask_irq();
  s_watermark_seen = false;
  s_irq_started = 0u;
  ++s_generation;
  s_counter_valid = false;
  s_startup_us = prv_time_us();
  s_last_sample_valid = false;
  s_shake_baseline_valid = false;
  const bool active = s_num_samples != 0u || s_shake_enabled;
  const uint8_t watermark = prv_watermark(s_num_samples);
  bool ok = prv_write(QMI8658_CTRL7, 0u) && prv_write(QMI8658_FIFO_CTRL, 0u);
  if (ok && watermark > 1u) {
    // Low-power ODRs slow command processing too. Reset at the normal clock,
    // then select the requested ODR; never silently retain a faster rate.
    ok = prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_125HZ) &&
         prv_command(QMI8658_CMD_RESET_FIFO) && prv_write(QMI8658_FIFO_WTM, watermark) &&
         prv_write(QMI8658_FIFO_CTRL, QMI8658_FIFO_STREAM);
  }
  ok = ok && prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | s_odr);
  if (ok && active) {
    ok = prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE);
  }
  s_watermark = watermark;
  s_active = active;
  s_recovery_required = !ok;
  if (!ok)
    PBL_LOG_WRN("QMI8658 configuration failed interval=%lu batch=%lu", (unsigned long)s_interval_us,
                (unsigned long)s_num_samples);
  if (ok && active && !s_poll_pending) {
    prv_arm_irq();
  }
  return ok;
}

static void prv_update_timer(void) {
  if (s_active || s_recovery_required) {
    uint32_t period_ms = (2u * s_watermark * s_interval_us + 999u) / 1000u;
    if (period_ms < 100u) {
      period_ms = 100u;
    }
    (void)new_timer_start(s_timer, period_ms, prv_timer_callback, NULL, TIMER_START_FLAG_REPEATING);
  } else {
    (void)new_timer_stop(s_timer);
  }
}

// Called only under the service lock (offload callback or public API). No I2C.
static void prv_process_pending(void) {
  if (!s_active || s_sample_generation != s_generation || !s_pending_count) {
    s_pending_count = 0u;
    return;
  }
  const uint64_t latest =
      s_pending_first_us + (uint64_t)(s_pending_count - 1u) * s_pending_interval_us;
  if (latest < s_startup_us) {
    s_startup_us = latest;
    s_shake_baseline_valid = false;
  }
  unsigned first = 0u;
  for (unsigned i = 0; i < s_pending_count; ++i) {
    AccelDriverSample sample = {
      .timestamp_us = s_pending_first_us + (uint64_t)i * s_pending_interval_us,
    };
    if (sample.timestamp_us < s_startup_us ||
        sample.timestamp_us - s_startup_us < QMI8658_STARTUP_SETTLE_MS * 1000u) {
      first = i + 1u;
      continue;
    }
    prv_decode(s_raw + i * QMI8658_ACCEL_SAMPLE_BYTES, s_pending_rotated, &sample);
    s_last_sample = sample;
    s_last_sample_valid = true;
    prv_detect_shake(&sample);
  }
  const unsigned count = s_pending_count - first;
  if (count && s_num_samples) {
    const int8_t rotate = s_pending_rotated ? -1 : 1;
    const AccelRawBatch batch = {
      .data = s_raw + first * QMI8658_ACCEL_SAMPLE_BYTES,
      .num_samples = count,
      .stride = QMI8658_ACCEL_SAMPLE_BYTES,
      .axis =
          {{.offset = QMI8658_ACCEL_Y_OFFSET, .sign = rotate},
           {.offset = QMI8658_ACCEL_X_OFFSET, .sign = -rotate},
           {.offset = QMI8658_ACCEL_Z_OFFSET, .sign = 1}},
      .scale_num = 1000,
      .scale_den = QMI8658_COUNTS_PER_G,
      .first_timestamp_us = s_pending_first_us + (uint64_t)first * s_pending_interval_us,
      .sampling_interval_us = s_pending_interval_us,
    };
    accel_cb_new_samples(&batch);
  }
  qmi_profile_delivery(count, prv_time_us(), s_pending_first_us);
  s_pending_count = 0u;
}

static void prv_deliver_sample(void) {
  prv_process_pending();
  prv_finish_poll();
}

static uint32_t prv_counter(const uint8_t *raw) {
  return raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16);
}

static void prv_timestamp(uint32_t counter, unsigned count, uint64_t now) {
  const uint32_t delta = (counter - s_last_counter) & 0xffffffu;
  const uint64_t span = (uint64_t)(count - 1u) * s_interval_us;
  uint64_t latest = now;
  if (s_counter_valid && delta && delta < 0x800000u && now >= s_last_counter_us) {
    const uint64_t predicted = s_last_counter_us + (uint64_t)delta * s_interval_us;
    // Re-anchor after clock changes, reset or long drift; never fabricate a
    // future sample. Low-power ODR periods retain the legacy rounded values.
    if (predicted <= now && now - predicted <= s_interval_us) {
      latest = predicted;
    }
    qmi_profile_gap(delta > count ? delta - count : 0u);
  }
  s_pending_first_us = latest >= span ? latest - span : 0u;
  s_last_counter = counter;
  s_last_counter_us = latest;
  s_counter_valid = true;
}

// One bounded pass, with no callbacks while the hardware lock is held.
static void prv_acquire_sample(void) {
  qmi_profile_acquire_start();
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
  bool ok = s_initialized && (s_active || s_recovery_required);
  unsigned count = 0u;
  uint32_t counter = 0u;
  uint64_t now = 0u;
  if (ok && s_recovery_required) {
    ok = prv_configure_locked();
    s_startup_us = prv_time_us();
    qmi_profile_recovery();
    // Do not read startup data in the recovery pass.
    pbl_mutex_unlock(&s_io_lock);
    prv_update_timer();
    qmi_profile_acquire_end(0u);
    prv_finish_poll();
    return;
  }
  if (ok && s_watermark == 1u) {
    // STATUS0, STATUS1, 24-bit sample count, temperature, XYZ in one transfer.
    uint8_t data[13];
    ok = prv_read(QMI8658_STATUS0, data, sizeof(data));
    now = prv_time_us();
    if (ok && (data[0] & QMI8658_ACCEL_DATA_READY)) {
      for (unsigned i = 0; i < QMI8658_ACCEL_SAMPLE_BYTES; ++i) {
        s_raw[i] = data[7u + i];
      }
      counter = prv_counter(data + 2u);
      count = 1u;
    }
  } else if (ok) {
    uint8_t status[2] = {0};
    bool watermark = s_watermark_seen;
    s_watermark_seen = false;
#ifdef QMI8658_FIFO_SELF_TEST
    if (s_test_precheck)
      watermark = false;
#endif
    // INT2 already establishes watermark. Only watchdog passes need a status
    // precheck; another transfer here consumes the next-conversion budget.
    if (!watermark)
      ok = prv_read(QMI8658_FIFO_COUNT, status, sizeof(status));
    if (ok && (watermark || (status[1] & 0x40u))) {
      now = prv_time_us();
      const uint32_t frozen = qmi_profile_io_start();
      if (s_irq_started)
        qmi_profile_irq_latency(s_irq_started);
      s_irq_started = 0u;
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
          ok = prv_read(QMI8658_FIFO_DATA, s_raw, bytes);
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
  if (!ok && s_active) {
    s_recovery_required = true;
    count = 0u;
    s_counter_valid = false;
  }
  if (ok && count && s_watermark == 1u && s_counter_valid && counter == s_last_counter) {
    count = 0u;
  }
  if (ok && count) {
    s_last_acquire_ms = rtc_get_ticks();
    if (s_watermark == 1u) {
      prv_timestamp(counter, count, now);
    } else {
      // Revision 7c leaves TIMESTAMP frozen while FIFO is enabled. Like the
      // other FIFO drivers, anchor the newest sample to the host read time.
      const uint64_t span = (uint64_t)(count - 1u) * s_interval_us;
      s_pending_first_us = now >= span ? now - span : 0u;
      s_counter_valid = false;
    }
    s_pending_count = count;
    s_pending_interval_us = s_interval_us;
    s_pending_rotated = s_rotated;
    s_sample_generation = s_generation;
  }
  pbl_mutex_unlock(&s_io_lock);
  qmi_profile_acquire_end(count);
  if (!count || !accel_offload_work(prv_deliver_sample)) {
    if (count) {
      qmi_profile_drop(count);
    }
    s_pending_count = 0u;
    prv_finish_poll();
  }
}

static void prv_poll_entry(void *context) {
  (void)context;
  while (true) {
    if (pbl_sem_take(&s_poll_signal, PBL_FOREVER) == 0) {
      prv_acquire_sample();
    }
  }
}

void accel_init(void) {
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
#ifdef CONFIG_T5AI_ACCEL_DISABLED
  (void)prv_write(QMI8658_CTRL7, 0u);
  pbl_mutex_unlock(&s_io_lock);
  return;
#endif
  bool ok = prv_write(QMI8658_RESET, QMI8658_RESET_COMMAND);
  psleep(QMI8658_RESET_SETTLE_MS);
  uint8_t id[2] = {0};
  ok = ok && prv_read(QMI8658_WHO_AM_I, id, 1u) && prv_read(QMI8658_WHO_AM_I + 1u, id + 1u, 1u) &&
       id[0] == QMI8658_ID;
  ok = ok && prv_write(QMI8658_CTRL1, QMI8658_CTRL1_INIT) && prv_write(QMI8658_CTRL8, 0x80u) &&
       prv_write(QMI8658_CTRL7, 0u);
  pbl_mutex_unlock(&s_io_lock);
  if (!ok) {
    PBL_LOG_ERR("QMI8658 initialization failed id=%02x rev=%02x", id[0], id[1]);
    return;
  }
  PBL_LOG_DBG("QMI8658 interrupt acquisition id=%02x rev=%02x", id[0], id[1]);
  s_timer = new_timer_create();
  if (s_timer == TIMER_INVALID_ID) {
    return;
  }
  exti_configure_pin(BOARD_QMI8658_INT2, ExtiTrigger_Rising, prv_int2_handler);
  const struct pbl_thread_attr attr = {
    .name = "QMI8658",
    .entry = prv_poll_entry,
    .prio = PBL_PRIO_MAX - 1,
    .privileged = true,
    .stack = s_poll_stack,
    .stack_size = sizeof(s_poll_stack),
  };
  if (pbl_thread_create(&s_poll_thread, &attr) != 0) {
    return;
  }
  s_initialized = true;
#ifdef QMI8658_FIFO_SELF_TEST
  prv_self_test_start();
#endif
}

void accel_set_rotated(bool rotated) {
  if (s_rotated == rotated) {
    return;
  }
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
  prv_process_pending();
  s_rotated = rotated;
  if (s_initialized) {
    (void)prv_configure_locked();
    s_startup_us = prv_time_us();
    prv_update_timer();
  }
  pbl_mutex_unlock(&s_io_lock);
}

uint32_t accel_set_sampling_interval(uint32_t interval_us) {
  const uint32_t requested = interval_us;
  if (s_shake_enabled && interval_us > QMI8658_SHAKE_INTERVAL_US) {
    interval_us = QMI8658_SHAKE_INTERVAL_US;
  }
  const QMI8658Rate *rate = &s_rates[0];
  for (size_t i = 1; i < sizeof(s_rates) / sizeof(s_rates[0]); ++i) {
    if (s_rates[i].interval_us <= interval_us) {
      rate = &s_rates[i];
    }
  }
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
  if (s_initialized && s_odr != rate->odr) {
    prv_process_pending();
    const uint32_t old_interval = s_interval_us;
    const uint8_t old_odr = s_odr;
    s_interval_us = rate->interval_us;
    s_odr = rate->odr;
    if (!prv_configure_locked()) {
      s_interval_us = old_interval;
      s_odr = old_odr;
      (void)prv_configure_locked();
    }
    s_startup_us = prv_time_us();
    prv_update_timer();
  }
  s_requested_interval_us = requested;
  pbl_mutex_unlock(&s_io_lock);
  return s_interval_us;
}

uint32_t accel_get_sampling_interval(void) {
  return s_interval_us;
}

void accel_set_num_samples(uint32_t count) {
  if (count > QMI8658_BATCH_MAX) {
    count = QMI8658_BATCH_MAX;
  }
  if (!s_initialized || count == s_num_samples) {
    return;
  }
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
  prv_process_pending();
  const bool was_active = s_active;
  const uint32_t previous = s_num_samples;
  s_num_samples = count;
  if (!prv_configure_locked()) {
    s_num_samples = previous;
    (void)prv_configure_locked();
  }
  if (!was_active && s_active) {
    s_startup_us = prv_time_us();
  }
  prv_update_timer();
  pbl_mutex_unlock(&s_io_lock);
}

uint32_t accel_get_max_num_samples(void) {
  return QMI8658_BATCH_MAX;
}

int accel_peek(AccelDriverSample *data) {
  if (!s_initialized || !data) {
    return E_ERROR;
  }
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
  prv_process_pending();
  if (s_active) {
    // Reading output registers in FIFO mode destroys FIFO contents.
    if (!s_last_sample_valid) {
      pbl_mutex_unlock(&s_io_lock);
      return E_ERROR;
    }
    *data = s_last_sample;
    pbl_mutex_unlock(&s_io_lock);
    return S_SUCCESS;
  }
  bool ok = prv_write(QMI8658_FIFO_CTRL, 0u) &&
            prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_125HZ) &&
            prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE);
  if (ok) {
    psleep(QMI8658_STARTUP_SETTLE_MS);
    uint8_t status, raw[QMI8658_ACCEL_SAMPLE_BYTES];
    ok = prv_read(QMI8658_STATUS0, &status, 1) && (status & QMI8658_ACCEL_DATA_READY) &&
         prv_read(QMI8658_AX_L, raw, sizeof(raw));
    if (ok) {
      prv_decode(raw, s_rotated, data);
      data->timestamp_us = prv_time_us();
      s_last_sample = *data;
      s_last_sample_valid = true;
    }
  }
  const bool disabled =
      prv_write(QMI8658_CTRL7, 0u) && prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | s_odr);
  if (!disabled) {
    s_recovery_required = true;
    prv_update_timer();
  }
  pbl_mutex_unlock(&s_io_lock);
  return ok && disabled ? S_SUCCESS : E_ERROR;
}

void accel_enable_shake_detection(bool on) {
  if (!s_initialized || on == s_shake_enabled) {
    return;
  }
  const uint32_t requested = s_requested_interval_us;
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
  prv_process_pending();
  const bool was_active = s_active;
  s_shake_enabled = on;
  if (!prv_configure_locked()) {
    s_shake_enabled = !on;
    (void)prv_configure_locked();
  }
  if (!was_active && s_active) {
    s_startup_us = prv_time_us();
  }
  prv_update_timer();
  pbl_mutex_unlock(&s_io_lock);
  (void)accel_set_sampling_interval(requested);
}
bool accel_get_shake_detection_enabled(void) {
  return s_shake_enabled;
}

void accel_enable_double_tap_detection(bool on) {
  (void)on;
}

bool accel_get_double_tap_detection_enabled(void) {
  return false;
}

void accel_set_shake_sensitivity_high(bool sensitivity_high) {
  s_shake_sensitivity_high = sensitivity_high;
}

void accel_set_shake_sensitivity_percent(uint8_t percent) {
  s_shake_sensitivity_percent = percent > 100u ? 100u : percent;
}
#ifdef QMI8658_FIFO_SELF_TEST
static TimerID s_test_timer;
static unsigned s_test_phase;
static uint32_t s_test_interval, s_test_count;
static bool s_test_shake, s_test_rotated, s_test_pending;
static void prv_self_test_schedule(void *context);
static void prv_self_test_step(void) {
  PBL_LOG_DBG("QMITEST phase=%u", s_test_phase);
  PBL_LOG_DBG("QMITEST previous valid=%u x=%d y=%d z=%d", s_last_sample_valid, s_last_sample.x,
              s_last_sample.y, s_last_sample.z);
  switch (s_test_phase++) {
    case 0:
      s_test_interval = s_requested_interval_us;
      s_test_count = s_num_samples;
      s_test_shake = s_shake_enabled;
      s_test_rotated = s_rotated;
      accel_enable_shake_detection(false);
      (void)accel_set_sampling_interval(8000u);
      accel_set_num_samples(2u);
      s_test_precheck = true;
      break;
    case 1:
      s_test_precheck = false;
      break;
    case 2:
      accel_set_num_samples(4u);
      break;
    case 3:
      (void)accel_set_sampling_interval(16000u);
      break;
    case 4:
      (void)accel_set_sampling_interval(32000u);
      break;
    case 5:
      (void)accel_set_sampling_interval(91000u);
      accel_set_num_samples(2u);
      break;
    case 6:
      (void)accel_set_sampling_interval(333000u);
      break;
    default:
      accel_set_rotated(s_test_rotated);
      accel_enable_shake_detection(s_test_shake);
      (void)accel_set_sampling_interval(s_test_interval);
      accel_set_num_samples(s_test_count);
      (void)new_timer_stop(s_test_timer);
      PBL_LOG_DBG("QMITEST restored");
      s_test_pending = false;
      return;
  }
  PBL_LOG_DBG("QMITEST state interval=%lu batch=%lu shake=%u rotated=%u active=%u",
              (unsigned long)s_interval_us, (unsigned long)s_num_samples, s_shake_enabled,
              s_rotated, s_active);
  (void)new_timer_start(s_test_timer, 15000u, prv_self_test_schedule, NULL,
                        TIMER_START_FLAG_REPEATING);
  s_test_pending = false;
}
static void prv_self_test_schedule(void *context) {
  (void)context;
  if (!s_test_pending) {
    s_test_pending = true;
    if (!accel_offload_work(prv_self_test_step)) {
      s_test_pending = false;
    }
  }
}
static void prv_self_test_start(void) {
  s_test_timer = new_timer_create();
  if (s_test_timer != TIMER_INVALID_ID) {
    (void)new_timer_start(s_test_timer, 45000u, prv_self_test_schedule, NULL, 0);
  }
}
#endif
