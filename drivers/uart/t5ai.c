/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
#include <pbl/soc/t5ai/sleep.h>
#endif

#include <errno.h>

#include <pbl/drivers/uart/t5ai.h>
#include <pbl/soc/t5ai/core.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/util/misc.h>

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
  UART_INT_TX_FINISH = 1u << 5,
  UART_INT_RX_FINISH = 1u << 6,
};

#define UART_GLOBAL_RESET_RELEASE     (1u << 0)
#define UART_GLOBAL_CLOCK_GATE_BYPASS (1u << 1)
#define UART_TX_DRAIN_TIMEOUT_US      100000u
#define UART_FIFO_TIMEOUT_US          1000000u
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

// AON bounds wall time; the iteration limit also handles a stopped AON clock.
static bool prv_wait_tx(const struct pbl_uart_t5ai *dev, uint32_t fifo_mask, bool finish,
                        uint32_t timeout_us) {
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t ticks = ((uint64_t)timeout_us * T5AI_AON_RTC_HZ + 999999u) / 1000000u;
  for (uint32_t waited = 0; waited < timeout_us; ++waited) {
    if ((prv_regs(dev)->fifo_status & fifo_mask) == fifo_mask &&
        (!finish || (prv_regs(dev)->int_status & UART_INT_TX_FINISH))) {
      return true;
    }
    if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) >= ticks) {
      break;
    }
    delay_us(1u);
  }
  return false;
}

static void prv_wait_fault(const struct pbl_uart_t5ai *dev) {
  (void)dev;
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  dev->power->drain_fault = true;
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_fault();
#endif
}

#ifdef CONFIG_T5AI_UART_IDLE_GATING
static void prv_clock_acquire(const struct pbl_uart_t5ai *dev) {
  if (!dev->power->active) {
    prv_regs(dev)->global_ctrl |= UART_GLOBAL_CLOCK_GATE_BYPASS;
    __DSB();
    (void)prv_regs(dev)->global_ctrl;
    delay_us(10u);
    dev->power->active = true;
  }
}

static void prv_clock_release(const struct pbl_uart_t5ai *dev) {
  T5AIUartRegisters *regs = prv_regs(dev);
  if (!dev->power->active || dev->power->tx_pending || dev->power->drain_fault ||
      (regs->config & UART_CONFIG_RX_ENABLE))
    return;
  NVIC_DisableIRQ(dev->irqn);
  regs->int_enable = 0u;
  NVIC_ClearPendingIRQ(dev->irqn);
  dev->power->active = false;
  __DSB();
  regs->global_ctrl &= ~UART_GLOBAL_CLOCK_GATE_BYPASS;
  __DSB();
}

static void prv_drain(const struct pbl_uart_t5ai *dev) {
  if (!dev->power->active)
    return;
  if (dev->power->tx_pending) {
    if (prv_wait_tx(dev, UART_FIFO_TX_EMPTY, true, UART_TX_DRAIN_TIMEOUT_US)) {
      dev->power->tx_pending = false;
    } else {
      dev->power->drain_fault = true;
    }
  }
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  if (dev->power->drain_fault)
    t5ai_sleep_fault();
  if (!dev->power->tx_pending && dev->sleep->tx) {
    dev->sleep->tx = false;
    t5ai_sleep_unblock(T5AISleepOwner_UART);
  }
#endif
  prv_clock_release(dev);
}
#endif

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
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  *dev->power = (struct pbl_uart_t5ai_power_state){.active = true};
#endif
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
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  prv_clock_acquire(dev);
#endif
  prv_regs(dev)->config = UART_CONFIG_TX_ENABLE | UART_CONFIG_DATA_BITS_8 |
                          (uart->state->rx_enabled ? UART_CONFIG_RX_ENABLE : 0U) |
                          ((uint32_t)(divisor - 1U) << UART_BAUD_DIVISOR_SHIFT);
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  prv_clock_release(dev);
#endif
  return 0;
}

static void prv_poll_out(const struct pbl_uart *uart, uint8_t c) {
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  struct pbl_uart_t5ai_sleep_state *sleep = prv_dev(uart)->sleep;
  if (!sleep->tx) {
    t5ai_sleep_block(T5AISleepOwner_UART);
    sleep->tx = true;
  }
#endif
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  prv_clock_acquire(prv_dev(uart));
#endif
  T5AIUartRegisters *regs = prv_regs(prv_dev(uart));
  if (!prv_wait_tx(prv_dev(uart), UART_FIFO_WRITE_READY, false, UART_FIFO_TIMEOUT_US)) {
    prv_wait_fault(prv_dev(uart));
    return; // The void console API cannot report failure; drop this byte.
  }
#if defined(CONFIG_T5AI_UART_IDLE_GATING) || defined(CONFIG_T5AI_RETAINED_SLEEP) || \
    defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  regs->int_status = UART_INT_TX_FINISH;
#endif
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  prv_dev(uart)->power->tx_pending = true;
#endif
  regs->fifo_port = c;
}

static void prv_flush(const struct pbl_uart *uart) {
  const struct pbl_uart_t5ai *dev = prv_dev(uart);
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  if (!dev->power->active)
    return;
#endif
  if (!prv_wait_tx(dev, UART_FIFO_TX_EMPTY, false, UART_FIFO_TIMEOUT_US)) {
    prv_wait_fault(dev);
    return; // Keep clocks and sleep blockers when completion is uncertain.
  }
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  prv_drain(dev);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  // FIFO empty precedes the final stop bit. Hold the blocker until TX_FINISH.
  if (dev->sleep->tx) {
    if (!prv_wait_tx(dev, 0u, true, UART_TX_DRAIN_TIMEOUT_US)) {
      prv_wait_fault(dev);
    } else {
      dev->sleep->tx = false;
      t5ai_sleep_unblock(T5AISleepOwner_UART);
    }
  }
#endif
}

static int prv_rx_enable(const struct pbl_uart *uart) {
  const struct pbl_uart_t5ai *dev = prv_dev(uart);
  T5AIUartRegisters *regs = prv_regs(dev);
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  prv_clock_acquire(dev);
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  if (!dev->sleep->rx) {
    t5ai_sleep_block(T5AISleepOwner_UART);
    dev->sleep->rx = true;
  }
#endif
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
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  if (dev->sleep->rx) {
    dev->sleep->rx = false;
    t5ai_sleep_unblock(T5AISleepOwner_UART);
  }
#endif
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  prv_drain(dev);
#endif
}

void pbl_uart_t5ai_irq_handler(const struct pbl_uart_t5ai *dev) {
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  if (!dev->power->active)
    return;
#endif
  T5AIUartRegisters *regs = prv_regs(dev);
  // Clear only the flags observed on entry, before draining the level-sensitive FIFO.
  const uint32_t status = regs->int_status;
#ifdef CONFIG_T5AI_UART_IDLE_GATING
  regs->int_status = status & ~UART_INT_TX_FINISH;
#else
  regs->int_status = status;
#endif
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
