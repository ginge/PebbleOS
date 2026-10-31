# SPDX-License-Identifier: Apache-2.0
"""Guard failures and ARM execution for isolated Unhygienix CP profiles.

Set UNHYGIENIX_CP_OFF_WORK, UNHYGIENIX_CP_ON_WORK and UNHYGIENIX_CP_PACKAGES
for linked-artifact qualification. Unicorn and pyelftools are isolated test deps.
"""

from io import BytesIO
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

from elftools.elf.elffile import ELFFile
from tools import unhygienix_cp_build as build
from tools import unhygienix_cp_variant as variant
from tools import patch_unhygienix_cp_latency as legacy

try:
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import (
        UC_ARM_REG_PC,
        UC_ARM_REG_LR,
        UC_ARM_REG_SP,
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_R2,
    )
except ImportError:
    Uc = None


class LibraryGuard(unittest.TestCase):
    def check_link(self, tokens, wifi):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "CMakeFiles/app.elf.dir/link.txt"
            path.parent.mkdir(parents=True)
            path.write_text(tokens)
            return build.inspect_libraries(root, wifi)

    def test_adapter_and_bt_only_phy_are_distinct(self):
        self.assertEqual(
            self.check_link(
                "armino/bk_phy/libbk_phy.a "
                "/sdk/components/bk_libs/bk7258/libs/libcom_phy.a",
                "off",
            ),
            "libcom_phy.a",
        )

    def test_off_rejects_proprietary_wifi_phy_and_exchange(self):
        for archive in [
            "libwifi.a",
            "libwifi_v1.a",
            "libbk_phy.a",
            "libble_wifi_exchange.a",
        ]:
            with self.subTest(archive=archive), self.assertRaises(ValueError):
                self.check_link(
                    "/sdk/components/bk_libs/bk7258/libs/libcom_phy.a "
                    "/sdk/components/bk_libs/bk7258/libs/" + archive,
                    "off",
                )

    def test_off_requires_bt_phy(self):
        with self.assertRaises(ValueError):
            self.check_link("armino/bk_phy/libbk_phy.a", "off")


