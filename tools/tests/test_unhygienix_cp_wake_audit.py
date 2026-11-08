# SPDX-License-Identifier: Apache-2.0
"""Reject flash and unresolved calls in the linked native wake-critical interval."""

import unittest

from tools.unhygienix_cp_build import audit_itcm_calls, audit_masked_wfi_leaf


class NativeWakeAudit(unittest.TestCase):
    def audit(self, entry, helper="180: bx lr"):
        symbols = {"entry": 0x100, "helper": 0x180, "flash": 0x02001234}
        assembly = {"entry": entry, "helper": helper}
        return audit_itcm_calls(symbols, 0x100, 0x300, assembly.__getitem__, ("entry",))

    def test_recursive_itcm_calls_and_mmio_literals(self):
        self.assertEqual(
            self.audit(
                "100: bl 180 <helper>\n104: .word 0x44010000",
                "180: bne.w 1a0 <helper+0x20>\n184: bx lr",
            ),
            ["entry", "helper"],
        )

    def test_direct_flash_call(self):
        with self.assertRaisesRegex(ValueError, "leaves ITCM"):
            self.audit("100: bl 02001234 <flash>")

    def test_conditional_tail_call_to_flash(self):
        with self.assertRaisesRegex(ValueError, "leaves ITCM"):
            self.audit("100: bne.w 02001234 <flash>")

    def test_nested_flash_veneer_literal(self):
        with self.assertRaisesRegex(ValueError, "XIP literal"):
            self.audit("100: bl 180 <helper>", "180: .word 0x02001235")

    def test_indirect_calls_and_tail_branches(self):
        for branch in ["blx r3", "bx ip"]:
            with (
                self.subTest(branch=branch),
                self.assertRaisesRegex(ValueError, "Indirect"),
            ):
                self.audit("100: " + branch)

    def test_empty_disassembly_is_not_a_proof(self):
        with self.assertRaisesRegex(ValueError, "Empty"):
            self.audit("100: bl 180 <helper>", "file format elf32-littlearm")

    def test_compare_branch_to_flash(self):
        with self.assertRaisesRegex(ValueError, "leaves ITCM"):
            self.audit("100: cbnz r0, 02001234 <flash>")

    def test_absolute_alias_resolves_by_function_range(self):
        assembly = {"entry": "100: bl 180 <__SWAP_SIZE>", "helper": "180: bx lr"}
        self.assertEqual(
            audit_itcm_calls(
                {"entry": 0x100, "helper": 0x180},
                0x100,
                0x300,
                assembly.__getitem__,
                ("entry",),
                {"entry": (0x100, 0x120), "helper": (0x180, 0x190)},
            ),
            ["entry", "helper"],
        )

    def test_branch_without_function_ownership(self):
        with self.assertRaisesRegex(ValueError, "no function"):
            audit_itcm_calls(
                {"entry": 0x100},
                0x100,
                0x300,
                lambda _: "100: bl 180 <alias>",
                ("entry",),
                {"entry": (0x100, 0x120)},
            )

    def test_missing_target_symbol(self):
        with self.assertRaisesRegex(ValueError, "outside ITCM"):
            self.audit("100: bl 180 <missing>")


class MaskedEntryAudit(unittest.TestCase):
    leaf = """100: 6001 str r1, [r0, #0]
102: f3bf 8f4f dsb sy
106: bf30 wfi
108: f3bf 8f6f isb sy
10c: 4770 bx lr"""

    def test_exact_stack_free_leaf(self):
        self.assertEqual(
            audit_masked_wfi_leaf(self.leaf), ["str", "dsb", "wfi", "isb", "bx"]
        )

    def test_memory_access_after_mask_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Masked WFI"):
            audit_masked_wfi_leaf(
                self.leaf.replace(
                    "106: bf30 wfi", "104: 6802 ldr r2, [r0, #0]\n106: bf30 wfi"
                )
            )

    def test_wrong_register_or_missing_barrier_is_rejected(self):
        for leaf in [
            self.leaf.replace("r1, [r0", "r2, [r0"),
            self.leaf.replace("102: f3bf 8f4f dsb sy", ""),
        ]:
            with (
                self.subTest(leaf=leaf),
                self.assertRaisesRegex(ValueError, "Masked WFI"),
            ):
                audit_masked_wfi_leaf(leaf)


