/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

#include <pbl/drivers/gpio.h>

typedef struct {
  struct pbl_gpio charge;
  struct pbl_irq_spec interrupt;
  uint8_t channel, samples;
  uint16_t cal_low, cal_span;
  uint16_t divider_numerator, divider_denominator;
} T5AIBatteryConfig;
extern const T5AIBatteryConfig T5AI_BATTERY_CONFIG;
