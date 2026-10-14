/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "definitions.h"

#include <stdint.h>

typedef const struct I2CBusHal {
  uintptr_t base;
  int32_t irqn;
  uint32_t frequency;
  uint8_t scl_pin;
  uint8_t sda_pin;
} I2CBusHal;

void i2c_irq_handler(I2CBus *bus);

#ifdef CONFIG_T5AI_I2C_PROFILE
// Called from thread context by the QMI profile window, never from the ISR.
void t5ai_i2c_profile_dump(void);
#endif
