/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system/status_codes.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

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
#include <pbl/kernel/time.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/devices.h>
#include <pbl/soc/t5ai/rtc.h>
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
// Later QMI8658 silicon uses bit 4 for INT2 enable (reserved in early C
// documentation).
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
#define QMI8658_WOM_QUIET_MS      5000u
#define QMI8658_WOM_HEALTH_MS     5000u
#define QMI8658_WOM_THRESHOLD_MG  32u
#define QMI8658_CAL1_L            0x0bu
#define QMI8658_CAL1_H            0x0cu
#define QMI8658_STATUS1           0x2fu
#define QMI8658_WOM_EVENT         0x04u
#define QMI8658_CMD_WRITE_WOM     0x08u
#define QMI8658_CMD_CONFIG_MOTION 0x0eu
#define QMI8658_CAL4_H            0x12u
#define QMI8658_ANY_MOTION_EVENT  0x20u
#define QMI8658_CTRL8_HANDSHAKE   0x80u
#define QMI8658_CTRL8_ANY_MOTION  0x02u
#define QMI8658_DRDY_DISABLE      0x20u
// Asterix/Obelix LSM6DSO reference (pebble/boards/{asterix,obelix}.conf).
#define QMI8658_ST_THS_MIN         1u
#define QMI8658_ST_THS_MAX         40u
#define QMI8658_ST_THS_DEFAULT     16u
#define QMI8658_ST_INTERVAL_US     80000u
#define QMI8658_ST_WAKE_DURATION   1u
#define QMI8658_MOTION_INTERVAL_US 48000u
// Re-arm after five absent motion pulses; no additional timer or sampling.
#define QMI8658_MOTION_REARM_MS 240u
// (4 g / 64) / (1 g / 32), times 2 for the ST half-difference filter.
#define QMI8658_ST_SLOPE_SCALE 4u
// 56.25 mg/code: the accepted fallback tuning is 90% of ST's 62.5 mg/code.
#define QMI8658_FALLBACK_MG_NUMERATOR   225u
#define QMI8658_FALLBACK_MG_DENOMINATOR 4u

typedef struct {
  uint32_t interval_us;
  uint8_t odr;
} QMI8658Rate;

// Poll intervals approximate the sensor ODRs; preserve the existing rounded
// periods.
static const QMI8658Rate s_rates[] = {
  {QMI8658_DEFAULT_INTERVAL_US, QMI8658_ODR_125HZ},
  {16000u, QMI8658_ODR_62_5HZ},
  {32000u, QMI8658_ODR_31_25HZ},
  {48000u, QMI8658_ODR_LOW_POWER_21HZ},
  {91000u, QMI8658_ODR_LOW_POWER_11HZ},
  {333000u, QMI8658_ODR_LOW_POWER_3HZ},
};

static const QMI8658Rate *prv_select_rate(uint32_t requested_us, bool shake) {
  if (shake && requested_us > QMI8658_SHAKE_INTERVAL_US)
    requested_us = QMI8658_SHAKE_INTERVAL_US;
  const QMI8658Rate *rate = &s_rates[0];
  for (size_t i = 1; i < sizeof(s_rates) / sizeof(s_rates[0]); ++i) {
    if (s_rates[i].interval_us <= requested_us)
      rate = &s_rates[i];
  }
  return rate;
}

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

static const struct pbl_qmi8658 *s_dev;
#ifdef CONFIG_T5AI_POWER_PROFILE
static T5AIQMIPowerStats s_power;
#define QMI_POWER_ADD(field, value) (s_power.field += (value))
void t5ai_qmi8658_power_stats(T5AIQMIPowerStats *stats) {
  if (stats == NULL)
    return;
  pbl_irq_lock();
  *stats = s_power;
  if (s_dev != NULL) {
    stats->initialized = s_dev->state->initialized;
    stats->active = s_dev->state->active;
    stats->shake = s_dev->state->shake_enabled;
    stats->recovery = s_dev->state->recovery_required;
    stats->irq_armed = s_dev->state->irq_armed;
    stats->odr = s_dev->state->motion_active ? QMI8658_ODR_LOW_POWER_21HZ : s_dev->state->odr;
    stats->watermark = s_dev->state->watermark;
    stats->interval_us =
        s_dev->state->motion_active ? QMI8658_MOTION_INTERVAL_US : s_dev->state->interval_us;
    stats->batch = s_dev->state->num_samples;
  }
  pbl_irq_unlock();
}
#else
#define QMI_POWER_ADD(field, value) ((void)0)
#endif
static bool prv_queue_work(void (*work)(void));
#include "qmi8658_profile.h"
#include "qmi8658_self_test.h"

// These helpers require s_dev->state->io_lock. The bus mutex still serializes
// other devices.
static bool prv_read(uint8_t reg, uint8_t *data, size_t length) {
  const uint32_t started = qmi_profile_io_start();
  bool result;
  if (s_dev->state->motion_active) {
    // Match the vendor's STOP-separated register selection in motion mode.
    result =
        pbl_i2c_write(&s_dev->i2c, &reg, 1u) == 0 && pbl_i2c_read(&s_dev->i2c, data, length) == 0;
  } else {
    result = pbl_i2c_burst_read(&s_dev->i2c, reg, data, length) == 0;
  }
  qmi_profile_io_end(started, length, result);
  return result;
}

static bool prv_write(uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  const uint32_t started = qmi_profile_io_start();
  const bool result = (pbl_i2c_write(&s_dev->i2c, data, sizeof(data)) == 0);
  qmi_profile_io_end(started, sizeof(data), result);
  return result;
}

