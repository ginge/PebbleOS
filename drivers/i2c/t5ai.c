/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>

#include <pbl/drivers/i2c/t5ai.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/irq.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/util/misc.h>

#include <cmsis_core.h>

typedef volatile struct {
  uint32_t device_id;
  uint32_t version_id;
  uint32_t global_control;
  uint32_t device_status;
  uint32_t config;
  uint32_t status;
  uint32_t data;
  uint32_t extended_config;
} T5AII2CRegisters;

// BK7258 I2C0 clock, register fields and pin mux from the pinned BSP.
#define T5AI_I2C0_BASE                     0x45850000u
#define T5AI_I2C_SOURCE_HZ                 26000000u
#define T5AI_I2C_GLOBAL_RESET_RELEASE      (1u << 0)
#define T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS  (1u << 1)
#define T5AI_I2C_CLOCK_ENABLE              (1u << 0)
#define T5AI_I2C_BACKUP_POWER_DOWN         (1u << 4)
#define T5AI_I2C0_INTERRUPT_ENABLE         (1u << 6)
#define T5AI_I2C_IRQ_PRIORITY              6u
#define T5AI_I2C_DIVIDER_MAX               0x3ffu
#define T5AI_I2C_IDLE_CONTROL              (3u << 0)
#define T5AI_I2C_SCL_CONTROL               (4u << 3)
#define T5AI_I2C_CLOCK_SOURCE              (3u << 26)
#define T5AI_I2C_EXT_DATA_OUTPUT_ENABLE    (1u << 0)
#define T5AI_I2C_EXT_ADDRESS_OUTPUT_ENABLE (1u << 1)
#define T5AI_I2C_ENABLE_SETTLE_US          5u
#define T5AI_I2C_STOP_TIMEOUT_US           200u
#define T5AI_I2C_BUS_FREE_US               5u
#define T5AI_I2C_RECOVERY_CLOCKS           9u
#define T5AI_I2C_RECOVERY_STRETCH_US       100u
// Vendor SCL formula: source clocks = 3 * (divider + 1) + 6.
#define T5AI_I2C_SCL_FIXED_CLOCKS   6u
#define T5AI_I2C_SCL_DIVIDER_CLOCKS 3u

#define T5AI_I2C_STATUS_INTERRUPT     (1u << 0)
#define T5AI_I2C_STATUS_TIMEOUT       (1u << 1)
#define T5AI_I2C_STATUS_ARB_LOST      (1u << 3)
#define T5AI_I2C_STATUS_INT_MODE_MASK (3u << 6)
#define T5AI_I2C_STATUS_ACK           (1u << 8)
#define T5AI_I2C_STATUS_STOP          (1u << 9)
#define T5AI_I2C_STATUS_START         (1u << 10)
#define T5AI_I2C_STATUS_BUSY          (1u << 15)

#define T5AI_I2C_CONFIG_FREQ_DIV_SHIFT     6u
#define T5AI_I2C_CONFIG_TIMEOUT_ENABLE     (1u << 28)
#define T5AI_I2C_CONFIG_IDLE_DETECT_ENABLE (1u << 29)
#define T5AI_I2C_CONFIG_ENABLE             (1u << 31)

#define T5AI_SYSTEM_DEVICE_CLOCK_ENABLE   (*(volatile uint32_t *)0x44010030u)
#define T5AI_SYSTEM_POWER_SLEEP_WAKEUP    (*(volatile uint32_t *)0x44010040u)
#define T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE (*(volatile uint32_t *)0x44010088u)

