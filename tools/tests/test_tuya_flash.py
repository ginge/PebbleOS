# SPDX-License-Identifier: Apache-2.0
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class TuyaFlashTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_mipc_wire_format(self):
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <pbl/soc/t5ai/mipc.h>

int main(void) {
  static const uint8_t vector[] = "123456789";
  assert(t5ai_mipc_crc8(vector, sizeof(vector) - 1, 0xff) == 0xab);
  assert(t5ai_mipc_header(1, T5AI_MIPC_TYPE_COMMAND) == 0x09);
  assert(t5ai_mipc_header(0, T5AI_MIPC_TYPE_ACK) == 0x10);
  assert(t5ai_mipc_frame_header(0x09, 2, 1, 0x34, 0x17) == 0x17341209);

  T5AIMIPCData data = {
    .result = 0x11223344,
    .handle = 0x55667788,
    .data = 0x12345678,
    .size = 20,
    .flags = 1,
  };
  T5AIMIPCFrame frame = {
    .header = t5ai_mipc_frame_header(0x09, 0, 0, 1,
                                      T5AI_MIPC_RX_LOGICAL_CHANNEL),
    .object = 0x20001000,
    .result = t5ai_mipc_data_crc(&data),
  };
  assert(t5ai_mipc_frame_valid(&frame, &data));
  /* Logical delivery strips the physical mailbox channel metadata. */
  frame.header = 0x09;
  assert(t5ai_mipc_frame_valid(&frame, &data));
  frame.result ^= 1;
  assert(!t5ai_mipc_frame_valid(&frame, &data));
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
                    str(ROOT / "src/fw/drivers/t5ai/mipc.c"),
                    str(test_source), "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_mailbox_protocol_encoding_and_crc(self):
        transport = (ROOT / "src/fw/drivers/t5ai/mailbox_channel.c").read_text()
        ipc = (ROOT / "src/fw/drivers/t5ai/mb_ipc.c").read_text()
        flash = (ROOT / "src/fw/drivers/flash/t5ai_flash_ipc.c").read_text()
        compatibility = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()

        self.assertIn("MailboxOwnedSlot s_ack_slots[2]", transport)
        self.assertIn("dcache_invalidate((void *)address", transport)
        self.assertNotIn("dcache_flush((void *)address", transport)
        self.assertIn("sequence != s_sequence", transport)
        self.assertIn("for (uint8_t index = 0; index < T5AI_MAILBOX_CHANNEL_COUNT", transport)
        self.assertIn("MBOX_RESET", transport)

        self.assertIn("uint8_t tx_tag", ipc)
        self.assertIn("bool tx_in_process", ipc)
        self.assertIn("bool rx_in_process", ipc)
        self.assertIn("prv_tag(message) != socket->tx_tag", ipc)
        self.assertIn("MB_IPC_ROUTE_RX_BUSY", ipc)
        self.assertIn("prv_respond(socket, &request)", ipc)

        self.assertIn("prv_flash_transaction(FLASH_READ_DONE)", flash)
        self.assertIn("t5ai_mb_ipc_discard(s_flash_socket)", flash)
        self.assertNotIn("FLASH_CONNECT_ATTEMPTS", flash)
        self.assertIn("CP_FLASH_NOTIFY_RX_CHANNEL 0x4bu", compatibility)
        self.assertIn("message->parameter1 = CP_FLASH_NOTIFY_ACK", compatibility)

    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_flash_backend_bounds_chunking_and_protection(self):
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "system/status_codes.h"

static uint8_t storage[0x800000];
static unsigned reads, writes, erases;
static bool fail_next_read, fail_next_write, fail_next_erase;

status_t t5ai_flash_ipc_init(void) { memset(storage, 0xff, sizeof(storage)); return S_SUCCESS; }
status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length) {
  assert(length <= 0x200); reads++;
  if (fail_next_read) { fail_next_read = false; return E_ERROR; }
  memcpy(buffer, storage + offset, length); return S_SUCCESS;
}
status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer, size_t length) {
  assert(length <= 0x100); writes++; memcpy(storage + offset, buffer, length);
  if (fail_next_write) { fail_next_write = false; return E_ERROR; }
  return S_SUCCESS;
}
status_t t5ai_flash_ipc_erase_4k(uint32_t offset) {
  assert((offset & 0xfff) == 0); erases++; memset(storage + offset, 0xff, 0x1000);
  if (fail_next_erase) { fail_next_erase = false; return E_ERROR; }
  return S_SUCCESS;
}

#include "src/fw/drivers/flash/t5ai.c"