static uint64_t prv_time_us(void) {
  return pbl_realtime_ms() * 1000u;
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

static uint8_t prv_st_threshold_percent(uint8_t percent) {
  return QMI8658_ST_THS_MAX - percent * (QMI8658_ST_THS_MAX - QMI8658_ST_THS_MIN) / 100u;
}

static uint32_t prv_shake_threshold_mg(void) {
#ifdef CONFIG_T5AI_QMI8658_ANY_MOTION
  if (s_dev->state->shake_sensitivity_high)
    return QMI8658_SHAKE_MIN_MG;
  // The baseline filter uses an amplitude threshold, without slope duration.
  const uint32_t threshold = (s_dev->state->motion_st_threshold * QMI8658_FALLBACK_MG_NUMERATOR +
                              QMI8658_FALLBACK_MG_DENOMINATOR - 1u) /
                             QMI8658_FALLBACK_MG_DENOMINATOR;
  return threshold < QMI8658_SHAKE_MIN_MG ? QMI8658_SHAKE_MIN_MG : threshold;
#else
  return s_dev->state->shake_sensitivity_high
             ? QMI8658_SHAKE_MIN_MG
             : QMI8658_SHAKE_MAX_MG - s_dev->state->shake_sensitivity_percent *
                                          (QMI8658_SHAKE_MAX_MG - QMI8658_SHAKE_MIN_MG) / 100u;
#endif
}

// Preserve the original 7/8 retention per 8 ms without floating point.
static uint32_t prv_baseline_retention(uint32_t elapsed_us) {
  uint32_t periods = elapsed_us / QMI8658_DEFAULT_INTERVAL_US;
  uint32_t factor = 57344u; // 7/8 in Q16.
  uint32_t retained = 65536u;
  while (periods) {
    if (periods & 1u)
      retained = (uint32_t)(((uint64_t)retained * factor) >> 16);
    factor = (uint32_t)(((uint64_t)factor * factor) >> 16);
    periods >>= 1;
  }
  // Interpolate the remaining fraction of one 8 ms period.
  return (uint32_t)(((uint64_t)retained * (65536u - (elapsed_us % QMI8658_DEFAULT_INTERVAL_US) *
                                                        8192u / QMI8658_DEFAULT_INTERVAL_US)) >>
                    16);
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
  const uint32_t elapsed_us = (uint32_t)(now - s_dev->state->shake_previous_us);
  s_dev->state->shake_previous_us = now;
  const uint32_t threshold = prv_shake_threshold_mg();
  const uint32_t alpha = 65536u - prv_baseline_retention(elapsed_us);
  int32_t peak = 0;
  size_t axis = 0;
  for (size_t i = 0; i < 3; ++i) {
    const int32_t delta = values[i] - s_dev->state->shake_baseline[i];
    if ((delta < 0 ? -delta : delta) > (peak < 0 ? -peak : peak)) {
      peak = delta;
      axis = i;
    }
    s_dev->state->shake_baseline[i] += (int32_t)((int64_t)delta * alpha / 65536);
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
  const bool slow = command == QMI8658_CMD_RESET_FIFO || command == QMI8658_CMD_WRITE_WOM ||
                    command == QMI8658_CMD_CONFIG_MOTION || s_dev->state->interval_us >= 48000u;
  unsigned attempts = slow ? 50u : s_dev->state->interval_us / 1000u;
  if (attempts < QMI8658_CMD_ATTEMPTS)
    attempts = QMI8658_CMD_ATTEMPTS;
  if (attempts > 50u)
    attempts = 50u;
  if (command == QMI8658_CMD_WRITE_WOM || command == QMI8658_CMD_CONFIG_MOTION)
    attempts = 200u;
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
  const bool idle = (command == QMI8658_CMD_WRITE_WOM || command == QMI8658_CMD_CONFIG_MOTION)
                        ? (s_dev->state->command_idle = prv_wait_command(false, 200u, true))
                        : prv_command_finish();
  return done && idle;
}

static void prv_mask_irq(void) {
  (void)pbl_intc_disable(&s_dev->int2);
  s_dev->state->irq_armed = false;
}

static void prv_arm_irq(void) {
  // FIFO watermark delivery masks the pad; finish_poll checks its level.
  // Direct-mode INT2 pulses stay armed while the raw slot is busy.
  if (s_dev->state->active && !s_dev->state->recovery_required && !s_dev->state->irq_armed) {
    s_dev->state->irq_armed = true;
    (void)pbl_intc_enable(&s_dev->int2);
  }
}

static void prv_finish_poll(void) {
  pbl_irq_lock();
  s_dev->state->poll_pending = false;
  const uint32_t elapsed_ms = (uint32_t)t5ai_rtc_get_ticks() - s_dev->state->last_acquire_ms;
  // Watermark deassertion is asynchronous. A high pin immediately after
  // resume is not evidence of a fresh batch; allow one conversion to settle.
  const bool fifo_catchup =
      s_dev->state->watermark > 1u && elapsed_ms >= (s_dev->state->interval_us + 999u) / 1000u &&
      s_dev->state->catchup_available && (pbl_gpio_get_raw(&s_dev->int2_gpio) > 0);
  const bool catchup =
      s_dev->state->active && !s_dev->state->recovery_required &&
      (s_dev->state->watermark == 1u
           ? (s_dev->state->direct_edge_pending ||
              ((s_dev->state->wom_active || s_dev->state->motion_active) &&
               s_dev->state->catchup_available && pbl_gpio_get_raw(&s_dev->int2_gpio) > 0))
           : fifo_catchup);
  if (catchup) {
    // At most one immediate catchup per real edge/watchdog pass. A stuck-high
    // pad cannot create an unbounded acquisition/delivery loop.
    s_dev->state->catchup_available = false;
    s_dev->state->watermark_seen = false;
    s_dev->state->poll_pending = true;
    if (s_dev->state->watermark > 1u || s_dev->state->wom_active || s_dev->state->motion_active) {
      s_dev->state->irq_started = 0u;
      prv_mask_irq();
    }
    qmi_profile_catchup();
  } else if (s_dev->state->recovery_required) {
    prv_mask_irq();
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
    if (s_dev->state->watermark > 1u || s_dev->state->wom_active || s_dev->state->motion_active ||
        s_dev->state->recovery_required)
      prv_mask_irq();
  }
  pbl_irq_unlock();
  if (schedule) {
    pbl_sem_give(&s_dev->state->poll_signal);
  }
}

static void prv_int2_handler(void *arg) {
  if (s_dev->state->watermark == 1u && s_dev->state->poll_pending)
    qmi_profile_direct_busy();
  QMI_POWER_ADD(irqs, 1u);
  qmi_profile_irq();
  if (s_dev->state->watermark > 1u || !s_dev->state->direct_edge_pending)
    s_dev->state->irq_started = qmi_profile_io_start();
  if (s_dev->state->initialized && s_dev->state->active && !s_dev->state->recovery_required &&
      s_dev->state->watermark == 1u)
    s_dev->state->direct_edge_pending = true;
  if (s_dev->state->motion_active) {
    const uint32_t now_ms = (uint32_t)t5ai_rtc_get_ticks();
    if (!s_dev->state->motion_irq_seen ||
        (uint32_t)(now_ms - s_dev->state->motion_last_irq_ms) >= QMI8658_MOTION_REARM_MS)
      s_dev->state->motion_rearm_pending = true;
    s_dev->state->motion_last_irq_ms = now_ms;
    s_dev->state->motion_irq_seen = true;
  }
  s_dev->state->watermark_seen = s_dev->state->watermark > 1u;
  s_dev->state->catchup_available = true;
  s_dev->state->watchdog_passes = 0u;
  prv_schedule();
}

static void prv_timer_callback(void *context) {
  (void)context;
  QMI_POWER_ADD(watchdogs, 1u);
  qmi_profile_watchdog();
  if ((s_dev->state->wom_active || s_dev->state->motion_active) &&
      !s_dev->state->recovery_required) {
    s_dev->state->catchup_available = true;
    prv_schedule();
    return;
  }
  // No bus work on NewTimers. A persistent high pad generates at most one
  // worker pass per watchdog period, including after transfer failures.
  const uint32_t deadline_ms =
      (2u * s_dev->state->watermark * s_dev->state->interval_us + 999u) / 1000u;
  if ((uint32_t)t5ai_rtc_get_ticks() - s_dev->state->last_acquire_ms >= deadline_ms ||
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

static void prv_reset_samples_locked(void) {
  ++s_dev->state->generation;
  s_dev->state->counter_valid = false;
  s_dev->state->startup_us = prv_time_us();
  s_dev->state->last_sample_valid = false;
  s_dev->state->shake_baseline_valid = false;
  s_dev->state->quiet_valid = false;
}

static bool prv_reset_sensor_locked(void) {
  bool ok = prv_write(QMI8658_RESET, QMI8658_RESET_COMMAND);
  pbl_sleep_ms(QMI8658_RESET_SETTLE_MS);
  uint8_t id = 0u;
  ok = ok && prv_read(QMI8658_WHO_AM_I, &id, 1u) && id == QMI8658_ID &&
       prv_write(QMI8658_CTRL1, QMI8658_CTRL1_INIT) &&
       prv_write(QMI8658_CTRL8, QMI8658_CTRL8_HANDSHAKE) && prv_write(QMI8658_CTRL7, 0u) &&
       prv_write(QMI8658_FIFO_CTRL, 0u);
  s_dev->state->command_idle = false;
  return ok;
}

// io_lock held. A failed/partial WOM command must be undone before normal
// configuration.
static bool prv_exit_wom_locked(bool ok) {
  if (s_dev->state->wom_configured) {
    // Undo even a partial WOM entry. Reset only the sensor if exit fails.
    ok = ok && prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_125HZ) &&
         prv_write(QMI8658_CAL1_L, 0u) && prv_write(QMI8658_CAL1_H, 0x40u) &&
         prv_command(QMI8658_CMD_WRITE_WOM);
    if (!ok) {
      s_dev->state->wom_failed = true;
      ok = prv_reset_sensor_locked();
    }
    if (ok) {
      s_dev->state->wom_configured = false;
      s_dev->state->wom_active = false;
    }
  }
  return ok;
}

#ifdef CONFIG_T5AI_QMI8658_ANY_MOTION
// Match the ST slope threshold for constant-rate acceleration ramps. The
// filters still differ for arbitrary waveforms; this is not gesture equivalence.
static uint8_t prv_motion_threshold(void) {
  const uint32_t st_code =
      s_dev->state->shake_sensitivity_high ? QMI8658_ST_THS_MIN : s_dev->state->motion_st_threshold;
  const uint32_t scaled = st_code * QMI8658_ST_SLOPE_SCALE * QMI8658_MOTION_INTERVAL_US;
  return (scaled + QMI8658_ST_INTERVAL_US - 1u) / QMI8658_ST_INTERVAL_US;
}

static uint8_t prv_motion_window(void) {
  return (QMI8658_ST_WAKE_DURATION * QMI8658_ST_INTERVAL_US + QMI8658_MOTION_INTERVAL_US - 1u) /
         QMI8658_MOTION_INTERVAL_US;
}

// Sensors are off. Section 9.4 uses two CAL banks and CTRL9 command 0x0e.
static bool prv_configure_motion_locked(void) {
  const uint8_t threshold = prv_motion_threshold();
  const uint8_t window = prv_motion_window();
  const uint8_t banks[][8] = {
    {threshold, threshold, threshold, 0u, 0u, 0u, 0x07u, 0x01u},
    {window, 0u, 0u, 0u, 0u, 0u, 0u, 0x02u},
  };
  if (!prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_LOW_POWER_21HZ))
    return false;
  for (unsigned bank = 0; bank < 2u; ++bank) {
    for (unsigned i = 0; i < 8u; ++i) {
      if (!prv_write(QMI8658_CAL1_L + i, banks[bank][i]))
        return false;
    }
    if (!prv_command(QMI8658_CMD_CONFIG_MOTION))
      return false;
  }
  uint8_t status;
  return prv_read(QMI8658_STATUS1, &status, 1u) &&
         prv_write(QMI8658_CTRL8, QMI8658_CTRL8_HANDSHAKE | QMI8658_CTRL8_ANY_MOTION) &&
         prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE | QMI8658_DRDY_DISABLE);
}
#endif

static bool prv_configure_locked(void) {
  prv_mask_irq();
  s_dev->state->direct_edge_pending = false;
  s_dev->state->watermark_seen = false;
  s_dev->state->irq_started = 0u;
  prv_reset_samples_locked();
  const bool active = s_dev->state->num_samples != 0u || s_dev->state->shake_enabled;
  const uint8_t watermark = prv_watermark(s_dev->state->num_samples);
  bool ok = prv_write(QMI8658_CTRL7, 0u) && prv_write(QMI8658_FIFO_CTRL, 0u);
  ok = prv_exit_wom_locked(ok);
  if (s_dev->state->motion_active || s_dev->state->motion_failed)
    ok = ok && prv_write(QMI8658_CTRL8, QMI8658_CTRL8_HANDSHAKE);
  s_dev->state->motion_active = false;
  s_dev->state->motion_asserted = false;
  s_dev->state->motion_irq_seen = false;
  s_dev->state->motion_rearm_pending = false;
#ifdef CONFIG_T5AI_QMI8658_ANY_MOTION
  if (ok && s_dev->state->shake_enabled && !s_dev->state->num_samples &&
      !s_dev->state->motion_failed) {
    s_dev->state->motion_active = prv_configure_motion_locked();
    if (!s_dev->state->motion_active) {
      s_dev->state->motion_failed = true;
      // Undo even a partial setup before restoring the software/WOM path.
      ok = prv_reset_sensor_locked();
      PBL_LOG_WRN("QMI8658 any-motion setup failed; using software fallback");
    }
  }
#endif
  if (ok && !s_dev->state->motion_active && watermark > 1u) {
    // Low-power ODRs slow command processing too. Reset at the normal clock,
    // then select the requested ODR; never silently retain a faster rate.
    ok = prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_125HZ) &&
         prv_command(QMI8658_CMD_RESET_FIFO) && prv_write(QMI8658_FIFO_WTM, watermark) &&
         prv_write(QMI8658_FIFO_CTRL, QMI8658_FIFO_STREAM);
  }
  if (!s_dev->state->motion_active) {
    ok = ok && prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | s_dev->state->odr);
    if (ok && active)
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

static void prv_timer_work(struct pbl_work *work) {
  prv_timer_callback(nullptr);
  if (s_dev->state->timer_period != 0) {
    pbl_delayed_work_schedule_next(&s_dev->state->timer, s_dev->state->timer_period);
  }
}

static void prv_update_timer(void) {
  if (s_dev->state->active || s_dev->state->recovery_required) {
    uint32_t period_ms = (2u * s_dev->state->watermark * s_dev->state->interval_us + 999u) / 1000u;
    if ((s_dev->state->wom_active || s_dev->state->motion_active) &&
        !s_dev->state->recovery_required)
      period_ms = QMI8658_WOM_HEALTH_MS;
    if (period_ms < 100u) {
      period_ms = 100u;
    }
    s_dev->state->timer_period = pbl_ms_to_ticks(period_ms);
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq_high, &s_dev->state->timer,
                                      PBL_TICKS(s_dev->state->timer_period));
  } else {
    s_dev->state->timer_period = 0;
    (void)pbl_delayed_work_cancel(&s_dev->state->timer);
  }
}

