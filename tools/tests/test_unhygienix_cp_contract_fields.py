# SPDX-License-Identifier: Apache-2.0
"""Named CP probe and AP/CP scalar equivalence; fail closed on definition drift."""

import os
import re
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from elftools.elf.elffile import ELFFile

from tools import unhygienix_cp_build as build

# Frozen pre-refactor wire values; changing definitions must not silently change ABI.
LEGACY = struct.pack(
    "<26I",
    0x55435031,
    16,
    20,
    4,
    12,
    0,
    1,
    2,
    2,
    20,
    22,
    30,
    91,
    93,
    101,
    122,
    9,
    12,
    38,
    4,
    5,
    6,
    64,
    66,
    78,
    75,
)


class ContractFieldsTest(unittest.TestCase):
    def test_pinned_wire_values_and_each_named_mismatch(self):
        self.assertEqual(struct.pack("<26I", *build.CONTRACT), LEGACY)
        build.verify_contract(LEGACY)
        for i, name in enumerate(build.CONTRACT_FIELDS):
            with self.subTest(field=name):
                wrong = bytearray(LEGACY)
                wrong[4 * i] ^= 1
                with self.assertRaisesRegex(ValueError, name):
                    build.verify_contract(wrong)
        for data in (b"", LEGACY[:-1], LEGACY + b"\0"):
            with self.assertRaisesRegex(ValueError, "size"):
                build.verify_contract(data)

    def test_package_verifier_uses_header_derived_abi(self):
        from tools import unhygienix_cp_variant as variant

        self.assertEqual(build.NATIVE_ALARM_ABI, build.native_alarm_contract()[0])
        with self.assertRaises(ValueError):
            variant.verify_sleep_link_hashes(b"unreviewed", b"config", "off")

    def test_invalid_definition_rejected(self):
        for text in ("", "CP_CONTRACT(bad)", "CP_CONTRACT(word, 0x1, VALUE)\n" * 2):
            with self.subTest(text=text), tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "invalid.def"
                path.write_text(text)
                with self.assertRaises(ValueError):
                    build.contract_fields(path)

    def test_native_scalars_use_complete_production_header(self):
        self.assertEqual(
            build.native_alarm_contract(), [0x50424167, 0x70, 0x71, 64, 16000]
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "probe.c"
            source.write_text("""
#include <stdio.h>
#include <pbl/soc/t5ai/sleep_protocol.h>
int main(void) {
  const uint32_t values[] = {
    T5AI_PM_NATIVE_ALARM_ABI, T5AI_PM_NATIVE_ALARM_COMMAND,
    T5AI_PM_NATIVE_ALARM_WAKE, T5AI_PM_NATIVE_ALARM_MIN_TICKS,
    T5AI_PM_NATIVE_ALARM_MAX_TICKS,
  };
  return fwrite(values, sizeof(values), 1, stdout) != 1;
}
""")
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Werror",
                    "-I",
                    str(build.ROOT / "include"),
                    str(source),
                    "-o",
                    str(root / "probe"),
                ],
                check=True,
            )
            result = subprocess.check_output([str(root / "probe")])
            self.assertEqual(
                list(struct.unpack("=5I", result)), build.native_alarm_contract()
            )
            text = build.PROTOCOL.read_text()
            for bad in (
                text.replace("#define T5AI_PM_NATIVE_ALARM_ABI", "#define MISSING"),
                text + "\n#define T5AI_PM_NATIVE_ALARM_ABI 1u\n",
                text.replace("0x70u", "(0x70u)"),
            ):
                path = root / "bad.h"
                path.write_text(bad)
                with self.assertRaises(ValueError):
                    build.native_alarm_contract(path)

    @unittest.skipUnless(
        os.getenv("UNHYGIENIX_CP_CONTRACT_FLAGS"), "requires vendor CP build flags"
    )
    def test_complete_arm_probe_with_vendor_headers(self):
        flags_file = Path(os.environ["UNHYGIENIX_CP_CONTRACT_FLAGS"])
        text = flags_file.read_text()
        flags = []
        for key in ("C_DEFINES", "C_INCLUDES", "C_FLAGS"):
            flags += shlex.split(re.search(rf"^{key} = (.*)$", text, re.MULTILINE)[1])
        compiler = os.environ["UNHYGIENIX_CP_CONTRACT_CC"]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            shutil.copy2(
                build.BOARD / "contract.def", root / "unhygienix_cp_contract.def"
            )
            source = root / "probe.c"
            shutil.copy2(build.BOARD / "contract.c", source)
            subprocess.run(
                [compiler, *flags, "-c", str(source), "-o", str(root / "probe.o")],
                check=True,
            )
            with (root / "probe.o").open("rb") as handle:
                elf = ELFFile(handle)
                symbol = elf.get_section_by_name(".symtab").get_symbol_by_name(
                    "unhygienix_cp_contract"
                )[0]
                section = elf.get_section(symbol["st_shndx"])
                offset = symbol["st_value"] - section["sh_addr"]
                data = section.data()[offset : offset + symbol["st_size"]]
                self.assertEqual(data, LEGACY)
                build.verify_contract(data)
