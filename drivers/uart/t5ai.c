/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/uart/t5ai.h>
#include <pbl/kernel/kernel.h>
#include <cmsis_core.h>
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
  UART_INT_RX_FINISH = 1u << 6,
};

#define UART_GLOBAL_RESET_RELEASE (1u << 0)
#define UART_GLOBAL_CLOCK_GATE_BYPASS (1u << 1)
#define UART_ALL_INTERRUPT_FLAGS 0xffu
#define UART_BAUD_DIVISOR_SHIFT 8u
#define UART_BAUD_DIVISOR_MASK (0xffffu << UART_BAUD_DIVISOR_SHIFT)
#define UART_BAUD_DIVISOR_MIN 5u
#define UART_BAUD_DIVISOR_MAX 65536u
#define UART_FIFO_RX_DATA_SHIFT 8u
#define UART_GPIO_BASE ((volatile uint32_t *)0x44000400u)
#define UART_GPIO_FUNCTION_BASE ((volatile uint32_t *)0x440100c0u)
#define UART_GPIO_CONFIG_MASK 0x3fffu
#define UART_GPIO_OUTPUT_DISABLE (1u << 3)
#define UART_GPIO_SECOND_FUNCTION (1u << 6)
// UART0 is mux function zero on GPIO10/11 (word 1, fields 2/3).
#define UART0_GPIO_FUNCTION_WORD 1u
#define UART0_GPIO_FUNCTION_MASK ((0xfu << 8) | (0xfu << 12))

#define T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE (*(volatile uint32_t *)0x44010080u)
#define T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE (*(volatile uint32_t *)0x44010088u)
#define T5AI_UART0_INTERRUPT_ENABLE (1u << 4)

static T5AIUartRegisters *prv_regs(UARTDevice *dev) {
  return (T5AIUartRegisters *)dev->base;
}

static void prv_configure_pins(UARTDevice *dev) {
  volatile uint32_t *gpio = UART_GPIO_BASE;
  volatile uint32_t *const function_select = UART_GPIO_FUNCTION_BASE;
  const uint32_t peripheral_mode = UART_GPIO_OUTPUT_DISABLE | UART_GPIO_SECOND_FUNCTION;

  /* UART0 is function index zero for GPIO10/11. Disable the ordinary GPIO
   * drivers and pulls before handing both pins to the peripheral. */
  function_select[UART0_GPIO_FUNCTION_WORD] &= ~UART0_GPIO_FUNCTION_MASK;
  gpio[dev->tx_pin] = (gpio[dev->tx_pin] & ~UART_GPIO_CONFIG_MASK) | peripheral_mode;
  gpio[dev->rx_pin] = (gpio[dev->rx_pin] & ~UART_GPIO_CONFIG_MASK) | peripheral_mode;
}

static void prv_init(UARTDevice *dev, bool tx, bool rx) {
  PBL_ASSERTN(dev && dev->state && dev->clock_hz && dev->baud_rate);
  T5AIUartRegisters *uart = prv_regs(dev);
  prv_configure_pins(dev);
  /* BK7258 calls bit 0 soft_reset, but the vendor HAL asserts it to release
   * the UART. Bit 1 bypasses the peripheral's local clock gate. */
  uart->global_ctrl = UART_GLOBAL_RESET_RELEASE | UART_GLOBAL_CLOCK_GATE_BYPASS;
  uart->int_enable = 0;
  uart->int_status = UART_ALL_INTERRUPT_FLAGS;
  uart->fifo_config = 0;
  const uint32_t divisor = (dev->clock_hz + dev->baud_rate / 2u) / dev->baud_rate;
  PBL_ASSERTN(divisor >= UART_BAUD_DIVISOR_MIN && divisor <= UART_BAUD_DIVISOR_MAX);
  uart->config = (tx ? UART_CONFIG_TX_ENABLE : 0u) | (rx ? UART_CONFIG_RX_ENABLE : 0u) |
                 UART_CONFIG_DATA_BITS_8 | ((divisor - 1u) << UART_BAUD_DIVISOR_SHIFT);
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
  PBL_ASSERTN(divisor >= UART_BAUD_DIVISOR_MIN && divisor <= UART_BAUD_DIVISOR_MAX);
  T5AIUartRegisters *uart = prv_regs(dev);
  uart->config =
      (uart->config & ~UART_BAUD_DIVISOR_MASK) | ((divisor - 1u) << UART_BAUD_DIVISOR_SHIFT);
}
void uart_write_byte(UARTDevice *dev, uint8_t data) {
  while (!uart_is_tx_ready(dev)) {
  }
  prv_regs(dev)->fifo_port = data;
}
uint8_t uart_read_byte(UARTDevice *dev) {
  return (uint8_t)(prv_regs(dev)->fifo_port >> UART_FIFO_RX_DATA_SHIFT);
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
    T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE |= T5AI_UART0_INTERRUPT_ENABLE;
    T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE |= T5AI_UART0_INTERRUPT_ENABLE;
    NVIC_SetPriority(dev->irqn, dev->irq_priority);
    NVIC_EnableIRQ(dev->irqn);
  } else {
    NVIC_DisableIRQ(dev->irqn);
  }
}
void uart_set_rx_interrupt_enabled(UARTDevice *dev, bool enabled) {
  const uint32_t mask = UART_INT_RX_READY | UART_INT_RX_OVERFLOW | UART_INT_PARITY_ERROR |
                        UART_INT_STOP_ERROR | UART_INT_RX_FINISH;
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
  prv_regs(dev)->int_status = UART_ALL_INTERRUPT_FLAGS;
}
void uart_irq_handler(UARTDevice *dev) {
  const UARTRXErrorFlags errors = uart_has_errored_out(dev);
  while (dev->state->rx_int_enabled && uart_is_rx_ready(dev)) {
    const uint8_t byte = uart_read_byte(dev);
    if (dev->state->rx_irq_handler) dev->state->rx_irq_handler(dev, byte, &errors);
  }
  if (dev->state->tx_int_enabled && uart_is_tx_ready(dev) && dev->state->tx_irq_handler)
    dev->state->tx_irq_handler(dev);
  uart_clear_all_interrupt_flags(dev);
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