// io_lock held; no listener callbacks during mode changes.
static bool prv_resume_wom_locked(void) {
  const bool valid = s_dev->state->shake_baseline_valid;
  int32_t baseline[3];
  for (unsigned i = 0; i < 3u; ++i)
    baseline[i] = s_dev->state->shake_baseline[i];
  const uint64_t holdoff = s_dev->state->shake_holdoff_us;
  const bool ok = prv_configure_locked();
  if (ok && valid && s_dev->state->shake_enabled && !s_dev->state->num_samples) {
    for (unsigned i = 0; i < 3u; ++i)
      s_dev->state->shake_baseline[i] = baseline[i];
    s_dev->state->shake_baseline_valid = true;
    s_dev->state->shake_holdoff_us = holdoff;
    s_dev->state->shake_previous_us = prv_time_us();
  }
  PBL_LOG_DBG("QMIWOM resumed ok=%u", ok);
  return ok;
}

static bool prv_track_quiet_locked(void) {
#ifdef CONFIG_T5AI_QMI8658_WOM
  if (s_dev->state->motion_active || s_dev->state->num_samples || !s_dev->state->shake_enabled ||
      s_dev->state->wom_failed)
    return true;
  AccelDriverSample sample = {};
  prv_decode(s_dev->state->raw, s_dev->state->rotated, &sample);
  const int16_t values[] = {sample.x, sample.y, sample.z};
  const uint32_t now_ms = t5ai_rtc_get_ticks();
  bool motion = !s_dev->state->quiet_valid || !s_dev->state->shake_baseline_valid;
  for (unsigned i = 0; i < 3u; ++i) {
    const int32_t delta = (int32_t)values[i] - s_dev->state->shake_baseline[i];
    if (delta > (int32_t)QMI8658_WOM_THRESHOLD_MG || delta < -(int32_t)QMI8658_WOM_THRESHOLD_MG)
      motion = true;
  }
  if (motion) {
    s_dev->state->quiet_valid = true;
    s_dev->state->quiet_since_ms = now_ms;
  }
  // Sampling/health deadlines bound this interval well below the 16-bit wrap.
  if ((uint16_t)(now_ms - s_dev->state->quiet_since_ms) < QMI8658_WOM_QUIET_MS)
    return true;
  prv_mask_irq();
  s_dev->state->direct_edge_pending = false;
  s_dev->state->irq_started = 0u;
  s_dev->state->catchup_available = true;
  // Recovery must undo even a partially applied WOM command.
  s_dev->state->wom_configured = true;
  const bool ok = prv_write(QMI8658_CTRL7, 0u) && prv_write(QMI8658_FIFO_CTRL, 0u) &&
                  prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_LOW_POWER_21HZ) &&
                  prv_write(QMI8658_CAL1_L, QMI8658_WOM_THRESHOLD_MG) &&
                  prv_write(QMI8658_CAL1_H, 0x41u) && prv_command(QMI8658_CMD_WRITE_WOM) &&
                  prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE);
  s_dev->state->wom_active = ok;
  s_dev->state->wom_failed = !ok;
  s_dev->state->counter_valid = false;
  if (!ok)
    PBL_LOG_WRN("QMI8658 WOM entry failed; using continuous fallback");
  PBL_LOG_DBG("QMIWOM entered ok=%u", ok);
  return ok;
