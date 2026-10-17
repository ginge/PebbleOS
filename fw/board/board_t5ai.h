/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "display.h"
#include <pbl/drivers/button_id.h>
#include <stdbool.h>
#include <stdint.h>

#define IRQ_PRIORITY_INVALID (1 << __NVIC_PRIO_BITS)
#define GPIO_Port_NULL NULL
#define GPIO_Pin_NULL 0U

#define IRQ_MAP(irq, handler, device) \
  void irq##_IRQHandler(void) { handler(device); }

typedef struct {
  uintptr_t port;
  uint8_t pin;
  uint8_t pull;
} ExtiConfig;
typedef enum {
  GPIO_OType_PP,
  GPIO_OType_OD,
} GPIOOType_TypeDef;
typedef enum {
  GPIO_PuPd_NOPULL,
  GPIO_PuPd_UP,
  GPIO_PuPd_DOWN,
} GPIOPuPd_TypeDef;
typedef struct {
  uint8_t pin;
} InputConfig;
typedef struct {
  const char *name;
  uint8_t pin;
  uint8_t pull;
  bool active_high;
} ButtonConfig;
typedef struct {
  ButtonConfig buttons[NUM_BUTTONS];
} BoardConfigButton;
typedef struct {
  uint8_t pin;
  bool active_high;
} OutputConfig;
typedef struct {
  const OutputConfig ctl;
} BoardConfigActuator;
typedef struct {
  ExtiConfig pmic_int;
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
typedef const struct I2CBus I2CBus;
typedef const struct I2CSlavePort I2CSlavePort;
#include <pbl/drivers/i2c/t5ai_legacy_definitions.h>
#include <pbl/drivers/i2c/t5ai.h>
typedef const struct MicDevice MicDevice;
typedef const struct AudioDevice AudioDevice;
typedef const struct HRMDevice HRMDevice;
typedef struct ADC_TypeDef ADC_TypeDef;

void board_early_init(void);
void board_init(void);
#include "board_definitions.h"
