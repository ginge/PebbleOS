# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Check the real T5 flash driver's synchronous erase completion contract."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#define nullptr NULL
typedef int status_t;
#define S_SUCCESS 0
#define S_NO_ACTION_REQUIRED 1
#define E_ERROR -1
#define E_INVALID_OPERATION -4
#define PASSED(s) ((s) >= 0)
#define FAILED(s) ((s) < 0)
#define PBL_LOG_MODULE_DECLARE(...)
#define PBL_LOG_ERR(...)
#define PBL_LOG_WRN(...)
#define PART_FILESYSTEM_OFFSET 0x500000u
#define PART_FILESYSTEM_SIZE 0x2c0000u
#define PART_SYSTEM_RESOURCES_BANK_0_OFFSET 0x2c0000u
#define PART_SYSTEM_RESOURCES_BANK_0_SIZE 0x100000u
#define PART_SHARED_PRF_STORAGE_OFFSET 0x4ea000u
#define PART_SHARED_PRF_STORAGE_SIZE 0x10000u
#define PART_FIRMWARE_SLOT_0_OFFSET 0x120000u
#define PART_FIRMWARE_SLOT_0_SIZE 0x1a0000u
#define PART_FIRMWARE_SLOT_1_OFFSET 0x460000u
#define PART_CD_OFFSET 0x450000u
#define PART_DEBUG_DB_OFFSET 0x490000u
#define T5AI_FLASH_XIP_BASE 0x02000000u
#define T5AI_FLASH_SIZE 0x800000u
#define PAGE_SIZE_BYTES 256u
#define SUBSECTOR_SIZE_BYTES 4096u
#define SECTOR_SIZE_BYTES 65536u
struct pbl_flash;
struct pbl_flash_ops {
 int (*init)(const struct pbl_flash *);
 int (*read)(const struct pbl_flash *,uint32_t,void *,size_t);
 int (*write)(const struct pbl_flash *,uint32_t,const void *,size_t);
 int (*erase)(const struct pbl_flash *,uint32_t,uint32_t);
};
struct pbl_flash_geometry {uint32_t size,page_size,sector_size,subsector_size;};
struct pbl_flash {const struct pbl_flash_ops *ops;const struct pbl_flash_geometry *geometry;bool coredump;};
static bool pbl_flash_in_coredump(const struct pbl_flash *f){return f->coredump;}
#define PBL_FLASH_STATE_DEFINE(name)
#define PBL_FLASH_INIT(n,l,p,e,o,g,t,a) {.ops=o,.geometry=g}
#define PBL_DEVICE_REGISTER(...)
status_t t5ai_flash_ipc_init(void);
bool t5ai_flash_ipc_is_available(void);
status_t t5ai_flash_ipc_read(uint32_t,void *,size_t);
status_t t5ai_flash_ipc_write(uint32_t,const void *,size_t);
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
 assert(T5AI_FLASH.ops->init(&T5AI_FLASH) == 0);
 assert(T5AI_FLASH.ops->erase(&T5AI_FLASH,PART_FILESYSTEM_OFFSET,SECTOR_SIZE_BYTES) == 1);
 assert(erases == 16);
 assert(T5AI_FLASH.ops->erase(&T5AI_FLASH,PART_FILESYSTEM_OFFSET,SUBSECTOR_SIZE_BYTES) == 1);
 assert(erases == 17);
 fail = true;
 assert(T5AI_FLASH.ops->erase(&T5AI_FLASH,PART_FILESYSTEM_OFFSET,SUBSECTOR_SIZE_BYTES) == -EIO);
 assert(erases == 18); // Ambiguous mutations are never retried.
 fail = false;
 assert(T5AI_FLASH.ops->erase(&T5AI_FLASH,PART_FILESYSTEM_OFFSET,SUBSECTOR_SIZE_BYTES) == 1);
 assert(T5AI_FLASH.ops->erase(&T5AI_FLASH,PART_FILESYSTEM_OFFSET+1,SUBSECTOR_SIZE_BYTES) == -EINVAL);
 assert(T5AI_FLASH.ops->erase(&T5AI_FLASH,PART_FILESYSTEM_OFFSET,1) == -EINVAL);
 assert(erases == 19);
 struct pbl_flash crash = T5AI_FLASH;crash.coredump=true;
 assert(crash.ops->init(&crash) == -ENOTSUP);
 return 0;
}
"""


class FlashCompletionTest(unittest.TestCase):
    def test_synchronous_erase_poll_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "stub.h").write_text(STUB)
            for name in [
                "pbl/drivers/flash.h",
                "pbl/board/partitions.h",
                "pbl/soc/t5ai/image_layout.h",
                "pbl/soc/t5ai/devices.h",
                "pbl/logging/logging.h",
                "pbl/soc/t5ai/flash_ipc.h",
                "flash_region/flash_region.h",
                "pbl/system/rtc_registers.h",
                "system/status_codes.h",
            ]:
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "stub.h"\n')
            harness = directory / "test.c"
            harness.write_text(HARNESS)
            binary = directory / "test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Wno-unused-parameter",
                    "-Werror",
                    "-I",
                    str(directory),
                    "-I",
                    str(ROOT),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
