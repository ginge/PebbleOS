/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/flash/flash_impl.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/flash_ipc.h>

#ifdef CONFIG_T5AI_IO_PROFILE
#include <pbl/soc/t5ai/io_profile.h>
#else
#define T5_IO_BEGIN(name)
#define T5_IO_END(kind, name, amount) \
  do {                                \
  } while (0)
#define T5_IO_ADD(kind, elapsed, amount) \
  do {                                   \
  } while (0)
#endif

#ifdef CONFIG_T5AI_RESOURCE_WIDE_COPY
#include <pbl/soc/t5ai/row_memory.h>
#endif
#include "flash_region/flash_region.h"
#include "system/rtc_registers.h"
#include "system/status_codes.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DECLARE(driver_flash, CONFIG_DRIVER_FLASH_LOG_LEVEL);

#define T5AI_FLASH_CACHE_ALIGNMENT   32u
#define T5AI_PFS_VALID_BITS_PER_WORD 32u
#define T5AI_FLASH_ERASED_BYTE       UINT8_MAX

#define T5AI_FLASH_IPC_MAX_TRANSFER 0x200u
#define T5AI_FLASH_IPC_ATTEMPTS     2u
// CP erase requests complete before begin() returns. The shared flash API
// still needs a nonzero initial poll delay to consume the completion and release
// its erase semaphore. It computes duration * 7 / 8, so 2 ms yields 1 ms;
// 0 or 1 would skip polling and strand the completion callback.
#define T5AI_FLASH_COMPLETION_POLL_DURATION_MS 2u
#define T5AI_PFS_PAGE_SIZE                     0x1000u
#define T5AI_PFS_HEADER_SIZE                   32u
#define T5AI_FLASH_READ_CACHE_LINE_SIZE        T5AI_FLASH_IPC_MAX_TRANSFER
#define T5AI_FLASH_READ_CACHE_LINES            4u
// Bound SRAM use independently of filesystem capacity. Later pages use the
// ordinary flash read cache, with the same write/erase invalidation.
#define T5AI_PFS_PAGE_COUNT 256u

typedef enum {
  T5AI_FLASH_IDLE,
  T5AI_FLASH_WRITE_COMPLETE,
  T5AI_FLASH_ERASE_COMPLETE,
} T5AIFlashOperation;

static T5AIFlashOperation s_operation;
static bool s_protected;
static FlashAddress s_protected_start;
static FlashAddress s_protected_end;
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
/* Keep flash data in internal SRAM. The T5AI_DISPLAY_RAM reservation is
 * write-back PSRAM; using it here makes a flash read and its cached copy share
 * a second possible corruption domain, which defeats read diagnostics and can
 * preserve a bad PFS header for the rest of the boot. */
static uint8_t s_pfs_header_cache[T5AI_PFS_PAGE_COUNT][T5AI_PFS_HEADER_SIZE]
    __attribute__((aligned(T5AI_FLASH_CACHE_ALIGNMENT)));
static uint32_t s_pfs_header_valid[(T5AI_PFS_PAGE_COUNT + T5AI_PFS_VALID_BITS_PER_WORD - 1u) /
                                   T5AI_PFS_VALID_BITS_PER_WORD];
#endif
// Cache IPC-sized lines to amortize CP request overhead for nearby reads.
// Immutable system resources bypass this cache through their mapped aperture.
static uint8_t s_flash_read_cache[T5AI_FLASH_READ_CACHE_LINES][T5AI_FLASH_READ_CACHE_LINE_SIZE]
    __attribute__((aligned(T5AI_FLASH_CACHE_ALIGNMENT)));
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
  const bool in_system_resources = address >= FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN &&
                                   end < FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END;
  const bool in_shared_prf =
      address >= FLASH_REGION_SHARED_PRF_STORAGE_BEGIN && end < FLASH_REGION_SHARED_PRF_STORAGE_END;
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

#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
static bool prv_pfs_header_cached(size_t page) {
  return (s_pfs_header_valid[page / T5AI_PFS_VALID_BITS_PER_WORD] &
          (1u << (page % T5AI_PFS_VALID_BITS_PER_WORD))) != 0u;
}

