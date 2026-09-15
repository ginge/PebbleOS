/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/flash/flash_impl.h>
#include <pbl/soc/t5ai/flash_ipc.h>

#include <stdint.h>
#include <string.h>

#include "flash_region/flash_region.h"
#include "system/status_codes.h"

#define T5AI_FLASH_IPC_MAX_TRANSFER 0x200u
#define T5AI_FLASH_IPC_ATTEMPTS 2u
#define T5AI_PFS_PAGE_SIZE 0x1000u
#define T5AI_PFS_HEADER_SIZE 32u
#define T5AI_FLASH_READ_CACHE_LINE_SIZE T5AI_FLASH_IPC_MAX_TRANSFER
#define T5AI_FLASH_READ_CACHE_LINES 4u
#define T5AI_PFS_PAGE_COUNT \
  ((FLASH_REGION_FILESYSTEM_END - FLASH_REGION_FILESYSTEM_BEGIN) / T5AI_PFS_PAGE_SIZE)

typedef enum {
  T5AI_FLASH_IDLE,
  T5AI_FLASH_WRITE_COMPLETE,
  T5AI_FLASH_ERASE_COMPLETE,
} T5AIFlashOperation;

static T5AIFlashOperation s_operation;
static bool s_protected;
static FlashAddress s_protected_start;
static FlashAddress s_protected_end;
static uint8_t s_pfs_header_cache[T5AI_PFS_PAGE_COUNT][T5AI_PFS_HEADER_SIZE]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));
static uint32_t s_pfs_header_valid[(T5AI_PFS_PAGE_COUNT + 31u) / 32u];
// CP flash IPC has a substantial fixed cost per request. Resource rendering performs many
// small, nearby reads (font hash entries, glyph metadata and glyph bitmaps), so cache complete
// IPC-sized lines for every flash region rather than limiting read-ahead to PFS files.
static uint8_t s_flash_read_cache[T5AI_FLASH_READ_CACHE_LINES][T5AI_FLASH_READ_CACHE_LINE_SIZE]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));
static FlashAddress s_flash_read_cache_address[T5AI_FLASH_READ_CACHE_LINES];
static bool s_flash_read_cache_valid[T5AI_FLASH_READ_CACHE_LINES];
static unsigned s_flash_read_cache_next;

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
  const bool in_shared_prf = address >= FLASH_REGION_SHARED_PRF_STORAGE_BEGIN &&
                             end < FLASH_REGION_SHARED_PRF_STORAGE_END;
  if (!(in_filesystem || in_system_resources || in_shared_prf)) {
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

static bool prv_pfs_header_location(FlashAddress address, size_t *page, size_t *offset) {
  if (address < FLASH_REGION_FILESYSTEM_BEGIN || address >= FLASH_REGION_FILESYSTEM_END) {
    return false;
  }
  const size_t relative = address - FLASH_REGION_FILESYSTEM_BEGIN;
  const size_t header_offset = relative % T5AI_PFS_PAGE_SIZE;
  if (header_offset >= T5AI_PFS_HEADER_SIZE) {
    return false;
  }
  *page = relative / T5AI_PFS_PAGE_SIZE;
  *offset = header_offset;
  return true;
}

static bool prv_pfs_header_cached(size_t page) {
  return (s_pfs_header_valid[page / 32u] & (1u << (page % 32u))) != 0u;
}

static void prv_pfs_header_mark_cached(size_t page) {
  s_pfs_header_valid[page / 32u] |= 1u << (page % 32u);
}

static status_t prv_read_with_retry(uint32_t offset, void *buffer, size_t length) {
  status_t status = E_ERROR;
  for (unsigned attempt = 0; attempt < T5AI_FLASH_IPC_ATTEMPTS; ++attempt) {
    status = t5ai_flash_ipc_read(offset, buffer, length);
    if (PASSED(status)) {
      break;
    }
  }
  return status;
}

static status_t prv_read_cached(FlashAddress address, void *buffer, size_t length,
                                size_t *bytes_read) {
  const FlashAddress line_address =
      address & ~(FlashAddress)(T5AI_FLASH_READ_CACHE_LINE_SIZE - 1u);
  unsigned line = T5AI_FLASH_READ_CACHE_LINES;
  for (unsigned i = 0; i < T5AI_FLASH_READ_CACHE_LINES; ++i) {
    if (s_flash_read_cache_valid[i] && s_flash_read_cache_address[i] == line_address) {
      line = i;
      break;
    }
  }

  if (line == T5AI_FLASH_READ_CACHE_LINES) {
    line = s_flash_read_cache_next;
    s_flash_read_cache_next = (s_flash_read_cache_next + 1u) % T5AI_FLASH_READ_CACHE_LINES;
    status_t status = prv_read_with_retry(prv_offset(line_address), s_flash_read_cache[line],
                                          T5AI_FLASH_READ_CACHE_LINE_SIZE);
    if (FAILED(status)) {
      s_flash_read_cache_valid[line] = false;
      return status;
    }
    s_flash_read_cache_address[line] = line_address;
    s_flash_read_cache_valid[line] = true;
  }

  const size_t line_offset = address - line_address;
  *bytes_read = length < T5AI_FLASH_READ_CACHE_LINE_SIZE - line_offset
                    ? length
                    : T5AI_FLASH_READ_CACHE_LINE_SIZE - line_offset;
  memcpy(buffer, &s_flash_read_cache[line][line_offset], *bytes_read);
  return S_SUCCESS;
}

static void prv_invalidate_read_cache(FlashAddress address, size_t length) {
  const FlashAddress end = address + length;
  for (unsigned i = 0; i < T5AI_FLASH_READ_CACHE_LINES; ++i) {
    const FlashAddress line_begin = s_flash_read_cache_address[i];
    const FlashAddress line_end = line_begin + T5AI_FLASH_READ_CACHE_LINE_SIZE;
    if (s_flash_read_cache_valid[i] && address < line_end && line_begin < end) {
      s_flash_read_cache_valid[i] = false;
    }
  }
}

static status_t prv_write_with_retry(uint32_t offset, const void *buffer, size_t length) {
  status_t status = E_ERROR;
  for (unsigned attempt = 0; attempt < T5AI_FLASH_IPC_ATTEMPTS; ++attempt) {
    status = t5ai_flash_ipc_write(offset, buffer, length);
    if (PASSED(status)) {
      break;
    }
  }
  return status;
}

static status_t prv_erase_with_retry(uint32_t offset) {
  status_t status = E_ERROR;
  for (unsigned attempt = 0; attempt < T5AI_FLASH_IPC_ATTEMPTS; ++attempt) {
    status = t5ai_flash_ipc_erase_4k(offset);
    if (PASSED(status)) {
      break;
    }
  }
  return status;
}

static status_t prv_read_pfs_header(FlashAddress address, void *buffer, size_t length,
                                    size_t *bytes_read) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset)) {
    return S_NO_ACTION_REQUIRED;
  }

  if (!prv_pfs_header_cached(page)) {
    const FlashAddress page_address =
        FLASH_REGION_FILESYSTEM_BEGIN + page * T5AI_PFS_PAGE_SIZE;
    status_t status = prv_read_with_retry(prv_offset(page_address), s_pfs_header_cache[page],
                                          T5AI_PFS_HEADER_SIZE);
    if (FAILED(status)) {
      return status;
    }
    prv_pfs_header_mark_cached(page);
  }

  *bytes_read = length < T5AI_PFS_HEADER_SIZE - header_offset
                    ? length
                    : T5AI_PFS_HEADER_SIZE - header_offset;
  memcpy(buffer, &s_pfs_header_cache[page][header_offset], *bytes_read);
  return S_SUCCESS;
}

