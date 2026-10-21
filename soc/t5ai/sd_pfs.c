/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system/passert.h"

#include <string.h>

#include <pbl/drivers/t5ai/sd.h>
#include <pbl/kernel/kernel.h>
#include <pbl/logging/logging.h>
#include <pbl/services/filesystem/pfs.h>

#define VOLUME_SIZE    (32u * 1024u * 1024u)
#define HEADER_SECTOR  2048u
#define FIRST_SECTOR   (HEADER_SECTOR + 128u)
#define CACHE_LINES    8u
#define ERASE_BYTES    (T5AI_SD_MAX_BLOCKS * T5AI_SD_SECTOR_SIZE)
#define INVALID_SECTOR UINT32_MAX

PBL_LOG_MODULE_DEFINE(t5ai_sd_pfs, LOG_LEVEL_DEBUG);
static PBL_MUTEX_DEFINE(s_lock);
static uint8_t s_cache[CACHE_LINES][512] __attribute__((section(".t5ai_board_cache"), aligned(32)));
static uint32_t s_tags[CACHE_LINES];
static unsigned s_next;
static uint8_t *s_work;
static bool s_new_volume;

typedef struct {
  char magic[8];
  uint32_t version, size, first_sector, size_inverse;
  uint8_t reserved[488];
} VolumeHeader;
_Static_assert(sizeof(VolumeHeader) == 512, "SD volume header size");

static void prv_io(int status) {
  // Stock FTL has no error return. Stop before PFS can acknowledge failed I/O.
  PBL_ASSERT(status == 0, "SD PFS I/O error %d", status);
}

static void prv_bounds(uint32_t offset, size_t size) {
  PBL_ASSERTN(size <= VOLUME_SIZE && offset <= VOLUME_SIZE - size);
}

static void prv_invalidate(void) {
  for (unsigned i = 0; i < CACHE_LINES; ++i) {
    s_tags[i] = INVALID_SECTOR;
  }
}

static uint8_t *prv_sector(uint32_t sector) {
  for (unsigned i = 0; i < CACHE_LINES; ++i) {
    if (s_tags[i] == sector) {
      return s_cache[i];
    }
  }
  const unsigned slot = s_next++ % CACHE_LINES;
  s_tags[slot] = INVALID_SECTOR;
  prv_io(t5ai_sd_read(sector, s_cache[slot], 1));
  s_tags[slot] = sector;
  return s_cache[slot];
}

void __wrap_ftl_read(void *buffer, size_t size, uint32_t offset) {
  prv_bounds(offset, size);
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  s_work = t5ai_sd_lock_workspace();
  uint8_t *out = buffer;
  while (size) {
    const uint32_t sector = FIRST_SECTOR + offset / 512u;
    const size_t within = offset % 512u;
    size_t bytes;
    if (!within && size >= 1024u) {
      uint32_t count = size / 512u;
      if (count > T5AI_SD_MAX_BLOCKS)
        count = T5AI_SD_MAX_BLOCKS;
      bytes = count * 512u;
      prv_io(t5ai_sd_read(sector, out, count));
    } else {
      bytes = 512u - within;
      if (bytes > size)
        bytes = size;
      memcpy(out, prv_sector(sector) + within, bytes);
    }
    out += bytes;
    offset += bytes;
    size -= bytes;
  }
  t5ai_sd_unlock_workspace();
  pbl_mutex_unlock(&s_lock);
}

