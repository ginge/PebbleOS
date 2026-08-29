/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/exti.h>
#include <pbl/drivers/gpio.h>

#include "kernel/events.h"
#include "system/passert.h"

#include <cmsis_core.h>
#include <stdbool.h>
#include <stdint.h>

#define T5AI_GPIO_BASE 0x44000400u
#define T5AI_GPIO_COUNT 56u
#define T5AI_GPIO_STATUS_0_31 (*(volatile uint32_t *)(T5AI_GPIO_BASE + 0x100u))
#define T5AI_GPIO_STATUS_32_55 (*(volatile uint32_t *)(T5AI_GPIO_BASE + 0x104u))

#define T5AI_GPIO_INTERRUPT_TYPE_MASK (3u << 10)
#define T5AI_GPIO_INTERRUPT_ENABLE (1u << 12)

#define T5AI_GPIO_INT_LOW_LEVEL 0u
#define T5AI_GPIO_INT_HIGH_LEVEL 1u
#define T5AI_GPIO_INT_RISING_EDGE 2u
#define T5AI_GPIO_INT_FALLING_EDGE 3u

#define T5AI_SYSTEM_CPU0_INT_32_63_ENABLE (*(volatile uint32_t *)0x44010084u)
#define T5AI_SYSTEM_GPIO_INTERRUPT_ENABLE (1u << 23)

typedef struct {
  ExtiHandlerCallback callback;
  ExtiTrigger trigger;
} T5AIExtiHandler;

static T5AIExtiHandler s_handlers[T5AI_GPIO_COUNT];

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

static void prv_clear_pending(uint8_t pin) {
  if (pin < 32u) {
    T5AI_GPIO_STATUS_0_31 = 1u << pin;
  } else {
    T5AI_GPIO_STATUS_32_55 = 1u << (pin - 32u);
  }
}

static uint32_t prv_interrupt_type(uint8_t pin, ExtiTrigger trigger) {
  if (trigger == ExtiTrigger_Rising) {
    return T5AI_GPIO_INT_RISING_EDGE;
  }
  if (trigger == ExtiTrigger_Falling) {
    return T5AI_GPIO_INT_FALLING_EDGE;
  }

  // Hardware has no both-edge mode. Arm the transition away from the current
  // input level and reverse it after each interrupt.
  return ((*prv_pin_register(pin) & 1u) != 0u) ?
      T5AI_GPIO_INT_FALLING_EDGE : T5AI_GPIO_INT_RISING_EDGE;
}

static void prv_set_interrupt_type(uint8_t pin, ExtiTrigger trigger) {
  volatile uint32_t *reg = prv_pin_register(pin);
  uint32_t value = *reg;
  value &= ~T5AI_GPIO_INTERRUPT_TYPE_MASK;
  value |= prv_interrupt_type(pin, trigger) << 10;
  *reg = value;
}

void exti_configure_pin(ExtiConfig cfg, ExtiTrigger trigger,
                        ExtiHandlerCallback cb) {
  PBL_ASSERTN(cb != NULL);
  PBL_ASSERTN(cfg.pin < T5AI_GPIO_COUNT);

  const InputConfig input = { .pin = cfg.pin };
  gpio_input_init_pull_up_down(&input, (GPIOPuPd_TypeDef)cfg.pull);

  const uint32_t primask = prv_lock();
  volatile uint32_t *reg = prv_pin_register(cfg.pin);
  *reg &= ~T5AI_GPIO_INTERRUPT_ENABLE;
  s_handlers[cfg.pin].callback = cb;
  s_handlers[cfg.pin].trigger = trigger;
  prv_set_interrupt_type(cfg.pin, trigger);
  prv_clear_pending(cfg.pin);

  T5AI_SYSTEM_CPU0_INT_32_63_ENABLE |= T5AI_SYSTEM_GPIO_INTERRUPT_ENABLE;
  NVIC_ClearPendingIRQ(GPIO_IRQn);
  NVIC_SetPriority(GPIO_IRQn, 6u);
  NVIC_EnableIRQ(GPIO_IRQn);
  prv_unlock(primask);
}

void exti_enable(ExtiConfig cfg) {
  PBL_ASSERTN(cfg.pin < T5AI_GPIO_COUNT);
  PBL_ASSERTN(s_handlers[cfg.pin].callback != NULL);

  const uint32_t primask = prv_lock();
  prv_set_interrupt_type(cfg.pin, s_handlers[cfg.pin].trigger);
  prv_clear_pending(cfg.pin);
  *prv_pin_register(cfg.pin) |= T5AI_GPIO_INTERRUPT_ENABLE;
  prv_unlock(primask);
}

void exti_disable(ExtiConfig cfg) {
  PBL_ASSERTN(cfg.pin < T5AI_GPIO_COUNT);

  const uint32_t primask = prv_lock();
  *prv_pin_register(cfg.pin) &= ~T5AI_GPIO_INTERRUPT_ENABLE;
  prv_clear_pending(cfg.pin);
  prv_unlock(primask);
}

void GPIO_IRQHandler(void) {
  const uint32_t status_low = T5AI_GPIO_STATUS_0_31;
  const uint32_t status_high = T5AI_GPIO_STATUS_32_55;
  bool should_context_switch = false;

  for (uint8_t pin = 0; pin < T5AI_GPIO_COUNT; ++pin) {
    const uint32_t status = pin < 32u ? status_low : status_high;
    const uint8_t bit = pin < 32u ? pin : pin - 32u;
    if ((status & (1u << bit)) == 0u) {
      continue;
    }

    prv_clear_pending(pin);
    T5AIExtiHandler *handler = &s_handlers[pin];
    if (handler->callback == NULL) {
      *prv_pin_register(pin) &= ~T5AI_GPIO_INTERRUPT_ENABLE;
      continue;
    }
    if (handler->trigger == ExtiTrigger_RisingFalling) {
      prv_set_interrupt_type(pin, handler->trigger);
    }
    handler->callback(&should_context_switch);
  }

  portEND_SWITCHING_ISR(should_context_switch);
}