#else
  return true;
#endif
}

// Called only under the accelerometer device lock (offload callback or public
// API). No I2C.
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
    if (!s_dev->state->shake_enabled && i + 1u < s_dev->state->pending_count)
      continue;
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

typedef struct {
  unsigned count;
  uint32_t counter;
  uint64_t timestamp_us;
} QMI8658Batch;

// io_lock held. Keep validation separate from event qualification.
static bool prv_motion_config_valid_locked(void) {
  uint8_t ctrl2, ctrl7, ctrl8;
  return prv_read(QMI8658_CTRL2, &ctrl2, 1u) && prv_read(QMI8658_CTRL7, &ctrl7, 1u) &&
         prv_read(QMI8658_CTRL8, &ctrl8, 1u) &&
         ctrl2 == (QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_LOW_POWER_21HZ) &&
         ctrl7 == (QMI8658_ACCEL_ENABLE | QMI8658_DRDY_DISABLE) &&
         ctrl8 == (QMI8658_CTRL8_HANDSHAKE | QMI8658_CTRL8_ANY_MOTION);
}

// io_lock held. Quiet hardware motion is not a stalled sample stream.
static bool prv_service_motion_locked(AccelDriverSample *sample, bool *event) {
  pbl_irq_lock();
  const bool rearm = s_dev->state->motion_rearm_pending;
  s_dev->state->motion_rearm_pending = false;
  s_dev->state->direct_edge_pending = false;
  s_dev->state->irq_started = 0u;
  pbl_irq_unlock();
  uint8_t status, raw[QMI8658_ACCEL_SAMPLE_BYTES];
  bool ok = prv_read(QMI8658_STATUS1, &status, 1u) && prv_motion_config_valid_locked();
  if (ok) {
    const bool asserted = (status & QMI8658_ANY_MOTION_EVENT) != 0u;
    const uint64_t now = prv_time_us();
    // QMI pulses with DRDY during continuing motion; an edge alone isn't a new shake.
    if (asserted && (rearm || !s_dev->state->motion_asserted) && now >= s_dev->state->startup_us &&
        now - s_dev->state->startup_us >= QMI8658_STARTUP_SETTLE_MS * 1000u &&
        now >= s_dev->state->shake_holdoff_us) {
      ok = prv_read(QMI8658_AX_L, raw, sizeof(raw));
      if (ok) {
        prv_decode(raw, s_dev->state->rotated, sample);
        sample->timestamp_us = now;
        *event = true;
      }
    }
    s_dev->state->motion_asserted = asserted;
  }
  if (!ok) {
    PBL_LOG_WRN("QMI8658 motion health/read failed; using software fallback");
    s_dev->state->motion_failed = true;
  }
  return ok;
}

