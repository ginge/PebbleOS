/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/flash.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/devices.h>
#include <pbl/soc/t5ai/flash_ipc.h>
#include <pbl/soc/t5ai/image_layout.h>

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
#include "system/status_codes.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DECLARE(driver_flash, CONFIG_DRIVER_FLASH_LOG_LEVEL);

#define T5AI_FLASH_CACHE_ALIGNMENT   32u
#define T5AI_PFS_VALID_BITS_PER_WORD 32u
#define T5AI_FLASH_ERASED_BYTE       UINT8_MAX

#define T5AI_FLASH_IPC_MAX_TRANSFER     0x200u
#define T5AI_FLASH_IPC_ATTEMPTS         2u
#define T5AI_PFS_PAGE_SIZE              0x1000u
#define T5AI_PFS_HEADER_SIZE            32u
#define T5AI_FLASH_READ_CACHE_LINE_SIZE T5AI_FLASH_IPC_MAX_TRANSFER
#define T5AI_FLASH_READ_CACHE_LINES     4u
// Bound SRAM use independently of filesystem capacity. Later pages use the
// ordinary flash read cache, with the same write/erase invalidation.
#define T5AI_PFS_PAGE_COUNT 256u

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
static uint32_t s_flash_read_cache_address[T5AI_FLASH_READ_CACHE_LINES];
static bool s_flash_read_cache_valid[T5AI_FLASH_READ_CACHE_LINES];
static unsigned s_flash_read_cache_next;

static bool prv_range_valid(uint32_t address, size_t length) {
  return length <= T5AI_FLASH_SIZE && address <= T5AI_FLASH_SIZE - length;
}

static int prv_write_allowed(uint32_t address, size_t length) {
  if (length == 0 || !prv_range_valid(address, length)) {
    return -EINVAL;
  }

  const uint32_t end = address + length - 1;
  const bool in_filesystem =
      address >= PART_FILESYSTEM_OFFSET && end < (PART_FILESYSTEM_OFFSET + PART_FILESYSTEM_SIZE);
  const bool in_system_resources =
      address >= PART_SYSTEM_RESOURCES_BANK_0_OFFSET &&
      end < (PART_SYSTEM_RESOURCES_BANK_0_OFFSET + PART_SYSTEM_RESOURCES_BANK_0_SIZE);
  const bool in_shared_prf = address >= PART_SHARED_PRF_STORAGE_OFFSET &&
                             end < (PART_SHARED_PRF_STORAGE_OFFSET + PART_SHARED_PRF_STORAGE_SIZE);
  if (!(in_filesystem || in_system_resources || in_shared_prf)) {
    return -EACCES;
  }
  return S_SUCCESS;
}

