/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/uart/t5ai.h>
#include "FreeRTOS.h"
#include "system/passert.h"

typedef volatile struct {
  uint32_t dev_id, dev_version, global_ctrl, dev_status, config, fifo_config;
  uint32_t fifo_status, fifo_port, int_enable, int_status;
} T5AIUartRegisters;

enum {
  UART_CONFIG_TX_ENABLE = 1u << 0,
  UART_CONFIG_RX_ENABLE = 1u << 1,
  UART_CONFIG_DATA_BITS_8 = 3u << 3,
  UART_FIFO_TX_EMPTY = 1u << 17,
  UART_FIFO_RX_EMPTY = 1u << 19,
  UART_FIFO_WRITE_READY = 1u << 20,
  UART_INT_TX_READY = 1u << 0,
  UART_INT_RX_READY = 1u << 1,
  UART_INT_RX_OVERFLOW = 1u << 2,
  UART_INT_PARITY_ERROR = 1u << 3,
  UART_INT_STOP_ERROR = 1u << 4,
};

static T5AIUartRegisters *prv_regs(UARTDevice *dev) {
  return (T5AIUartRegisters *)dev->base;
}

static void prv_configure_pins(UARTDevice *dev) {
  volatile uint32_t *gpio = (volatile uint32_t *)0x44000400u;
  const uint32_t input_enable = 1u << 2;
  const uint32_t pull_up = (1u << 4) | (1u << 5);
  const uint32_t second_function = 1u << 6;
  gpio[dev->tx_pin] = (gpio[dev->tx_pin] & ~0x3fffu) | pull_up | second_function;
  gpio[dev->rx_pin] = (gpio[dev->rx_pin] & ~0x3fffu) | input_enable | pull_up | second_function;
}

static void prv_init(UARTDevice *dev, bool tx, bool rx) {
  PBL_ASSERTN(dev && dev->state && dev->clock_hz && dev->baud_rate);
  T5AIUartRegisters *uart = prv_regs(dev);
  prv_configure_pins(dev);
  uart->global_ctrl = 1u << 1;
  uart->int_enable = 0;
  uart->int_status = 0xffu;
  uart->fifo_config = 0;
  const uint32_t divisor = (dev->clock_hz + dev->baud_rate / 2u) / dev->baud_rate;
  PBL_ASSERTN(divisor >= 5u && divisor <= 65536u);
  uart->config = (tx ? UART_CONFIG_TX_ENABLE : 0u) | (rx ? UART_CONFIG_RX_ENABLE : 0u) |
                 UART_CONFIG_DATA_BITS_8 | ((divisor - 1u) << 8);
  dev->state->initialized = true;
}

