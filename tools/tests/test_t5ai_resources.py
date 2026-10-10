# SPDX-License-Identifier: Apache-2.0
"""Exercise the production T5 backend and shared resource routing on the host."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUBS = {
    'applib/applib_resource.h': '',
    'pbl/kernel/compiler.h': '#define PBL_PACKED __attribute__((packed))\n',
    'pbl/logging/logging.h': '''
#define PBL_LOG_INFO(...)
#define PBL_LOG_WRN(...)
#define PBL_LOG_ERR(...)
#define PBL_LOG_DBG(...)
''',
    'pbl/drivers/flash.h': '''
#include <stdint.h>
#define FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN 0x02400000u
#ifndef TEST_BANK_SIZE
#define TEST_BANK_SIZE 0x100000u
#endif
#define FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END (0x02400000u + TEST_BANK_SIZE)
void flash_read_bytes(uint8_t *, uint32_t, uint32_t);
void flash_write_bytes(const uint8_t *, uint32_t, uint32_t);
uint32_t flash_crc32_legacy(uint32_t, uint32_t);
''',
    'pbl/services/process_management/app_storage.h': '',
    'system/bootbits.h': '''
#define BOOT_BIT_NEW_SYSTEM_RESOURCES_AVAILABLE 1
void boot_bit_clear(int);
''',
    'pbl/services/filesystem/app_file.h': '''
void app_file_name_make(char *, size_t, uint32_t, const char *, size_t);
''',
    'system/hexdump.h': '',
    'system/passert.h': '#include <assert.h>\n#define PBL_ASSERTN(x) assert(x)\n',
    'system/version.h': '',
    'pbl/util/size.h': '#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))\n',
    'pbl/util/testing.h': '#define PBL_T_STATIC static\n',
    'resource/resource_version.auto.h': '''
#define SYSTEM_RESOURCE_VERSION ((ResourceVersion){0x12345678, 42})
''',
}
HARNESS = r'''
#define _GNU_SOURCE
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "resource/resource_storage_impl.h"
#include "resource/resource_storage_flash.h"
#include "resource/resource_version.auto.h"
#include "pbl/drivers/flash.h"

static uint8_t *bank;
static uint8_t app[RESOURCE_STORE_METADATA_BYTES + 8];
static unsigned reads, read_bytes, crcs, app_queries, builtin_queries, clears;
static uint32_t crc_address, crc_length;
static const uint32_t base = FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN;
void *tracked_memcpy(void *out, const void *in, size_t n) {
  const uintptr_t address = (uintptr_t)in;
  if (address >= base && address < base + 0x100000u) {
    assert(address - base <= TEST_BANK_SIZE && n <= TEST_BANK_SIZE - (address - base));
    reads++; read_bytes += n;
  }
  return memmove(out, in, n);
}
static ResourceManifest *manifest(void) { return (ResourceManifest *)bank; }
static ResTableEntry *table(unsigned i) {
  return (ResTableEntry *)(bank + MANIFEST_SIZE + i * TABLE_ENTRY_SIZE);
}
void flash_read_bytes(uint8_t *out, uint32_t address, uint32_t n) {
  assert(address >= base && address - base <= TEST_BANK_SIZE);
  assert(n <= TEST_BANK_SIZE - (address - base));
  memcpy(out, bank + address - base, n);
}
void flash_write_bytes(const uint8_t *p, uint32_t a, uint32_t n) {
  assert(a == base && n == MANIFEST_SIZE);
  memcpy(bank, p, n);
}
uint32_t flash_crc32_legacy(uint32_t address, uint32_t n) {
  crcs++; crc_address = address; crc_length = n; return 0xaabbccdd;
}
void boot_bit_clear(int bit) { assert(bit == 1); clears++; }
bool resource_version_matches(const ResourceVersion *a, const ResourceVersion *b) {
  return a->crc == b->crc;
}
void app_file_name_make(char *p, size_t n, uint32_t id, const char *suffix, size_t len) {
  (void)p; (void)n; (void)id; (void)suffix; (void)len;
}
static bool app_find(ResourceStoreEntry *e, ResAppNum a, uint32_t id) {
  (void)e; (void)id; app_queries++; return a == 42;
}
static bool builtin_find(ResourceStoreEntry *e, ResAppNum a, uint32_t id) {
  (void)e; (void)a; (void)id; builtin_queries++; return false;
}
static uint32_t app_read(ResourceStoreEntry *e, uint32_t offset, void *out, size_t n) {
  (void)e; assert(offset + n <= sizeof(app)); memcpy(out, app + offset, n); return n;
}
const ResourceStoreImplementation g_app_file_impl = {
  .type = ResourceStoreTypeAppFile, .find_resource = app_find,
  .get_resource = resource_storage_generic_get_resource,
  .metadata_size = resource_storage_generic_metadata_size, .read = app_read,
};
const ResourceStoreImplementation g_builtin_impl = {.find_resource = builtin_find};

static void setup(void) {
  memset(bank, 0xff, 0x100000);
  *manifest() = (ResourceManifest){.num_resources = 2, .version = SYSTEM_RESOURCE_VERSION};
  *table(0) = (ResTableEntry){1, 0, 4, 0xaabbccdd};
  *table(1) = (ResTableEntry){2, 4, 4, 0xaabbccdd};
  memcpy(bank + SYSTEM_STORE_METADATA_BYTES, "abcdefgh", 8);
  reads = read_bytes = crcs = 0;
}
static void init_expect(bool valid, unsigned expected_reads) {
  resource_storage_init();
  assert(reads == expected_reads);
  assert(read_bytes <= MANIFEST_SIZE + TABLE_ENTRY_SIZE);
  assert(crcs == 0); // Boot never scans payload or computes a full-pack CRC.
  assert(resource_storage_check(SYSTEM_APP, 0, &SYSTEM_RESOURCE_VERSION) == valid);
}
int main(void) {
  bank = mmap((void *)(uintptr_t)base, 0x100000, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  assert(bank == (void *)(uintptr_t)base);
  setup();
  if (TEST_BANK_SIZE < SYSTEM_STORE_METADATA_BYTES) {
    init_expect(false, 0); // Truncated manifest/table geometry: no mapped access.
    return 0;
  }
  init_expect(true, 2);
  assert(clears == 1);
  assert(resource_storage_flash_get_unused_bank()->begin == base);
  assert(resource_storage_flash_get_unused_bank()->end == base + TEST_BANK_SIZE);
  // Timestamp remains informational, matching stock resource_version_matches.
  setup(); manifest()->version.timestamp++; init_expect(true, 2);
  setup(); manifest()->version.crc++; init_expect(false, 1);
  setup(); manifest()->num_resources = 0; init_expect(true, 1);
  setup(); manifest()->num_resources = MAX_RESOURCES_FOR_SYSTEM_STORE + 1;
  init_expect(false, 1);
  setup(); manifest()->num_resources = UINT32_MAX; init_expect(false, 1);
  setup(); table(1)->resource_id = 3; init_expect(false, 2);
  setup(); table(1)->offset = UINT32_MAX - 1; table(1)->length = 8;
  init_expect(false, 2);
  setup(); table(1)->offset = TEST_BANK_SIZE - (uint32_t)SYSTEM_STORE_METADATA_BYTES;
  table(1)->length = 1; init_expect(false, 2);
  setup(); table(1)->offset = TEST_BANK_SIZE - (uint32_t)SYSTEM_STORE_METADATA_BYTES - 4;
  table(1)->length = 4; init_expect(true, 2); // Exact data end is valid.
  setup(); manifest()->num_resources = MAX_RESOURCES_FOR_SYSTEM_STORE;
  *table(MAX_RESOURCES_FOR_SYSTEM_STORE - 1) =
      (ResTableEntry){MAX_RESOURCES_FOR_SYSTEM_STORE, 4, 4, 0xaabbccdd};
  init_expect(true, 2); // Maximum table still costs just two metadata reads.
  setup(); init_expect(true, 2);
  ResourceStoreEntry e;
  app_queries = builtin_queries = 0;
  resource_storage_get_resource(SYSTEM_APP, 1, &e);
  assert(e.impl == &g_system_bank_impl && e.offset == SYSTEM_STORE_METADATA_BYTES);
  assert(app_queries == 1 && builtin_queries == 1);
  char out[4]; assert(resource_storage_read(&e, 0, out, 4) == 4);
  assert(memcmp(out, "abcd", 4) == 0);
  assert(e.impl->readonly_bytes(&e, false) == NULL);
#ifdef CONFIG_MMAP_RESOURCES
  assert(e.impl->readonly_bytes(&e, true) == (const uint8_t *)(uintptr_t)(base + e.offset));
  assert(resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)base));
  assert(resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)(base + TEST_BANK_SIZE - 1)));
#else
  assert(e.impl->readonly_bytes(&e, true) == NULL);
  assert(!resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)base));
#endif
  assert(!resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)(base - 1)));
  assert(!resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)(base + TEST_BANK_SIZE)));
  assert(resource_storage_check(SYSTEM_APP, 1, &SYSTEM_RESOURCE_VERSION));
  assert(crcs == 1 && crc_address == base + SYSTEM_STORE_METADATA_BYTES && crc_length == 4);
  table(0)->crc++; assert(!resource_storage_check(SYSTEM_APP, 1, &SYSTEM_RESOURCE_VERSION));
  resource_storage_get_resource(SYSTEM_APP, 3, &e); assert(e.impl == NULL);
  ResourceManifest am = {.num_resources = 1};
  ResTableEntry at = {1, 0, 4, 0};
  memcpy(app, &am, sizeof(am)); memcpy(app + MANIFEST_SIZE, &at, sizeof(at));
  memcpy(app + RESOURCE_STORE_METADATA_BYTES, "face", 4);
  app_queries = builtin_queries = 0;
  resource_storage_get_resource(42, 1, &e);
  assert(e.impl == &g_app_file_impl && app_queries == 1 && builtin_queries == 0);
  assert(resource_storage_read(&e, 0, out, 4) == 4 && memcmp(out, "face", 4) == 0);
  setup(); manifest()->version.crc++; init_expect(false, 1);
  resource_storage_get_resource(SYSTEM_APP, 1, &e); assert(e.impl == NULL);
  resource_storage_get_resource(42, 1, &e); assert(e.impl == &g_app_file_impl);
  puts("bounded boot metadata, CRC delegation, mapped reads and app/system routing passed");
  return 0;
}
'''


class T5ResourcesTest(unittest.TestCase):
    def compile_and_run(self, *defines):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            for name, content in STUBS.items():
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#pragma once\n' + content)
            harness = directory / 'test.c'
            harness.write_text(HARNESS)
            binary = directory / 'test'
            subprocess.run([
                'cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-DCONFIG_SOC_T5AI=1', '-Dmemcpy=tracked_memcpy',
                '-DTEST_BANK_SIZE=0x100000u', *defines,
                '-I', str(directory), '-I', str(ROOT / 'fw'),
                str(harness), str(ROOT / 'soc/t5ai/resource_storage.c'),
                str(ROOT / 'fw/resource/resource_storage.c'), '-o', str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_mapped_backend_and_routing(self):
        self.compile_and_run('-DCONFIG_MMAP_RESOURCES=1')

    def test_copy_only_backend(self):
        self.compile_and_run()

    def test_truncated_manifest_geometry(self):
        self.compile_and_run('-UTEST_BANK_SIZE', '-DTEST_BANK_SIZE=4u')

    def test_truncated_table_geometry(self):
        self.compile_and_run('-UTEST_BANK_SIZE', '-DTEST_BANK_SIZE=32u')


if __name__ == '__main__':
    unittest.main()
