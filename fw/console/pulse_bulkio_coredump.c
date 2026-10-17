/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pulse_bulkio_domain_handler.h"

#include <console/pulse_protocol_impl.h>
#include <flash_region/flash_region.h>
#include <stdint.h>
#include <string.h>

#include <pbl/drivers/flash.h>
#include <pbl/kernel/compiler.h>

#include <kernel/core_dump.h>
#include <kernel/core_dump_private.h>
#include <system/status_codes.h>

typedef struct PBL_PACKED CoredumpStatResp {
  uint8_t flags;
  uint8_t unread;
  uint32_t size;
} CoredumpStatResp;

static status_t prv_get_coredump_index(const void *packet_data, size_t length, uint32_t *index) {
  if (packet_data == NULL || index == NULL || length != sizeof(*index)) {
    return E_INVALID_ARGUMENT;
  }

  memcpy(index, packet_data, sizeof(*index));
  return *index < CORE_DUMP_MAX_IMAGES ? S_SUCCESS : E_INVALID_ARGUMENT;
}

static bool prv_coredump_range_valid(uint32_t address, uint32_t length) {
  const uint32_t data_size = CORE_DUMP_MAX_SIZE - sizeof(CoreDumpFlashRegionHeader);
  return length > 0 && length <= data_size && address <= data_size - length;
}

static int coredump_domain_read(uint8_t *buf, uint32_t address, uint32_t length, void *context) {
  if (buf == NULL || !prv_coredump_range_valid(address, length)) {
    return E_RANGE;
  }

  uint32_t index = (uintptr_t)context;
  uint32_t core_base_addr = core_dump_get_slot_address(index) + sizeof(CoreDumpFlashRegionHeader);
  flash_read_bytes(buf, core_base_addr + address, length);
  return length;
}

static int coredump_domain_write(uint8_t *buf, uint32_t address, uint32_t length, void *context) {
  return E_INVALID_OPERATION;
}

static int coredump_domain_stat(uint8_t *resp, size_t resp_max_len, void *context) {
  if (resp == NULL || resp_max_len < sizeof(CoredumpStatResp)) {
    return E_RANGE;
  }

  uint32_t index = (uintptr_t)context;
  uint32_t addr = core_dump_get_slot_address(index);

  CoredumpStatResp *stat_resp = (CoredumpStatResp *)resp;
  *stat_resp = (CoredumpStatResp){
    .flags = 0,
    .unread = core_dump_is_unread_available(addr),
    // Size of 0 indicates no core dump available
    .size = 0
  };

  if (stat_resp->unread == 1) {
    uint32_t size = 0;
    status_t ret = core_dump_size(addr, &size);
    stat_resp->size = size;

    if (FAILED(ret)) {
      return ret;
    }
  }

  return sizeof(CoredumpStatResp);
}

static status_t coredump_domain_erase(uint8_t *packet_data, size_t length, uint8_t cookie) {
  uint32_t index;
  status_t status = prv_get_coredump_index(packet_data, length, &index);
  if (FAILED(status)) {
    return status;
  }

  uint32_t addr = core_dump_get_slot_address(index);
  core_dump_mark_read(addr);
  return S_SUCCESS;
}

static status_t coredump_domain_open(uint8_t *packet_data, size_t length, void **resp) {
  if (resp == NULL) {
    return E_INVALID_ARGUMENT;
  }

  uint32_t index;
  status_t status = prv_get_coredump_index(packet_data, length, &index);
  if (FAILED(status)) {
    return status;
  }

  *resp = (void *)(uintptr_t)index;
  return S_SUCCESS;
}

static status_t coredump_domain_close(void *context) {
  return S_SUCCESS;
}

PulseBulkIODomainHandler pulse_bulkio_domain_coredump = {
  .id = PulseBulkIODomainType_Coredump,
  .open_proc = coredump_domain_open,
  .close_proc = coredump_domain_close,
  .read_proc = coredump_domain_read,
  .write_proc = coredump_domain_write,
  .stat_proc = coredump_domain_stat,
  .erase_proc = coredump_domain_erase
};
