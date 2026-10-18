/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/mcu.h>

#include <cmsis_core.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* BK7258 AP eFuse registers and factory device-ID allocation. */
#define T5AI_EFUSE_WRITE_MODE (1u << 1)

#define T5AI_SYSTEM_DEVICE_CLOCK_ENABLE (*(volatile uint32_t *)0x44010030u)
#define T5AI_EFUSE_BASE 0x44880000u
#define T5AI_EFUSE_GLOBAL_CTRL (*(volatile uint32_t *)(T5AI_EFUSE_BASE + 0x08u))
#define T5AI_EFUSE_CTRL (*(volatile uint32_t *)(T5AI_EFUSE_BASE + 0x10u))
#define T5AI_EFUSE_OPTR (*(volatile uint32_t *)(T5AI_EFUSE_BASE + 0x14u))

#define T5AI_EFUSE_CLOCK_ENABLE (1u << 7)
#define T5AI_EFUSE_SOFT_RESET (1u << 0)
#define T5AI_EFUSE_OPERATION_ENABLE (1u << 0)
#define T5AI_EFUSE_ADDRESS_SHIFT 8u
#define T5AI_EFUSE_ADDRESS_MASK (0x1fu << T5AI_EFUSE_ADDRESS_SHIFT)
#define T5AI_EFUSE_READ_VALID (1u << 8)
#define T5AI_EFUSE_DEVICE_ID_OFFSET 0x18u
#define T5AI_EFUSE_DEVICE_ID_SIZE 5u
#define T5AI_EFUSE_READ_TIMEOUT 10000u

static bool prv_read_byte(uint8_t address, uint8_t *value) {
  uint32_t ctrl = T5AI_EFUSE_CTRL;
  ctrl &= ~(T5AI_EFUSE_OPERATION_ENABLE | T5AI_EFUSE_WRITE_MODE | T5AI_EFUSE_ADDRESS_MASK);
  ctrl |= (uint32_t)address << T5AI_EFUSE_ADDRESS_SHIFT;
  T5AI_EFUSE_CTRL = ctrl | T5AI_EFUSE_OPERATION_ENABLE;

  uint32_t timeout = T5AI_EFUSE_READ_TIMEOUT;
  while ((T5AI_EFUSE_CTRL & T5AI_EFUSE_OPERATION_ENABLE) != 0 &&
         timeout-- != 0) {
  }
  if (timeout == 0 || (T5AI_EFUSE_OPTR & T5AI_EFUSE_READ_VALID) == 0) {
    return false;
  }

  *value = (uint8_t)T5AI_EFUSE_OPTR;
  return true;
}

StatusCode mcu_get_serial(void *buf, size_t *buf_sz) {
  if (buf == NULL || buf_sz == NULL) {
    return E_INVALID_ARGUMENT;
  }
  if (*buf_sz < T5AI_EFUSE_DEVICE_ID_SIZE) {
    *buf_sz = T5AI_EFUSE_DEVICE_ID_SIZE;
    return E_OUT_OF_MEMORY;
  }

  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool clock_was_enabled =
      (T5AI_SYSTEM_DEVICE_CLOCK_ENABLE & T5AI_EFUSE_CLOCK_ENABLE) != 0;
  T5AI_SYSTEM_DEVICE_CLOCK_ENABLE |= T5AI_EFUSE_CLOCK_ENABLE;
  T5AI_EFUSE_GLOBAL_CTRL |= T5AI_EFUSE_SOFT_RESET;

  StatusCode status = S_SUCCESS;
  for (uint8_t i = 0; i < T5AI_EFUSE_DEVICE_ID_SIZE; ++i) {
    if (!prv_read_byte(T5AI_EFUSE_DEVICE_ID_OFFSET + i,
                       &((uint8_t *)buf)[i])) {
      status = E_ERROR;
      break;
    }
  }

  if (!clock_was_enabled) {
    T5AI_SYSTEM_DEVICE_CLOCK_ENABLE &= ~T5AI_EFUSE_CLOCK_ENABLE;
  }
  if (primask == 0) {
    __enable_irq();
  }

  if (status == S_SUCCESS) {
    *buf_sz = T5AI_EFUSE_DEVICE_ID_SIZE;
  }
  return status;
}

uint32_t mcu_cycles_to_milliseconds(uint64_t cpu_ticks) {
  return (uint32_t)((cpu_ticks * 1000u) / SystemCoreClock);
}