static void prv_pfs_header_mark_cached(size_t page) {
  s_pfs_header_valid[page / T5AI_PFS_VALID_BITS_PER_WORD] |=
      1u << (page % T5AI_PFS_VALID_BITS_PER_WORD);
}

#endif
static status_t prv_read_with_retry(uint32_t offset, void *buffer, size_t length) {
  T5_IO_BEGIN(cp_at);
  status_t status = E_ERROR;
  for (unsigned attempt = 0; attempt < T5AI_FLASH_IPC_ATTEMPTS; ++attempt) {
    status = t5ai_flash_ipc_read(offset, buffer, length);
    if (PASSED(status) || !t5ai_flash_ipc_is_available()) {
      break;
    }
  }
  T5_IO_END(T5IO_CP_READ, cp_at, length);
  return status;
}

static status_t prv_read_cached(FlashAddress address, void *buffer, size_t length,
                                size_t *bytes_read) {
  const FlashAddress line_address = address & ~(FlashAddress)(T5AI_FLASH_READ_CACHE_LINE_SIZE - 1u);
  unsigned line = T5AI_FLASH_READ_CACHE_LINES;
  for (unsigned i = 0; i < T5AI_FLASH_READ_CACHE_LINES; ++i) {
    if (s_flash_read_cache_valid[i] && s_flash_read_cache_address[i] == line_address) {
      line = i;
      break;
    }
  }

  if (line != T5AI_FLASH_READ_CACHE_LINES)
    T5_IO_ADD(T5IO_CACHE_HIT, 0u, 0u);
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

#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
static status_t prv_read_pfs_header(FlashAddress address, void *buffer, size_t length,
                                    size_t *bytes_read) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset) || page >= T5AI_PFS_PAGE_COUNT) {
    return S_NO_ACTION_REQUIRED;
  }

  if (!prv_pfs_header_cached(page)) {
    const FlashAddress page_address = FLASH_REGION_FILESYSTEM_BEGIN + page * T5AI_PFS_PAGE_SIZE;
    status_t status = prv_read_with_retry(prv_offset(page_address), s_pfs_header_cache[page],
                                          T5AI_PFS_HEADER_SIZE);
    if (FAILED(status)) {
      return status;
    }
    prv_pfs_header_mark_cached(page);
  }

  *bytes_read =
      length < T5AI_PFS_HEADER_SIZE - header_offset ? length : T5AI_PFS_HEADER_SIZE - header_offset;
  memcpy(buffer, &s_pfs_header_cache[page][header_offset], *bytes_read);
  return S_SUCCESS;
}

static bool prv_invalidate_pfs_header_cache(FlashAddress address) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset) || page >= T5AI_PFS_PAGE_COUNT ||
      !prv_pfs_header_cached(page)) {
    return false;
  }
  s_pfs_header_valid[page / T5AI_PFS_VALID_BITS_PER_WORD] &=
      ~(1u << (page % T5AI_PFS_VALID_BITS_PER_WORD));
  return true;
}

static void prv_update_pfs_header_cache(FlashAddress address, const void *buffer, size_t length) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset) || page >= T5AI_PFS_PAGE_COUNT) {
    return;
  }
  const size_t update_length =
      length < T5AI_PFS_HEADER_SIZE - header_offset ? length : T5AI_PFS_HEADER_SIZE - header_offset;
  const uint8_t *programmed = buffer;
  for (size_t i = 0; i < update_length; ++i) {
    s_pfs_header_cache[page][header_offset + i] &= programmed[i];
  }
  prv_pfs_header_mark_cached(page);
}

static void prv_cache_erased_pfs_page(FlashAddress address) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset) || page >= T5AI_PFS_PAGE_COUNT ||
      header_offset != 0u) {
    return;
  }
  memset(s_pfs_header_cache[page], T5AI_FLASH_ERASED_BYTE, T5AI_PFS_HEADER_SIZE);
  prv_pfs_header_mark_cached(page);
}

#endif

