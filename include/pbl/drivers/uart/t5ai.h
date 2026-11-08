/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pbl/drivers/pinctrl.h>
#include <pbl/drivers/uart.h>

#ifdef CONFIG_T5AI_UART_IDLE_GATING
struct pbl_uart_t5ai_power_state {
  bool active, tx_pending, drain_fault;
};
#define T5AI_UART_POWER_DEFINE(sym) static struct pbl_uart_t5ai_power_state sym##_power;
#define T5AI_UART_POWER_INIT(sym)   .power = &sym##_power,
#else
#define T5AI_UART_POWER_DEFINE(sym)
#define T5AI_UART_POWER_INIT(sym)
#endif

#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
struct pbl_uart_t5ai_sleep_state {
  bool tx, rx;
};
#define T5AI_UART_SLEEP_DEFINE(sym) static struct pbl_uart_t5ai_sleep_state sym##_sleep;
#define T5AI_UART_SLEEP_INIT(sym)   .sleep = &sym##_sleep,
#else
#define T5AI_UART_SLEEP_DEFINE(sym)
#define T5AI_UART_SLEEP_INIT(sym)
#endif

struct pbl_uart_t5ai {
  struct pbl_uart uart;
  uintptr_t base;
  int32_t irqn;
  uint8_t irq_priority;
  struct pbl_pinctrl pinctrl;
  uint32_t clock_hz;
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  struct pbl_uart_t5ai_sleep_state *sleep;
#endif
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  struct pbl_uart_t5ai_power_state *power;
#endif
};

extern const struct pbl_uart_ops pbl_uart_t5ai_ops;
void pbl_uart_t5ai_irq_handler(const struct pbl_uart_t5ai *dev);

#define PBL_UART_T5AI_DEFINE(sym, _base, _irqn, _prio, _pins, _clock, _baudrate) \
  T5AI_UART_SLEEP_DEFINE(sym)                                                    \
  T5AI_UART_POWER_DEFINE(sym)                                                    \
  PBL_UART_STATE_DEFINE(sym);                                                    \
  const struct pbl_uart_t5ai sym = {                                             \
    .uart = PBL_UART_INIT(sym, "uart0", &pbl_uart_t5ai_ops, _baudrate, nullptr), \
    .base = (_base),                                                             \
    .irqn = (_irqn),                                                             \
    .irq_priority = (_prio),                                                     \
    .pinctrl = PBL_PINCTRL(_pins, nullptr, _base),                               \
    T5AI_UART_SLEEP_INIT(sym) T5AI_UART_POWER_INIT(sym).clock_hz = (_clock),     \
  };                                                                             \
  PBL_DEVICE_REGISTER(sym, &sym.uart.dev)