int main(void) {
  const uint32_t base = FLASH_REGION_BASE_ADDRESS;
  uint8_t input[600], output[600];
  memset(input, 0x5a, sizeof(input));
  assert(flash_impl_init(false) == S_SUCCESS);
  assert(flash_impl_read_sync(output, base, sizeof(output)) == S_SUCCESS && reads == 2);
  fail_next_read = true;
      assert(flash_impl_read_sync(output, base + 0x1000, 1) == S_SUCCESS && reads == 4);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FIRMWARE_SLOT_0_BEGIN - 1, 1) == E_INVALID_OPERATION);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FIRMWARE_SLOT_0_BEGIN, 1) == E_INVALID_OPERATION);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FILESYSTEM_BEGIN - 1, 1) == E_INVALID_OPERATION);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_CD_BEGIN, 1) == E_INVALID_OPERATION);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FIRMWARE_SLOT_1_BEGIN, 1) == E_INVALID_OPERATION);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_DEBUG_DB_BEGIN, 1) == E_INVALID_OPERATION);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_SHARED_PRF_STORAGE_BEGIN - 1, 1) == E_INVALID_OPERATION);
  fail_next_write = true;
      assert(flash_impl_write_page_begin(input, FLASH_REGION_SHARED_PRF_STORAGE_BEGIN, 1) == E_ERROR);
      assert(writes == 1 && flash_impl_get_write_status() == S_NO_ACTION_REQUIRED);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_SHARED_PRF_STORAGE_END, 1) == E_INVALID_OPERATION);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FILESYSTEM_BEGIN + 250, sizeof(input)) == 6);
      assert(writes == 2 && flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_write_protect(FLASH_REGION_FILESYSTEM_BEGIN, FLASH_REGION_FILESYSTEM_BEGIN + 0xfff) == S_SUCCESS);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FILESYSTEM_BEGIN, 1) == E_INVALID_OPERATION);
  assert(flash_impl_unprotect() == S_SUCCESS);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FILESYSTEM_END - 1, 2) == 1);
      assert(writes == 3 && flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_write_page_begin(input, FLASH_REGION_FILESYSTEM_END, 1) == E_INVALID_OPERATION);
  assert(flash_impl_erase_subsector_begin(FLASH_REGION_FIRMWARE_SLOT_0_BEGIN) == E_INVALID_OPERATION);
  assert(flash_impl_erase_subsector_begin(FLASH_REGION_CD_BEGIN) == E_INVALID_OPERATION);
  assert(flash_impl_erase_subsector_begin(FLASH_REGION_FIRMWARE_SLOT_1_BEGIN) == E_INVALID_OPERATION);
  assert(flash_impl_erase_subsector_begin(FLASH_REGION_DEBUG_DB_BEGIN) == E_INVALID_OPERATION);
  fail_next_erase = true;
      assert(flash_impl_erase_subsector_begin(FLASH_REGION_SHARED_PRF_STORAGE_BEGIN) == E_ERROR);
      assert(erases == 1 && flash_impl_get_erase_status() == S_NO_ACTION_REQUIRED);
  assert(flash_impl_erase_subsector_begin(FLASH_REGION_SHARED_PRF_STORAGE_END) == E_INVALID_OPERATION);
      assert(flash_impl_erase_subsector_begin(FLASH_REGION_FILESYSTEM_BEGIN + 1) == S_SUCCESS && erases == 2);
  assert(flash_impl_erase_suspend(FLASH_REGION_FILESYSTEM_BEGIN) == S_NO_ACTION_REQUIRED);
      assert(flash_impl_erase_sector_begin(FLASH_REGION_FILESYSTEM_BEGIN) == S_SUCCESS && erases == 18);
  assert(flash_impl_get_erase_status() == S_SUCCESS);
  assert(flash_impl_erase_subsector_begin(FLASH_REGION_FILESYSTEM_END) == E_INVALID_OPERATION);
  assert(flash_impl_read_sync(output, base - 1, 1) == E_INVALID_ARGUMENT);
  assert(flash_impl_read_sync(output, base + BOARD_NOR_FLASH_SIZE, 1) == E_INVALID_ARGUMENT);

  const uint32_t cached_page = FLASH_REGION_FILESYSTEM_BEGIN + 0x20000;
  const uint32_t cached_offset = cached_page - FLASH_REGION_BASE_ADDRESS;
  memset(storage + cached_offset, 0xa5, 32);
  unsigned reads_before_cache = reads;
  assert(flash_impl_read_sync(output, cached_page + 3, 4) == S_SUCCESS);
  assert(reads == reads_before_cache + 1 && output[0] == 0xa5 && output[3] == 0xa5);
  assert(flash_impl_read_sync(output, cached_page + 3, 4) == S_SUCCESS);
  assert(reads == reads_before_cache + 1);

  uint8_t header_update = 0x33;
  assert(flash_impl_write_page_begin(&header_update, cached_page + 3, 1) == 1);
  assert(flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_read_sync(output, cached_page + 3, 1) == S_SUCCESS);
  assert(reads == reads_before_cache + 1 && output[0] == header_update);

  assert(flash_impl_erase_subsector_begin(cached_page) == S_SUCCESS);
  assert(flash_impl_get_erase_status() == S_SUCCESS);
  assert(flash_impl_read_sync(output, cached_page, 1) == S_SUCCESS);
  assert(reads == reads_before_cache + 1 && output[0] == 0xff);
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
