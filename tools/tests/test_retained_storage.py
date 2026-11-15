# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import unittest
from pathlib import Path

from tools.tests import test_t5ai_reset as reset_test

ROOT = Path(__file__).resolve().parents[2]

STUBS = {
    **reset_test.HEADERS,
    "pbl/board/partitions.h": "#pragma once\n",
    "pbl/crc/crc.h": "#pragma once\nuint32_t pbl_crc32(uint32_t, const void *, size_t);\n",
    "pbl/drivers/flash.h": "#pragma once\nvoid flash_read_bytes(uint8_t *, uint32_t, size_t);\n",
    "pbl/drivers/rtc.h": "#pragma once\nvoid rtc_init(void);\n",
    "flash_region/flash_region.h": "#pragma once\n#define FLASH_REGION_BOOTLOADER_END 0x10000\n",
    "system/version.h": "#pragma once\n",
    "pbl/kernel/assert.h": "#pragma once\n#define PBL_ASSERTN(x) assert(x)\n",
    "bf0_hal.h": "#pragma once\nuint32_t HAL_Get_backup(uint32_t);\nvoid HAL_Set_backup(uint32_t, uint32_t);\n",
}
STUBS["pbl/soc/t5ai/cache.h"] = (
    "#pragma once\n#define t5ai_cache_clean(p, n) ((void)0)\n"
)
STUBS["pbl/soc/t5ai/core.h"] += (
    "\n#define __get_PRIMASK() 0u\n#define __set_PRIMASK(v) ((void)(v))\n"
)
STUBS["pbl/kernel/compiler.h"] += (
    "\n#define PBL_SECTION(s) __attribute__((section(s)))\n"
)
STUBS["pbl/logging/logging.h"] = """
#pragma once
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_WRN(...) ((void)0)
#define PBL_LOG_INFO(...) ((void)0)
#define PBL_LOG_DBG(...) ((void)0)
"""

PRELUDE = """
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <pbl/soc/t5ai/core.h>
#include <pbl/kernel/compiler.h>
#include <pbl/system/rtc_registers.h>
"""

BOOT_HARNESS = (
    PRELUDE
    + r"""
#include RETAINED_SOURCE
#include BOOT_SOURCE
int main(void) {
  boot_bit_init();
  assert(boot_bits_get() == BOOT_BIT_INITIALIZED);
  assert(boot_version_read() == 0);
  t5ai_retained_write(BOOTLOADER_VERSION_REGISTER, 0x12345678);
  assert(boot_version_read() == 0);
  for (unsigned i = 1; i < 20; ++i) {
    BootBitValue bit = 1u << i;
    boot_bit_set(bit);
    assert(boot_bit_test(bit));
    assert(boot_bit_test(BOOT_BIT_INITIALIZED));
    boot_bit_clear(bit);
    assert(!boot_bit_test(bit));
    assert(boot_bits_get() == BOOT_BIT_INITIALIZED);
  }
  boot_bit_set(BOOT_BIT_FW_STABLE | BOOT_BIT_RESET_LOOP_DETECT_ONE);
  t5ai_retained_write(SLOT_OF_LAST_LAUNCHED_APP, 42);
  boot_bit_init(); // Valid warm state survives initialization.
  assert(boot_bit_test(BOOT_BIT_FW_STABLE));
  assert(boot_bit_test(BOOT_BIT_RESET_LOOP_DETECT_ONE));
  assert(t5ai_retained_read(SLOT_OF_LAST_LAUNCHED_APP) == 42);
  s_retained.values[SLOT_OF_LAST_LAUNCHED_APP] ^= 1;
  boot_bit_init(); // Payload corruption invalidates the complete store.
  assert(boot_bits_get() == BOOT_BIT_INITIALIZED);
  assert(t5ai_retained_read(SLOT_OF_LAST_LAUNCHED_APP) == 0);
  boot_bit_set(BOOT_BIT_FW_STABLE);
  s_retained.checksum ^= 1;
  boot_bit_init();
  assert(boot_bits_get() == BOOT_BIT_INITIALIZED);
  boot_bit_set(BOOT_BIT_FW_STABLE);
  s_retained.magic ^= 1;
  boot_bit_init();
  assert(boot_bits_get() == BOOT_BIT_INITIALIZED);
  assert(sizeof(s_retained) == 136);
  return 0;
}
"""
)


class RetainedStorageTest(unittest.TestCase):
    def test_t5ai_bootbits_and_actual_store_invalidation(self):
        reset_test.T5AIResetTest().compile_run(
            BOOT_HARNESS,
            STUBS,
            (
                "-DCONFIG_SOC_T5AI=1",
                f'-DRETAINED_SOURCE="{ROOT / "soc/t5ai/retained.c"}"',
                f'-DBOOT_SOURCE="{ROOT / "pebble/system/bootbits.c"}"',
            ),
        )

    def test_reset_with_actual_retained_backend(self):
        headers = {
            **reset_test.HEADERS,
            "pbl/soc/t5ai/cache.h": STUBS["pbl/soc/t5ai/cache.h"],
            "pbl/soc/t5ai/core.h": STUBS["pbl/soc/t5ai/core.h"],
            "pbl/kernel/assert.h": STUBS["pbl/kernel/assert.h"],
        }
        reset_test.T5AIResetTest().compile_run(
            reset_test.RESET_HARNESS,
            headers,
            (
                "-DCONFIG_WATCHDOG=1",
                "-DCONFIG_SOC_T5AI=1",
                "-DCONFIG_WATCHDOG_TIMEOUT_MS=30000",
                f'-DWATCHDOG_SOURCE="{ROOT / "drivers/watchdog/t5ai.c"}"',
                f'-DREBOOT_SOURCE="{ROOT / "soc/t5ai/reboot_reason.c"}"',
                f'-DCOMMON_REBOOT_SOURCE="{ROOT / "pebble/system/reboot_reason.c"}"',
                f'-DRETAINED_SOURCE="{ROOT / "soc/t5ai/retained.c"}"',
            ),
        )


if __name__ == "__main__":
    unittest.main()
