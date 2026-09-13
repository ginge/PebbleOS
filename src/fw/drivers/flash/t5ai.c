/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/flash/flash_impl.h>
#include <pbl/soc/t5ai/flash_ipc.h>

#include <stdint.h>
#include <string.h>

#include "flash_region/flash_region.h"
#include "system/status_codes.h"

#define T5AI_FLASH_IPC_MAX_TRANSFER 0x200u

typedef enum {
  T5AI_FLASH_IDLE,
  T5AI_FLASH_WRITE_COMPLETE,
  T5AI_FLASH_ERASE_COMPLETE,
} T5AIFlashOperation;

static T5AIFlashOperation s_operation;
static bool s_protected;
static FlashAddress s_protected_start;
static FlashAddress s_protected_end;

static bool prv_range_valid(FlashAddress address, size_t length) {
  const uint32_t offset = address - FLASH_REGION_BASE_ADDRESS;
  return address >= FLASH_REGION_BASE_ADDRESS && length > 0 && length <= BOARD_NOR_FLASH_SIZE &&
         offset <= BOARD_NOR_FLASH_SIZE - length;
}

static status_t prv_write_allowed(FlashAddress address, size_t length) {
  if (!prv_range_valid(address, length)) {
    return E_RANGE;
  }

  const FlashAddress end = address + length - 1;
  const bool in_filesystem =
      address >= FLASH_REGION_FILESYSTEM_BEGIN && end < FLASH_REGION_FILESYSTEM_END;
  const bool in_system_resources =
      address >= FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN &&
      end < FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END;
  if (!(in_filesystem || in_system_resources)) {
    return E_INVALID_OPERATION;
  }
  if (s_protected && address <= s_protected_end && end >= s_protected_start) {
    return E_INVALID_OPERATION;
  }
  return S_SUCCESS;
}

static uint32_t prv_offset(FlashAddress address) {
  return address - FLASH_REGION_BASE_ADDRESS;
}

FlashAddress flash_impl_get_sector_base_address(FlashAddress address) {
  return address & SECTOR_ADDR_MASK;
}

FlashAddress flash_impl_get_subsector_base_address(FlashAddress address) {
  return address & SUBSECTOR_ADDR_MASK;
}

size_t flash_impl_get_capacity(void) {
  return BOARD_NOR_FLASH_SIZE;
}

status_t flash_impl_init(bool coredump_mode) {
  (void)coredump_mode;
  s_operation = T5AI_FLASH_IDLE;
  return t5ai_flash_ipc_init();
}

status_t flash_impl_read_sync(void *buffer, FlashAddress address, size_t length) {
  if (buffer == NULL || !prv_range_valid(address, length)) {
    return E_INVALID_ARGUMENT;
  }

  uint8_t *output = buffer;
  while (length != 0) {
    const size_t chunk =
        length > T5AI_FLASH_IPC_MAX_TRANSFER ? T5AI_FLASH_IPC_MAX_TRANSFER : length;
    const status_t status = t5ai_flash_ipc_read(prv_offset(address), output, chunk);
    if (FAILED(status)) {
      return status;
    }
    address += chunk;
    output += chunk;
    length -= chunk;
  }
  return S_SUCCESS;
}

int flash_impl_write_page_begin(const void *buffer, FlashAddress address, size_t length) {
  if (buffer == NULL || s_operation != T5AI_FLASH_IDLE) {
    return E_BUSY;
  }

  const size_t page_remaining = PAGE_SIZE_BYTES - (address & (PAGE_SIZE_BYTES - 1u));
  const size_t written = length < page_remaining ? length : page_remaining;
  status_t status = prv_write_allowed(address, written);
  if (FAILED(status)) {
    return status;
  }
  status = t5ai_flash_ipc_write(prv_offset(address), buffer, written);
  if (FAILED(status)) {
    return status;
  }
  s_operation = T5AI_FLASH_WRITE_COMPLETE;
  return (int)written;
}

status_t flash_impl_get_write_status(void) {
  if (s_operation == T5AI_FLASH_WRITE_COMPLETE) {
    s_operation = T5AI_FLASH_IDLE;
    return S_SUCCESS;
  }
  return s_operation == T5AI_FLASH_IDLE ? S_NO_ACTION_REQUIRED : E_BUSY;
}