static void prv_update_pfs_header_cache(FlashAddress address, const void *buffer, size_t length) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset) || !prv_pfs_header_cached(page)) {
    return;
  }
  const size_t update_length = length < T5AI_PFS_HEADER_SIZE - header_offset
                                   ? length
                                   : T5AI_PFS_HEADER_SIZE - header_offset;
  memcpy(&s_pfs_header_cache[page][header_offset], buffer, update_length);
}

static void prv_cache_erased_pfs_page(FlashAddress address) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset) || header_offset != 0u) {
    return;
  }
  memset(s_pfs_header_cache[page], 0xff, T5AI_PFS_HEADER_SIZE);
  prv_pfs_header_mark_cached(page);
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
  memset(s_pfs_header_valid, 0, sizeof(s_pfs_header_valid));
  memset(s_flash_read_cache_valid, 0, sizeof(s_flash_read_cache_valid));
  s_flash_read_cache_next = 0;
  return t5ai_flash_ipc_init();
}

status_t flash_impl_read_sync(void *buffer, FlashAddress address, size_t length) {
  if (buffer == NULL || !prv_range_valid(address, length)) {
    return E_INVALID_ARGUMENT;
  }

  uint8_t *output = buffer;
  while (length != 0) {
    size_t chunk = 0;
    status_t status = prv_read_pfs_header(address, output, length, &chunk);
    if (status == S_NO_ACTION_REQUIRED) {
      status = prv_read_cached(address, output, length, &chunk);
    }
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
  status = prv_write_with_retry(prv_offset(address), buffer, written);
  if (FAILED(status)) {
    return status;
  }
  prv_invalidate_read_cache(address, written);
  prv_update_pfs_header_cache(address, buffer, written);
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
    status = prv_erase_with_retry(prv_offset(address + offset));
    if (FAILED(status)) {
      return status;
    }
    prv_invalidate_read_cache(address + offset, SUBSECTOR_SIZE_BYTES);
    prv_cache_erased_pfs_page(address + offset);
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
      /* Shared flash code asserts on a blank-check transport error before it
       * can report the erase failure asynchronously. The CP can retain a
       * wedged flash transaction across an AP reset, so conservatively report
       * "not blank" and let the T5 erase path return the recoverable error. */
      return S_FALSE;
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