void __wrap_ftl_write(const void *buffer, size_t size, uint32_t offset) {
  prv_bounds(offset, size);
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  s_work = t5ai_sd_lock_workspace();
  const uint8_t *in = buffer;
  while (size) {
    const uint32_t sector = FIRST_SECTOR + offset / 512u;
    const size_t within = offset % 512u;
    if (!within && size >= 1024u) {
      uint32_t count = size / 512u;
      if (count > T5AI_SD_MAX_BLOCKS)
        count = T5AI_SD_MAX_BLOCKS;
      const size_t bytes = count * 512u;
      prv_io(t5ai_sd_read(sector, s_work, count));
      for (size_t i = 0; i < bytes; ++i)
        s_work[i] &= in[i];
      prv_io(t5ai_sd_write(sector, s_work, count));
      prv_invalidate();
      offset += bytes;
      size -= bytes;
      in += bytes;
    } else {
      size_t bytes = 512u - within;
      if (bytes > size)
        bytes = size;
      uint8_t *data = prv_sector(sector);
      for (size_t i = 0; i < bytes; ++i)
        data[within + i] &= in[i];
      prv_io(t5ai_sd_write(sector, data, 1));
      offset += bytes;
      size -= bytes;
      in += bytes;
    }
  }
  t5ai_sd_unlock_workspace();
  pbl_mutex_unlock(&s_lock);
}

static void prv_erase(uint32_t size, uint32_t offset) {
  prv_bounds(offset, size);
  PBL_ASSERTN(!(offset % 512u) && !(size % 512u));
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  s_work = t5ai_sd_lock_workspace();
  prv_invalidate();
  memset(s_work, 0xff, ERASE_BYTES);
  while (size) {
    const uint32_t bytes = size < ERASE_BYTES ? size : ERASE_BYTES;
    prv_io(t5ai_sd_write(FIRST_SECTOR + offset / 512u, s_work, bytes / 512u));
    offset += bytes;
    size -= bytes;
    pbl_thread_yield();
  }
  t5ai_sd_unlock_workspace();
  pbl_mutex_unlock(&s_lock);
}

void __wrap_ftl_erase_sector(uint32_t size, uint32_t offset) {
  PBL_ASSERTN(size == 65536u && !(offset % size));
  prv_erase(size, offset);
}

void __wrap_ftl_erase_subsector(uint32_t size, uint32_t offset) {
  PBL_ASSERTN(size == 4096u && !(offset % size));
  prv_erase(size, offset);
}

void __wrap_filesystem_regions_erase_all(void) {
  prv_erase(VOLUME_SIZE, 0);
}

uint32_t __wrap_ftl_get_size(void) {
  return VOLUME_SIZE;
}

void __wrap_ftl_format(void) {
}

void __wrap_ftl_populate_region_list(void) {
  // Never probe NOR regions or grow the card volume using flash addresses.
  pfs_set_size(VOLUME_SIZE, false);
  if (s_new_volume) {
    pfs_format(true);
  } else if (pfs_active()) {
    pfs_reboot_cleanup();
  }
}

status_t __real_pfs_init(bool run_filesystem_check);
status_t __wrap_pfs_init(bool run_filesystem_check) {
  PBL_LOG_INFO("SD PFS initialization");
  prv_io(t5ai_sd_init());
  PBL_ASSERTN(t5ai_sd_sector_count() >= FIRST_SECTOR + VOLUME_SIZE / 512u);
  prv_invalidate();
  s_work = t5ai_sd_lock_workspace();
  prv_io(t5ai_sd_read(HEADER_SECTOR, s_work, 1));
  const VolumeHeader *header = (const VolumeHeader *)s_work;
  s_new_volume = memcmp(header->magic, "PBLSDPFS", 8) || header->version != 1 ||
                 header->size != VOLUME_SIZE || header->first_sector != FIRST_SECTOR ||
                 header->size_inverse != ~VOLUME_SIZE;
  t5ai_sd_unlock_workspace();
  PBL_LOG_INFO("SD PFS: 32 MiB, new=%u", s_new_volume);
  const status_t status = __real_pfs_init(run_filesystem_check);
  if (status == S_SUCCESS && s_new_volume) {
    s_work = t5ai_sd_lock_workspace();
    memset(s_work, 0, 512);
    VolumeHeader *new_header = (VolumeHeader *)s_work;
    memcpy(new_header->magic, "PBLSDPFS", 8);
    new_header->version = 1;
    new_header->size = VOLUME_SIZE;
    new_header->first_sector = FIRST_SECTOR;
    new_header->size_inverse = ~VOLUME_SIZE;
    prv_io(t5ai_sd_write(HEADER_SECTOR, s_work, 1));
    t5ai_sd_unlock_workspace();
  }
  return status;
}