#ifdef CONFIG_T5AI_I2C_PROFILE
#include <pbl/kernel/irq.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/rtc.h>
PBL_LOG_MODULE_DEFINE(t5ai_i2c_profile, LOG_LEVEL_DEBUG);
static struct {
  uint32_t irq, cycles, maximum, rx_irq, rx_bytes, errors, started;
} s_profile;
void t5ai_i2c_profile_dump(void) {
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  pbl_irq_lock();
  const __typeof__(s_profile) p = s_profile;
  s_profile = (__typeof__(s_profile)){.started = now};
  pbl_irq_unlock();
  PBL_LOG_DBG("T5I2C dt=%lu irq=%lu cycles=%lu max_cycles=%lu rx_irq=%lu rx_bytes=%lu errors=%lu",
              (unsigned long)(now - p.started), (unsigned long)p.irq, (unsigned long)p.cycles,
              (unsigned long)p.maximum, (unsigned long)p.rx_irq, (unsigned long)p.rx_bytes,
              (unsigned long)p.errors);
  PBL_LOG_DBG("T5I2CCLOCK hz=%lu fifo=%u", (unsigned long)SystemCoreClock,
#ifdef CONFIG_T5AI_I2C_RX_FIFO
              1u
#else
              0u
#endif
  );
}
#endif

static const struct pbl_i2c_t5ai *prv_dev(const struct pbl_i2c_bus *bus) {
  return container_of(bus, const struct pbl_i2c_t5ai, bus);
}

static uint32_t prv_read_control(unsigned remaining) {
#ifdef CONFIG_T5AI_I2C_RX_FIFO
  // Keep the last byte separate; never wait for more bytes than remain.
#ifdef CONFIG_T5AI_I2C_RX_FIFO_LIMIT4
  const unsigned mode = remaining > 4u ? 2u : 3u;
#else
  const unsigned mode = remaining > 12u ? 0u : remaining > 8u ? 1u : remaining > 4u ? 2u : 3u;
#endif
  return T5AI_I2C_STATUS_ACK | (mode << 6);
#else
  (void)remaining;
  return T5AI_I2C_STATUS_ACK | T5AI_I2C_STATUS_INT_MODE_MASK;
#endif
}

static unsigned prv_read_count(uint32_t status) {
#ifdef CONFIG_T5AI_I2C_RX_FIFO
  // BK7258 RX mode 0/1/2/3 reports 12/8/4/1 bytes, unlike TX mode.
  const unsigned counts[] = {12u, 8u, 4u, 1u};
  return counts[(status & T5AI_I2C_STATUS_INT_MODE_MASK) >> 6];
#else
  (void)status;
  return 1u;
#endif
}

static T5AII2CRegisters *prv_regs(const struct pbl_i2c_bus *bus) {
  return (T5AII2CRegisters *)prv_dev(bus)->base;
}

static void prv_complete(const struct pbl_i2c_bus *bus, int status) {
  if (prv_dev(bus)->state->active) {
    prv_dev(bus)->state->active = false;
    pbl_i2c_bus_complete(bus, status);
  }
}

static void prv_write_status(T5AII2CRegisters *regs, uint32_t control) {
  regs->status = control & ~T5AI_I2C_STATUS_INTERRUPT;
}

static void prv_stop(T5AII2CRegisters *regs) {
  prv_write_status(regs, T5AI_I2C_STATUS_STOP);
}

static void prv_start_address(T5AII2CRegisters *regs, uint16_t address, bool read) {
  regs->data = ((uint32_t)address << 1) | (read ? 1u : 0u);
  prv_write_status(regs, T5AI_I2C_STATUS_START);
}

// Called with the bus mutex held, never from an interrupt.
static bool prv_wait_scl(const struct pbl_gpio *scl) {
  for (unsigned us = 0; us < T5AI_I2C_RECOVERY_STRETCH_US; ++us) {
    if (pbl_gpio_get_raw(scl) > 0) {
      return true;
    }
    delay_us(1u);
  }
  return false;
}

