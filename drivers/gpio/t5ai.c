/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/gpio/t5ai_legacy.h>

#include "system/passert.h"

#include <cmsis_core.h>
#include <stdint.h>

// BK7258 AON GPIO register layout from the pinned T5AI BSP. Each pin has one
// configuration register; output-enable is active low.
#define T5AI_GPIO_INPUT (1u << 0)

#define T5AI_GPIO_BASE 0x44000400u
#define T5AI_GPIO_COUNT 56u

#define T5AI_GPIO_OUTPUT (1u << 1)
#define T5AI_GPIO_INPUT_ENABLE (1u << 2)
#define T5AI_GPIO_OUTPUT_DISABLE (1u << 3)
#define T5AI_GPIO_PULL_UP (1u << 4)
#define T5AI_GPIO_PULL_ENABLE (1u << 5)
#define T5AI_GPIO_SECOND_FUNCTION (1u << 6)

static uint64_t s_open_drain_pins;

static volatile uint32_t *prv_pin_register(uint8_t pin) {
  PBL_ASSERTN(pin < T5AI_GPIO_COUNT);
  return (volatile uint32_t *)(T5AI_GPIO_BASE + ((uintptr_t)pin * sizeof(uint32_t)));
}

static uint32_t prv_lock(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void prv_unlock(uint32_t primask) {
  if (primask == 0u) {
    __enable_irq();
  }
}

void gpio_output_init(const OutputConfig *pin_config, GPIOOType_TypeDef otype) {
  volatile uint32_t *reg = prv_pin_register(pin_config->pin);
  const uint64_t pin_mask = UINT64_C(1) << pin_config->pin;
  // A deasserted active-low output is physically high; an open-drain output
  // represents that state by releasing the line.
  const bool physical_high = !pin_config->active_high;
  const uint32_t primask = prv_lock();
  uint32_t value = *reg;

  value &= ~(T5AI_GPIO_OUTPUT | T5AI_GPIO_INPUT_ENABLE |
             T5AI_GPIO_PULL_UP | T5AI_GPIO_PULL_ENABLE |
             T5AI_GPIO_SECOND_FUNCTION);
  if (otype == GPIO_OType_OD) {
    s_open_drain_pins |= pin_mask;
    value |= T5AI_GPIO_INPUT_ENABLE;
    if (physical_high) {
      value |= T5AI_GPIO_OUTPUT_DISABLE;
    } else {
      value &= ~T5AI_GPIO_OUTPUT_DISABLE;
    }
  } else {
    s_open_drain_pins &= ~pin_mask;
    if (physical_high) {
      value |= T5AI_GPIO_OUTPUT;
    }
    value &= ~T5AI_GPIO_OUTPUT_DISABLE;
  }
  *reg = value;
  prv_unlock(primask);
}

void gpio_output_set(const OutputConfig *pin_config, bool asserted) {
  volatile uint32_t *reg = prv_pin_register(pin_config->pin);
  const uint64_t pin_mask = UINT64_C(1) << pin_config->pin;
  const bool physical_high = pin_config->active_high ? asserted : !asserted;
  const uint32_t primask = prv_lock();
  uint32_t value = *reg;

  if ((s_open_drain_pins & pin_mask) != 0u) {
    value &= ~T5AI_GPIO_OUTPUT;
    if (physical_high) {
      value |= T5AI_GPIO_OUTPUT_DISABLE;
    } else {
      value &= ~T5AI_GPIO_OUTPUT_DISABLE;
    }
  } else {
    if (physical_high) {
      value |= T5AI_GPIO_OUTPUT;
    } else {
      value &= ~T5AI_GPIO_OUTPUT;
    }
    value &= ~T5AI_GPIO_OUTPUT_DISABLE;
  }
  *reg = value;
  prv_unlock(primask);
}

void gpio_input_init_pull_up_down(const InputConfig *input_cfg,
                                  GPIOPuPd_TypeDef pupd) {
  volatile uint32_t *reg = prv_pin_register(input_cfg->pin);
  const uint32_t primask = prv_lock();
  uint32_t value = *reg;

  value &= ~(T5AI_GPIO_PULL_UP | T5AI_GPIO_PULL_ENABLE |
             T5AI_GPIO_SECOND_FUNCTION);
  value |= T5AI_GPIO_INPUT_ENABLE | T5AI_GPIO_OUTPUT_DISABLE;
  if (pupd == GPIO_PuPd_UP) {
    value |= T5AI_GPIO_PULL_UP | T5AI_GPIO_PULL_ENABLE;
  } else if (pupd == GPIO_PuPd_DOWN) {
    value |= T5AI_GPIO_PULL_ENABLE;
  }
  *reg = value;
  s_open_drain_pins &= ~(UINT64_C(1) << input_cfg->pin);
  prv_unlock(primask);
}

void gpio_input_init(const InputConfig *input_cfg) {
  gpio_input_init_pull_up_down(input_cfg, GPIO_PuPd_NOPULL);
}

bool gpio_input_read(const InputConfig *input_cfg) {
  return (*prv_pin_register(input_cfg->pin) & T5AI_GPIO_INPUT) != 0u;
}