@unittest.skipUnless(
    all(os.getenv("UNHYGIENIX_CP_" + p + "_WORK") for p in ["OFF", "ON"])
    and os.getenv("UNHYGIENIX_CP_PACKAGES"),
    "requires both CP artifact sets",
)
class LinkedProfiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.links = {}
        for wifi in ["off", "on"]:
            work = Path(os.environ["UNHYGIENIX_CP_" + wifi.upper() + "_WORK"])
            output = work / "build/bk7258/tuya_app/bk7258"
            elf = ELFFile(BytesIO((output / "app.elf").read_bytes()))
            image = (output / "app.bin").read_bytes()
            package = Path(os.environ["UNHYGIENIX_CP_PACKAGES"]) / ("packaged-" + wifi)
            fixed = (package / "cp-variant.bin").read_bytes()
            manifest = json.loads((package / "manifest.json").read_text())
            calls, proof = variant.verify_controller(elf, image)
            cls.links[wifi] = (elf, image, fixed, calls, proof, output, manifest)

    def test_profiles_abi_and_libraries(self):
        for wifi, (
            elf,
            image,
            fixed,
            calls,
            proof,
            output,
            manifest,
        ) in self.links.items():
            with self.subTest(wifi=wifi):
                report = build.inspect(output / "app.elf", output / "sdkconfig", wifi)
                self.assertEqual(report["abi_contract"], build.CONTRACT)
                build.inspect_libraries(output, wifi)
                self.assertFalse(manifest["flash_qualified"])
                self.assertEqual(manifest["stock_sha256"], variant.STOCK_SHA256)
                with self.assertRaises(ValueError):
                    legacy.verify_base(image)
                expected = bytearray(image)
                for call in calls:
                    expected[call - variant.CP_XIP : call - variant.CP_XIP + 4] = (
                        variant.thumb_bl(call, manifest["hook_address"])
                    )
                self.assertEqual(fixed[: len(image)], expected)

    def test_reject_changed_instruction_or_call_even_with_matching_raw(self):
        for wifi, (
            elf,
            image,
            fixed,
            calls,
            proof,
            output,
            manifest,
        ) in self.links.items():
            for offset in [
                proof["lld_con_sched"]["address"] - variant.CP_XIP,
                calls[0] - variant.CP_XIP,
            ]:
                with self.subTest(wifi=wifi, offset=offset):
                    data = bytearray((output / "app.elf").read_bytes())
                    address = variant.CP_XIP + offset
                    for section in elf.iter_sections():
                        if (
                            section["sh_addr"]
                            <= address
                            < section["sh_addr"] + section["sh_size"]
                        ):
                            data[
                                section["sh_offset"] + address - section["sh_addr"]
                            ] ^= 1
                            break
                    corrupt = bytearray(image)
                    corrupt[offset] ^= 1
                    with self.assertRaises(ValueError):
                        variant.verify_controller(ELFFile(BytesIO(data)), corrupt)

    def test_reviewed_hashes_and_unknown_links(self):
        for wifi, (
            elf,
            image,
            fixed,
            calls,
            proof,
            output,
            manifest,
        ) in self.links.items():
            config = (output / "sdkconfig").read_bytes()
            variant.verify_link_hashes(image, config, wifi)
            with self.assertRaises(ValueError):
                variant.verify_link_hashes(image + b"x", config, wifi)
            with self.assertRaises(ValueError):
                variant.verify_link_hashes(image, config + b"x", wifi)
            with self.assertRaises(ValueError):
                variant.verify_link_hashes(image, config, "unknown")

    def test_legacy_manifest_rejects_experimental_schema(self):
        from tools import unhygienix_cp

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.links["off"][-1]
            (root / "pebbleos.t5ai-cp.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "pinned Unhygienix inputs"):
                unhygienix_cp.validate_manifest(root)

    def test_itcm_copy_rejects_missing_raw_load_bytes(self):
        elf, image, *rest = self.links["off"]
        variant.verify_flash_copy(elf, image)
        corrupt = bytearray(image)
        load = variant.symbol(elf, "__itcm_text")["st_value"]
        corrupt[load - variant.CP_XIP] ^= 1
        with self.assertRaises(ValueError):
            variant.verify_flash_copy(elf, corrupt)

    def test_phy_table_rejects_null_or_wrong_callback(self):
        elf, image, fixed, calls, proof, output, manifest = self.links["off"]
        table = variant.symbol(elf, "g_phy_os_funcs")
        section = elf.get_section(table["st_shndx"])
        offset = section["sh_offset"] + table["st_value"] - section["sh_addr"] + 64
        for value in (0, variant.symbol(elf, "save_info_item")["st_value"]):
            data = bytearray((output / "app.elf").read_bytes())
            struct.pack_into("<I", data, offset, value)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "bad.elf"
                path.write_bytes(data)
                with self.assertRaisesRegex(ValueError, "calibration callback"):
                    build.inspect(path, output / "sdkconfig", "off")

    def test_raw_elf_mismatch(self):
        elf, image, *rest = self.links["off"]
        corrupt = bytearray(image)
        corrupt[
            (variant.symbol(elf, "lld_con_data_tx")["st_value"] & ~1) - variant.CP_XIP
        ] ^= 1
        with self.assertRaises(ValueError):
            variant.verify_controller(elf, corrupt)

    @unittest.skipUnless(Uc, "requires Unicorn")
    def test_scheduler_tx_failure_both_paths_wrap_and_normal_latency(self):
        # External helpers are stubbed; scheduler, TX and compiled correction run.
        env = 0x28070554
        for wifi, (
            elf,
            image,
            fixed_image,
            calls,
            proof,
            output,
            manifest,
        ) in self.links.items():

            def addr(name):
                return variant.symbol(elf, name)["st_value"] & ~1

            for fixed in [False, True]:
                for second, wrap, instant in [
                    (False, False, True),
                    (True, False, True),
                    (False, True, True),
                    (True, True, True),
                    (False, False, False),
                ]:
                    with self.subTest(
                        wifi=wifi,
                        fixed=fixed,
                        second=second,
                        wrap=wrap,
                        instant=instant,
                    ):
                        u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
                        for start, size in [
                            (0x02020000, 0x100000),
                            (0x28000000, 0x80000),
                            (0x60000000, 0x20000),
                        ]:
                            u.mem_map(start, size)
                        u.mem_write(variant.CP_XIP, fixed_image if fixed else image)
                        u.mem_write(addr("lld_con_env") + 4, struct.pack("<I", env))
                        state = dict(assertions=0, removes=0, inserts=0, maxcalc=0)

                        def hook(u, pc, size, data):
                            value = 0
                            if pc == 0x02021000:
                                u.emu_stop()
                                return
                            if pc == addr("assert_err2"):
                                state["assertions"] += 1
                                u.emu_stop()
                                return
                            if pc == addr("sch_arb_remove"):
                                state["removes"] += 1
                            elif pc == addr("sch_arb_insert"):
                                state["inserts"] += 1
                                value = int(second and state["inserts"] == 1)
                            elif pc == addr("lld_con_max_lat_calc"):
                                state["maxcalc"] += 1
                            elif pc == addr("lld_sync_win_compute"):
                                value = 100
                            elif pc == addr("lld_read_clock"):
                                value = 414005
                            elif pc in {
                                addr(n)
                                for n in [
                                    "_disable_int_wrapper",
                                    "_enable_int_wrapper",
                                    "common_list_push_back",
                                    "lld_con_tx_prog",
                                    "lld_con_prio_idx_get",
                                    "rand",
                                ]
                            }:
                                pass
                            else:
                                return
                            u.reg_write(UC_ARM_REG_R0, value)
                            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

                        u.hook_add(UC_HOOK_CODE, hook)

                        def put(off, val, fmt="I"):
                            u.mem_write(env + off, struct.pack("<" + fmt, val))

                        def get(off, fmt="I"):
                            return struct.unpack(
                                "<" + fmt, u.mem_read(env + off, struct.calcsize(fmt))
                            )[0]

                        event = 65520 if wrap else 4640
                        for off, value, fmt in [
                            (56, 48, "I"),
                            (60, 19200, "I"),
                            (66, 0, "H"),
                            (72, (event + 24) & 65535, "H"),
                            (74, int(instant), "B"),
                            (76, 413520, "I"),
                            (84, 413424, "I"),
                            (92, 413424, "I"),
                            (100, 96, "I"),
                            (104, 19200, "I"),
                            (114, 22 if second else 30, "H"),
                            (120, event, "H"),
                            (122, 1, "H"),
                            (130, 0x573, "H"),
                            (142, 1, "B"),
                        ]:
                            put(off, value, fmt)

                        def call(name, r1):
                            for reg, value in [
                                (UC_ARM_REG_R0, 1),
                                (UC_ARM_REG_R1, r1),
                                (UC_ARM_REG_R2, 0),
                                (UC_ARM_REG_SP, 0x28020000),
                                (UC_ARM_REG_LR, 0x02021001),
                            ]:
                                u.reg_write(reg, value)
                            u.emu_start(addr(name) | 1, 0, count=100000)
                            self.assertEqual(
                                u.reg_read(UC_ARM_REG_PC),
                                addr("assert_err2")
                                if state["assertions"]
                                else 0x02021000,
                            )

                        call("lld_con_sched", 414005)
                        self.assertEqual(state["maxcalc"], int(instant))
                        self.assertEqual(get(150, "B"), int(not (fixed and instant)))
                        before = (get(76), get(122, "H"))
                        call("lld_con_data_tx", 0x28030000)
                        if fixed and instant:
                            self.assertEqual(state["assertions"], 0)
                            self.assertEqual(state["removes"], 0)
                            self.assertEqual((get(76), get(122, "H")), before)
                        elif not instant:
                            self.assertEqual(
                                (
                                    state["assertions"],
                                    state["removes"],
                                    get(76),
                                    get(122, "H"),
                                ),
                                (0, 1, 414096, 7),
                            )
                        elif not second and not wrap:
                            self.assertEqual(
                                (
                                    state["assertions"],
                                    state["removes"],
                                    get(76),
                                    get(122, "H"),
                                ),
                                (1, 1, 414576, 0),
                            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
