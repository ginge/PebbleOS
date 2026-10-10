# SPDX-License-Identifier: Apache-2.0
"""Fault-injection tests for the production T5 flash caches."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.tests.test_t5ai_flash_completion import STUB

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "stub.h"
#include <assert.h>
#include <string.h>
#include "fw/drivers/flash/t5ai.c"

static uint8_t storage[BOARD_NOR_FLASH_SIZE];
static unsigned reads, writes, erases;
static bool fail_mutation, apply_mutation, fail_read, corrupt_write;
static unsigned fail_erase_at;
static const uint32_t base = FLASH_REGION_BASE_ADDRESS;
status_t t5ai_flash_ipc_init(void) { return S_SUCCESS; }
status_t t5ai_flash_ipc_read(uint32_t offset, void *out, size_t n) {
  reads++;
  if (fail_read) return E_INVALID_OPERATION;
  assert(offset + n <= sizeof(storage));
  memcpy(out, storage + offset, n);
  return S_SUCCESS;
}
status_t t5ai_flash_ipc_write(uint32_t offset, const void *in, size_t n) {
  writes++;
  if (!fail_mutation || apply_mutation) {
    const uint8_t *p = in;
    for (size_t i = 0; i < n; i++) storage[offset + i] &= p[i];
    if (corrupt_write) storage[offset] ^= 0x08;
  }
  return fail_mutation ? E_ERROR : S_SUCCESS;
}
status_t t5ai_flash_ipc_erase_4k(uint32_t offset) {
  erases++;
  if (fail_erase_at == erases) {
    if (apply_mutation) memset(storage + offset, 0xff, 128); // Partial erase.
    return E_ERROR;
  }
  memset(storage + offset, 0xff, 4096);
  return S_SUCCESS;
}
static uint8_t read_byte(uint32_t address) {
  uint8_t out = 0;
  assert(flash_impl_read_sync(&out, address, 1) == S_SUCCESS);
  return out;
}
static void reset(void) {
  memset(storage, 0xff, sizeof(storage));
  reads = writes = erases = 0;
  fail_mutation = apply_mutation = fail_read = corrupt_write = false;
  fail_erase_at = 0;
  assert(flash_impl_init(false) == S_SUCCESS);
}
static void failed_write(uint32_t address, bool applied, bool unavailable) {
  reset();
  storage[address - base] = 0xff;
  assert(read_byte(address) == 0xff);
  const uint32_t unrelated = FLASH_REGION_FILESYSTEM_BEGIN + 0x8000 + 64;
  storage[unrelated - base] = 0x42;
  assert(read_byte(unrelated) == 0x42);
  const unsigned before = reads;
  const uint8_t programmed = 0x50;
  fail_mutation = true; apply_mutation = applied;
  assert(flash_impl_write_page_begin(&programmed, address, 1) == E_ERROR);
  assert(writes == 1 && flash_impl_get_write_status() == S_NO_ACTION_REQUIRED);
  assert(read_byte(unrelated) == 0x42 && reads == before); // Keep unrelated lines.
  fail_read = unavailable;
  if (unavailable) {
    uint8_t out = 0;
    assert(flash_impl_read_sync(&out, address, 1) == E_INVALID_OPERATION);
    assert(reads > before); // Cannot serve old cache if transport is quarantined.
  } else {
    assert(read_byte(address) == (applied ? programmed : 0xff));
    assert(reads == before + 1); // Refetch even when the mutation changed nothing.
  }
}
static void failed_erase(bool applied, bool unavailable) {
  reset();
  const uint32_t address = FLASH_REGION_FILESYSTEM_BEGIN;
  storage[address - base + 3] = 0x12;
  storage[address - base + 64] = 0x34;
  assert(read_byte(address + 3) == 0x12);
  assert(read_byte(address + 64) == 0x34);
  fail_erase_at = 1; apply_mutation = applied;
  assert(flash_impl_erase_subsector_begin(address) == E_ERROR);
  assert(erases == 1 && flash_impl_get_erase_status() == S_NO_ACTION_REQUIRED);
  const unsigned before = reads;
  fail_read = unavailable;
  if (unavailable) {
    uint8_t out;
    assert(flash_impl_read_sync(&out, address + 3, 1) == E_INVALID_OPERATION);
    assert(flash_impl_read_sync(&out, address + 64, 1) == E_INVALID_OPERATION);
  } else {
    assert(read_byte(address + 3) == (applied ? 0xff : 0x12));
    assert(read_byte(address + 64) == (applied ? 0xff : 0x34));
  }
  assert(reads > before);
}
static void verification_failure(void) {
  reset();
  const uint32_t address = FLASH_REGION_FILESYSTEM_BEGIN;
  assert(read_byte(address) == 0xff);
  assert(read_byte(address + 64) == 0xff); // Also cache the line containing the header.
  const uint8_t version[] = {0x01, 0x50};
  corrupt_write = true;
  assert(flash_impl_write_page_begin(version, address, sizeof(version)) == E_ERROR);
  assert(writes == 1 && flash_impl_get_write_status() == S_NO_ACTION_REQUIRED);
  const unsigned before = reads;
  assert(read_byte(address) == 0x09);
  assert(reads == before + 1); // The version mismatch must leave the header invalid.
}
static void successful_write_and_rejection(void) {
  reset();
  const uint32_t address = FLASH_REGION_FILESYSTEM_BEGIN + 3;
  storage[address - base] = 0xfa;
  assert(read_byte(address) == 0xfa);
  unsigned before = reads;
  assert(flash_impl_write_protect(address, address) == S_SUCCESS);
  const uint8_t mask = 0xfd;
  assert(flash_impl_write_page_begin(&mask, address, 1) == E_INVALID_OPERATION);
  assert(writes == 0 && read_byte(address) == 0xfa && reads == before);
  assert(flash_impl_unprotect() == S_SUCCESS);
  assert(flash_impl_write_page_begin(&mask, address, 1) == 1);
  assert(flash_impl_get_write_status() == S_SUCCESS);
  assert(read_byte(address) == 0xf8);
#ifdef CONFIG_T5AI_PFS_HEADER_CACHE
  assert(reads == before); // Preserve successful header write-through/NOR semantics.
#else
  assert(reads == before + 1);
#endif
  // Uncached headers must not become valid from an incomplete cached baseline.
  const uint32_t uncached = FLASH_REGION_FILESYSTEM_BEGIN + 0x1000;
  storage[uncached - base + 2] = 0x13;
  assert(flash_impl_write_page_begin(&mask, uncached + 3, 1) == 1);
  assert(flash_impl_get_write_status() == S_SUCCESS);
  assert(read_byte(uncached + 2) == 0x13);
}
static void sector_partial_failure(void) {
  reset();
  const uint32_t address = FLASH_REGION_FILESYSTEM_BEGIN;
  for (unsigned page = 0; page < 4; page++) {
    storage[address - base + page * 4096 + 3] = 0x12;
    assert(read_byte(address + page * 4096 + 3) == 0x12);
  }
  fail_erase_at = 3; apply_mutation = true;
  assert(flash_impl_erase_sector_begin(address) == E_ERROR);
  assert(erases == 3 && flash_impl_get_erase_status() == S_NO_ACTION_REQUIRED);
  unsigned before = reads;
  assert(read_byte(address + 3 * 4096 + 3) == 0x12);
  assert(reads == before); // A page that was never attempted stays cached.
  assert(read_byte(address + 2 * 4096 + 3) == 0xff);
  assert(reads == before + 1); // The failed, partially erased page is refetched.
  before = reads;
  assert(read_byte(address + 3) == 0xff);
  assert(read_byte(address + 4096 + 3) == 0xff);
#ifdef CONFIG_T5AI_PFS_HEADER_CACHE
  assert(reads == before); // Completed erases still install known-blank headers.
#endif
}
int main(void) {
  const uint32_t addresses[] = {
    FLASH_REGION_FILESYSTEM_BEGIN + 3,
    FLASH_REGION_FILESYSTEM_BEGIN + 64,
    FLASH_REGION_FILESYSTEM_BEGIN + 0x100000 + 3, // Beyond bounded header cache.
    FLASH_REGION_SHARED_PRF_STORAGE_BEGIN + 16,
  };
  for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
    failed_write(addresses[i], false, false);
    failed_write(addresses[i], true, false);
    failed_write(addresses[i], true, true);
  }
  failed_erase(false, false);
  failed_erase(true, false);
  failed_erase(true, true);
  verification_failure();
  successful_write_and_rejection();
  sector_partial_failure();
  return 0;
}
'''


class FlashCacheTest(unittest.TestCase):
    def run_harness(self, header_cache):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "stub.h").write_text(STUB)
            for name in [
                "pbl/drivers/flash/flash_impl.h", "pbl/logging/logging.h",
                "pbl/soc/t5ai/flash_ipc.h", "flash_region/flash_region.h",
                "system/rtc_registers.h", "system/status_codes.h",
            ]:
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "stub.h"\n')
            harness = directory / "test.c"
            harness.write_text(HARNESS)
            binary = directory / "test"
            defines = ["-DCONFIG_T5AI_PFS_HEADER_CACHE=1"] if header_cache else []
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *defines,
                "-I", str(directory), "-I", str(ROOT), str(harness), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_header_cache_enabled(self):
        self.run_harness(True)

    def test_header_cache_disabled(self):
        self.run_harness(False)


if __name__ == "__main__":
    unittest.main()
