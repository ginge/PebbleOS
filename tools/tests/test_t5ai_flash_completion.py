# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Check the real T5 flash driver's synchronous erase completion contract."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef int status_t;
typedef uint32_t FlashAddress;
typedef struct { int unused; } FlashSecurityRegisters;
#define S_SUCCESS 0
#define S_NO_ACTION_REQUIRED 1
#define S_TRUE 2
#define S_FALSE 3
#define E_ERROR -1
#define E_RANGE -2
#define E_INVALID_ARGUMENT -3
#define E_INVALID_OPERATION -4
#define E_BUSY -5
#define PASSED(s) ((s) >= 0)
#define FAILED(s) ((s) < 0)
#define PBL_LOG_MODULE_DECLARE(...)
#define PBL_LOG_ERR(...)
#define PBL_LOG_WRN(...)
#define CONFIG_BOARD_UNHYGIENIX 1
#define FLASH_REGION_BASE_ADDRESS 0x02000000u
#define BOARD_NOR_FLASH_SIZE 0x800000u
#define FLASH_REGION_FILESYSTEM_BEGIN 0x02500000u
#define FLASH_REGION_FILESYSTEM_END 0x027c0000u
#define FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN 0x02400000u
#define FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END 0x02500000u
#define FLASH_REGION_SHARED_PRF_STORAGE_BEGIN 0x027c0000u
#define FLASH_REGION_SHARED_PRF_STORAGE_END 0x027d0000u
#define PAGE_SIZE_BYTES 256u
#define SUBSECTOR_SIZE_BYTES 4096u
#define SECTOR_SIZE_BYTES 65536u
#define SECTOR_ADDR_MASK (~(SECTOR_SIZE_BYTES-1u))
#define SUBSECTOR_ADDR_MASK (~(SUBSECTOR_SIZE_BYTES-1u))
status_t t5ai_flash_ipc_init(void);
bool t5ai_flash_ipc_is_available(void);
status_t t5ai_flash_ipc_read(uint32_t, void *, size_t);
status_t t5ai_flash_ipc_write(uint32_t, const void *, size_t);
status_t t5ai_flash_ipc_erase_4k(uint32_t);
"""
HARNESS = r"""
#include "stub.h"
#include <assert.h>
#include <string.h>
#include "drivers/flash/t5ai.c"
static unsigned erases;
static bool fail;
status_t t5ai_flash_ipc_init(void) { return S_SUCCESS; }
bool t5ai_flash_ipc_is_available(void) { return true; }
status_t t5ai_flash_ipc_read(uint32_t a, void *p, size_t n) {
 (void)a; memset(p, 0xff, n); return S_SUCCESS;
}
status_t t5ai_flash_ipc_write(uint32_t a, const void *p, size_t n) {
 (void)a; (void)p; (void)n; return S_SUCCESS;
}
status_t t5ai_flash_ipc_erase_4k(uint32_t a) {
 assert(a >= 0x500000 && a < 0x7c0000 && a % 4096 == 0);
 ++erases; return fail ? E_ERROR : S_SUCCESS;
}
int main(void) {
 assert(flash_impl_init(false) == S_SUCCESS);
 // Stock flash_api computes the first poll delay as duration * 7 / 8.
 // Zero skips completion handling entirely; legacy values add 43/700 ms.
 assert(flash_impl_get_typical_sector_erase_duration_ms() * 7 / 8 == 1);
 assert(flash_impl_get_typical_subsector_erase_duration_ms() * 7 / 8 == 1);
 assert(flash_impl_erase_sector_begin(FLASH_REGION_FILESYSTEM_BEGIN) == S_SUCCESS);
 assert(erases == 16); // Every CP erase completed before begin returned.
 assert(flash_impl_erase_subsector_begin(FLASH_REGION_FILESYSTEM_BEGIN) == E_BUSY);
 assert(flash_impl_get_erase_status() == S_SUCCESS);
 assert(flash_impl_get_erase_status() == S_NO_ACTION_REQUIRED);
 assert(flash_impl_erase_subsector_begin(FLASH_REGION_FILESYSTEM_BEGIN) == S_SUCCESS);
 assert(erases == 17 && flash_impl_get_erase_status() == S_SUCCESS);
 fail = true;
 assert(flash_impl_erase_subsector_begin(FLASH_REGION_FILESYSTEM_BEGIN) == E_ERROR);
 assert(erases == 18); // An ambiguous mutation is never retried here.
 assert(flash_impl_get_erase_status() == S_NO_ACTION_REQUIRED);
 fail = false;
 assert(flash_impl_erase_subsector_begin(FLASH_REGION_FILESYSTEM_BEGIN) == S_SUCCESS);
 assert(flash_impl_get_erase_status() == S_SUCCESS);
 return 0;
}
"""


class FlashCompletionTest(unittest.TestCase):
    def test_synchronous_erase_poll_contract(self):
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
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(directory), "-I", str(ROOT), str(harness),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
