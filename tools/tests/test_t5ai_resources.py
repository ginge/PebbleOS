# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production T5 backend and shared resource routing on the host."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]
STUBS = {
    # Use the real layout; override only geometry deliberately varied by a case.
    "pbl/board/partitions.h": """
#include_next <pbl/board/partitions.h>
enum { TEST_MAPPING_SIZE = PART_SYSTEM_RESOURCES_BANK_0_SIZE };
#ifdef TEST_BANK_SIZE
#undef PART_SYSTEM_RESOURCES_BANK_0_SIZE
#define PART_SYSTEM_RESOURCES_BANK_0_SIZE TEST_BANK_SIZE
#endif
#ifdef TEST_STOCK_BANKS
// Stock backend coverage needs a spare bank, absent on the production board.
#undef PART_SYSTEM_RESOURCES_BANK_1_SIZE
#define PART_SYSTEM_RESOURCES_BANK_1_SIZE PART_SYSTEM_RESOURCES_BANK_0_SIZE
#endif
""",
    "applib/applib_resource.h": "",
    "pbl/logging/logging.h": """
#define PBL_LOG_INFO(...)
#define PBL_LOG_WRN(...)
#define PBL_LOG_ERR(...)
#define PBL_LOG_DBG(...) test_log(__VA_ARGS__)
static inline void test_log(const char *fmt, ...) { (void)fmt; }
""",
    "pbl/services/process_management/app_storage.h": "",
    "system/bootbits.h": """
#define BOOT_BIT_NEW_SYSTEM_RESOURCES_AVAILABLE 1
void boot_bit_clear(int);
""",
    "pbl/services/process_management/app_file.h": """
void app_file_path_make(char *, size_t, uint32_t, const char *, size_t);
""",
    "system/hexdump.h": "",
    "pbl/kernel/assert.h": "#include <assert.h>\n#define PBL_ASSERTN(x) assert(x)\n",
    "system/version.h": "",
    "pbl/util/size.h": "#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))\n",
    "pbl/util/testing.h": "#define PBL_T_STATIC static\n",
    "resource/resource_version.auto.h": """
extern ResourceVersion test_resource_version;
#define SYSTEM_RESOURCE_VERSION test_resource_version
""",
}
HARNESS = r"""
#define _GNU_SOURCE
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "resource/resource_storage_impl.h"
#include "resource/resource_storage_flash.h"
#include "resource/resource_version.auto.h"
#include "pbl/drivers/flash.h"
#include <pbl/soc/t5ai/image_layout.h>
#include <pbl/crc/crc.h>

ResourceVersion test_resource_version;
const struct pbl_flash *const FLASH = NULL;
static uint8_t *bank;
static uint8_t app[RESOURCE_STORE_METADATA_BYTES + 8];
static unsigned reads, read_bytes, crcs, app_queries, builtin_queries, clears;
static uint32_t crc_address, crc_length;
static const uint32_t base = T5AI_FLASH_XIP_BASE + PART_SYSTEM_RESOURCES_BANK_0_OFFSET;
void *tracked_memcpy(void *out, const void *in, size_t n) {
  const uintptr_t address = (uintptr_t)in;
  if (address >= base && address < base + TEST_MAPPING_SIZE) {
    assert(address - base <= PART_SYSTEM_RESOURCES_BANK_0_SIZE && n <= PART_SYSTEM_RESOURCES_BANK_0_SIZE - (address - base));
    reads++; read_bytes += n;
  }
  return memmove(out, in, n);
}
static ResourceManifest *manifest(void) { return (ResourceManifest *)bank; }
static ResTableEntry *table(unsigned i) {
  return (ResTableEntry *)(bank + MANIFEST_SIZE + i * TABLE_ENTRY_SIZE);
}
void flash_read_bytes(uint8_t *out, uint32_t address, uint32_t n) {
  assert(address >= base && address - base <= PART_SYSTEM_RESOURCES_BANK_0_SIZE);
  assert(n <= PART_SYSTEM_RESOURCES_BANK_0_SIZE - (address - base));
  memcpy(out, bank + address - base, n);
}
void flash_write_bytes(const uint8_t *p, uint32_t a, uint32_t n) {
  assert(a == base && n == MANIFEST_SIZE);
  memcpy(bank, p, n);
}
uint32_t flash_crc32_legacy(uint32_t address, uint32_t n) {
  assert(address >= base && address - base <= PART_SYSTEM_RESOURCES_BANK_0_SIZE);
  assert(n <= PART_SYSTEM_RESOURCES_BANK_0_SIZE - (address - base));
  crcs++; crc_address = address; crc_length = n;
  return pbl_crc32_legacy(bank + address - base, n);
}
void boot_bit_clear(int bit) { assert(bit == 1); clears++; }
bool resource_version_matches(const ResourceVersion *a, const ResourceVersion *b) {
  return a->crc == b->crc;
}
void app_file_path_make(char *p, size_t n, uint32_t id, const char *suffix, size_t len) {
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
static bool file_find(ResourceStoreEntry *e, ResAppNum a, uint32_t id) {
  (void)e; (void)a; (void)id; return false;
}
const ResourceStoreImplementation g_file_impl = {.find_resource = file_find};

static void setup(void) {
  memset(bank, 0xff, TEST_MAPPING_SIZE);
  test_resource_version = (ResourceVersion){pbl_crc32_legacy("abcdefgh", 8), 42};
  *manifest() = (ResourceManifest){.num_resources = 2, .version = SYSTEM_RESOURCE_VERSION};
  *table(0) = (ResTableEntry){1, 0, 4, 0xaabbccdd};
  *table(1) = (ResTableEntry){2, 4, 4, 0xaabbccdd};
  memcpy(bank + SYSTEM_STORE_METADATA_BYTES, "abcdefgh", 8);
  table(0)->crc = pbl_crc32_legacy("abcd", 4);
  table(1)->crc = pbl_crc32_legacy("efgh", 4);
  reads = read_bytes = crcs = 0;
}
static uint32_t physical(uint32_t a){return a<T5AI_FLASH_XIP_BASE?a+T5AI_FLASH_XIP_BASE:a;}
int pbl_flash_read(const struct pbl_flash *f,uint32_t a,void *p,size_t n){flash_read_bytes(p,physical(a),n);return 0;}
int pbl_flash_write(const struct pbl_flash *f,uint32_t a,const void *p,size_t n){flash_write_bytes(p,physical(a),n);return 0;}
uint32_t pbl_flash_crc32_legacy(const struct pbl_flash *f,uint32_t a,size_t n){return flash_crc32_legacy(physical(a),n);}
static void init_expect(bool valid, unsigned expected_crcs) {
  resource_storage_init();
  assert(crcs == expected_crcs);
  if (expected_crcs) {
    assert(crc_address == base + SYSTEM_STORE_METADATA_BYTES);
  }
  assert(resource_storage_check(SYSTEM_APP, 0, &SYSTEM_RESOURCE_VERSION) == valid);
}
int main(void) {
  bank = mmap((void *)(uintptr_t)base, TEST_MAPPING_SIZE, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  assert(bank == (void *)(uintptr_t)base);
  setup();
  if (PART_SYSTEM_RESOURCES_BANK_0_SIZE < SYSTEM_STORE_METADATA_BYTES) {
    init_expect(false, 0); // Truncated manifest/table geometry: no mapped access.
    return 0;
  }
  init_expect(true, 1);
  assert(clears == 1);
  assert(resource_storage_flash_get_unused_bank() == NULL);
  g_system_bank_impl.clear(NULL); // An immutable bank is never cleared.
  assert(manifest()->num_resources == 2);
  // Timestamp remains informational, matching stock resource_version_matches.
  setup(); manifest()->version.timestamp++; init_expect(true, 1);
  setup(); manifest()->version.crc++; init_expect(false, 0);
  setup(); manifest()->num_resources = 0; init_expect(true, 0);
  setup(); manifest()->num_resources = MAX_RESOURCES_FOR_SYSTEM_STORE + 1;
  init_expect(false, 0);
  setup(); manifest()->num_resources = UINT32_MAX; init_expect(false, 0);
  setup(); table(1)->resource_id = 3; init_expect(false, 0);
  setup(); table(1)->offset = UINT32_MAX - 1; table(1)->length = 8;
  init_expect(false, 0);
  setup(); table(1)->offset = PART_SYSTEM_RESOURCES_BANK_0_SIZE - (uint32_t)SYSTEM_STORE_METADATA_BYTES;
  table(1)->length = 1; init_expect(false, 0);
  setup(); table(1)->offset = PART_SYSTEM_RESOURCES_BANK_0_SIZE - (uint32_t)SYSTEM_STORE_METADATA_BYTES - 4;
  table(1)->length = 4;
  test_resource_version.crc = pbl_crc32_legacy(bank + SYSTEM_STORE_METADATA_BYTES,
                                            PART_SYSTEM_RESOURCES_BANK_0_SIZE - SYSTEM_STORE_METADATA_BYTES);
  manifest()->version = test_resource_version;
  init_expect(true, 1); // Exact bank end is valid with the correct full CRC.
  setup(); manifest()->num_resources = MAX_RESOURCES_FOR_SYSTEM_STORE;
  for (unsigned i = 0; i < MAX_RESOURCES_FOR_SYSTEM_STORE; ++i)
    *table(i) = (ResTableEntry){i + 1, 4, 4, 0};
  init_expect(true, 1); // Entire maximum-size table is checked before full CRC.
  setup(); table(0)->resource_id = 7; init_expect(false, 0);
  setup(); table(0)->offset = UINT32_MAX; init_expect(false, 0);
  setup(); table(0)->offset = 7; table(0)->length = 2; init_expect(false, 0);
  setup(); bank[SYSTEM_STORE_METADATA_BYTES + 2] ^= 1; init_expect(false, 2);
  setup(); bank[SYSTEM_STORE_METADATA_BYTES + 6] ^= 1; init_expect(false, 2);
  setup(); init_expect(true, 1);
  ResourceStoreEntry e;
  app_queries = builtin_queries = 0;
  resource_storage_get_resource(SYSTEM_APP, 1, &e);
  assert(e.impl == &g_system_bank_impl && e.offset == SYSTEM_STORE_METADATA_BYTES);
  assert(app_queries == 1 && builtin_queries == 1);
  char out[4]; assert(resource_storage_read(&e, 0, out, 4) == 4);
  assert(memcmp(out, "abcd", 4) == 0);
#ifdef CONFIG_MMAP_RESOURCES
  assert(e.impl->readonly_bytes(&e) == (const uint8_t *)(uintptr_t)(base + e.offset));
  assert(resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)base));
  assert(resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)(base + PART_SYSTEM_RESOURCES_BANK_0_SIZE - 1)));
#else
  assert(e.impl->readonly_bytes(&e) == NULL);
  assert(!resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)base));
#endif
  assert(!resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)(base - 1)));
  assert(!resource_storage_flash_bytes_are_readonly((const void *)(uintptr_t)(base + PART_SYSTEM_RESOURCES_BANK_0_SIZE)));
  assert(resource_storage_check(SYSTEM_APP, 1, &SYSTEM_RESOURCE_VERSION));
  assert(crcs == 2 && crc_address == base + SYSTEM_STORE_METADATA_BYTES && crc_length == 4);
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
  setup(); manifest()->version.crc++; init_expect(false, 0);
  resource_storage_get_resource(SYSTEM_APP, 1, &e); assert(e.impl == NULL);
  resource_storage_get_resource(42, 1, &e); assert(e.impl == &g_app_file_impl);
  puts("full boot CRC, bounded tables, immutable bank and resource routing passed");
  return 0;
}
"""