static bool prv_pfs_header_location(uint32_t address, size_t *page, size_t *offset) {
  if (address < PART_FILESYSTEM_OFFSET ||
      address >= (PART_FILESYSTEM_OFFSET + PART_FILESYSTEM_SIZE)) {
    return false;
  }
  const size_t relative = address - PART_FILESYSTEM_OFFSET;
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
static int prv_read_with_retry(uint32_t offset, void *buffer, size_t length) {
  T5_IO_BEGIN(cp_at);
  status_t status = E_ERROR;
  for (unsigned attempt = 0; attempt < T5AI_FLASH_IPC_ATTEMPTS; ++attempt) {
    status = t5ai_flash_ipc_read(offset, buffer, length);
    if (PASSED(status) || !t5ai_flash_ipc_is_available()) {
      break;
    }
  }
  T5_IO_END(T5IO_CP_READ, cp_at, length);
  return FAILED(status) ? -EIO : 0;
}

static int prv_read_cached(uint32_t address, void *buffer, size_t length, size_t *bytes_read) {
  const uint32_t line_address = address & ~(uint32_t)(T5AI_FLASH_READ_CACHE_LINE_SIZE - 1u);
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
    status_t status = prv_read_with_retry(line_address, s_flash_read_cache[line],
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

static void prv_invalidate_read_cache(uint32_t address, size_t length) {
  const uint32_t end = address + length;
  for (unsigned i = 0; i < T5AI_FLASH_READ_CACHE_LINES; ++i) {
    const uint32_t line_begin = s_flash_read_cache_address[i];
    const uint32_t line_end = line_begin + T5AI_FLASH_READ_CACHE_LINE_SIZE;
    if (s_flash_read_cache_valid[i] && address < line_end && line_begin < end) {
      s_flash_read_cache_valid[i] = false;
    }
  }
}

#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
static int prv_read_pfs_header(uint32_t address, void *buffer, size_t length, size_t *bytes_read) {
  size_t page;
  size_t header_offset;
  if (!prv_pfs_header_location(address, &page, &header_offset) || page >= T5AI_PFS_PAGE_COUNT) {
    return S_NO_ACTION_REQUIRED;
  }

  if (!prv_pfs_header_cached(page)) {
    const uint32_t page_address = PART_FILESYSTEM_OFFSET + page * T5AI_PFS_PAGE_SIZE;
    status_t status =
        prv_read_with_retry(page_address, s_pfs_header_cache[page], T5AI_PFS_HEADER_SIZE);
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

static bool prv_invalidate_pfs_header_cache(uint32_t address) {
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

static void prv_update_pfs_header_cache(uint32_t address, const void *buffer, size_t length) {
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

static void prv_cache_erased_pfs_page(uint32_t address) {
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

static int prv_verify_pfs_header_write(uint32_t address, const void *buffer, size_t length) {
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
  status_t status = prv_read_with_retry(address, readback, verify_length);
  if (FAILED(status)) {
    return status;
  }
  if (memcmp(readback, buffer, verify_length) == 0) {
    return S_SUCCESS;
  }
  PBL_LOG_ERR("T5AI PFS version verify mismatch addr=0x%08" PRIx32, address);
  return -EIO;
}

static int prv_init(const struct pbl_flash *flash) {
  if (pbl_flash_in_coredump(flash))
    return -ENOTSUP;
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
  memset(s_pfs_header_valid, 0, sizeof(s_pfs_header_valid));
#endif
  memset(s_flash_read_cache_valid, 0, sizeof(s_flash_read_cache_valid));
  s_flash_read_cache_next = 0;
  return FAILED(t5ai_flash_ipc_init()) ? -EIO : 0;
}

static int prv_read(const struct pbl_flash *flash, uint32_t address, void *buffer, size_t length) {
  if (length == 0)
    return 0;
  if (buffer == NULL || !prv_range_valid(address, length)) {
    return -EINVAL;
  }

  // The resource bank is immutable and mapped through the protected XIP aperture.
  // PFS and shared records must continue through the CP physical-flash service.
  if (address >= PART_SYSTEM_RESOURCES_BANK_0_OFFSET &&
      address < (PART_SYSTEM_RESOURCES_BANK_0_OFFSET + PART_SYSTEM_RESOURCES_BANK_0_SIZE) &&
      length <=
          (PART_SYSTEM_RESOURCES_BANK_0_OFFSET + PART_SYSTEM_RESOURCES_BANK_0_SIZE) - address) {
    T5_IO_BEGIN(xip_at);
#ifdef CONFIG_T5AI_RESOURCE_WIDE_COPY
    t5_row_copy(buffer, (const void *)(uintptr_t)(T5AI_FLASH_XIP_BASE + address), length);
#else
    memcpy(buffer, (const void *)(uintptr_t)(T5AI_FLASH_XIP_BASE + address), length);
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

static int prv_write_page(const void *buffer, uint32_t address, size_t length) {
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
  status = t5ai_flash_ipc_write(address, buffer, written);
  if (FAILED(status)) {
    PBL_LOG_ERR("T5AI flash IPC write failed addr=0x%08" PRIx32 " len=%lu status=%" PRId32, address,
                (unsigned long)written, status);
    return -EIO;
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
  return (int)written;
}

static int prv_erase(const struct pbl_flash *flash, uint32_t address, uint32_t length) {
  if ((length != SUBSECTOR_SIZE_BYTES && length != SECTOR_SIZE_BYTES) || address % length != 0)
    return -EINVAL;
  status_t status = prv_write_allowed(address, length);
  if (FAILED(status)) {
    return status;
  }
  for (size_t offset = 0; offset < length; offset += SUBSECTOR_SIZE_BYTES) {
    prv_invalidate_read_cache(address + offset, SUBSECTOR_SIZE_BYTES);
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
    prv_invalidate_pfs_header_cache(address + offset);
#endif
    status = t5ai_flash_ipc_erase_4k(address + offset);
    if (FAILED(status)) {
      return -EIO;
    }
#if defined(CONFIG_T5AI_PFS_HEADER_CACHE)
    prv_cache_erased_pfs_page(address + offset);
#endif
  }
  return 1;
}

static int prv_write(const struct pbl_flash *flash, uint32_t address, const void *buffer,
                     size_t length) {
  if (!prv_range_valid(address, length) || (buffer == nullptr && length != 0))
    return -EINVAL;
  if (length == 0)
    return 0;
  // Reject the whole request before a page can cross into a protected partition.
  int ret = prv_write_allowed(address, length);
  if (ret != 0)
    return ret;
  const uint8_t *src = buffer;
  while (length != 0) {
    ret = prv_write_page(src, address, length);
    if (ret < 0)
      return ret;
    address += ret;
    src += ret;
    length -= ret;
  }
  return 0;
}

static const struct pbl_flash_ops s_ops = {
  .init = prv_init,
  .read = prv_read,
  .write = prv_write,
  .erase = prv_erase,
};
static const struct pbl_flash_geometry s_geometry = {
  .size = T5AI_FLASH_SIZE,
  .page_size = PAGE_SIZE_BYTES,
  .sector_size = SECTOR_SIZE_BYTES,
  .subsector_size = SUBSECTOR_SIZE_BYTES,
};
PBL_FLASH_STATE_DEFINE(T5AI_FLASH);
const struct pbl_flash T5AI_FLASH = PBL_FLASH_INIT(T5AI_FLASH, "t5ai-cp-flash", nullptr, nullptr,
                                                   &s_ops, &s_geometry, nullptr, nullptr);
PBL_DEVICE_REGISTER(T5AI_FLASH, &T5AI_FLASH.dev);
