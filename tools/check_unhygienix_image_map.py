#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import argparse
import re
from itertools import pairwise
from pathlib import Path

SLOT_BEGIN = 0x02120000
SLOT_END = 0x022C0000
NOR_BEGIN = 0x02000000
NOR_END = 0x02800000

EXPECTED_MEMORY = {
    "FLASH": (0x02120000, 0x001A0000),
    "RAM": (0x28010000, 0x0001F000),
    "T5AI_DISPLAY_RAM": (0x60000000, 0x00020000),
}
PARTITION_SYMBOLS = {
    "__WORKER_RAM__": 0x2802F000,
    "__WORKER_RAM_size__": 0x3000,
    "__APP_RAM__": 0x28032000,
    "__APP_RAM_size__": 0x34000,
}
LEGACY_STACK_SYMBOLS = ("__kernel_main_stack_start__", "__kernel_bg_stack_start__")
REQUIRED_SYMBOLS = (
    "__isr_stack_start__",
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


# Pinned backing capacities, not metadata layouts. Changes require placement review.
NIMBLE_POOLS = {
    "os_msys_1_data": ("msys", 0x27EC),
    "pool_acl_buf": ("acl", 0x2580),
}


def validate_nimble_pools(text: str) -> list[str]:
    errors = []
    symbols = {
        match[2]: int(match[1], 16)
        for line in text.splitlines()
        if (match := SYMBOL_RE.match(line))
    }
    cache = re.findall(
        r"^\.t5ai_board_cache[ \t]*(?:\n[ \t]+)?"
        r"0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)",
        text,
        re.MULTILINE,
    )
    if len(cache) != 1:
        return ["NimBLE pools require exactly one .t5ai_board_cache output section"]
    begin, size = (int(value, 16) for value in cache[0])
    end = begin + size
    psram, length = EXPECTED_MEMORY["T5AI_DISPLAY_RAM"]
    if not (
        psram
        <= symbols.get("__t5ai_framebuffer_end__", psram + length + 1)
        <= begin
        < end
        <= psram + length
    ):
        errors.append("NimBLE board cache overlaps framebuffer or exceeds AP PSRAM")
    if (
        symbols.get("__t5ai_board_cache_start__"),
        symbols.get("__t5ai_board_cache_end__"),
    ) != (begin, end):
        errors.append("NimBLE board cache symbols disagree with output section")
    previous_end = begin
    for name, (label, expected_size) in NIMBLE_POOLS.items():
        # Require an allocated input contribution, not a KEEP selector or symbol alone.
        entries = re.findall(
            rf"^[ \t]+\.bss\.{name}[ \t]*(?:\n[ \t]+)?"
            r"0x([0-9a-fA-F]+)[ \t]+0x([0-9a-fA-F]+)[ \t]+(\S+\.o(?:bj)?(?:\))?)[ \t]*$",
            text,
            re.MULTILINE,
        )
        if len(entries) != 1:
            errors.append(f"NimBLE pool {name}: expected one allocated input object")
            continue
        address, actual_size = (int(value, 16) for value in entries[0][:2])
        pool_end = address + actual_size
        if actual_size != expected_size:
            errors.append(
                f"NimBLE pool {name}: size 0x{actual_size:x}, expected 0x{expected_size:x}"
            )
        if (
            address != previous_end
            or address % 4
            or not (begin <= address < pool_end <= end)
        ):
            errors.append(f"NimBLE pool {name}: outside ordered AP PSRAM backing range")
        if (
            symbols.get(f"__t5ai_{label}_pool_start__"),
            symbols.get(f"__t5ai_{label}_pool_end__"),
        ) != (address, pool_end):
            errors.append(
                f"NimBLE pool {name}: boundary symbols disagree with input object"
            )
        previous_end = pool_end
    return errors


def validate_map(text: str) -> list[str]:
    errors = []
    memory_regions = {}
    symbols = {}
    saw_flash_section = False
    vector_section = None
    # GNU ld places long output-section names on a line of their own.
    pfs_flags = re.search(
        r"^\.t5ai_pfs_flags\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)",
        text,
        re.MULTILINE,
    )
    pfs_flags_section = (
        (int(pfs_flags.group(1), 16), int(pfs_flags.group(2), 16))
        if pfs_flags
        else None
    )

    for line in text.splitlines():
        memory = MEMORY_RE.match(line)
        if memory:
            memory_regions[memory.group(1)] = (
                int(memory.group(2), 16),
                int(memory.group(3), 16),
            )

        symbol = SYMBOL_RE.match(line)
        if symbol and symbol.group(2) in (
            *REQUIRED_SYMBOLS,
            *LEGACY_STACK_SYMBOLS,
            *PARTITION_SYMBOLS,
        ):
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
            # BSS/stack map entries have synthetic LMAs but occupy no flash bytes.
            no_load = name in (
                ".kernel_ro_bss",
                ".kernel_bss",
                ".stack",
                ".kernel_heap",
            )
            load_size = 0 if no_load else size
            if load_address < SLOT_BEGIN or load_address + load_size > SLOT_END:
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
        stack_heap = [symbols[name] for name in REQUIRED_SYMBOLS[:3]]
        if stack_heap != sorted(stack_heap) or not all(
            kernel_begin <= address <= kernel_end for address in stack_heap
        ):
            errors.append("stack/heap symbols are unordered or outside RAM")
        display_begin, display_length = EXPECTED_MEMORY["T5AI_DISPLAY_RAM"]
        framebuffer = [symbols[name] for name in REQUIRED_SYMBOLS[3:]]
        if framebuffer != sorted(framebuffer) or not (
            display_begin
            <= framebuffer[0]
            <= framebuffer[1]
            <= display_begin + display_length
        ):
            errors.append("framebuffer symbols are outside T5AI_DISPLAY_RAM")

    # Upstream keeps thread stacks as static BSS buffers instead of linker reservations.
    stacks = {}
    for match in re.finditer(
        r"^\s*\.bss\.(s_main_stack|s_sys_stack|s_sys_high_stack)(?:\.lto_priv\.\d+)?"
        r"\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)",
        text,
        re.MULTILINE,
    ):
        stacks[match[1]] = (int(match[2], 16), int(match[3], 16))
    legacy = all(name in symbols for name in LEGACY_STACK_SYMBOLS)
    if legacy:
        layout = [
            symbols.get(name, 0)
            for name in (
                "__isr_stack_start__",
                *LEGACY_STACK_SYMBOLS,
                "_heap_start",
                "_heap_end",
            )
        ]
        ram_begin, ram_size = EXPECTED_MEMORY["RAM"]
        if layout != sorted(layout) or not all(
            ram_begin <= addr <= ram_begin + ram_size for addr in layout
        ):
            errors.append("legacy stack/heap symbols are unordered or outside RAM")
    else:
        for name in ("s_main_stack", "s_sys_stack", "s_sys_high_stack"):
            if name not in stacks:
                errors.append(f"map is missing kernel thread stack {name}")
                continue
            begin, size = stacks[name]
            ram_begin, ram_size = EXPECTED_MEMORY["RAM"]
            if (
                size < 128
                or begin < ram_begin
                or begin + size
                > min(
                    ram_begin + ram_size, symbols.get("__isr_stack_start__", ram_begin)
                )
            ):
                errors.append(f"kernel thread stack {name} is outside RAM or too small")
        ranges = sorted(stacks.values())
        if any(a + size > b for (a, size), (b, _) in pairwise(ranges)):
            errors.append("kernel thread stacks overlap")

    for name, expected in PARTITION_SYMBOLS.items():
        if symbols.get(name) != expected:
            errors.append(f"incorrect or missing RAM partition symbol {name}")

    errors.extend(validate_nimble_pools(text))

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
