/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pbl/drivers/pinctrl.h>
#include <pbl/drivers/uart.h>

struct pbl_uart_t5ai {
  struct pbl_uart uart;
  uintptr_t base;
  int32_t irqn;
  uint8_t irq_priority;
  struct pbl_pinctrl pinctrl;
  uint32_t clock_hz;
};

extern const struct pbl_uart_ops pbl_uart_t5ai_ops;
void pbl_uart_t5ai_irq_handler(const struct pbl_uart_t5ai *dev);

#define PBL_UART_T5AI_DEFINE(sym, _base, _irqn, _prio, _pins, _clock, _baudrate) \
  PBL_UART_STATE_DEFINE(sym);                                                    \
  const struct pbl_uart_t5ai sym = {                                             \
    .uart = PBL_UART_INIT(sym, "uart0", &pbl_uart_t5ai_ops, _baudrate, nullptr), \
    .base = (_base),                                                             \
    .irqn = (_irqn),                                                             \
    .irq_priority = (_prio),                                                     \
    .pinctrl = PBL_PINCTRL(_pins, nullptr, _base),                               \
    .clock_hz = (_clock),                                                        \
  };                                                                             \
  PBL_DEVICE_REGISTER(sym, &sym.uart.dev)