static void prv_clear_bus(const struct pbl_i2c_bus *bus) {
  const struct pbl_gpio scl = prv_dev(bus)->scl;
  const struct pbl_gpio sda = prv_dev(bus)->sda;
  (void)pbl_gpio_configure(&scl, PBL_GPIO_OUTPUT_HIGH);
  (void)pbl_gpio_configure(&sda, PBL_GPIO_OUTPUT_HIGH);
  if (prv_wait_scl(&scl)) {
    for (unsigned i = 0; i < T5AI_I2C_RECOVERY_CLOCKS && pbl_gpio_get_raw(&sda) == 0; ++i) {
      (void)pbl_gpio_set_raw(&scl, false);
      delay_us(T5AI_I2C_BUS_FREE_US);
      (void)pbl_gpio_set_raw(&scl, true);
      if (!prv_wait_scl(&scl)) {
        break;
      }
      delay_us(T5AI_I2C_BUS_FREE_US);
    }
    // Generate STOP without ever driving either line high.
    (void)pbl_gpio_set_raw(&scl, false);
    (void)pbl_gpio_set_raw(&sda, false);
    delay_us(T5AI_I2C_BUS_FREE_US);
    (void)pbl_gpio_set_raw(&scl, true);
    if (prv_wait_scl(&scl)) {
      delay_us(T5AI_I2C_BUS_FREE_US);
    }
    (void)pbl_gpio_set_raw(&sda, true);
    delay_us(T5AI_I2C_BUS_FREE_US);
  }
  PBL_ASSERTN(pbl_pinctrl_select_state(&prv_dev(bus)->pinctrl, PBL_PINCTRL_STATE_DEFAULT) == 0);
}

static void prv_release_idle_clock(const struct pbl_i2c_bus *bus) {
#ifdef CONFIG_T5AI_I2C_IDLE_GATING
  // STOP and bus-free are complete. Keep the shared BAKP domain and source clock on.
  NVIC_DisableIRQ(prv_dev(bus)->irqn);
  NVIC_ClearPendingIRQ(prv_dev(bus)->irqn);
  prv_regs(bus)->global_control &= ~T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS;
#endif
}

static int prv_init(const struct pbl_i2c_bus *bus) {
  PBL_ASSERTN(prv_dev(bus)->base == T5AI_I2C0_BASE);
  PBL_ASSERTN(prv_dev(bus)->frequency != 0u);
#ifdef CONFIG_T5AI_I2C_PROFILE
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= 1u;
#endif
  NVIC_DisableIRQ(prv_dev(bus)->irqn);

  // BAKP owns I2C0. The boot-stage CP PM service is not yet response-ready
  // here, so reproduce its authoritative zero-is-powered domain operation.
  T5AI_SYSTEM_POWER_SLEEP_WAKEUP &= ~T5AI_I2C_BACKUP_POWER_DOWN;
  T5AI_SYSTEM_DEVICE_CLOCK_ENABLE |= T5AI_I2C_CLOCK_ENABLE;
  T5AII2CRegisters *regs = prv_regs(bus);
  regs->config = 0u;
  prv_clear_bus(bus);
  regs->global_control = 0u;
  // BK7258 names bit 0 soft_reset, but its HAL leaves it asserted as the
  // reset-release state and adds clock-gate bypass in bit 1.
  regs->global_control = T5AI_I2C_GLOBAL_RESET_RELEASE | T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS;
  regs->status = 0u;
  const uint32_t source_hz = T5AI_I2C_SOURCE_HZ;
  const uint32_t clocks = (source_hz + prv_dev(bus)->frequency - 1u) / prv_dev(bus)->frequency;
  const uint32_t divider =
      ((clocks - T5AI_I2C_SCL_FIXED_CLOCKS) + T5AI_I2C_SCL_DIVIDER_CLOCKS - 1u) /
          T5AI_I2C_SCL_DIVIDER_CLOCKS -
      1u;
  PBL_ASSERTN(divider <= T5AI_I2C_DIVIDER_MAX);
  regs->config = T5AI_I2C_IDLE_CONTROL | T5AI_I2C_SCL_CONTROL |
                 (divider << T5AI_I2C_CONFIG_FREQ_DIV_SHIFT) | T5AI_I2C_CLOCK_SOURCE |
                 T5AI_I2C_CONFIG_TIMEOUT_ENABLE | T5AI_I2C_CONFIG_IDLE_DETECT_ENABLE;
  regs->extended_config = T5AI_I2C_EXT_DATA_OUTPUT_ENABLE | T5AI_I2C_EXT_ADDRESS_OUTPUT_ENABLE;

  T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE |= T5AI_I2C0_INTERRUPT_ENABLE;
  NVIC_ClearPendingIRQ(prv_dev(bus)->irqn);
  NVIC_SetPriority(prv_dev(bus)->irqn, T5AI_I2C_IRQ_PRIORITY);
  NVIC_EnableIRQ(prv_dev(bus)->irqn);
  prv_release_idle_clock(bus);
  return 0;
}

