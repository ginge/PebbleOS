/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system/passert.h"

#include <errno.h>
#include <stdint.h>

#include <pbl/drivers/gpio/t5ai.h>

#include <cmsis_core.h>

// BK7258 AON GPIO register layout from the pinned T5AI BSP. Each pin has one
// configuration register; output-enable is active low.
#define T5AI_GPIO_INPUT (1u << 0)

#define T5AI_GPIO_BASE  0x44000400u
#define T5AI_GPIO_COUNT 56u

#define T5AI_GPIO_OUTPUT          (1u << 1)
#define T5AI_GPIO_INPUT_ENABLE    (1u << 2)
#define T5AI_GPIO_OUTPUT_DISABLE  (1u << 3)
#define T5AI_GPIO_PULL_UP         (1u << 4)
#define T5AI_GPIO_PULL_ENABLE     (1u << 5)
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

static int prv_configure(const struct pbl_gpio_port *port, uint8_t pin, uint32_t flags) {
  if (pin >= T5AI_GPIO_COUNT || ((flags & PBL_GPIO_INPUT) && (flags & PBL_GPIO_OUTPUT)) ||
      ((flags & PBL_GPIO_PULL_UP) && (flags & PBL_GPIO_PULL_DOWN))) {
    return -EINVAL;
  }
  if (!(flags & (PBL_GPIO_INPUT | PBL_GPIO_OUTPUT))) {
    return -ENOTSUP;
  }
  volatile uint32_t *reg = prv_pin_register(pin);
  const uint64_t mask = UINT64_C(1) << pin;
  const uint32_t primask = prv_lock();
  uint32_t value = *reg;
  bool high = (value & T5AI_GPIO_OUTPUT) != 0;
  if (s_open_drain_pins & mask) {
    high = (value & T5AI_GPIO_OUTPUT_DISABLE) != 0;
  }
  if (flags & PBL_GPIO_OUTPUT_INIT_HIGH)
    high = true;
  if (flags & PBL_GPIO_OUTPUT_INIT_LOW)
    high = false;
  value &= ~(T5AI_GPIO_PULL_UP | T5AI_GPIO_PULL_ENABLE | T5AI_GPIO_SECOND_FUNCTION);
  if (flags & PBL_GPIO_PULL_UP)
    value |= T5AI_GPIO_PULL_UP | T5AI_GPIO_PULL_ENABLE;
  if (flags & PBL_GPIO_PULL_DOWN)
    value |= T5AI_GPIO_PULL_ENABLE;
  s_open_drain_pins &= ~mask;
  if (flags & PBL_GPIO_INPUT) {
    value |= T5AI_GPIO_INPUT_ENABLE | T5AI_GPIO_OUTPUT_DISABLE;
  } else {
    value &= ~(T5AI_GPIO_OUTPUT | T5AI_GPIO_INPUT_ENABLE | T5AI_GPIO_OUTPUT_DISABLE);
    if (flags & PBL_GPIO_OPEN_DRAIN) {
      s_open_drain_pins |= mask;
      value |= T5AI_GPIO_INPUT_ENABLE;
      if (high)
        value |= T5AI_GPIO_OUTPUT_DISABLE;
    } else if (high) {
      value |= T5AI_GPIO_OUTPUT;
    }
  }
  *reg = value;
  prv_unlock(primask);
  return 0;
}

static int prv_set(const struct pbl_gpio_port *port, uint8_t pin, bool high) {
  if (pin >= T5AI_GPIO_COUNT)
    return -EINVAL;
  const uint32_t primask = prv_lock();
  volatile uint32_t *reg = prv_pin_register(pin);
  uint32_t value = *reg;
  if (s_open_drain_pins & (UINT64_C(1) << pin)) {
    value &= ~T5AI_GPIO_OUTPUT;
    if (high)
      value |= T5AI_GPIO_OUTPUT_DISABLE;
    else
      value &= ~T5AI_GPIO_OUTPUT_DISABLE;
  } else {
    if (high)
      value |= T5AI_GPIO_OUTPUT;
    else
      value &= ~T5AI_GPIO_OUTPUT;
    value &= ~T5AI_GPIO_OUTPUT_DISABLE;
  }
  *reg = value;
  prv_unlock(primask);
  return 0;
}

static int prv_get(const struct pbl_gpio_port *port, uint8_t pin) {
  if (pin >= T5AI_GPIO_COUNT)
    return -EINVAL;
  return (*prv_pin_register(pin) & T5AI_GPIO_INPUT) != 0;
}

static const struct pbl_gpio_port_ops s_ops = {
  .configure = prv_configure,
  .get = prv_get,
  .set = prv_set,
};
PBL_DEVICE_STATE_DEFINE(T5AI_GPIO);
const struct pbl_gpio_port T5AI_GPIO = PBL_GPIO_PORT_INIT(T5AI_GPIO, "gpio", nullptr, nullptr,
                                                          &s_ops, T5AI_GPIO_COUNT, &T5AI_GPIO_INTC);
PBL_DEVICE_REGISTER(T5AI_GPIO, &T5AI_GPIO.dev);
