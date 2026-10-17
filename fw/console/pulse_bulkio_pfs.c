/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pulse_bulkio_domain_handler.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <pbl/kernel/compiler.h>
#include <pbl/services/filesystem/pfs.h>
#include <pbl/util/math.h>

#include <system/passert.h>
#include <system/status_codes.h>

typedef struct PBL_PACKED PFSStatResp {
  uint8_t flags;
  uint32_t size;
} PFSStatResp;

typedef struct PBL_PACKED PFSOpenOptions {
  uint8_t op_flags;
  uint8_t filetype;
  uint32_t start_size;
  char filename[0];
} PFSOpenOptions;

#if !defined(CONFIG_RECOVERY_FW)
static int prv_open_file(void *packet_data, size_t length) {
  if (packet_data == NULL || length <= sizeof(PFSOpenOptions)) {
    return E_INVALID_ARGUMENT;
  }

  const PFSOpenOptions *options = packet_data;
  const size_t filename_length = MIN(FILE_MAX_NAME_LEN, length - sizeof(*options));

  char filename[FILE_MAX_NAME_LEN + 1];
  memcpy(filename, options->filename, filename_length);
  filename[filename_length] = '\0';

  int fd = pfs_open(filename, options->op_flags, options->filetype, options->start_size);
  return fd;
}

static int prv_fd_from_context(void *context) {
  return (uintptr_t)context;
}

static int pfs_domain_read(uint8_t *buf, uint32_t address, uint32_t length, void *context) {
  if (address > INT_MAX) {
    return E_RANGE;
  }

  int fd = prv_fd_from_context(context);
  int status = pfs_seek(fd, (int)address, FSeekSet);
  if (FAILED(status)) {
    return status;
  }
  return pfs_read(fd, buf, length);
}

static int pfs_domain_write(uint8_t *buf, uint32_t address, uint32_t length, void *context) {
  if (address > INT_MAX) {
    return E_RANGE;
  }

  int fd = prv_fd_from_context(context);
  int status = pfs_seek(fd, (int)address, FSeekSet);
  if (FAILED(status)) {
    return status;
  }
  return pfs_write(fd, buf, length);
}

static int pfs_domain_stat(uint8_t *resp, size_t resp_max_len, void *context) {
  if (resp == NULL || resp_max_len < sizeof(PFSStatResp)) {
    return E_RANGE;
  }

  int fd = prv_fd_from_context(context);

  PFSStatResp *stat_resp = (PFSStatResp *)resp;
  *stat_resp = (PFSStatResp){.flags = 0, .size = (uint32_t)pfs_get_file_size(fd)};

  return sizeof(PFSStatResp);
}

static status_t pfs_domain_erase(uint8_t *packet_data, size_t length, uint8_t cookie) {
  if (packet_data == NULL || length == 0) {
    return E_INVALID_ARGUMENT;
  }

  const size_t filename_length = MIN(FILE_MAX_NAME_LEN, length);
  char filename[FILE_MAX_NAME_LEN + 1];
  memcpy(filename, packet_data, filename_length);
  filename[filename_length] = '\0';

  return pfs_remove(filename);
}

static status_t pfs_domain_open(uint8_t *packet_data, size_t length, void **resp) {
  int fd = prv_open_file(packet_data, length);
  if (fd < 0) {
    return fd;
  }

  *resp = (void *)(uintptr_t)fd;
  return S_SUCCESS;
}

static status_t pfs_domain_close(void *context) {
  int fd = prv_fd_from_context(context);
  return pfs_close(fd);
}

PulseBulkIODomainHandler pulse_bulkio_domain_pfs = {
  .id = PulseBulkIODomainType_PFS,
  .open_proc = pfs_domain_open,
  .read_proc = pfs_domain_read,
  .write_proc = pfs_domain_write,
  .close_proc = pfs_domain_close,
  .stat_proc = pfs_domain_stat,
  .erase_proc = pfs_domain_erase
};
#endif