static void prv_enable(const struct pbl_i2c_bus *bus) {
  T5AI_SYSTEM_POWER_SLEEP_WAKEUP &= ~T5AI_I2C_BACKUP_POWER_DOWN;
  T5AI_SYSTEM_DEVICE_CLOCK_ENABLE |= T5AI_I2C_CLOCK_ENABLE;
  prv_regs(bus)->global_control |= T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS;
  prv_regs(bus)->config |= T5AI_I2C_CONFIG_ENABLE;
  delay_us(T5AI_I2C_ENABLE_SETTLE_US);
#ifdef CONFIG_T5AI_I2C_IDLE_GATING
  // The pinned HAL requires bypass throughout transfers to read ACK reliably.
  NVIC_ClearPendingIRQ(prv_dev(bus)->irqn);
  NVIC_EnableIRQ(prv_dev(bus)->irqn);
#endif
}

static void prv_disable(const struct pbl_i2c_bus *bus) {
  // Completion requests STOP; keep the controller clocked until it leaves the bus.
  unsigned us = 0;
  while ((prv_regs(bus)->status & T5AI_I2C_STATUS_BUSY) && us++ < T5AI_I2C_STOP_TIMEOUT_US) {
    delay_us(1u);
  }
  const bool stopped = (prv_regs(bus)->status & T5AI_I2C_STATUS_BUSY) == 0u;
  if (!stopped) {
    prv_dev(bus)->state->recovery_required = true;
  }
  delay_us(T5AI_I2C_BUS_FREE_US);
  prv_regs(bus)->config &= ~T5AI_I2C_CONFIG_ENABLE;
  // A stuck STOP keeps bypass asserted until recovery; never gate a live bus.
  if (stopped) {
    prv_release_idle_clock(bus);
  }
}

static void prv_abort(const struct pbl_i2c_bus *bus) {
  pbl_irq_lock();
  prv_dev(bus)->state->active = false;
  T5AII2CRegisters *regs = prv_regs(bus);
  prv_stop(regs);
  regs->config &= ~T5AI_I2C_CONFIG_ENABLE;
  NVIC_DisableIRQ(prv_dev(bus)->irqn);
  NVIC_ClearPendingIRQ(prv_dev(bus)->irqn);
  prv_dev(bus)->state->recovery_required = true;
  pbl_irq_unlock();
}

static void prv_start_message(const struct pbl_i2c_bus *bus) {
  struct pbl_i2c_t5ai_state *state = prv_dev(bus)->state;
  const struct pbl_i2c_msg *msg = &state->msgs[state->msg_index];
  state->index = 0U;
  state->phase = (msg->flags & PBL_I2C_MSG_READ) ? T5AI_I2C_READ : T5AI_I2C_WRITE;
  state->addressing = true;
  prv_start_address(prv_regs(bus), state->addr, state->phase == T5AI_I2C_READ);
}

// Called after the final byte of a message has been transferred.
static void prv_next_message(const struct pbl_i2c_bus *bus) {
  struct pbl_i2c_t5ai_state *state = prv_dev(bus)->state;
  while (++state->msg_index < state->num_msgs) {
    const struct pbl_i2c_msg *msg = &state->msgs[state->msg_index];
    if ((msg->flags & PBL_I2C_MSG_NOSTART) == 0U) {
      prv_start_message(bus);
      return;
    }
    state->index = 0U;
    if (msg->len != 0U) {
      prv_regs(bus)->data = msg->buf[state->index++];
      prv_write_status(prv_regs(bus), 0U);
      return;
    }
  }
  prv_stop(prv_regs(bus));
  prv_complete(bus, 0);
}

