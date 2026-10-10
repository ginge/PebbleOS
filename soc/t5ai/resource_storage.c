/* SPDX-License-Identifier: Apache-2.0 */

#include "resource/resource_storage_flash.h"
#include "resource/resource_storage_impl.h"
#include "resource/resource_version.auto.h"

#include <pbl/drivers/flash.h>
#include <pbl/logging/logging.h>
#include "pbl/services/process_management/app_storage.h"
#include "system/bootbits.h"

#include <string.h>

static const SystemResourceBank s_bank = {
  .begin = FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN,
  .end = FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END,
};

static bool s_valid_resources_found;

static bool prv_metadata_valid(const ResourceVersion *expected_version) {
  const uint32_t bank_size = s_bank.end - s_bank.begin;
  if (s_bank.end < s_bank.begin || bank_size < SYSTEM_STORE_METADATA_BYTES) {
    return false;
  }

  ResourceManifest manifest;
  memcpy(&manifest, (const void *)(uintptr_t)s_bank.begin, sizeof(manifest));
  if (!resource_version_matches(&manifest.version, expected_version)) {
    return false;
  }
  if (manifest.num_resources == 0) {
    return true;
  }
  if (manifest.num_resources > MAX_RESOURCES_FOR_SYSTEM_STORE) {
    return false;
  }

  ResTableEntry last_entry;
  const uint32_t last_entry_address =
      s_bank.begin + MANIFEST_SIZE + (manifest.num_resources - 1u) * TABLE_ENTRY_SIZE;
  memcpy(&last_entry, (const void *)(uintptr_t)last_entry_address, sizeof(last_entry));

  const uint32_t resource_end = last_entry.offset + last_entry.length;
  return last_entry.resource_id == manifest.num_resources && resource_end >= last_entry.offset &&
         resource_end <= bank_size - SYSTEM_STORE_METADATA_BYTES;
}

static void prv_init(void) {
  boot_bit_clear(BOOT_BIT_NEW_SYSTEM_RESOURCES_AVAILABLE);
  // Immutable resources are flashed with matching firmware. Check bounded metadata at boot;
  // individual resource CRCs are still checked on access by the generic implementation.
  s_valid_resources_found = prv_metadata_valid(&SYSTEM_RESOURCE_VERSION);
  if (s_valid_resources_found) {
    PBL_LOG_INFO("Valid system resources found!");
  } else {
    PBL_LOG_WRN("Invalid system resource metadata");
  }
}

const SystemResourceBank *resource_storage_flash_get_unused_bank(void) {
  // Preserve the single-bank interface; update entry points remain disabled on T5.
  return &s_bank;
}

static uint32_t prv_metadata_size(ResourceStoreEntry *entry) {
  return SYSTEM_STORE_METADATA_BYTES;
}

static uint32_t prv_get_crc(ResourceStoreEntry *entry, uint32_t num_bytes, uint32_t entry_offset) {
  const uint32_t start_offset = resource_store_get_metadata_size(entry) + entry_offset;
  return flash_crc32_legacy(s_bank.begin + start_offset, num_bytes);
}

static uint32_t prv_read(ResourceStoreEntry *entry, uint32_t offset, void *data, size_t num_bytes) {
  flash_read_bytes(data, s_bank.begin + offset, num_bytes);
  return num_bytes;
}

bool resource_storage_flash_bytes_are_readonly(const void *bytes) {
#ifdef CONFIG_MMAP_RESOURCES
  return bytes >= (const void *)(uintptr_t)s_bank.begin &&
         bytes < (const void *)(uintptr_t)s_bank.end;
#else
  return false;
#endif
}

static const uint8_t *prv_readonly_bytes(ResourceStoreEntry *entry, bool has_privileged_access) {
#ifdef CONFIG_MMAP_RESOURCES
  if (has_privileged_access) {
    return (const uint8_t *)(uintptr_t)(s_bank.begin + entry->offset);
  }
#endif
  return NULL;
}

static void prv_clear(ResourceStoreEntry *entry) {
  uint8_t buffer[MANIFEST_SIZE] = {0};
  flash_write_bytes(buffer, s_bank.begin, MANIFEST_SIZE);
}

bool resource_storage_system_bank_check(ResAppNum app_num, uint32_t resource_id,
                                        ResourceStoreEntry *entry,
                                        const ResourceVersion *expected_version) {
  if (!s_valid_resources_found) {
    return false;
  }
  if (resource_id == 0) {
    return true;
  }
  return resource_storage_generic_check(app_num, resource_id, entry, expected_version);
}

static bool prv_find_resource(ResourceStoreEntry *entry, ResAppNum app_num, uint32_t resource_id) {
  return app_num == SYSTEM_APP && s_valid_resources_found;
}

const ResourceStoreImplementation g_system_bank_impl = {
  .type = ResourceStoreTypeSystemBank,
  .init = prv_init,
  .clear = prv_clear,
  .check = resource_storage_system_bank_check,
  .metadata_size = prv_metadata_size,
  .find_resource = prv_find_resource,
  .get_resource = resource_storage_generic_get_resource,
  .get_length = resource_storage_generic_get_length,
  .get_crc = prv_get_crc,
  .write = resource_storage_generic_write,
  .read = prv_read,
  .readonly_bytes = prv_readonly_bytes,
  .watch = resource_storage_generic_watch,
  .unwatch = resource_storage_generic_unwatch,
};
