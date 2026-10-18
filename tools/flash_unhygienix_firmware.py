#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

"""Validate and flash only the CRC-protected T5AI PebbleOS AP firmware."""

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.unhygienix_image import (
    PADDING_BYTE,
    PEBBLE_AP_PHYSICAL_OFFSET,
    PEBBLE_AP_SLOT_SIZE,
    PEBBLE_AP_SLOT_PHYSICAL_SIZE,
    PRIMARY_AP_APP_XIP_BASE,
    verify_protected_blocks,
)


def validate_firmware(raw_path: Path, protected_path: Path) -> tuple[bytes, bytes]:
    raw = raw_path.read_bytes()
    protected = protected_path.read_bytes()
    if not raw or not protected:
        raise ValueError("firmware artifacts must not be empty")
    if len(protected) > PEBBLE_AP_SLOT_PHYSICAL_SIZE:
        raise ValueError("protected firmware exceeds the Pebble AP slot")

    decoded = verify_protected_blocks(protected)
    if decoded[:len(raw)] != raw:
        raise ValueError("protected firmware does not decode to pebbleos.bin")
    if any(byte != PADDING_BYTE for byte in decoded[len(raw):]):
        raise ValueError("protected firmware has non-erased bytes after the logical image")

    initial_sp = int.from_bytes(raw[0:4], "little")
    reset_vector = int.from_bytes(raw[4:8], "little")
    if not 0x28010000 <= initial_sp <= 0x28066000 or initial_sp & 7:
        raise ValueError(f"invalid initial stack pointer 0x{initial_sp:08x}")
    if not (reset_vector & 1) or not (
        PRIMARY_AP_APP_XIP_BASE
        <= reset_vector & ~1
        < PRIMARY_AP_APP_XIP_BASE + PEBBLE_AP_SLOT_SIZE
    ):
        raise ValueError(f"invalid reset vector 0x{reset_vector:08x}")
    return raw, protected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", type=Path, default=Path("build/pebbleos.bin"))
    parser.add_argument(
        "--protected", type=Path, default=Path("build/pebbleos.t5ai-crc.bin")
    )
    parser.add_argument("--tyutool", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument(
        "--flash", action="store_true",
        help="perform the write; without this flag validation is read-only",
    )
    args = parser.parse_args()

    raw, protected = validate_firmware(args.raw, args.protected)
    start = PEBBLE_AP_PHYSICAL_OFFSET
    end = start + len(protected)
    print(f"raw:       {len(raw):7d} bytes  sha256={hashlib.sha256(raw).hexdigest()}")
    print(
        f"protected: {len(protected):7d} bytes  "
        f"sha256={hashlib.sha256(protected).hexdigest()}"
    )
    print(f"AP-only physical range: 0x{start:x}..0x{end:x} (end exclusive)")

    if not args.flash:
        print("validated; add --flash to write this exact AP-only range")
        return 0

    command = [
        str(args.tyutool), "write", "-d", "t5ai", "-p", str(args.port),
        "-s", hex(start), "--end", hex(end), "-f", str(args.protected), "--plain",
    ]
    subprocess.run(command, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
