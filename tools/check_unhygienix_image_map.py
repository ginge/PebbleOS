#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import argparse
import re
from pathlib import Path

SLOT_BEGIN = 0x02120000
SLOT_END = 0x022B0000
NOR_BEGIN = 0x02000000
NOR_END = 0x02800000

EXPECTED_MEMORY = {
    "FLASH": (0x02120000, 0x00190000),
    "RAM": (0x28010000, 0x0001F000),
    "T5AI_DISPLAY_RAM": (0x60000000, 0x00020000),
}
PARTITION_SYMBOLS = {
    "__WORKER_RAM__": 0x2802F000,
    "__WORKER_RAM_size__": 0x3000,
    "__APP_RAM__": 0x28032000,
    "__APP_RAM_size__": 0x34000,
}
REQUIRED_SYMBOLS = (
    "__isr_stack_start__",
    "__kernel_main_stack_start__",
    "__kernel_bg_stack_start__",
    "_heap_start",
    "_heap_end",
    "__t5ai_framebuffer_start__",
    "__t5ai_framebuffer_end__",
)
BANNED_RUNTIME_PATHS = (
    "boards/unhygienix/tuya/",
    "bk_idk/",
    "tuyaos/",
)

MEMORY_RE = re.compile(
    r"^([A-Z][A-Z0-9_]*)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+.*$"
)
SECTION_RE = re.compile(
    r"^(\.[^\s]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)"
    r"(?:\s+load address 0x([0-9a-fA-F]+))?"
)
SYMBOL_RE = re.compile(r"^\s*0x([0-9a-fA-F]+)\s+([A-Za-z_][A-Za-z0-9_]*)\s+=\s+.+$")


def validate_map(text: str) -> list[str]:
    errors = []
    memory_regions = {}
    symbols = {}
    saw_flash_section = False
    vector_section = None
    # GNU ld places long output-section names on a line of their own.
    pfs_flags = re.search(
        r"^\.t5ai_pfs_flags\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)",
        text, re.MULTILINE,
    )
    pfs_flags_section = (
        (int(pfs_flags.group(1), 16), int(pfs_flags.group(2), 16))
        if pfs_flags else None
    )

    for line in text.splitlines():
        memory = MEMORY_RE.match(line)
        if memory:
            memory_regions[memory.group(1)] = (
                int(memory.group(2), 16),
                int(memory.group(3), 16),
            )

        symbol = SYMBOL_RE.match(line)
        if symbol and symbol.group(2) in (*REQUIRED_SYMBOLS, *PARTITION_SYMBOLS):
            symbols[symbol.group(2)] = int(symbol.group(1), 16)

        section = SECTION_RE.match(line)
        if not section:
            continue
        name = section.group(1)
        address = int(section.group(2), 16)
        size = int(section.group(3), 16)
        load_address = int(section.group(4), 16) if section.group(4) else None

        if name == ".isr_vector":
            vector_section = (address, size)
        if name == ".t5ai_pfs_flags":
            pfs_flags_section = (address, size)

        if NOR_BEGIN <= address < NOR_END:
            saw_flash_section = True
            if address < SLOT_BEGIN or address + size > SLOT_END:
                errors.append(
                    f"{name} VMA 0x{address:08x}..0x{address + size:08x} "
                    "is outside firmware slot 0"
                )
        if load_address is not None and NOR_BEGIN <= load_address < NOR_END:
            if not SLOT_BEGIN <= load_address < SLOT_END:
                errors.append(
                    f"{name} load address 0x{load_address:08x} is outside "
                    "firmware slot 0"
                )

    for name, expected in EXPECTED_MEMORY.items():
        actual = memory_regions.get(name)
        if actual is None:
            errors.append(f"map has no {name} memory region")
        elif actual != expected:
            errors.append(
                f"{name} region is 0x{actual[0]:08x} length 0x{actual[1]:x}; "
                f"expected 0x{expected[0]:08x} length 0x{expected[1]:x}"
            )
    pfs_ram = memory_regions.get("T5AI_PFS_RAM")
    if pfs_ram is not None or pfs_flags_section is not None:
        expected = (0x60020000, 8192)
        if pfs_ram != expected:
            errors.append("SD PFS metadata has an invalid private PSRAM region")
        if pfs_flags_section != expected:
            errors.append("SD PFS flags do not fill their private PSRAM region")

    if not saw_flash_section:
        errors.append("map has no output section in the T5AI firmware slot")
    if vector_section != (SLOT_BEGIN, 0x140):
        errors.append(".isr_vector is not the 0x140-byte T5AI table at slot start")

    missing_symbols = [name for name in REQUIRED_SYMBOLS if name not in symbols]
    if missing_symbols:
        errors.append("map is missing layout symbols: " + ", ".join(missing_symbols))
    else:
        kernel_begin, kernel_length = EXPECTED_MEMORY["RAM"]
        kernel_end = kernel_begin + kernel_length
        stack_heap = [symbols[name] for name in REQUIRED_SYMBOLS[:5]]
        if stack_heap != sorted(stack_heap) or not all(
            kernel_begin <= address <= kernel_end for address in stack_heap
        ):
            errors.append("stack/heap symbols are unordered or outside RAM")
        display_begin, display_length = EXPECTED_MEMORY["T5AI_DISPLAY_RAM"]
        framebuffer = [symbols[name] for name in REQUIRED_SYMBOLS[5:]]
        if framebuffer != sorted(framebuffer) or not (
            display_begin
            <= framebuffer[0]
            <= framebuffer[1]
            <= display_begin + display_length
        ):
            errors.append("framebuffer symbols are outside T5AI_DISPLAY_RAM")

    for name, expected in PARTITION_SYMBOLS.items():
        if symbols.get(name) != expected:
            errors.append(f"incorrect or missing RAM partition symbol {name}")

    lowered = text.lower()
    for path in BANNED_RUNTIME_PATHS:
        if path in lowered:
            errors.append(f"map links forbidden vendor/reference runtime path: {path}")
    return errors


def check_map(path: Path) -> None:
    errors = validate_map(path.read_text())
    if errors:
        raise ValueError("unsafe T5AI image map:\n  " + "\n  ".join(errors))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject T5AI firmware maps outside AP slot 0"
    )
    parser.add_argument("map", type=Path)
    parser.add_argument("--stamp", type=Path)
    args = parser.parse_args()
    try:
        check_map(args.map)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.stamp:
        args.stamp.write_text("T5AI image layout validated\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