void uart_init(UARTDevice *dev) {
  prv_init(dev, true, true);
}
void uart_init_tx_only(UARTDevice *dev) {
  prv_init(dev, true, false);
}
void uart_init_rx_only(UARTDevice *dev) {
  prv_init(dev, false, true);
}
void uart_init_open_drain(UARTDevice *dev) {
  (void)dev;
  PBL_ASSERTN(false);
}
void uart_deinit(UARTDevice *dev) {
  NVIC_DisableIRQ(dev->irqn);
  prv_regs(dev)->int_enable = 0;
  prv_regs(dev)->config = 0;
  dev->state->initialized = false;
}
void uart_set_baud_rate(UARTDevice *dev, uint32_t baud_rate) {
  PBL_ASSERTN(dev->state->initialized && baud_rate);
  const uint32_t divisor = (dev->clock_hz + baud_rate / 2u) / baud_rate;
  PBL_ASSERTN(divisor >= 5u && divisor <= 65536u);
  T5AIUartRegisters *uart = prv_regs(dev);
  uart->config = (uart->config & ~(0xffffu << 8)) | ((divisor - 1u) << 8);
}
void uart_write_byte(UARTDevice *dev, uint8_t data) {
  while (!uart_is_tx_ready(dev)) {
  }
  prv_regs(dev)->fifo_port = data;
}
uint8_t uart_read_byte(UARTDevice *dev) {
  return (uint8_t)(prv_regs(dev)->fifo_port >> 8);
}
bool uart_is_rx_ready(UARTDevice *dev) {
  return !(prv_regs(dev)->fifo_status & UART_FIFO_RX_EMPTY);
}
bool uart_has_rx_overrun(UARTDevice *dev) {
  return prv_regs(dev)->int_status & UART_INT_RX_OVERFLOW;
}
bool uart_has_rx_framing_error(UARTDevice *dev) {
  return prv_regs(dev)->int_status & UART_INT_STOP_ERROR;
}
bool uart_is_tx_ready(UARTDevice *dev) {
  return prv_regs(dev)->fifo_status & UART_FIFO_WRITE_READY;
}
bool uart_is_tx_complete(UARTDevice *dev) {
  return prv_regs(dev)->fifo_status & UART_FIFO_TX_EMPTY;
}
void uart_wait_for_tx_complete(UARTDevice *dev) {
  while (!uart_is_tx_complete(dev)) {
  }
}
UARTRXErrorFlags uart_has_errored_out(UARTDevice *dev) {
  const uint32_t status = prv_regs(dev)->int_status;
  return (UARTRXErrorFlags){.parity_error = !!(status & UART_INT_PARITY_ERROR),
                            .overrun_error = !!(status & UART_INT_RX_OVERFLOW),
                            .framing_error = !!(status & UART_INT_STOP_ERROR)};
}
void uart_set_rx_interrupt_handler(UARTDevice *dev, UARTRXInterruptHandler handler) {
  PBL_ASSERTN(dev->state->initialized);
  dev->state->rx_irq_handler = handler;
}
void uart_set_tx_interrupt_handler(UARTDevice *dev, UARTTXInterruptHandler handler) {
  PBL_ASSERTN(dev->state->initialized);
  dev->state->tx_irq_handler = handler;
}
static void prv_update_nvic(UARTDevice *dev) {
  if (dev->state->rx_int_enabled || dev->state->tx_int_enabled) {
    NVIC_SetPriority(dev->irqn, dev->irq_priority);
    NVIC_EnableIRQ(dev->irqn);
  } else {
    NVIC_DisableIRQ(dev->irqn);
  }
}
void uart_set_rx_interrupt_enabled(UARTDevice *dev, bool enabled) {
  const uint32_t mask =
      UART_INT_RX_READY | UART_INT_RX_OVERFLOW | UART_INT_PARITY_ERROR | UART_INT_STOP_ERROR;
  dev->state->rx_int_enabled = enabled;
  if (enabled)
    prv_regs(dev)->int_enable |= mask;
  else
    prv_regs(dev)->int_enable &= ~mask;
  prv_update_nvic(dev);
}
void uart_set_tx_interrupt_enabled(UARTDevice *dev, bool enabled) {
  dev->state->tx_int_enabled = enabled;
  if (enabled)
    prv_regs(dev)->int_enable |= UART_INT_TX_READY;
  else
    prv_regs(dev)->int_enable &= ~UART_INT_TX_READY;
  prv_update_nvic(dev);
}
void uart_clear_all_interrupt_flags(UARTDevice *dev) {
  prv_regs(dev)->int_status = 0xffu;
}
void uart_irq_handler(UARTDevice *dev) {
  bool switch_required = false;
  const UARTRXErrorFlags errors = uart_has_errored_out(dev);
  while (dev->state->rx_int_enabled && uart_is_rx_ready(dev)) {
    const uint8_t byte = uart_read_byte(dev);
    if (dev->state->rx_irq_handler && dev->state->rx_irq_handler(dev, byte, &errors))
      switch_required = true;
  }
  if (dev->state->tx_int_enabled && uart_is_tx_ready(dev) && dev->state->tx_irq_handler &&
      dev->state->tx_irq_handler(dev))
    switch_required = true;
  uart_clear_all_interrupt_flags(dev);
  portEND_SWITCHING_ISR(switch_required);
}
void uart_start_rx_dma(UARTDevice *dev, void *buffer, uint32_t length) {
  (void)dev;
  (void)buffer;
  (void)length;
  PBL_ASSERTN(false);
}
void uart_stop_rx_dma(UARTDevice *dev) {
  (void)dev;
}
void uart_clear_rx_dma_buffer(UARTDevice *dev) {
  (void)dev;
}