STOCK_HARNESS = (
    HARNESS.split("static void init_expect")[0]
    + r"""
int main(void) {
  bank = mmap((void *)(uintptr_t)base, TEST_MAPPING_SIZE, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  assert(bank == (void *)(uintptr_t)base);
  setup();
  resource_storage_init();
  assert(resource_storage_check(SYSTEM_APP, 0, &SYSTEM_RESOURCE_VERSION));
  assert(crcs == 1 && crc_length == 8);
  const SystemResourceBank *unused = resource_storage_flash_get_unused_bank();
  assert(unused && unused->begin == PART_SYSTEM_RESOURCES_BANK_1_OFFSET);
  assert(unused->end == PART_SYSTEM_RESOURCES_BANK_1_OFFSET + PART_SYSTEM_RESOURCES_BANK_1_SIZE);
  ResourceStoreEntry entry;
  resource_storage_get_resource(SYSTEM_APP, 1, &entry);
  char out[4];
  assert(resource_storage_read(&entry, 0, out, sizeof(out)) == sizeof(out));
  assert(!memcmp(out, "abcd", sizeof(out)));
  return 0;
}
"""
)


class T5ResourcesTest(unittest.TestCase):
    def compile_and_run(self, *defines, stock=False):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            for name, content in STUBS.items():
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#pragma once\n" + content)
            config = load_config("unhygienix")
            # Only configure dependencies used by these translation units. Cases
            # select mmap independently; no configured build directory is needed.
            (directory / "config.h").write_text(
                "".join(
                    f"#define CONFIG_{name} {config.syms[name].str_value}\n"
                    for name in ("FW_FLASH_SIZE", "KERNEL_THREAD_TLS_SLOTS")
                )
            )
            harness = directory / "test.c"
            harness.write_text(STOCK_HARNESS if stock else HARNESS)
            binary = directory / "test"
            subprocess.run(
                [
                    "clang-21",
                    "-std=gnu23",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-DCONFIG_SOC_T5AI=1",
                    "-Dmemcpy=tracked_memcpy",
                    "-include",
                    str(directory / "config.h"),
                    *(["-DTEST_STOCK_BANKS=1"] if stock else []),
                    *defines,
                    "-I",
                    str(directory),
                    "-I",
                    str(ROOT / "pebble"),
                    "-I",
                    str(ROOT / "include"),
                    "-I",
                    str(ROOT / "kernel/arch/posix/include"),
                    str(harness),
                    str(
                        ROOT
                        / (
                            "pebble/resource/resource_storage_flash.c"
                            if stock
                            else "soc/t5ai/resource_storage.c"
                        )
                    ),
                    str(ROOT / "pebble/resource/resource_storage.c"),
                    str(ROOT / "subsys/crc/crc32_legacy.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_stock_backend_keeps_spare_bank(self):
        self.compile_and_run(stock=True)

    def test_mapped_backend_and_routing(self):
        self.compile_and_run("-DCONFIG_MMAP_RESOURCES=1")

    def test_copy_only_backend(self):
        self.compile_and_run()

    def test_truncated_manifest_geometry(self):
        self.compile_and_run("-UTEST_BANK_SIZE", "-DTEST_BANK_SIZE=4u")

    def test_truncated_table_geometry(self):
        self.compile_and_run("-UTEST_BANK_SIZE", "-DTEST_BANK_SIZE=32u")


if __name__ == "__main__":
    unittest.main()
