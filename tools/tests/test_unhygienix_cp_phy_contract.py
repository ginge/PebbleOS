# SPDX-License-Identifier: Apache-2.0
"""Replay the hardware-failing calibration callback path from linked CP code."""

from io import BytesIO
import os
from pathlib import Path
import struct
import unittest

from elftools.elf.elffile import ELFFile
from tools import unhygienix_cp_variant as variant

try:
    from unicorn import (
        Uc,
        UcError,
        UC_ARCH_ARM,
        UC_MODE_MCLASS,
        UC_MODE_THUMB,
        UC_HOOK_CODE,
    )
    from unicorn.arm_const import (
        UC_ARM_REG_R0,
        UC_ARM_REG_LR,
        UC_ARM_REG_SP,
        UC_ARM_REG_PC,
    )
except ImportError:
    Uc = None


@unittest.skipUnless(
    Uc and all(os.getenv("UNHYGIENIX_CP_" + p + "_WORK") for p in ["OFF", "ON"]),
    "requires Unicorn and both linked CP profiles",
)
class CalibrationPath(unittest.TestCase):
    def test_actual_getter_fallback_otp_and_null_callback_failure(self):
        for wifi in ["off", "on"]:
            root = (
                Path(os.environ["UNHYGIENIX_CP_" + wifi.upper() + "_WORK"])
                / "build/bk7258/tuya_app/bk7258"
            )
            elf = ELFFile(BytesIO((root / "app.elf").read_bytes()))

            def sym(name):
                return variant.symbol(elf, name)["st_value"]

            table = sym("g_phy_os_funcs")
            getter = struct.unpack("<I", variant.memory(elf, table + 64, 4))[0]
            saver = struct.unpack("<I", variant.memory(elf, table + 60, 4))[0]
            self.assertEqual(getter, sym("get_info_item"))
            self.assertEqual(saver, sym("save_info_item"))
            printer = struct.unpack("<I", variant.memory(elf, table + 260, 4))[0] & ~1
            for broken, otp_ready in [(False, 0), (False, 1), (True, 0)]:
                with self.subTest(wifi=wifi, broken=broken, otp_ready=otp_ready):
                    u = Uc(UC_ARCH_ARM, UC_MODE_MCLASS | UC_MODE_THUMB)
                    for base, size in [(0x02020000, 0x100000), (0x28000000, 0x100000)]:
                        u.mem_map(base, size)
                    for section in elf.iter_sections():
                        if (
                            section["sh_type"] == "SHT_NOBITS"
                            or not section["sh_flags"] & 2
                        ):
                            continue
                        base = section["sh_addr"]
                        if (
                            0x02020000 <= base < 0x02120000
                            or 0x28000000 <= base < 0x28100000
                        ):
                            u.mem_write(base, section.data())
                    u.mem_write(sym("g_phy_funcs_t"), struct.pack("<I", table))
                    u.mem_write(
                        sym("g_phy_vars"), struct.pack("<I", sym("g_phy_os_variable"))
                    )
                    if broken:
                        u.mem_write(table + 64, b"\0" * 4)
                    seen = {"getter": 0, "otp": 0, "printf": 0, "stopped": False}

                    def code(u, pc, size, data):
                        if pc == 0x02021000:
                            seen["stopped"] = True
                            u.emu_stop()
                            return
                        if pc == getter & ~1:
                            seen["getter"] += 1
                        if (
                            pc
                            == sym("manual_cal_txpwr_tab_ready_in_flash_or_otp2") & ~1
                        ):
                            seen["otp"] += 1
                            u.reg_write(UC_ARM_REG_R0, otp_ready)
                            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
                        elif pc == printer:
                            seen["printf"] += 1
                            u.reg_write(UC_ARM_REG_R0, 0)
                            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

                    u.hook_add(UC_HOOK_CODE, code)
                    u.reg_write(UC_ARM_REG_SP, 0x28090000)
                    u.reg_write(UC_ARM_REG_LR, 0x02021001)
                    if broken:
                        with self.assertRaises(UcError):
                            u.emu_start(
                                sym("bk7011_get_rfcali_mode") | 1, 0, count=10000
                            )
                        self.assertEqual(u.reg_read(UC_ARM_REG_PC), 0)
                        self.assertEqual(seen["getter"], 0)
                    else:
                        u.emu_start(sym("bk7011_get_rfcali_mode") | 1, 0, count=10000)
                        self.assertTrue(seen["stopped"])
                        self.assertEqual(seen["getter"], 1)
                        self.assertEqual(seen["otp"], 1)
                        self.assertEqual(seen["printf"], 1)
                        mode = struct.unpack(
                            "<I", u.mem_read(sym("gcali_context") + 12, 4)
                        )[0]
                        self.assertEqual(mode, otp_ready)


if __name__ == "__main__":
    unittest.main(verbosity=2)