class TracedLeafMachineCode(unittest.TestCase):
    def test_actual_instructions_need_no_stack_or_xip_and_preserve_other_fields(self):
        import os
        import re
        import shutil
        import struct
        import subprocess
        import tempfile
        from pathlib import Path

        try:
            from elftools.elf.elffile import ELFFile
            from unicorn import (
                UC_ARCH_ARM,
                UC_HOOK_CODE,
                UC_HOOK_MEM_READ,
                UC_HOOK_MEM_WRITE,
                UC_MODE_MCLASS,
                UC_MODE_THUMB,
                Uc,
            )
            from unicorn.arm_const import (
                UC_ARM_REG_LR,
                UC_ARM_REG_R0,
                UC_ARM_REG_R1,
                UC_ARM_REG_R2,
                UC_ARM_REG_R3,
                UC_ARM_REG_R12,
                UC_ARM_REG_SP,
            )
        except ImportError:
            self.skipTest("requires pyelftools and Unicorn")
        compiler = os.getenv("UNHYGIENIX_CP_GCC") or shutil.which("arm-none-eabi-gcc")
        if not compiler:
            self.skipTest("requires an ARM compiler")
        root = Path(__file__).resolve().parents[2]
        header = (root / "include/pbl/soc/t5ai/sleep_protocol.h").read_text()
        woke = int(re.search(r"T5AISleepStage_CP_Woke\s*=\s*(\d+)", header)[1])
        with tempfile.TemporaryDirectory() as directory:
            obj = Path(directory) / "entry.o"
            subprocess.run(
                [
                    compiler,
                    "-mcpu=cortex-m33",
                    "-mthumb",
                    "-c",
                    str(root / "boards/unhygienix/tuya/cp/variants/flash_contract.S"),
                    "-o",
                    str(obj),
                ],
                check=True,
            )
            with obj.open("rb") as stream:
                elf = ELFFile(stream)
                section = elf.get_section_by_name(".itcm_sec_code.unhygienix_sleep")
                code = section.data()
                symbol = elf.get_section_by_name(".symtab").get_symbol_by_name(
                    "unhygienix_cp_masked_wfi_trace"
                )[0]
                entry = 0x1000 + (symbol["st_value"] & ~1)
            reason_mask = 0xFF0
            encoded = (0x80 | woke) << 4
            for argument in [encoded, encoded | 0xF0000000, 0xFFFFFFFF]:
                with self.subTest(argument=argument):
                    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
                    for address, length in [
                        (0x1000, 0x4000),
                        (0x44000000, 0x1000),
                        (0x44010000, 0x1000),
                    ]:
                        cpu.mem_map(address, length)
                    cpu.mem_write(0x1000, code)
                    cpu.mem_write(0x44000000, struct.pack("<I", 0x4F000083))
                    for register, value in [
                        (UC_ARM_REG_R0, 0x44010010),
                        (UC_ARM_REG_R1, 0x15),
                        (UC_ARM_REG_R2, argument),
                        (UC_ARM_REG_LR, 0x3001),
                        (UC_ARM_REG_SP, 0x28000000),
                    ]:
                        cpu.reg_write(register, value)
                    writes, reads, wfi = [], [], []

                    def instruction(machine, address, size, _, wfi=wfi):
                        if machine.mem_read(address, size) == b"\x30\xbf":
                            wfi.append(address)
                            machine.emu_stop()

                    def write(machine, access, address, size, value, _, writes=writes):
                        writes.append((address, size, value))

                    def read(machine, access, address, size, value, _, reads=reads):
                        reads.append(address)

                    cpu.hook_add(UC_HOOK_CODE, instruction)
                    cpu.hook_add(UC_HOOK_MEM_WRITE, write)
                    cpu.hook_add(UC_HOOK_MEM_READ, read)
                    cpu.emu_start(entry | 1, 0x3000, count=100)
                    self.assertEqual(len(wfi), 1)
                    self.assertEqual(writes, [(0x44010010, 4, 0x15)])
                    self.assertEqual(reads, [])
                    # Simulate a wake; hardware sleep/residency itself is not emulated.
                    live = 0x12800083
                    cpu.mem_write(0x44000000, struct.pack("<I", live))
                    cpu.reg_write(UC_ARM_REG_R3, 0xDEAD0000)
                    cpu.reg_write(UC_ARM_REG_R12, 0xDEAD1000)
                    cpu.emu_start((wfi[0] + 2) | 1, 0x3000, count=100)
                    expected = (live & ~reason_mask) | (argument & reason_mask)
                    self.assertEqual(
                        writes[1:],
                        [
                            (0x44000000, 4, expected),
                            (0x44000094, 4, 0x424B55AA),
                            (0x44000094, 4, 0xBDB4AA55),
                        ],
                    )
                    self.assertTrue(
                        all(
                            0x1000 <= address < 0x5000 or address == 0x44000000
                            for address in reads
                        )
                    )
                    self.assertEqual(cpu.reg_read(UC_ARM_REG_SP), 0x28000000)


