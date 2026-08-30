#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import argparse
import re
from pathlib import Path

SLOT_BEGIN = 0x02120000
SLOT_END = 0x02310000
NOR_BEGIN = 0x02000000
NOR_END = 0x02800000

MEMORY_RE = re.compile(
    r"^FLASH\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+.*$"
)
SECTION_RE = re.compile(
    r"^(\.[^\s]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)"
    r"(?:\s+load address 0x([0-9a-fA-F]+))?"
)


def validate_map(text: str) -> list[str]:
    errors = []
    flash_region = None
    saw_flash_section = False

    for line in text.splitlines():
        memory = MEMORY_RE.match(line)
        if memory:
            origin = int(memory.group(1), 16)
            length = int(memory.group(2), 16)
            flash_region = (origin, origin + length)

        section = SECTION_RE.match(line)
        if not section:
            continue
        name = section.group(1)
        address = int(section.group(2), 16)
        size = int(section.group(3), 16)
        load_address = int(section.group(4), 16) if section.group(4) else None

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

    if flash_region is None:
        errors.append("map has no FLASH memory region")
    elif flash_region != (SLOT_BEGIN, SLOT_END):
        errors.append(
            f"FLASH region is 0x{flash_region[0]:08x}..0x{flash_region[1]:08x}; "
            f"expected 0x{SLOT_BEGIN:08x}..0x{SLOT_END:08x}"
        )
    if not saw_flash_section:
        errors.append("map has no output section in the T5AI firmware slot")
    return errors


def check_map(path: Path) -> None:
    errors = validate_map(path.read_text())
    if errors:
        raise ValueError("unsafe T5AI image map:\n  " + "\n  ".join(errors))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject T5AI firmware maps outside experimental slot 0"
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