// Device lock held, after releasing io_lock. Any-motion has no axis status;
// use the dominant sampled axis only as approximate event metadata.
static void prv_report_motion(const AccelDriverSample *sample) {
  const int16_t values[] = {sample->x, sample->y, sample->z};
  unsigned axis = 0u;
  for (unsigned i = 1u; i < 3u; ++i) {
    if (abs(values[i]) > abs(values[axis]))
      axis = i;
  }
  s_dev->state->shake_holdoff_us = sample->timestamp_us + QMI8658_SHAKE_HOLDOFF_US;
  pbl_accel_report_event(&s_dev->accel, &(const struct pbl_accel_event){
                                          .type = PBL_ACCEL_EVENT_SHAKE,
                                          .axis = (IMUCoordinateAxis)axis,
                                          .direction = values[axis] < 0 ? -1 : 1,
                                        });
}

// io_lock held. Each mode owns its complete bus transaction and cleanup.
static bool prv_service_wom_locked(void) {
  bool ok;
  uint8_t status = 0u, config[2] = {0};
  s_dev->state->direct_edge_pending = false;
  s_dev->state->irq_started = 0u;
  // STATUS1 acknowledges WOM; quiet is not a stalled sample stream.
  ok = prv_read(QMI8658_STATUS1, &status, 1u);
  if (ok && (status & QMI8658_WOM_EVENT)) {
    ok = prv_resume_wom_locked();
  } else if (ok) {
    ok = prv_read(QMI8658_CTRL2, config, 1u) && prv_read(QMI8658_CTRL7, config + 1u, 1u) &&
         config[0] == (QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_LOW_POWER_21HZ) &&
         config[1] == QMI8658_ACCEL_ENABLE;
  }
  if (!ok)
    s_dev->state->wom_failed = true;
  return ok;
}

