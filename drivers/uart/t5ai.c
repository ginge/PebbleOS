/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>

#include <pbl/drivers/uart/t5ai.h>
#include <pbl/util/misc.h>

#include <cmsis_core.h>

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

#define UART_GLOBAL_RESET_RELEASE     (1u << 0)
#define UART_GLOBAL_CLOCK_GATE_BYPASS (1u << 1)
#define UART_ALL_INTERRUPT_FLAGS      0xffu
#define UART_BAUD_DIVISOR_SHIFT       8u
#define UART_BAUD_DIVISOR_MASK        (0xffffu << UART_BAUD_DIVISOR_SHIFT)
#define UART_BAUD_DIVISOR_MIN         5u
#define UART_BAUD_DIVISOR_MAX         65536u
#define UART_FIFO_RX_DATA_SHIFT       8u

#define T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE (*(volatile uint32_t *)0x44010080u)
#define T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE (*(volatile uint32_t *)0x44010088u)
#define T5AI_UART0_INTERRUPT_ENABLE       (1u << 4)

static T5AIUartRegisters *prv_regs(const struct pbl_uart_t5ai *dev) {
  return (T5AIUartRegisters *)dev->base;
}

static const struct pbl_uart_t5ai *prv_dev(const struct pbl_uart *uart) {
  return container_of(uart, const struct pbl_uart_t5ai, uart);
}

static int prv_early_init(const struct pbl_uart *uart) {
  const struct pbl_uart_t5ai *dev = prv_dev(uart);
  T5AIUartRegisters *regs = prv_regs(dev);
  NVIC_DisableIRQ(dev->irqn);
  if (dev->pinctrl.states[PBL_PINCTRL_STATE_DEFAULT] != nullptr) {
    const int ret = pbl_pinctrl_select_state(&dev->pinctrl, PBL_PINCTRL_STATE_DEFAULT);
    if (ret != 0) {
      return ret;
    }
  }
  // The vendor HAL asserts bit 0 to release reset, and bit 1 bypasses clock gating.
  regs->global_ctrl = UART_GLOBAL_RESET_RELEASE | UART_GLOBAL_CLOCK_GATE_BYPASS;
  regs->int_enable = 0U;
  regs->int_status = UART_ALL_INTERRUPT_FLAGS;
  regs->fifo_config = 0U;
  regs->config = 0U;
  NVIC_ClearPendingIRQ(dev->irqn);
  return 0;
}

static int prv_configure(const struct pbl_uart *uart, const struct pbl_uart_config *cfg) {
  const struct pbl_uart_t5ai *dev = prv_dev(uart);
  if (cfg->parity != PBL_UART_PARITY_NONE || cfg->flow_ctrl != PBL_UART_FLOW_CTRL_NONE) {
    return -ENOTSUP;
  }
  if (cfg->baudrate == 0U) {
    return -EINVAL;
  }
  const uint64_t divisor = ((uint64_t)dev->clock_hz + cfg->baudrate / 2U) / cfg->baudrate;
  if (divisor < UART_BAUD_DIVISOR_MIN || divisor > UART_BAUD_DIVISOR_MAX) {
    return -ENOTSUP;
  }
  prv_regs(dev)->config = UART_CONFIG_TX_ENABLE | UART_CONFIG_DATA_BITS_8 |
                          (uart->state->rx_enabled ? UART_CONFIG_RX_ENABLE : 0U) |
                          ((uint32_t)(divisor - 1U) << UART_BAUD_DIVISOR_SHIFT);
  return 0;
}

static void prv_poll_out(const struct pbl_uart *uart, uint8_t c) {
  T5AIUartRegisters *regs = prv_regs(prv_dev(uart));
  while ((regs->fifo_status & UART_FIFO_WRITE_READY) == 0U) {
  }
  regs->fifo_port = c;
}

static void prv_flush(const struct pbl_uart *uart) {
  while ((prv_regs(prv_dev(uart))->fifo_status & UART_FIFO_TX_EMPTY) == 0U) {
  }
}

static int prv_rx_enable(const struct pbl_uart *uart) {
  const struct pbl_uart_t5ai *dev = prv_dev(uart);
  T5AIUartRegisters *regs = prv_regs(dev);
  regs->config |= UART_CONFIG_RX_ENABLE;
  regs->int_enable = UART_INT_RX_READY | UART_INT_RX_OVERFLOW | UART_INT_PARITY_ERROR |
                     UART_INT_STOP_ERROR | UART_INT_RX_FINISH;
  T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE |= T5AI_UART0_INTERRUPT_ENABLE;
  T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE |= T5AI_UART0_INTERRUPT_ENABLE;
  NVIC_SetPriority(dev->irqn, dev->irq_priority);
  NVIC_EnableIRQ(dev->irqn);
  return 0;
}

static void prv_rx_disable(const struct pbl_uart *uart) {
  const struct pbl_uart_t5ai *dev = prv_dev(uart);
  NVIC_DisableIRQ(dev->irqn);
  prv_regs(dev)->int_enable = 0U;
  prv_regs(dev)->config &= ~UART_CONFIG_RX_ENABLE;
}

void pbl_uart_t5ai_irq_handler(const struct pbl_uart_t5ai *dev) {
  T5AIUartRegisters *regs = prv_regs(dev);
  // Clear only the flags observed on entry, before draining the level-sensitive FIFO.
  const uint32_t status = regs->int_status;
  regs->int_status = status;
  while (dev->uart.state->rx_enabled && (regs->fifo_status & UART_FIFO_RX_EMPTY) == 0U) {
    const uint8_t c = (uint8_t)(regs->fifo_port >> UART_FIFO_RX_DATA_SHIFT);
    if (!pbl_uart_receive(&dev->uart, c)) {
      break;
    }
  }
}

const struct pbl_uart_ops pbl_uart_t5ai_ops = {
  .init = prv_early_init,
  .early_init = prv_early_init,
  .configure = prv_configure,
  .poll_out = prv_poll_out,
  .flush = prv_flush,
  .rx_enable = prv_rx_enable,
  .rx_disable = prv_rx_disable,
};
