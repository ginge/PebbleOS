/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/i2c/hal.h>
#include <pbl/drivers/i2c/definitions.h>
#include <pbl/drivers/i2c/t5ai.h>
#include <pbl/drivers/gpio.h>
#include "FreeRTOS.h"
#include "kernel/util/delay.h"
#include "semphr.h"
#include "system/passert.h"

#include <cmsis_core.h>
#include <stdint.h>

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
#define T5AI_I2C0_BASE 0x45850000u
#define T5AI_I2C_SOURCE_HZ 26000000u
#define T5AI_I2C_GLOBAL_RESET_RELEASE (1u << 0)
#define T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS (1u << 1)
#define T5AI_I2C_CLOCK_ENABLE (1u << 0)
#define T5AI_I2C_BACKUP_POWER_DOWN (1u << 4)
#define T5AI_I2C0_INTERRUPT_ENABLE (1u << 6)
#define T5AI_I2C_IRQ_PRIORITY 6u
#define T5AI_I2C_DIVIDER_MAX 0x3ffu
#define T5AI_I2C_IDLE_CONTROL (3u << 0)
#define T5AI_I2C_SCL_CONTROL (4u << 3)
#define T5AI_I2C_CLOCK_SOURCE (3u << 26)
#define T5AI_I2C_EXT_DATA_OUTPUT_ENABLE (1u << 0)
#define T5AI_I2C_EXT_ADDRESS_OUTPUT_ENABLE (1u << 1)
#define T5AI_I2C_ENABLE_SETTLE_US 5u
#define T5AI_I2C_BUS_FREE_US 5u
#define T5AI_I2C_GPIO_BASE ((volatile uint32_t *)0x44000400u)
#define T5AI_I2C_GPIO_CONFIG_MASK 0x3fffu
#define T5AI_I2C_GPIO_OUTPUT_DISABLE (1u << 3)
#define T5AI_I2C_GPIO_PULL_UP (1u << 4)
#define T5AI_I2C_GPIO_PULL_ENABLE (1u << 5)
#define T5AI_I2C_GPIO_SECOND_FUNCTION (1u << 6)
#define T5AI_I2C_PINS_PER_FUNCTION_WORD 8u
#define T5AI_I2C_FUNCTION_BITS 4u
#define T5AI_I2C_FUNCTION_MASK 0xfu
// Vendor SCL formula: source clocks = 3 * (divider + 1) + 6.
#define T5AI_I2C_SCL_FIXED_CLOCKS 6u
#define T5AI_I2C_SCL_DIVIDER_CLOCKS 3u

#define T5AI_I2C_STATUS_INTERRUPT (1u << 0)
#define T5AI_I2C_STATUS_TIMEOUT (1u << 1)
#define T5AI_I2C_STATUS_ARB_LOST (1u << 3)
#define T5AI_I2C_STATUS_INT_MODE_MASK (3u << 6)
#define T5AI_I2C_STATUS_ACK (1u << 8)
#define T5AI_I2C_STATUS_STOP (1u << 9)
#define T5AI_I2C_STATUS_START (1u << 10)
#define T5AI_I2C_STATUS_BUSY (1u << 15)

#define T5AI_I2C_CONFIG_FREQ_DIV_SHIFT 6u
#define T5AI_I2C_CONFIG_TIMEOUT_ENABLE (1u << 28)
#define T5AI_I2C_CONFIG_IDLE_DETECT_ENABLE (1u << 29)
#define T5AI_I2C_CONFIG_ENABLE (1u << 31)

#define T5AI_SYSTEM_DEVICE_CLOCK_ENABLE (*(volatile uint32_t *)0x44010030u)
#define T5AI_SYSTEM_POWER_SLEEP_WAKEUP (*(volatile uint32_t *)0x44010040u)
#define T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE (*(volatile uint32_t *)0x44010088u)
#define T5AI_SYSTEM_GPIO_FUNCTION_BASE ((volatile uint32_t *)0x440100c0u)