static bool prv_acquire_direct_locked(QMI8658Batch *batch) {
  bool ok;
  // STATUS0, STATUS1, 24-bit sample count, temperature, XYZ in one transfer.
  uint8_t data[13];
  // Consume only edges preceding this read. Pulses during I/O or delivery
  // request one follow-up after the raw slot is released.
  pbl_irq_lock();
  const uint32_t started = s_dev->state->irq_started;
  s_dev->state->irq_started = 0u;
  s_dev->state->direct_edge_pending = false;
  pbl_irq_unlock();
  if (started)
    qmi_profile_irq_latency(started);
  ok = prv_read(QMI8658_STATUS0, data, sizeof(data));
  batch->timestamp_us = prv_time_us();
  if (ok && (data[0] & QMI8658_ACCEL_DATA_READY)) {
    for (unsigned i = 0; i < QMI8658_ACCEL_SAMPLE_BYTES; ++i) {
      s_dev->state->raw[i] = data[7u + i];
    }
    batch->counter = prv_counter(data + 2u);
    batch->count = 1u;
  } else if (ok) {
    qmi_profile_empty();
  }
  return ok;
}

static bool prv_acquire_fifo_locked(QMI8658Batch *batch) {
  bool ok = true;
  uint8_t status[2] = {0};
  bool watermark = s_dev->state->watermark_seen;
  s_dev->state->watermark_seen = false;
#ifdef QMI8658_FIFO_SELF_TEST
  if (s_test.precheck)
    watermark = false;
#endif
  // INT2 already establishes watermark. Only watchdog passes need a status
  // precheck; another transfer here consumes the next-conversion budget.
  if (!watermark)
    ok = prv_read(QMI8658_FIFO_COUNT, status, sizeof(status));
  if (ok && (watermark || (status[1] & 0x40u))) {
    batch->timestamp_us = prv_time_us();
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
      batch->count = bytes / QMI8658_ACCEL_SAMPLE_BYTES;
      qmi_profile_fifo_count(batch->count);
      if ((status[1] & QMI8658_FIFO_OVERFLOW) || bytes % QMI8658_ACCEL_SAMPLE_BYTES ||
          batch->count > QMI8658_BATCH_MAX) {
        qmi_profile_drop(batch->count);
        ok = false;
      } else if (batch->count) {
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
  return ok;
}

// One bounded pass, with no callbacks while the hardware lock is held.
static void prv_acquire_sample(void) {
  qmi_profile_acquire_start();
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  bool ok = s_dev->state->initialized && (s_dev->state->active || s_dev->state->recovery_required);
  const bool was_wom = s_dev->state->wom_active;
  const bool was_recovery = s_dev->state->recovery_required;
  QMI8658Batch batch = {0};
  bool motion_event = false;
  AccelDriverSample motion_sample = {};
  const uint32_t generation = s_dev->state->generation;
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
  if (ok) {
    if (s_dev->state->motion_active)
      ok = prv_service_motion_locked(&motion_sample, &motion_event);
    else if (s_dev->state->wom_active)
      ok = prv_service_wom_locked();
    else if (s_dev->state->watermark == 1u)
      ok = prv_acquire_direct_locked(&batch);
    else
      ok = prv_acquire_fifo_locked(&batch);
  }
  if (!ok && s_dev->state->active) {
    s_dev->state->recovery_required = true;
    batch.count = 0u;
    s_dev->state->counter_valid = false;
  }
  if (ok && batch.count && s_dev->state->watermark == 1u && s_dev->state->counter_valid &&
      batch.counter == s_dev->state->last_counter) {
    qmi_profile_duplicate();
    batch.count = 0u;
  }
  if (ok && batch.count) {
    s_dev->state->last_acquire_ms = t5ai_rtc_get_ticks();
    if (s_dev->state->watermark == 1u) {
      prv_timestamp(batch.counter, batch.count, batch.timestamp_us);
    } else {
      // Revision 7c leaves TIMESTAMP frozen while FIFO is enabled. Like the
      // other FIFO drivers, anchor the newest sample to the host read time.
      const uint64_t span = (uint64_t)(batch.count - 1u) * s_dev->state->interval_us;
      s_dev->state->pending_first_us = batch.timestamp_us >= span ? batch.timestamp_us - span : 0u;
      s_dev->state->counter_valid = false;
    }
    s_dev->state->pending_count = batch.count;
    s_dev->state->pending_interval_us = s_dev->state->interval_us;
    s_dev->state->pending_rotated = s_dev->state->rotated;
    s_dev->state->sample_generation = s_dev->state->generation;
  }
  if (ok && batch.count && s_dev->state->watermark == 1u && !prv_track_quiet_locked()) {
    s_dev->state->recovery_required = true;
    s_dev->state->pending_count = 0u;
    batch.count = 0u;
  }
  const bool update_timer =
      was_wom != s_dev->state->wom_active || was_recovery != s_dev->state->recovery_required;
  pbl_mutex_unlock(&s_dev->state->io_lock);
  if (update_timer)
    prv_update_timer();
  QMI_POWER_ADD(passes, 1u);
  QMI_POWER_ADD(acquired, batch.count);
  qmi_profile_acquire_end(batch.count);
  if (motion_event) {
    pbl_accel_lock(&s_dev->accel);
    if (generation == s_dev->state->generation && s_dev->state->motion_active &&
        s_dev->state->shake_enabled && !s_dev->state->recovery_required)
      prv_report_motion(&motion_sample);
    pbl_accel_unlock(&s_dev->accel);
  }
  if (batch.count) {
    // Shake-only samples need no data callback on NewTimers. Release io_lock
    // before taking the device lock, matching the configuration lock order.
    pbl_accel_lock(&s_dev->accel);
    const bool shake_only = s_dev->state->num_samples == 0u;
    if (shake_only) {
      prv_deliver_sample();
    }
    pbl_accel_unlock(&s_dev->accel);
    if (shake_only) {
      return;
    }
  }
  if (!batch.count || !prv_queue_work(prv_deliver_sample)) {
    if (batch.count) {
      qmi_profile_drop(batch.count);
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
#ifdef CONFIG_T5AI_POWER_PROFILE
  s_power.identity_valid = false;
#endif
  s_dev->state->watermark = 1;
  s_dev->state->odr = QMI8658_ODR_125HZ;
  s_dev->state->initialized = false;
  s_dev->state->timer_period = 0;
  s_dev->state->interval_us = QMI8658_DEFAULT_INTERVAL_US;
  // Until a data client requests a rate, shake alone needs only its rate limit.
  s_dev->state->requested_interval_us = QMI8658_SHAKE_INTERVAL_US;
  s_dev->state->shake_sensitivity_percent = 50;
  s_dev->state->motion_st_threshold = QMI8658_ST_THS_DEFAULT;
  pbl_mutex_init(&s_dev->state->io_lock);
  pbl_sem_init(&s_dev->state->poll_signal, 0, 1);
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
#ifdef CONFIG_T5AI_ACCEL_DISABLED
  const bool disabled = prv_write(QMI8658_CTRL7, 0u);
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return disabled ? 0 : -EIO;
#endif
  bool ok = false;
  uint8_t id[2] = {0};
  for (unsigned attempt = 0; attempt < 3u; ++attempt) {
    id[0] = id[1] = 0;
    ok = prv_write(QMI8658_RESET, QMI8658_RESET_COMMAND);
    pbl_sleep_ms(QMI8658_RESET_SETTLE_MS);
    ok = ok && prv_read(QMI8658_WHO_AM_I, id, 1u) && prv_read(QMI8658_WHO_AM_I + 1u, id + 1u, 1u) &&
         id[0] == QMI8658_ID;
#ifdef CONFIG_T5AI_POWER_PROFILE
    s_power.id = id[0];
    s_power.revision = id[1];
    s_power.identity_valid = ok;
#endif
    ok = ok && prv_write(QMI8658_CTRL1, QMI8658_CTRL1_INIT) && prv_write(QMI8658_CTRL8, 0x80u) &&
         prv_write(QMI8658_CTRL7, 0u);
    if (ok)
      break;
  }
  pbl_mutex_unlock(&s_dev->state->io_lock);
  if (!ok) {
    PBL_LOG_ERR("QMI8658 initialization failed id=%02x rev=%02x", id[0], id[1]);
    return -EIO;
  }
  PBL_LOG_DBG("QMI8658 interrupt acquisition id=%02x rev=%02x", id[0], id[1]);
  pbl_delayed_work_init(&s_dev->state->timer, prv_timer_work);
  int ret = pbl_gpio_configure(&s_dev->int2_gpio, PBL_GPIO_INPUT);
  if (ret == 0)
    ret = pbl_intc_request(&s_dev->int2, prv_int2_handler, (void *)s_dev);
  if (ret != 0) {
    (void)pbl_delayed_work_cancel_sync(&s_dev->state->timer);
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
    (void)pbl_delayed_work_cancel_sync(&s_dev->state->timer);
    (void)pbl_intc_free(&s_dev->int2);
    return -EIO;
  }
  s_dev->state->initialized = true;
#ifdef QMI8658_FIFO_SELF_TEST
  prv_self_test_start();
#endif
  return 0;
}

// Device lock and io_lock held. Publish the final rate and client settings
// together.
static int prv_apply_settings_locked(const QMI8658Rate *rate, uint32_t count, bool shake) {
  const QMI8658Rate previous = {s_dev->state->interval_us, s_dev->state->odr};
  const uint32_t previous_count = s_dev->state->num_samples;
  const bool previous_shake = s_dev->state->shake_enabled;
  prv_process_pending();
  s_dev->state->interval_us = rate->interval_us;
  s_dev->state->odr = rate->odr;
  s_dev->state->num_samples = count;
  s_dev->state->shake_enabled = shake;
  const bool ok = prv_configure_locked();
  if (!ok) {
    s_dev->state->interval_us = previous.interval_us;
    s_dev->state->odr = previous.odr;
    s_dev->state->num_samples = previous_count;
    s_dev->state->shake_enabled = previous_shake;
    (void)prv_configure_locked();
  }
  prv_update_timer();
  return ok ? 0 : -EIO;
}

static int prv_set_rotated(const struct pbl_accel *accel, bool rotated) {
  if (s_dev->state->rotated == rotated)
    return 0;
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  prv_process_pending();
  const bool previous = s_dev->state->rotated;
  s_dev->state->rotated = rotated;
  int ret = 0;
  if (s_dev->state->initialized) {
    // Rotation is a software transform. WOM still exits to rebuild its
    // baseline.
    if (s_dev->state->wom_configured) {
      if (!prv_configure_locked()) {
        s_dev->state->rotated = previous;
        (void)prv_configure_locked();
        ret = -EIO;
      }
      prv_update_timer();
    } else {
      prv_reset_samples_locked();
    }
  }
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return ret;
}

static int prv_set_sampling_interval(const struct pbl_accel *accel, uint32_t interval_us,
                                     uint32_t *actual_us) {
  const QMI8658Rate *rate = prv_select_rate(interval_us, s_dev->state->shake_enabled);
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  int ret = 0;
  if (s_dev->state->initialized && s_dev->state->odr != rate->odr)
    ret = prv_apply_settings_locked(rate, s_dev->state->num_samples, s_dev->state->shake_enabled);
  if (ret == 0)
    s_dev->state->requested_interval_us = interval_us;
  *actual_us = s_dev->state->interval_us;
  pbl_mutex_unlock(&s_dev->state->io_lock);
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
  const QMI8658Rate rate = {s_dev->state->interval_us, s_dev->state->odr};
  const int ret = prv_apply_settings_locked(&rate, count, s_dev->state->shake_enabled);
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return ret;
}

static uint32_t prv_get_max_num_samples(const struct pbl_accel *accel) {
  return QMI8658_BATCH_MAX;
}

static bool prv_read_fresh_locked(AccelDriverSample *sample) {
  pbl_sleep_ms(QMI8658_STARTUP_SETTLE_MS);
  uint8_t status, raw[QMI8658_ACCEL_SAMPLE_BYTES];
  if (!prv_read(QMI8658_STATUS0, &status, 1u) || !(status & QMI8658_ACCEL_DATA_READY) ||
      !prv_read(QMI8658_AX_L, raw, sizeof(raw)))
    return false;
  prv_decode(raw, s_dev->state->rotated, sample);
  sample->timestamp_us = prv_time_us();
  s_dev->state->last_sample = *sample;
  s_dev->state->last_sample_valid = true;
  return true;
}

static int prv_peek(const struct pbl_accel *accel, AccelDriverSample *data) {
  if (!data)
    return -EINVAL;
  if (!s_dev->state->initialized)
    return -ENODEV;
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  prv_process_pending();
  if (s_dev->state->motion_active) {
    const bool ok = prv_read_fresh_locked(data);
    if (!ok) {
      PBL_LOG_WRN("QMI8658 motion sample read failed; using software fallback");
      s_dev->state->motion_failed = true;
      s_dev->state->recovery_required = true;
      prv_update_timer();
    }
    pbl_mutex_unlock(&s_dev->state->io_lock);
    return ok ? 0 : -EIO;
  }
  if (s_dev->state->wom_active) {
    bool ok = prv_resume_wom_locked();
    if (ok)
      ok = prv_read_fresh_locked(data);
    if (!ok)
      s_dev->state->recovery_required = true;
    prv_update_timer();
    pbl_mutex_unlock(&s_dev->state->io_lock);
    return ok ? 0 : -EIO;
  }
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
  if (ok)
    ok = prv_read_fresh_locked(data);
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
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  const QMI8658Rate *rate = prv_select_rate(s_dev->state->requested_interval_us, on);
  const int ret = prv_apply_settings_locked(rate, s_dev->state->num_samples, on);
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return ret;
}

static int prv_update_motion_sensitivity(void) {
  int ret = 0;
  if (s_dev->state->motion_active) {
    (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
    if (!prv_configure_locked())
      ret = -EIO;
    prv_update_timer();
    pbl_mutex_unlock(&s_dev->state->io_lock);
  }
  return ret;
}

static int prv_set_shake_sensitivity_high(const struct pbl_accel *accel, bool sensitivity_high) {
  if (s_dev->state->shake_sensitivity_high == sensitivity_high)
    return 0;
  s_dev->state->shake_sensitivity_high = sensitivity_high;
  return prv_update_motion_sensitivity();
}

static int prv_set_shake_sensitivity_percent(const struct pbl_accel *accel, uint8_t percent) {
  percent = percent > 100u ? 100u : percent;
  const uint8_t st_threshold = prv_st_threshold_percent(percent);
  if (s_dev->state->shake_sensitivity_percent == percent &&
      s_dev->state->motion_st_threshold == st_threshold)
    return 0;
  s_dev->state->shake_sensitivity_percent = percent;
  s_dev->state->motion_st_threshold = st_threshold;
  return prv_update_motion_sensitivity();
}

static void prv_work_callback(void *data) {
  void (*work)(void) = data;
  pbl_accel_lock(&s_dev->accel);
  work();
  pbl_accel_unlock(&s_dev->accel);
}
static bool prv_queue_work(void (*work)(void)) {
  return pbl_sys_work_call_high(prv_work_callback, work);
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