class MaskBarrierMachineCode(unittest.TestCase):
    def test_actual_leaf_only_writes_mask_and_restores_the_original_word(self):
        import os
        import shutil
        import struct
        import subprocess
        import tempfile
        from pathlib import Path

        try:
            from elftools.elf.elffile import ELFFile
            from unicorn import (
                UC_ARCH_ARM,
                UC_HOOK_CODE,
                UC_HOOK_MEM_READ,
                UC_HOOK_MEM_WRITE,
                UC_MODE_MCLASS,
                UC_MODE_THUMB,
                Uc,
            )
            from unicorn.arm_const import (
                UC_ARM_REG_LR,
                UC_ARM_REG_R0,
                UC_ARM_REG_R1,
                UC_ARM_REG_R2,
                UC_ARM_REG_SP,
            )
        except ImportError:
            self.skipTest("requires pyelftools and Unicorn")
        compiler = os.getenv("UNHYGIENIX_CP_GCC") or shutil.which("arm-none-eabi-gcc")
        if not compiler:
            self.skipTest("requires an ARM compiler")
        root = Path(__file__).resolve().parents[2]
        control, interrupt_mask, stack = 0x44010010, 1 << 2, 0x28000000
        with tempfile.TemporaryDirectory() as directory:
            obj = Path(directory) / "entry.o"
            subprocess.run(
                [
                    compiler,
                    "-mcpu=cortex-m33",
                    "-mthumb",
                    "-c",
                    str(root / "boards/unhygienix/tuya/cp/variants/flash_contract.S"),
                    "-o",
                    str(obj),
                ],
                check=True,
            )
            with obj.open("rb") as stream:
                elf = ELFFile(stream)
                section = elf.get_section_by_name(".itcm_sec_code.unhygienix_sleep")
                code = section.data()
                symbol = elf.get_section_by_name(".symtab").get_symbol_by_name(
                    "unhygienix_cp_mask_barrier"
                )[0]
                entry = 0x1000 + (symbol["st_value"] & ~1)
            for before in [0x10, 0x7403, 0x40000001, 0xFFFFFFF9]:
                with self.subTest(before=before):
                    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
                    cpu.mem_map(0x1000, 0x4000)
                    cpu.mem_map(control & ~0xFFF, 0x1000)
                    cpu.mem_write(0x1000, code)
                    for register, value in [
                        (UC_ARM_REG_R0, control),
                        (UC_ARM_REG_R1, before | interrupt_mask),
                        (UC_ARM_REG_R2, before),
                        (UC_ARM_REG_SP, stack),
                        (UC_ARM_REG_LR, 0x3001),
                    ]:
                        cpu.reg_write(register, value)
                    writes, reads, instructions = [], [], []

                    def write(machine, access, address, size, value, _, writes=writes):
                        writes.append((address, size, value))

                    def read(machine, access, address, size, value, _, reads=reads):
                        reads.append(address)

                    def instruction(
                        machine, address, size, _, instructions=instructions
                    ):
                        instructions.append(bytes(machine.mem_read(address, size)))

                    cpu.hook_add(UC_HOOK_CODE, instruction)
                    cpu.hook_add(UC_HOOK_MEM_WRITE, write)
                    cpu.hook_add(UC_HOOK_MEM_READ, read)
                    cpu.emu_start(entry | 1, 0x3000, count=20)
                    self.assertEqual(
                        writes,
                        [(control, 4, before | interrupt_mask), (control, 4, before)],
                    )
                    self.assertEqual(reads, [])
                    self.assertEqual(len(instructions), 6)
                    self.assertNotIn(b"\x30\xbf", instructions)
                    self.assertEqual(cpu.reg_read(UC_ARM_REG_SP), stack)
                    self.assertEqual(
                        struct.unpack("<I", cpu.mem_read(control, 4))[0], before
                    )


if __name__ == "__main__":
    unittest.main()