static status_t prv_erase(FlashAddress address, size_t length) {
  if (s_operation != T5AI_FLASH_IDLE) {
    return E_BUSY;
  }
  status_t status = prv_write_allowed(address, length);
  if (FAILED(status)) {
    return status;
  }
  for (size_t offset = 0; offset < length; offset += SUBSECTOR_SIZE_BYTES) {
    status = t5ai_flash_ipc_erase_4k(prv_offset(address + offset));
    if (FAILED(status)) {
      return status;
    }
  }
  s_operation = T5AI_FLASH_ERASE_COMPLETE;
  return S_SUCCESS;
}

status_t flash_impl_erase_subsector_begin(FlashAddress address) {
  return prv_erase(flash_impl_get_subsector_base_address(address), SUBSECTOR_SIZE_BYTES);
}

status_t flash_impl_erase_sector_begin(FlashAddress address) {
  return prv_erase(flash_impl_get_sector_base_address(address), SECTOR_SIZE_BYTES);
}

status_t flash_impl_get_erase_status(void) {
  if (s_operation == T5AI_FLASH_ERASE_COMPLETE) {
    s_operation = T5AI_FLASH_IDLE;
    return S_SUCCESS;
  }
  return s_operation == T5AI_FLASH_IDLE ? S_NO_ACTION_REQUIRED : E_BUSY;
}

static status_t prv_blank_check(FlashAddress address, size_t length) {
  uint8_t buffer[T5AI_FLASH_IPC_MAX_TRANSFER];
  while (length != 0) {
    const size_t chunk = length > sizeof(buffer) ? sizeof(buffer) : length;
    status_t status = flash_impl_read_sync(buffer, address, chunk);
    if (FAILED(status)) {
      return status;
    }
    for (size_t i = 0; i < chunk; ++i) {
      if (buffer[i] != 0xff) {
        return S_FALSE;
      }
    }
    address += chunk;
    length -= chunk;
  }
  return S_TRUE;
}

status_t flash_impl_blank_check_subsector(FlashAddress address) {
  return prv_blank_check(flash_impl_get_subsector_base_address(address), SUBSECTOR_SIZE_BYTES);
}

status_t flash_impl_blank_check_sector(FlashAddress address) {
  return prv_blank_check(flash_impl_get_sector_base_address(address), SECTOR_SIZE_BYTES);
}

void flash_impl_enable_write_protection(void) {}

status_t flash_impl_write_protect(FlashAddress start, FlashAddress end) {
  if (start > end || !prv_range_valid(start, end - start + 1)) {
    return E_RANGE;
  }
  s_protected_start = start;
  s_protected_end = end;
  s_protected = true;
  return S_SUCCESS;
}

status_t flash_impl_unprotect(void) {
  s_protected = false;
  return S_SUCCESS;
}

status_t flash_impl_set_burst_mode(bool enable) {
  (void)enable;
  return S_SUCCESS;
}

status_t flash_impl_enter_low_power_mode(void) {
  return S_NO_ACTION_REQUIRED;
}

status_t flash_impl_exit_low_power_mode(void) {
  return S_NO_ACTION_REQUIRED;
}
status_t flash_impl_erase_suspend(FlashAddress address) {
  (void)address;
  if (s_operation == T5AI_FLASH_ERASE_COMPLETE) {
    s_operation = T5AI_FLASH_IDLE;
    return S_NO_ACTION_REQUIRED;
  }
  return s_operation == T5AI_FLASH_IDLE ? S_NO_ACTION_REQUIRED : E_BUSY;
}

status_t flash_impl_erase_resume(FlashAddress address) {
  (void)address;
  return S_NO_ACTION_REQUIRED;
}
uint32_t flash_impl_get_typical_subsector_erase_duration_ms(void) {
  return 50;
}

uint32_t flash_impl_get_typical_sector_erase_duration_ms(void) {
  return 800;
}

status_t flash_impl_read_security_register(uint32_t address, uint8_t *value) {
  (void)address;
  (void)value;
  return E_INVALID_OPERATION;
}

status_t flash_impl_security_register_is_locked(uint32_t address, bool *locked) {
  (void)address;
  (void)locked;
  return E_INVALID_OPERATION;
}

status_t flash_impl_erase_security_register(uint32_t address) {
  (void)address;
  return E_INVALID_OPERATION;
}

status_t flash_impl_write_security_register(uint32_t address, uint8_t value) {
  (void)address;
  (void)value;
  return E_INVALID_OPERATION;
}

const FlashSecurityRegisters *flash_impl_security_registers_info(void) {
  return NULL;
}
