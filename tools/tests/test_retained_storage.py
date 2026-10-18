# SPDX-License-Identifier: Apache-2.0

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests import test_t5ai_reset as reset_test

ROOT = Path(__file__).resolve().parents[2]
STOCK_BASELINE = "f40a377469d5ed0083a8b7b0be78b4e51954623e"

STUBS = {
    **reset_test.HEADERS,
    "pbl/crc/crc.h": "#pragma once\nuint32_t pbl_crc32(uint32_t, const void *, size_t);\n",
    "pbl/drivers/flash.h": "#pragma once\nvoid flash_read_bytes(uint8_t *, uint32_t, size_t);\n",
    "pbl/drivers/rtc.h": "#pragma once\nvoid rtc_init(void);\n",
    "flash_region/flash_region.h": "#pragma once\n#define FLASH_REGION_BOOTLOADER_END 0x10000\n",
    "system/version.h": "#pragma once\n",
    "system/passert.h": "#pragma once\n#define PBL_ASSERTN(x) assert(x)\n",
    "bf0_hal.h": "#pragma once\nuint32_t HAL_Get_backup(uint32_t);\nvoid HAL_Set_backup(uint32_t, uint32_t);\n",
}
STUBS["pbl/kernel/compiler.h"] += '\n#define PBL_SECTION(s) __attribute__((section(s)))\n'
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
#include <cmsis_core.h>
#include <pbl/kernel/compiler.h>
#include <system/rtc_registers.h>
"""

BOOT_HARNESS = PRELUDE + r"""
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


class RetainedStorageTest(unittest.TestCase):
    def test_t5ai_bootbits_and_actual_store_invalidation(self):
        reset_test.T5AIResetTest().compile_run(BOOT_HARNESS, STUBS, (
            "-DCONFIG_SOC_T5AI=1",
            f'-DRETAINED_SOURCE="{ROOT / "soc/t5ai/retained.c"}"',
            f'-DBOOT_SOURCE="{ROOT / "fw/system/bootbits.c"}"',
        ))

    def test_reset_with_actual_retained_backend(self):
        headers = {**reset_test.HEADERS, "system/passert.h": STUBS["system/passert.h"]}
        reset_test.T5AIResetTest().compile_run(reset_test.RESET_HARNESS, headers, (
            "-DCONFIG_WATCHDOG=1", "-DCONFIG_SOC_T5AI=1",
            "-DCONFIG_WATCHDOG_TIMEOUT_MS=30000",
            f'-DWATCHDOG_SOURCE="{ROOT / "drivers/watchdog/t5ai.c"}"',
            f'-DREBOOT_SOURCE="{ROOT / "soc/t5ai/reboot_reason.c"}"',
            f'-DCOMMON_REBOOT_SOURCE="{ROOT / "fw/system/reboot_reason.c"}"',
            f'-DRETAINED_SOURCE="{ROOT / "soc/t5ai/retained.c"}"',
        ))

    def test_stock_backend_generated_code_is_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, contents in STUBS.items():
                p = directory / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(contents)
            (directory / "prelude.h").write_text(PRELUDE)
            for backend in ("CONFIG_SOC_NRF52", "CONFIG_SOC_SF32LB52", "CONFIG_QEMU", "CONFIG_SOC_POSIX"):
                for filename in ("bootbits.c", "reboot_reason.c"):
                    with self.subTest(backend=backend, file=filename):
                        before = directory / "before" / filename
                        before.parent.mkdir(exist_ok=True)
                        before.write_bytes(subprocess.check_output([
                            "git", "show", f"{STOCK_BASELINE}:fw/system/{filename}"], cwd=ROOT))
                        assemblies = []
                        for source in (before, ROOT / "fw/system" / filename):
                            output = directory / "output.s"
                            subprocess.run([
                                "cc", "-std=gnu2x", "-O2", "-fno-ident",
                                "-Wno-address-of-packed-member", "-S", str(source),
                                "-include", str(directory / "prelude.h"),
                                "-I", str(directory), "-I", str(ROOT / "include"),
                                "-I", str(ROOT / "fw"), "-I", str(ROOT / "fw/system"),
                                f"-D{backend}=1", "-o", str(output),
                            ], check=True)
                            assembly = re.sub(r'^\s*\.file.*$', '', output.read_text(), flags=re.MULTILINE)
                            assembly = re.sub(r'\.LF([BE])\d+', r'.LF\1', assembly)
                            assemblies.append(assembly)
                        self.assertEqual(*assemblies)


if __name__ == "__main__":
    unittest.main()
