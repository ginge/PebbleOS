/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/gpio/t5ai.h>
#include <pbl/drivers/i2c.h>

enum t5ai_i2c_phase {
  T5AI_I2C_REGISTER,
  T5AI_I2C_RESTART,
  T5AI_I2C_WRITE,
  T5AI_I2C_READ
};
struct pbl_i2c_t5ai_state {
  enum t5ai_i2c_phase phase;
  uint32_t index;
  bool recovery_required;
  bool active;
};
struct pbl_i2c_t5ai {
  struct pbl_i2c_bus bus;
  struct pbl_i2c_t5ai_state *state;
  uintptr_t base;
  int32_t irqn;
  uint32_t frequency;
  struct pbl_gpio scl, sda;
};
extern const struct pbl_i2c_bus_ops pbl_i2c_t5ai_ops;
void pbl_i2c_t5ai_irq_handler(const struct pbl_i2c_t5ai *dev);
#define PBL_I2C_T5AI_DEFINE(sym, _base, _irqn, _hz, _scl, _sda)                               \
  PBL_I2C_BUS_STATE_DEFINE(sym);                                                              \
  static struct pbl_i2c_t5ai_state sym##_state;                                               \
  const struct pbl_i2c_t5ai sym = {                                                           \
    .bus = PBL_I2C_BUS_INIT(sym, "i2c0", &pbl_i2c_t5ai_ops, PBL_DEVICE_DEPS(&T5AI_GPIO.dev)), \
    .state = &sym##_state,                                                                    \
    .base = _base,                                                                            \
    .irqn = _irqn,                                                                            \
    .frequency = _hz,                                                                         \
    .scl = PBL_GPIO(&T5AI_GPIO, _scl, PBL_GPIO_OPEN_DRAIN | PBL_GPIO_PULL_UP),                \
    .sda = PBL_GPIO(&T5AI_GPIO, _sda, PBL_GPIO_OPEN_DRAIN | PBL_GPIO_PULL_UP),                \
  };                                                                                          \
  PBL_DEVICE_REGISTER(sym, &sym.bus.dev)
#ifdef CONFIG_T5AI_I2C_PROFILE
void t5ai_i2c_profile_dump(void);
#endif
