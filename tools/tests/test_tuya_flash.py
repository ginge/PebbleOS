# SPDX-License-Identifier: Apache-2.0
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class TuyaFlashTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_mailbox_protocol_encoding_and_crc(self):
        source = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "src/fw/drivers/flash/t5ai_flash_ipc.c"

void dcache_flush(const void *address, size_t size) { (void)address; (void)size; }
void dcache_invalidate(void *address, size_t size) { (void)address; (void)size; }
status_t t5ai_cp_mailbox_lock(void) { return S_SUCCESS; }
void t5ai_cp_mailbox_unlock(void) {}
status_t t5ai_cp_mailbox_send(const void *data, size_t length) {
  (void)data; (void)length; return S_SUCCESS;
}
status_t t5ai_cp_mailbox_receive(uint8_t channel, T5AICPMailboxFrame *frame) {
  (void)channel; (void)frame; return E_AGAIN;
}
void t5ai_cp_mailbox_discard_pending(void) {}

int main(void) {
  static const uint8_t vector[] = "123456789";
  assert(sizeof(T5AIIPCMessage) == 16);
  assert(sizeof(T5AIFlashCommand) == 16);
  assert(prv_crc8(vector, sizeof(vector) - 1) == 0xa2);
  assert(prv_crc32(vector, sizeof(vector) - 1) == 0x2dfd2d88);
  assert(prv_ipc_address_word(0x2a) == 0x002a5001);
  assert(prv_ipc_data_word(5, 16, 0xa2) == 0x05a20010);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test_source = directory / "test.c"
            test_source.write_text(source)
            binary = directory / "test"
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT), "-I", str(ROOT / "include"),
                    "-I", str(ROOT / "src/fw"), str(test_source),
                    "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_flash_backend_bounds_chunking_and_protection(self):
        source = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "system/status_codes.h"

static uint8_t storage[0x800000];
static unsigned reads, writes, erases;

status_t t5ai_flash_ipc_init(void) { memset(storage, 0xff, sizeof(storage)); return S_SUCCESS; }
status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length) {
  assert(length <= 0x200); reads++; memcpy(buffer, storage + offset, length); return S_SUCCESS;
}
status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer, size_t length) {
  assert(length <= 0x100); writes++; memcpy(storage + offset, buffer, length); return S_SUCCESS;
}
status_t t5ai_flash_ipc_erase_4k(uint32_t offset) {
  assert((offset & 0xfff) == 0); erases++; memset(storage + offset, 0xff, 0x1000); return S_SUCCESS;
}

#include "src/fw/drivers/flash/t5ai.c"

int main(void) {
  const uint32_t base = FLASH_REGION_BASE_ADDRESS;
  uint8_t input[600], output[600];
  memset(input, 0x5a, sizeof(input));
  assert(flash_impl_init(false) == S_SUCCESS);
  assert(flash_impl_read_sync(output, base, sizeof(output)) == S_SUCCESS && reads == 2);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FIRMWARE_SLOT_0_BEGIN - 1, 1) == E_INVALID_OPERATION);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FILESYSTEM_BEGIN + 250, sizeof(input)) == 6);
  assert(writes == 1 && flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_write_protect(FLASH_REGION_FILESYSTEM_BEGIN, FLASH_REGION_FILESYSTEM_BEGIN + 0xfff) == S_SUCCESS);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FILESYSTEM_BEGIN, 1) == E_INVALID_OPERATION);
  assert(flash_impl_unprotect() == S_SUCCESS);
  assert(flash_impl_erase_subsector_begin(FLASH_REGION_FILESYSTEM_BEGIN + 1) == S_SUCCESS && erases == 1);
  assert(flash_impl_erase_suspend(FLASH_REGION_FILESYSTEM_BEGIN) == S_NO_ACTION_REQUIRED);
  assert(flash_impl_erase_sector_begin(FLASH_REGION_FILESYSTEM_BEGIN) == S_SUCCESS && erases == 17);
  assert(flash_impl_read_sync(output, base - 1, 1) == E_INVALID_ARGUMENT);
  assert(flash_impl_read_sync(output, base + BOARD_NOR_FLASH_SIZE, 1) == E_INVALID_ARGUMENT);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test_source = directory / "test.c"
            test_source.write_text(source)
            binary = directory / "test"
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DCONFIG_SOC_T5AI=1",
                    "-I", str(ROOT), "-I", str(ROOT / "include"),
                    "-I", str(ROOT / "src/fw"), str(test_source),
                    "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_protected_address_translation(self):
        source = r'''
#include <assert.h>
#include <stdint.h>
#include <pbl/soc/t5ai/flash_translation.h>

int main(void) {
  static const struct { uint32_t logical, physical; } vectors[] = {
    {0, 0}, {31, 31}, {32, 34}, {63, 65}, {64, 68},
    {0x1000, 0x1100}, {0x10000, 0x11000},
  };
  for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
    uint32_t physical = 0, logical = 0;
    assert(t5ai_flash_logical_to_physical(vectors[i].logical, &physical));
    assert(physical == vectors[i].physical);
    assert(t5ai_flash_physical_to_logical(physical, &logical));
    assert(logical == vectors[i].logical);
  }
  uint32_t value;
  assert(!t5ai_flash_physical_to_logical(32, &value));
  assert(!t5ai_flash_physical_to_logical(33, &value));
  assert(t5ai_flash_physical_to_logical(34, &value) && value == 32);
  assert(!t5ai_flash_logical_to_physical(UINT32_MAX, &value));
  assert(!t5ai_flash_logical_to_physical(0, 0));
  assert(!t5ai_flash_physical_to_logical(0, 0));
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test_source = directory / "test.c"
            test_source.write_text(source)
            binary = directory / "test"
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "include"),
                    str(ROOT / "soc/t5ai/flash_translation.c"),
                    str(test_source), "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_experimental_regions_do_not_overlap(self):
        header = (ROOT / "src/fw/flash_region/flash_region_t5ai.h").read_text()
        values = {}
        for line in header.splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[0] == "#define" and fields[2].startswith("0x"):
                values[fields[1]] = int(fields[2], 16)

        regions = [
            "FIRMWARE_SLOT_0", "FILESYSTEM", "CD", "FIRMWARE_SLOT_1",
            "DEBUG_DB", "SHARED_PRF_STORAGE",
        ]
        spans = []
        for region in regions:
            begin = values[f"FLASH_REGION_{region}_BEGIN"]
            end = values[f"FLASH_REGION_{region}_END"]
            self.assertLess(begin, end, region)
            self.assertEqual(begin % 0x1000, 0, region)
            self.assertEqual(end % 0x1000, 0, region)
            spans.append((begin, end, region))

        for (_, left_end, left), (right_begin, _, right) in zip(spans, spans[1:]):
            self.assertLessEqual(left_end, right_begin, f"{left} overlaps {right}")


if __name__ == "__main__":
    unittest.main()
