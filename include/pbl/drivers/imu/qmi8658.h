/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/accel.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include <pbl/services/new_timer/new_timer.h>

/* One sensor instance is supported. */
struct pbl_qmi8658_state {
  TimerID timer;
  uint32_t interval_us;
  uint32_t requested_interval_us;
  uint32_t num_samples;
  bool shake_enabled;
  bool active;
  bool initialized;
  bool rotated;
  bool shake_sensitivity_high;
  uint8_t shake_sensitivity_percent;
  bool shake_baseline_valid;
  int32_t shake_baseline[3];
  uint64_t shake_previous_us;
  uint64_t shake_holdoff_us;
  uint64_t startup_us;
  AccelDriverSample last_sample;
  bool last_sample_valid;
  bool poll_pending;
  uint32_t generation;
  uint32_t sample_generation;
  struct pbl_thread poll_thread;
  struct pbl_sem poll_signal;
  struct pbl_mutex io_lock;
  uint8_t poll_stack[1536] PBL_ALIGNED(CONFIG_KERNEL_STACK_ALIGN);
  uint8_t watermark;
  uint8_t odr;
  bool irq_armed;
  bool catchup_available;
  bool watermark_seen;
  uint32_t irq_started;
  uint32_t last_acquire_ms;
  unsigned watchdog_passes;
  uint8_t raw[6 * 6];
  uint8_t pending_count;
  uint64_t pending_first_us;
  uint32_t pending_interval_us;
  bool pending_rotated;
  uint32_t last_counter;
  uint64_t last_counter_us;
  bool counter_valid;
  bool command_idle;
  bool recovery_required;
#ifdef CONFIG_T5AI_QMI8658_PROFILE
  bool test_precheck;
  TimerID test_timer;
  unsigned test_phase;
  uint32_t test_interval;
  uint32_t test_count;
  bool test_shake;
  bool test_rotated;
  bool test_pending;
#endif
};
struct pbl_qmi8658 {
  struct pbl_accel accel;
  struct pbl_qmi8658_state *state;
  struct pbl_i2c_dev i2c;
  struct pbl_gpio int2_gpio;
  struct pbl_irq_spec int2;
};
extern const struct pbl_accel_ops pbl_qmi8658_ops;
#define PBL_QMI8658_DEFINE(sym, _bus, _addr, _gpio, _intc, _pin)                   \
  PBL_ACCEL_STATE_DEFINE(sym);                                                     \
  static struct pbl_qmi8658_state sym##_state;                                     \
  const struct pbl_qmi8658 sym = {                                                 \
    .accel =                                                                       \
        {                                                                          \
          .dev = PBL_DEVICE_INIT(sym, "qmi8658", pbl_accel_dev_init, &(_bus)->dev, \
                                 PBL_DEVICE_DEPS(&(_intc)->dev)),                  \
          .ops = &pbl_qmi8658_ops,                                                 \
          .state = &sym##_accel_state,                                             \
        },                                                                         \
    .state = &sym##_state,                                                         \
    .i2c = PBL_I2C_DEV(_bus, _addr),                                               \
    .int2_gpio = PBL_GPIO(_gpio, _pin, 0),                                         \
    .int2 = PBL_IRQ_SPEC(_intc, _pin, PBL_IRQ_TYPE_EDGE_RISING),                   \
  };                                                                               \
  PBL_DEVICE_REGISTER(sym, &sym.accel.dev)
