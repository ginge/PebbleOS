/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "display.h"

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/button_id.h>

#define IRQ_PRIORITY_INVALID (1 << __NVIC_PRIO_BITS)
#define GPIO_Port_NULL       NULL
#define GPIO_Pin_NULL        0U

#define IRQ_MAP(irq, handler, device) \
  void irq##_IRQHandler(void) {       \
    handler(device);                  \
  }

#include <pbl/drivers/gpio/t5ai.h>
#include <pbl/drivers/i2c/t5ai.h>

typedef struct {
  const char *name;
  struct pbl_gpio gpio;
} ButtonConfig;
typedef struct {
  ButtonConfig buttons[NUM_BUTTONS];
} BoardConfigButton;
typedef struct {
  struct pbl_gpio ctl;
} BoardConfigActuator;
typedef struct {
  struct pbl_irq_spec pmic_int;
  uint8_t low_power_threshold;
  uint16_t battery_capacity_hours;
} BoardConfigPower;
typedef struct {
  uint8_t default_motion_sensitivity;
} BoardConfigAccel;
typedef struct {
  int axes_offsets[3];
  bool axes_inverts[3];
} MagConfig;
typedef struct {
  const MagConfig mag_config;
} BoardConfigMag;
typedef struct {
  uint8_t backlight_on_percent;
  uint32_t ambient_light_dark_threshold;
  uint32_t ambient_k_delta_threshold;
  uint32_t ambient_light_lux_dark_offset;
  uint32_t ambient_light_lux_num;
  uint32_t ambient_light_lux_den;
} BoardConfig;

#include <pbl/drivers/uart/t5ai.h>
typedef const struct UARTDevice UARTDevice;
typedef const struct MicDevice MicDevice;
typedef const struct AudioDevice AudioDevice;
typedef const struct HRMDevice HRMDevice;
typedef struct ADC_TypeDef ADC_TypeDef;

void board_early_init(void);
void board_init(void);
#include "board_definitions.h"
