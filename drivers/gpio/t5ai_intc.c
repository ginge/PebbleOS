/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/assert.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/gpio/t5ai.h>
#include <pbl/kernel/irq.h>
#include <pbl/soc/t5ai/interrupt_profile.h>

#include <cmsis_core.h>

#define T5AI_GPIO_INPUT                (1u << 0)
#define T5AI_GPIO_INTERRUPT_TYPE_SHIFT 10u
#define T5AI_GPIO_STATUS_BANK_BITS     32u
#define T5AI_GPIO_IRQ_PRIORITY         6u

#define T5AI_GPIO_BASE         0x44000400u
#define T5AI_GPIO_COUNT        56u
#define T5AI_GPIO_STATUS_0_31  (*(volatile uint32_t *)(T5AI_GPIO_BASE + 0x100u))
#define T5AI_GPIO_STATUS_32_55 (*(volatile uint32_t *)(T5AI_GPIO_BASE + 0x104u))

#define T5AI_GPIO_INTERRUPT_TYPE_MASK (3u << T5AI_GPIO_INTERRUPT_TYPE_SHIFT)
#define T5AI_GPIO_INTERRUPT_ENABLE    (1u << 12)
#define T5AI_GPIO_INTERRUPT_CLEAR     (1u << 13)

#define T5AI_GPIO_INT_RISING_EDGE  2u
#define T5AI_GPIO_INT_FALLING_EDGE 3u

#define T5AI_SYSTEM_CPU1_INT_32_63_ENABLE (*(volatile uint32_t *)0x4401008cu)
#define T5AI_SYSTEM_GPIO_INTERRUPT_ENABLE (1u << 23)

static volatile uint32_t *prv_pin_register(uint8_t pin) {
  PBL_ASSERTN(pin < T5AI_GPIO_COUNT);
  return (volatile uint32_t *)(T5AI_GPIO_BASE + ((uintptr_t)pin * sizeof(uint32_t)));
}

static void prv_clear_pending(uint8_t pin) {
  /* BK7258 clears a channel by writing its self-clearing gpio_int_clear bit.
   * The aggregate status words are snapshots, not the channel ACK path. */
  *prv_pin_register(pin) |= T5AI_GPIO_INTERRUPT_CLEAR;
}

static uint32_t prv_interrupt_type(uint8_t pin, uint32_t trigger) {
  if (trigger == PBL_IRQ_TYPE_EDGE_RISING) {
    return T5AI_GPIO_INT_RISING_EDGE;
  }
  if (trigger == PBL_IRQ_TYPE_EDGE_FALLING) {
    return T5AI_GPIO_INT_FALLING_EDGE;
  }

  // Hardware has no both-edge mode. Arm the transition away from the current
  // input level and reverse it after each interrupt.
  return ((*prv_pin_register(pin) & T5AI_GPIO_INPUT) != 0u) ? T5AI_GPIO_INT_FALLING_EDGE
                                                            : T5AI_GPIO_INT_RISING_EDGE;
}

static void prv_set_interrupt_type(uint8_t pin, uint32_t trigger) {
  volatile uint32_t *reg = prv_pin_register(pin);
  uint32_t value = *reg;
  value &= ~T5AI_GPIO_INTERRUPT_TYPE_MASK;
  value |= prv_interrupt_type(pin, trigger) << T5AI_GPIO_INTERRUPT_TYPE_SHIFT;
  *reg = value;
}

static int prv_configure(const struct pbl_intc *intc, uint32_t pin, uint32_t type) {
  if (pin >= T5AI_GPIO_COUNT)
    return -EINVAL;
  if (type != PBL_IRQ_TYPE_EDGE_RISING && type != PBL_IRQ_TYPE_EDGE_FALLING &&
      type != PBL_IRQ_TYPE_EDGE_BOTH)
    return -ENOTSUP;
  *prv_pin_register(pin) &= ~T5AI_GPIO_INTERRUPT_ENABLE;
  prv_set_interrupt_type(pin, type);
  prv_clear_pending(pin);
  return 0;
}

static int prv_enable(const struct pbl_intc *intc, uint32_t pin) {
  if (pin >= T5AI_GPIO_COUNT)
    return -EINVAL;
  prv_set_interrupt_type(pin, intc->lines[pin].type);
  prv_clear_pending(pin);
  *prv_pin_register(pin) |= T5AI_GPIO_INTERRUPT_ENABLE;
  return 0;
}

static int prv_disable(const struct pbl_intc *intc, uint32_t pin) {
  if (pin >= T5AI_GPIO_COUNT)
    return -EINVAL;
  *prv_pin_register(pin) &= ~T5AI_GPIO_INTERRUPT_ENABLE;
  prv_clear_pending(pin);
  return 0;
}

static int prv_init(const struct pbl_device *dev) {
  for (unsigned pin = 0; pin < T5AI_GPIO_COUNT; ++pin) {
    *prv_pin_register(pin) &= ~T5AI_GPIO_INTERRUPT_ENABLE;
    prv_clear_pending(pin);
  }
  T5AI_SYSTEM_CPU1_INT_32_63_ENABLE |= T5AI_SYSTEM_GPIO_INTERRUPT_ENABLE;
  NVIC_ClearPendingIRQ(GPIO_IRQn);
  NVIC_SetPriority(GPIO_IRQn, T5AI_GPIO_IRQ_PRIORITY);
  NVIC_EnableIRQ(GPIO_IRQn);
  return 0;
}

void pbl_gpio_t5ai_irq_handler(void) {
  const uint32_t irq_start = t5ai_interrupt_profile_irq_enter();
  const uint32_t low = T5AI_GPIO_STATUS_0_31;
  const uint32_t high = T5AI_GPIO_STATUS_32_55;
  for (unsigned pin = 0; pin < T5AI_GPIO_COUNT; ++pin) {
    if (!((pin < 32 ? low : high) & (1u << (pin % 32))))
      continue;
    prv_clear_pending(pin);
    const struct pbl_intc_line *line = &T5AI_GPIO_INTC.lines[pin];
    if (!line->enabled) {
      (void)prv_disable(&T5AI_GPIO_INTC, pin);
      continue;
    }
    if (line->type == PBL_IRQ_TYPE_EDGE_BOTH)
      prv_set_interrupt_type(pin, line->type);
    pbl_intc_dispatch(&T5AI_GPIO_INTC, pin);
  }
  t5ai_interrupt_profile_irq_exit(1, irq_start);
}

static const struct pbl_intc_ops s_ops = {
  .configure = prv_configure,
  .enable = prv_enable,
  .disable = prv_disable,
};
PBL_INTC_STATE_DEFINE(T5AI_GPIO_INTC, T5AI_GPIO_COUNT);
const struct pbl_intc T5AI_GPIO_INTC =
    PBL_INTC_INIT(T5AI_GPIO_INTC, "gpio-irq", prv_init, &T5AI_GPIO.dev, &s_ops);
PBL_DEVICE_REGISTER(T5AI_GPIO_INTC, &T5AI_GPIO_INTC.dev);
PBL_IRQ_CONNECT(GPIO, 6, pbl_gpio_t5ai_irq_handler, , 0);