static T5AII2CRegisters *prv_regs(I2CBus *bus) {
  return (T5AII2CRegisters *)bus->hal->base;
}

static void prv_complete(I2CBus *bus, I2CTransferEvent event) {
  const portBASE_TYPE woken = i2c_handle_transfer_event(bus, event);
  portEND_SWITCHING_ISR(woken);
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

static void prv_configure_pin(uint8_t pin) {
  volatile uint32_t *gpio = T5AI_I2C_GPIO_BASE;
  const uint32_t pull_up = T5AI_I2C_GPIO_PULL_UP | T5AI_I2C_GPIO_PULL_ENABLE;
  gpio[pin] = (gpio[pin] & ~T5AI_I2C_GPIO_CONFIG_MASK) | T5AI_I2C_GPIO_OUTPUT_DISABLE | pull_up |
              T5AI_I2C_GPIO_SECOND_FUNCTION;

  volatile uint32_t *function =
      &T5AI_SYSTEM_GPIO_FUNCTION_BASE[pin / T5AI_I2C_PINS_PER_FUNCTION_WORD];
  const uint32_t shift = (pin % T5AI_I2C_PINS_PER_FUNCTION_WORD) * T5AI_I2C_FUNCTION_BITS;
  *function &= ~(T5AI_I2C_FUNCTION_MASK << shift);  // I2C0 is peripheral function zero on P20/P21.
}

void i2c_hal_init(I2CBus *bus) {
  PBL_ASSERTN(bus->hal->base == T5AI_I2C0_BASE);
  PBL_ASSERTN(bus->hal->frequency != 0u);
  prv_configure_pin(bus->hal->scl_pin);
  prv_configure_pin(bus->hal->sda_pin);

  // BAKP owns I2C0. The boot-stage CP PM service is not yet response-ready
  // here, so reproduce its authoritative zero-is-powered domain operation.
  T5AI_SYSTEM_POWER_SLEEP_WAKEUP &= ~T5AI_I2C_BACKUP_POWER_DOWN;
  T5AI_SYSTEM_DEVICE_CLOCK_ENABLE |= T5AI_I2C_CLOCK_ENABLE;
  T5AII2CRegisters *regs = prv_regs(bus);
  // BK7258 names bit 0 soft_reset, but its HAL leaves it asserted as the
  // reset-release state and adds clock-gate bypass in bit 1.
  regs->global_control = T5AI_I2C_GLOBAL_RESET_RELEASE | T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS;
  const uint32_t source_hz = T5AI_I2C_SOURCE_HZ;
  const uint32_t clocks = (source_hz + bus->hal->frequency - 1u) / bus->hal->frequency;
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
  NVIC_ClearPendingIRQ(bus->hal->irqn);
  NVIC_SetPriority(bus->hal->irqn, T5AI_I2C_IRQ_PRIORITY);
  NVIC_EnableIRQ(bus->hal->irqn);
}

void i2c_hal_enable(I2CBus *bus) {
  T5AI_SYSTEM_DEVICE_CLOCK_ENABLE |= T5AI_I2C_CLOCK_ENABLE;
  prv_regs(bus)->global_control |= T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS;
  prv_regs(bus)->config |= T5AI_I2C_CONFIG_ENABLE;
  delay_us(T5AI_I2C_ENABLE_SETTLE_US);
}

void i2c_hal_disable(I2CBus *bus) {
  prv_regs(bus)->config &= ~T5AI_I2C_CONFIG_ENABLE;
}

bool i2c_hal_is_busy(I2CBus *bus) {
  return (prv_regs(bus)->status & T5AI_I2C_STATUS_BUSY) != 0u;
}

void i2c_hal_abort_transfer(I2CBus *bus) {
  T5AII2CRegisters *regs = prv_regs(bus);
  prv_stop(regs);
  regs->config &= ~T5AI_I2C_CONFIG_ENABLE;
  regs->config |= T5AI_I2C_CONFIG_ENABLE;
}

void i2c_hal_init_transfer(I2CBus *bus) {
  bus->state->transfer.idx = 0u;
}

void i2c_hal_start_transfer(I2CBus *bus) {
  I2CTransfer *transfer = &bus->state->transfer;
  if (transfer->size == 0u) {
    bus->state->transfer_event = I2CTransferEvent_Error;
    xSemaphoreGive(bus->state->event_semaphore);
    return;
  }

  /* Completion is reported when STOP is requested, before the external bus
   * free interval is guaranteed. Leave margin above the 400 kHz tBUF minimum
   * before issuing another START. */
  delay_us(T5AI_I2C_BUS_FREE_US);
  transfer->idx = 0u;
  if (transfer->direction == I2CTransferDirection_Read &&
      transfer->type == I2CTransferType_NoRegisterAddress) {
    transfer->state = I2CTransferState_ReadData;
    prv_start_address(prv_regs(bus), transfer->device_address, true);
  } else {
    transfer->state = transfer->type == I2CTransferType_SendRegisterAddress
                          ? I2CTransferState_WriteRegAddress
                          : I2CTransferState_WriteData;
    prv_start_address(prv_regs(bus), transfer->device_address, false);
  }
}

void i2c_irq_handler(I2CBus *bus) {
  T5AII2CRegisters *regs = prv_regs(bus);
  I2CTransfer *transfer = &bus->state->transfer;
  const uint32_t status = regs->status;

  if ((status & (T5AI_I2C_STATUS_TIMEOUT | T5AI_I2C_STATUS_ARB_LOST)) != 0u) {
    i2c_hal_abort_transfer(bus);
    prv_complete(bus, I2CTransferEvent_Error);
    return;
  }
  if ((status & T5AI_I2C_STATUS_INTERRUPT) == 0u) {
    return;
  }
  if ((status & T5AI_I2C_STATUS_ACK) == 0u &&
      (transfer->state != I2CTransferState_ReadData || (status & T5AI_I2C_STATUS_START) != 0u)) {
    prv_stop(regs);
    prv_complete(bus, I2CTransferEvent_NackReceived);
    return;
  }

  switch (transfer->state) {
    case I2CTransferState_WriteRegAddress:
      regs->data = transfer->register_address;
      transfer->state = transfer->direction == I2CTransferDirection_Read
                            ? I2CTransferState_RepeatStart
                            : I2CTransferState_WriteData;
      prv_write_status(regs, 0u);
      break;
    case I2CTransferState_RepeatStart:
      transfer->state = I2CTransferState_ReadData;
      prv_start_address(regs, transfer->device_address, true);
      break;
    case I2CTransferState_WriteData:
      if (transfer->idx == transfer->size) {
        prv_stop(regs);
        prv_complete(bus, I2CTransferEvent_TransferComplete);
      } else {
        regs->data = transfer->data[transfer->idx++];
        prv_write_status(regs, 0u);
      }
      break;
    case I2CTransferState_ReadData:
      if ((status & T5AI_I2C_STATUS_START) != 0u) {
        prv_write_status(regs, T5AI_I2C_STATUS_ACK | T5AI_I2C_STATUS_INT_MODE_MASK);
        break;
      }
      transfer->data[transfer->idx++] = (uint8_t)regs->data;
      if (transfer->idx == transfer->size) {
        prv_stop(regs);
        prv_complete(bus, I2CTransferEvent_TransferComplete);
      } else {
        prv_write_status(regs, T5AI_I2C_STATUS_ACK | T5AI_I2C_STATUS_INT_MODE_MASK);
      }
      break;
    default:
      i2c_hal_abort_transfer(bus);
      prv_complete(bus, I2CTransferEvent_Error);
      break;
  }
}