static int prv_start(const struct pbl_i2c_bus *bus, uint16_t addr, struct pbl_i2c_msg *msgs,
                     uint8_t num_msgs) {
  if (addr > 0x7fU || num_msgs == 0U) {
    return -EINVAL;
  }
  // Preserve the qualified read termination: the final received byte is followed by STOP.
  for (uint8_t i = 0U; i < num_msgs; ++i) {
    if ((msgs[i].flags & ~(PBL_I2C_MSG_READ | PBL_I2C_MSG_NOSTART | PBL_I2C_MSG_DMA_SAFE)) != 0U ||
        ((msgs[i].flags & PBL_I2C_MSG_READ) != 0U && (msgs[i].len == 0U || i + 1U != num_msgs))) {
      return -ENOTSUP;
    }
  }
  if (prv_dev(bus)->state->recovery_required) {
    prv_init(bus);
    prv_enable(bus);
    prv_dev(bus)->state->recovery_required = false;
  }

  // Completion requests STOP before the external bus-free interval has elapsed.
  delay_us(T5AI_I2C_BUS_FREE_US);
  pbl_irq_lock();
  struct pbl_i2c_t5ai_state *state = prv_dev(bus)->state;
  state->msgs = msgs;
  state->num_msgs = num_msgs;
  state->msg_index = 0U;
  state->addr = addr;
  state->active = true;
  prv_start_message(bus);
  pbl_irq_unlock();
  return 0;
}

static void prv_irq_handler(const struct pbl_i2c_bus *bus) {
  T5AII2CRegisters *regs = prv_regs(bus);
  struct pbl_i2c_t5ai_state *state = prv_dev(bus)->state;
  if (!state->active) {
    return;
  }
  const uint32_t status = regs->status;
  if ((status & (T5AI_I2C_STATUS_TIMEOUT | T5AI_I2C_STATUS_ARB_LOST)) != 0U) {
    prv_abort(bus);
    pbl_i2c_bus_complete(bus, (status & T5AI_I2C_STATUS_TIMEOUT) ? -ETIMEDOUT : -EIO);
    return;
  }
  if ((status & T5AI_I2C_STATUS_INTERRUPT) == 0U) {
    return;
  }
  if ((status & T5AI_I2C_STATUS_ACK) == 0U &&
      (state->addressing || state->phase == T5AI_I2C_WRITE)) {
    prv_stop(regs);
    prv_complete(bus, state->addressing ? -ENXIO : -EIO);
    return;
  }

  struct pbl_i2c_msg *msg = &state->msgs[state->msg_index];
  if (state->addressing) {
    state->addressing = false;
    if (state->phase == T5AI_I2C_READ) {
      prv_write_status(regs, prv_read_control(msg->len));
      return;
    }
  }
  if (state->phase == T5AI_I2C_WRITE) {
    if (state->index == msg->len) {
      prv_next_message(bus);
    } else {
      regs->data = msg->buf[state->index++];
      prv_write_status(regs, 0U);
    }
    return;
  }

  const unsigned count = prv_read_count(status);
  if (state->index >= msg->len || count > msg->len - state->index) {
    prv_abort(bus);
    pbl_i2c_bus_complete(bus, -EIO);
    return;
  }
  for (unsigned i = 0U; i < count; ++i) {
    msg->buf[state->index++] = (uint8_t)regs->data;
  }
#ifdef CONFIG_T5AI_I2C_PROFILE
  ++s_profile.rx_irq;
  s_profile.rx_bytes += count;
#endif
  if (state->index == msg->len) {
    prv_next_message(bus);
  } else {
    prv_write_status(regs, prv_read_control(msg->len - state->index));
  }
}

void pbl_i2c_t5ai_irq_handler(const struct pbl_i2c_t5ai *dev) {
  const struct pbl_i2c_bus *bus = &dev->bus;
#ifdef CONFIG_T5AI_I2C_PROFILE
  const uint32_t start = DWT->CYCCNT;
  const uint32_t status = prv_regs(bus)->status;
#endif
  prv_irq_handler(bus);
#ifdef CONFIG_T5AI_I2C_PROFILE
  const uint32_t elapsed = DWT->CYCCNT - start;
  ++s_profile.irq;
  s_profile.cycles += elapsed;
  if (elapsed > s_profile.maximum)
    s_profile.maximum = elapsed;
  s_profile.errors += (status & (T5AI_I2C_STATUS_TIMEOUT | T5AI_I2C_STATUS_ARB_LOST)) != 0u;
#endif
}

const struct pbl_i2c_bus_ops pbl_i2c_t5ai_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .start = prv_start,
  .abort = prv_abort,
};
