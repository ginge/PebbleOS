# SPDX-License-Identifier: Apache-2.0
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class TuyaFlashTest(unittest.TestCase):
    def test_t5ai_flash_crc_matches_vendor_and_read_lease_priority(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        source = (ROOT / "drivers/flash/t5ai_flash_ipc.c").read_text()
        crc = source[source.index("static const uint32_t s_flash_crc32_table"):
                     source.index("status_t t5ai_flash_ipc_init")]
        harness = r"""
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
""" + crc + r"""
static uint32_t reference(const uint8_t *p, size_t n) {
 uint32_t crc = 0;
 while (n--) {
  crc ^= *p++;
  for (unsigned bit = 0; bit < 8; ++bit)
   crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
 }
 return crc;
}
int main(void) {
 uint8_t data[2048];
 for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (i * 73u + i / 7u) & 255u;
 for (unsigned n = 0; n <= sizeof(data); ++n)
  assert(prv_crc32(data, n) == reference(data, n));
 assert(prv_crc32("123456789", 9) == 0x2dfd2d88u);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            c_file = Path(directory) / "crc.c"
            binary = Path(directory) / "crc"
            c_file.write_text(harness)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(c_file), "-o", str(binary)], check=True,
                           capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)
        read = source.split("status_t t5ai_flash_ipc_read(", 1)[1].split(
            "status_t t5ai_flash_ipc_write(", 1)[0]
        self.assertLess(read.index("prv_boost_client_priority()"),
                        read.index("prv_flash_transaction(FLASH_READ)"))
        self.assertLess(read.index("prv_flash_transaction(FLASH_READ_DONE)"),
                        read.index("prv_restore_client_priority(previous_priority)"))
        flash = (ROOT / "drivers/flash/t5ai.c").read_text()
        verification = flash.split("static status_t prv_verify_pfs_header_write", 1)[1].split(
            "FlashAddress flash_impl_get_sector_base_address", 1)[0]
        self.assertIn("return E_ERROR", verification)
        self.assertNotIn("prv_confirm_pfs_read", verification)
        self.assertNotIn("t5ai_flash_ipc_write", verification)

    def test_t5ai_write_path_has_no_retained_probe_instrumentation(self):
        flash = (ROOT / "drivers/flash/t5ai.c").read_text()
        main = (ROOT / "fw/main.c").read_text()
        registers = (ROOT / "fw/system/rtc_registers.h").read_text()

        self.assertNotIn("T5AI_FLASH_FAILURE_", registers)
        self.assertNotIn("T5AI_FLASH_FAILURE_", flash)
        self.assertNotIn("prv_record_write_failure", flash)
        self.assertNotIn("prv_t5ai_capture_retained_failure", main)

    def test_t5ai_flash_quarantines_ambiguous_transactions(self):
        flash = (ROOT / "drivers/flash/t5ai_flash_ipc.c").read_text()

        self.assertIn("t5ai_mb_ipc_poison(s_flash_socket)", flash)
        self.assertIn("s_flash_available = false", flash)
        self.assertIn("const bool ambiguous = status == E_AGAIN", flash)
        self.assertIn("quarantined=%u", flash)
        self.assertIn("ignored stale completion", flash)
        self.assertIn("t5ai_cp_mailbox_shared_range_valid", flash)
        self.assertNotIn("s_flash_reconnect_allowed", flash)
        self.assertNotIn("T5AI flash IPC reconnected", flash)

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
  assert(t5ai_mipc_crc8(vector, sizeof(vector) - 1, 0xff) == 0x7f);
  /* Pinned CP bk_crc.c table: validate every byte/seed transition. */
  static const uint8_t vendor_table[256] = {
    0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B, 0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
    0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF, 0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
    0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14, 0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
    0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80, 0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
    0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95, 0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
    0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01, 0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
    0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA, 0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
    0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E, 0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
    0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0, 0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
    0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54, 0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
    0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF, 0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
    0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B, 0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
    0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E, 0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
    0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA, 0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
    0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41, 0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
    0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5, 0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F,
  };
  for (unsigned seed = 0; seed < 256; ++seed) {
    for (unsigned value = 0; value < 256; ++value) {
      const uint8_t byte = (uint8_t)value;
      assert(t5ai_mipc_crc8(&byte, 1, (uint8_t)seed) == vendor_table[seed ^ value]);
    }
  }
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
  uint8_t expected_crc = 0xff;
  const uint8_t *wire = (const uint8_t *)&data.result;
  for (unsigned i = 0; i < 20; ++i) {
    expected_crc = vendor_table[expected_crc ^ wire[i]];
  }
  assert(t5ai_mipc_data_crc(&data) == expected_crc);
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
                    str(ROOT / "drivers/t5ai/mipc.c"),
                    str(test_source), "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_mailbox_protocol_encoding_and_crc(self):
        transport = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        ipc = (ROOT / "drivers/t5ai/mb_ipc.c").read_text()
        flash = (ROOT / "drivers/flash/t5ai_flash_ipc.c").read_text()
        compatibility = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()

        self.assertIn(
            "MailboxOwnedSlot s_ack_slots[T5AI_MAILBOX_CHANNEL_COUNT]", transport
        )
        self.assertIn("s_ack_slots[channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK]", transport)
        self.assertIn("dcache_invalidate((void *)address", transport)
        self.assertNotIn("dcache_flush((void *)address", transport)
        self.assertIn("sequence != tx->sequence", transport)
        self.assertIn("s_next_tx_channel", transport)
        self.assertIn("MBOX_RESET", transport)

        self.assertIn("uint8_t tx_tag", ipc)
        self.assertIn("bool tx_in_process", ipc)
        self.assertIn("bool rx_in_process", ipc)
        self.assertIn("prv_tag(message) != socket->tx_tag", ipc)
        self.assertIn("MB_IPC_ROUTE_RX_BUSY", ipc)
        self.assertIn("prv_respond(socket, &request)", ipc)
        self.assertIn("response route status is diagnostic", ipc)
        self.assertNotIn("return FAILED(status) ? status : response_status;", ipc)

        self.assertIn("prv_flash_transaction(FLASH_READ_DONE)", flash)
        self.assertIn("t5ai_mb_ipc_discard(s_flash_socket)", flash)
        self.assertNotIn("FLASH_CONNECT_ATTEMPTS", flash)
        self.assertRegex(compatibility, r"CP_FLASH_NOTIFY_RX_CHANNEL\s+0x4bu")
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
static bool fail_next_read, fail_next_write, fail_next_erase, corrupt_next_write;
static bool corrupt_next_successful_read;

void t5ai_retained_write(uint8_t index, uint32_t value) {
  (void)index;
  (void)value;
}

bool t5ai_flash_ipc_is_available(void) { return true; }
status_t t5ai_flash_ipc_init(void) { memset(storage, 0xff, sizeof(storage)); return S_SUCCESS; }
status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length) {
  assert(length <= 0x200); reads++;
  if (fail_next_read) { fail_next_read = false; return E_ERROR; }
  memcpy(buffer, storage + offset, length);
  if (corrupt_next_successful_read) {
    corrupt_next_successful_read = false;
    ((uint8_t *)buffer)[0] ^= 0x02;
  }
  return S_SUCCESS;
}
status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer, size_t length) {
  assert(length <= 0x100); writes++;
  const uint8_t *input = buffer;
  for (size_t i = 0; i < length; ++i) storage[offset + i] &= input[i];
  if (fail_next_write) { fail_next_write = false; return E_ERROR; }
  if (corrupt_next_write) { corrupt_next_write = false; storage[offset] ^= 0x08; }
  return S_SUCCESS;
}
status_t t5ai_flash_ipc_erase_4k(uint32_t offset) {
  assert((offset & 0xfff) == 0); erases++; memset(storage + offset, 0xff, 0x1000);
  if (fail_next_erase) { fail_next_erase = false; return E_ERROR; }
  return S_SUCCESS;
}

#include "drivers/flash/t5ai.c"

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
  storage[cached_offset] = 0x01;
  storage[cached_offset + 1] = 0x50;
  storage[cached_offset + 3] = 0xfa;
  unsigned reads_before_cache = reads;
  assert(flash_impl_read_sync(output, cached_page + 3, 1) == S_SUCCESS);
  assert(output[0] == 0xfa && reads == reads_before_cache + 1);
  uint8_t delete_mask = 0xfd;
  assert(flash_impl_write_page_begin(&delete_mask, cached_page + 3, 1) == 1);
  assert(flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_read_sync(output, cached_page + 3, 1) == S_SUCCESS);
  assert(output[0] == 0xf8 && storage[cached_offset + 3] == 0xf8);
  assert(reads == reads_before_cache + 1);

  const uint32_t continuation = cached_page + 0x1000;
  storage[cached_offset + 0x1000] = 0x01;
  storage[cached_offset + 0x1001] = 0x50;
  storage[cached_offset + 0x1003] = 0xf6;
  assert(flash_impl_read_sync(output, continuation + 3, 1) == S_SUCCESS);
  assert(output[0] == 0xf6);
  assert(flash_impl_write_page_begin(&delete_mask, continuation + 3, 1) == 1);
  assert(flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_read_sync(output, continuation + 3, 1) == S_SUCCESS);
  assert(output[0] == 0xf4 && storage[cached_offset + 0x1003] == 0xf4);

  assert(flash_impl_erase_subsector_begin(cached_page) == S_SUCCESS);
  assert(flash_impl_get_erase_status() == S_SUCCESS);
  reads_before_cache = reads;
  assert(flash_impl_read_sync(output, cached_page, 1) == S_SUCCESS);
  assert(reads == reads_before_cache && output[0] == 0xff);
  const uint8_t erased_header[] = {0x01, 0x50, 0xff, 0xfe};
  assert(flash_impl_write_page_begin(erased_header, cached_page, 4) == 4);
  assert(flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_read_sync(output, cached_page, 4) == S_SUCCESS);
  assert(memcmp(output, erased_header, 4) == 0);
  assert(FLASH_REGION_FILESYSTEM_END == 0x027c0000);
  // The last expanded page bypasses the bounded header cache and retains NOR semantics.
  const uint32_t expanded_page = FLASH_REGION_FILESYSTEM_END - 0x1000;
  assert(flash_impl_erase_subsector_begin(expanded_page) == S_SUCCESS);
  assert(flash_impl_get_erase_status() == S_SUCCESS);
  const uint8_t expanded_header[] = {0x01, 0x50, 0xff, 0xfa};
  assert(flash_impl_write_page_begin(expanded_header, expanded_page, 4) == 4);
  assert(flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_read_sync(output, expanded_page, 4) == S_SUCCESS);
  assert(memcmp(output, expanded_header, 4) == 0);
  assert(flash_impl_write_page_begin(&delete_mask, expanded_page + 3, 1) == 1);
  assert(flash_impl_get_write_status() == S_SUCCESS);
  assert(flash_impl_read_sync(output, expanded_page + 3, 1) == S_SUCCESS);
  assert(output[0] == 0xf8);

  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test_source = directory / "test.c"
            test_source.write_text(source)
            binary = directory / "test"
            for header_cache in (True, False):
                mode_source = source
                if not header_cache:
                    mode_source = mode_source.replace(
                        "assert(reads == reads_before_cache + 1);",
                        "assert(reads == reads_before_cache + 2);")
                    mode_source = mode_source.replace(
                        "assert(reads == reads_before_cache && output[0] == 0xff);",
                        "assert(reads == reads_before_cache + 1 && output[0] == 0xff);")
                test_source.write_text(mode_source)
                defines = ["-DCONFIG_T5AI_PFS_HEADER_CACHE=1"] if header_cache else []
                subprocess.run(
                    ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                     "-DCONFIG_SOC_T5AI=1", "-DCONFIG_BOARD_UNHYGIENIX=1",
                     *defines, "-I", str(ROOT), "-I", str(ROOT / "include"),
                     "-I", str(ROOT / "fw"), str(test_source), "-o", str(binary)],
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

    def test_board_regions_do_not_overlap(self):
        header = (ROOT / "fw/flash_region/flash_region_t5ai.h").read_text()
        values = {}
        for line in header.splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[0] == "#define" and fields[2].startswith("0x"):
                values[fields[1]] = int(fields[2], 16)

        regions = [
            "FIRMWARE_SLOT_0", "SYSTEM_RESOURCES_BANK_0", "CD",
            "FIRMWARE_SLOT_1", "DEBUG_DB", "SHARED_PRF_STORAGE", "FILESYSTEM",
        ]
        spans = []
        for region in regions:
            begin = values[f"FLASH_REGION_{region}_BEGIN"]
            end = values[f"FLASH_REGION_{region}_END"]
            self.assertLess(begin, end, region)
            self.assertEqual(begin % 0x1000, 0, region)
            self.assertEqual(end % 0x1000, 0, region)
            spans.append((begin, end, region))

        spans.sort()
        for (_, left_end, left), (right_begin, _, right) in zip(spans, spans[1:]):
            self.assertLessEqual(left_end, right_begin, f"{left} overlaps {right}")


if __name__ == "__main__":
    unittest.main()
