/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/process_management/app_storage.h"
#include "resource/resource_storage_flash.h"
#include "resource/resource_storage_impl.h"
#include "resource/resource_version.auto.h"
#include "system/bootbits.h"

#include <string.h>

#include <pbl/drivers/flash.h>
#include <pbl/logging/logging.h>

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
  const uint32_t capacity = bank_size - SYSTEM_STORE_METADATA_BYTES;
  if (last_entry.offset > capacity || last_entry.length > capacity - last_entry.offset) {
    return false;
  }
  const uint32_t payload_size = last_entry.offset + last_entry.length;
  for (uint32_t i = 0; i < manifest.num_resources; ++i) {
    ResTableEntry table_entry;
    const uint32_t address = s_bank.begin + MANIFEST_SIZE + i * TABLE_ENTRY_SIZE;
    memcpy(&table_entry, (const void *)(uintptr_t)address, sizeof(table_entry));
    if (table_entry.resource_id != i + 1 || table_entry.offset > payload_size ||
        table_entry.length > payload_size - table_entry.offset) {
      return false;
    }
  }
  return true;
}

static void prv_init(void) {
  boot_bit_clear(BOOT_BIT_NEW_SYSTEM_RESOURCES_AVAILABLE);
  ResourceStoreEntry entry = {
    .id = 0,
    .impl = &g_system_bank_impl,
    .length = s_bank.end - s_bank.begin,
  };
  s_valid_resources_found =
      prv_metadata_valid(&SYSTEM_RESOURCE_VERSION) &&
      resource_storage_generic_check(SYSTEM_APP, 0, &entry, &SYSTEM_RESOURCE_VERSION);
  if (s_valid_resources_found) {
    PBL_LOG_INFO("Valid system resources found!");
  } else {
    PBL_LOG_WRN("Invalid system resources");
  }
}

const SystemResourceBank *resource_storage_flash_get_unused_bank(void) {
  return NULL;
}

static uint32_t prv_metadata_size(ResourceStoreEntry *entry) {
  return SYSTEM_STORE_METADATA_BYTES;
}

static uint32_t prv_get_crc(ResourceStoreEntry *entry, uint32_t num_bytes, uint32_t entry_offset) {
  const uint32_t capacity = s_bank.end - s_bank.begin;
  const uint32_t metadata = resource_store_get_metadata_size(entry);
  if (metadata > capacity || entry_offset > capacity - metadata ||
      num_bytes > capacity - metadata - entry_offset) {
    return UINT32_MAX;
  }
  return flash_crc32_legacy(s_bank.begin + metadata + entry_offset, num_bytes);
}

static uint32_t prv_read(ResourceStoreEntry *entry, uint32_t offset, void *data, size_t num_bytes) {
  const uint32_t capacity = s_bank.end - s_bank.begin;
  if (offset > capacity || num_bytes > capacity - offset) {
    return 0;
  }
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
  PBL_LOG_WRN("Cannot clear immutable system resources");
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