static status_t prv_verify_pfs_header_write(FlashAddress address, const void *buffer,
                                            size_t length) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset) || header_offset != 0u ||
      length < sizeof(uint16_t)) {
    return S_SUCCESS;
  }

  /* PFS progressively clears mutable header fields, so their requested bytes
   * need not equal the resulting NOR contents. The immutable version at page
   * offset zero is written onto an erased page and must compare exactly. */
  const size_t verify_length = sizeof(uint16_t);
  uint8_t readback[T5AI_PFS_HEADER_SIZE];
  status_t status = prv_read_with_retry(prv_offset(address), readback, verify_length);
  if (FAILED(status)) {
    return status;
  }
  if (memcmp(readback, buffer, verify_length) == 0) {
    return S_SUCCESS;
  }
  PBL_LOG_ERR("T5AI PFS version verify mismatch addr=0x%08" PRIx32, address);
  return E_ERROR;
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
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
  memset(s_pfs_header_valid, 0, sizeof(s_pfs_header_valid));
#endif
  memset(s_flash_read_cache_valid, 0, sizeof(s_flash_read_cache_valid));
  s_flash_read_cache_next = 0;
  return t5ai_flash_ipc_init();
}

status_t flash_impl_read_sync(void *buffer, FlashAddress address, size_t length) {
  if (buffer == NULL || !prv_range_valid(address, length)) {
    return E_INVALID_ARGUMENT;
  }

  // The resource bank is immutable and mapped through the protected XIP aperture.
  // PFS and shared records must continue through the CP physical-flash service.
  if (address >= FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN &&
      address < FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END &&
      length <= FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END - address) {
    T5_IO_BEGIN(xip_at);
#ifdef CONFIG_T5AI_RESOURCE_WIDE_COPY
    t5_row_copy(buffer, (const void *)(uintptr_t)address, length);
#else
    memcpy(buffer, (const void *)(uintptr_t)address, length);
#endif
    T5_IO_END(T5IO_XIP_COPY, xip_at, length);
    return S_SUCCESS;
  }

  uint8_t *output = buffer;
  while (length != 0) {
    size_t chunk = 0;
    status_t status;
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
    status = prv_read_pfs_header(address, output, length, &chunk);
    if (status == S_NO_ACTION_REQUIRED)
#endif
    {
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
    PBL_LOG_ERR("T5AI flash write policy rejected addr=0x%08" PRIx32 " len=%lu status=%" PRId32,
                address, (unsigned long)written, status);
    return status;
  }
  // A failed mutation may still change flash. Retire cached data before issuing it.
  prv_invalidate_read_cache(address, written);
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
  const bool header_was_cached = prv_invalidate_pfs_header_cache(address);
#endif
  /* A timeout does not prove that the CP failed to program the page. Retrying
   * a mutation can duplicate or overlap an operation, so only reads retry. */
  status = t5ai_flash_ipc_write(prv_offset(address), buffer, written);
  if (FAILED(status)) {
    PBL_LOG_ERR("T5AI flash IPC write failed addr=0x%08" PRIx32 " len=%lu status=%" PRId32, address,
                (unsigned long)written, status);
    return status;
  }
  status = prv_verify_pfs_header_write(address, buffer, written);
  if (FAILED(status)) {
    PBL_LOG_ERR("T5AI PFS version verification failed addr=0x%08" PRIx32 " len=%lu status=%" PRId32,
                address, (unsigned long)written, status);
    return status;
  }
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
  if (header_was_cached) {
    prv_update_pfs_header_cache(address, buffer, written);
  }
#endif
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
    prv_invalidate_read_cache(address + offset, SUBSECTOR_SIZE_BYTES);
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
    prv_invalidate_pfs_header_cache(address + offset);
#endif
    status = t5ai_flash_ipc_erase_4k(prv_offset(address + offset));
    if (FAILED(status)) {
      return status;
    }
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
    prv_cache_erased_pfs_page(address + offset);
#endif
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
      if (buffer[i] != T5AI_FLASH_ERASED_BYTE) {
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

void flash_impl_enable_write_protection(void) {
}

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
  return T5AI_FLASH_COMPLETION_POLL_DURATION_MS;
}

uint32_t flash_impl_get_typical_sector_erase_duration_ms(void) {
  return T5AI_FLASH_COMPLETION_POLL_DURATION_MS;
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
