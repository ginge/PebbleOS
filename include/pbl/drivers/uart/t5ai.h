/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <pbl/drivers/uart.h>
typedef struct UARTDeviceState {
  UARTRXInterruptHandler rx_irq_handler;
  UARTTXInterruptHandler tx_irq_handler;
  bool initialized;
  bool rx_int_enabled;
  bool tx_int_enabled;
} UARTDeviceState;
struct UARTDevice {
  UARTDeviceState *state;
  uintptr_t base;
  int irqn;
  uint8_t irq_priority;
  uint8_t tx_pin;
  uint8_t rx_pin;
  uint32_t clock_hz;
  uint32_t baud_rate;
};
void uart_irq_handler(UARTDevice *dev);
