#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

"""Unhygienix partition geometry and checked Pebble image packaging."""

import argparse
import hashlib
import sys
from pathlib import Path

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.t5ai_image import (
    LOGICAL_BLOCK_SIZE,
    PHYSICAL_BLOCK_SIZE,
    PADDING_BYTE,
    crc16 as crc16,
    encode_protected_blocks,
    verify_protected_blocks,
)

# Pinned BK7258 tuya_app partition geometry. The CSV sizes are physical flash
# occupancy; executable capacity is reduced by the 34:32 protection ratio.
T5AI_XIP_BASE = 0x02000000
PRIMARY_BOOTLOADER_PHYSICAL_SIZE = 0x00011000
PRIMARY_TUYABOOT_PHYSICAL_SIZE = 0x00011000
PRIMARY_CP_APP_PHYSICAL_SIZE = 0x00110000
PRIMARY_AP_APP_PHYSICAL_SIZE = 0x003B8000
PRIMARY_CP_APP_PHYSICAL_OFFSET = (
    PRIMARY_BOOTLOADER_PHYSICAL_SIZE + PRIMARY_TUYABOOT_PHYSICAL_SIZE
)
PRIMARY_AP_APP_PHYSICAL_OFFSET = (
    PRIMARY_CP_APP_PHYSICAL_OFFSET + PRIMARY_CP_APP_PHYSICAL_SIZE
)
PRIMARY_CP_APP_LOGICAL_OFFSET = 0x00020000
PRIMARY_CP_APP_LOGICAL_SIZE = 0x00100000
PRIMARY_AP_APP_LOGICAL_OFFSET = 0x00120000
PRIMARY_AP_APP_LOGICAL_SIZE = 0x00380000
PRIMARY_OTA_PHYSICAL_OFFSET = 0x004EA000
PRIMARY_OTA_PHYSICAL_SIZE = 0x002DF000
PRIMARY_CP_APP_XIP_BASE = T5AI_XIP_BASE + PRIMARY_CP_APP_LOGICAL_OFFSET
PRIMARY_AP_APP_XIP_BASE = T5AI_XIP_BASE + PRIMARY_AP_APP_LOGICAL_OFFSET

# Pebble intentionally links only the first part of the factory AP partition.
# This is not a second factory/OTA partition and must not be packaged as one.
PEBBLE_AP_SLOT_SIZE = 0x00190000
PEBBLE_AP_SLOT_PHYSICAL_SIZE = (
    PEBBLE_AP_SLOT_SIZE * PHYSICAL_BLOCK_SIZE // LOGICAL_BLOCK_SIZE
)

PEBBLE_AP_PHYSICAL_OFFSET = PRIMARY_AP_APP_PHYSICAL_OFFSET
PEBBLE_RESOURCES_LOGICAL_OFFSET = 0x00190000
PEBBLE_RESOURCES_SIZE = 0x00100000
PEBBLE_FILESYSTEM_PHYSICAL_OFFSET = 0x00500000
PEBBLE_FILESYSTEM_SIZE = 0x002C0000
PEBBLE_SHARED_PRF_PHYSICAL_OFFSET = PRIMARY_OTA_PHYSICAL_OFFSET
PEBBLE_SHARED_PRF_SIZE = 0x00010000
PFS_PAGE_SIZE = 0x1000
PFS_VERSION = 0x5001
PFS_ERASED_PAGE_FLAGS = 0xFE
PFS_INITIAL_ERASE_COUNT = 1


def encode_pebble_application(data: bytes) -> bytes:
    """Encode a bounded AP-only payload, never a QIO/UA/UG image."""
    if len(data) > PEBBLE_AP_SLOT_SIZE:
        raise ValueError(
            "Pebble application exceeds the Unhygienix AP slot: "
            f"{len(data)} > {PEBBLE_AP_SLOT_SIZE} bytes"
        )
    return encode_protected_blocks(data)



def build_empty_pfs() -> bytes:
    """Build the empty PFS state produced by ``pfs_format(true)``.

    PFS uses active-low page flags. Formatting an erased filesystem writes the
    version, erased-page flag, and initial erase count at the start of every
    4 KiB page; all other bytes remain erased.
    """
    if PEBBLE_FILESYSTEM_SIZE % PFS_PAGE_SIZE:
        raise ValueError("T5AI PFS size is not page aligned")

    filesystem = bytearray([PADDING_BYTE]) * PEBBLE_FILESYSTEM_SIZE
    erased_header = (
        PFS_VERSION.to_bytes(2, "little")
        + bytes([PADDING_BYTE, PFS_ERASED_PAGE_FLAGS])
        + bytes([PADDING_BYTE]) * 4
        + PFS_INITIAL_ERASE_COUNT.to_bytes(4, "little")
    )
    for offset in range(0, PEBBLE_FILESYSTEM_SIZE, PFS_PAGE_SIZE):
        filesystem[offset : offset + len(erased_header)] = erased_header
    return bytes(filesystem)



def build_pebble_flash_image(firmware: bytes, resources: bytes) -> bytes:
    """Build the protected AP image containing firmware and system resources."""
    if len(firmware) > PEBBLE_RESOURCES_LOGICAL_OFFSET:
        raise ValueError("Pebble firmware overlaps the T5AI system resource bank")
    if len(resources) > PEBBLE_RESOURCES_SIZE:
        raise ValueError("Pebble system resources exceed the T5AI resource bank")

    protected_logical_size = PEBBLE_RESOURCES_LOGICAL_OFFSET + PEBBLE_RESOURCES_SIZE
    protected_logical = bytearray([PADDING_BYTE]) * protected_logical_size
    protected_logical[: len(firmware)] = firmware
    protected_logical[PEBBLE_RESOURCES_LOGICAL_OFFSET :
                      PEBBLE_RESOURCES_LOGICAL_OFFSET + len(resources)] = resources
    return encode_protected_blocks(protected_logical)



def validate_pinned_ap_layout() -> None:
    if PRIMARY_CP_APP_PHYSICAL_OFFSET * LOGICAL_BLOCK_SIZE != (
        PRIMARY_CP_APP_LOGICAL_OFFSET * PHYSICAL_BLOCK_SIZE
    ):
        raise ValueError("pinned T5AI CP partition offset is not 34:32 aligned")
    if PRIMARY_CP_APP_PHYSICAL_SIZE * LOGICAL_BLOCK_SIZE != (
        PRIMARY_CP_APP_LOGICAL_SIZE * PHYSICAL_BLOCK_SIZE
    ):
        raise ValueError("pinned T5AI CP partition size is not 34:32 aligned")
    if PRIMARY_AP_APP_PHYSICAL_OFFSET * LOGICAL_BLOCK_SIZE != (
        PRIMARY_AP_APP_LOGICAL_OFFSET * PHYSICAL_BLOCK_SIZE
    ):
        raise ValueError("pinned T5AI AP partition offset is not 34:32 aligned")
    if PRIMARY_AP_APP_PHYSICAL_SIZE * LOGICAL_BLOCK_SIZE != (
        PRIMARY_AP_APP_LOGICAL_SIZE * PHYSICAL_BLOCK_SIZE
    ):
        raise ValueError("pinned T5AI AP partition size is not 34:32 aligned")
    if PEBBLE_AP_SLOT_SIZE > PRIMARY_AP_APP_LOGICAL_SIZE:
        raise ValueError("Pebble AP slot exceeds the factory AP partition")
    protected_end = PRIMARY_AP_APP_PHYSICAL_OFFSET + (
        (PEBBLE_RESOURCES_LOGICAL_OFFSET + PEBBLE_RESOURCES_SIZE)
        * PHYSICAL_BLOCK_SIZE // LOGICAL_BLOCK_SIZE
    )
    if protected_end > PRIMARY_AP_APP_PHYSICAL_OFFSET + PRIMARY_AP_APP_PHYSICAL_SIZE:
        raise ValueError("firmware and resources exceed the factory AP partition")
    if PEBBLE_FILESYSTEM_PHYSICAL_OFFSET < (
        PEBBLE_SHARED_PRF_PHYSICAL_OFFSET + PEBBLE_SHARED_PRF_SIZE
    ):
        raise ValueError("Pebble filesystem overlaps shared PRF storage")
    if PEBBLE_FILESYSTEM_PHYSICAL_OFFSET + PEBBLE_FILESYSTEM_SIZE > (
        PRIMARY_OTA_PHYSICAL_OFFSET + PRIMARY_OTA_PHYSICAL_SIZE
    ):
        raise ValueError("Pebble filesystem exceeds the factory OTA data partition")



def package_ap_qio(base_qio: bytes, protected_ap: bytes,
                   expected_base_sha256: str) -> bytes:
    """Overlay a checked Pebble AP payload onto trusted Tuya boot/CP bytes."""
    validate_pinned_ap_layout()
    actual_digest = hashlib.sha256(base_qio).hexdigest()
    if actual_digest.lower() != expected_base_sha256.lower():
        raise ValueError(
            f"base QIO SHA-256 mismatch: {actual_digest} != {expected_base_sha256}"
        )
    if len(base_qio) < PRIMARY_AP_APP_PHYSICAL_OFFSET:
        raise ValueError("base QIO does not contain the complete bootloader and CP prefix")
    if not protected_ap or len(protected_ap) > PEBBLE_AP_SLOT_PHYSICAL_SIZE:
        raise ValueError("protected AP payload is empty or exceeds the AP slot")

    logical = verify_protected_blocks(protected_ap)
    initial_sp = int.from_bytes(logical[0:4], "little")
    reset_vector = int.from_bytes(logical[4:8], "little")
    if not 0x28010000 <= initial_sp <= 0x28066000 or initial_sp & 7:
        raise ValueError(f"invalid T5AI initial stack pointer 0x{initial_sp:08x}")
    if not (reset_vector & 1) or not (
        PRIMARY_AP_APP_XIP_BASE
        <= reset_vector & ~1
        < PRIMARY_AP_APP_XIP_BASE + PEBBLE_AP_SLOT_SIZE
    ):
        raise ValueError(f"invalid T5AI reset vector 0x{reset_vector:08x}")

    ap_end = PRIMARY_AP_APP_PHYSICAL_OFFSET + len(protected_ap)
    partition_end = PRIMARY_AP_APP_PHYSICAL_OFFSET + PRIMARY_AP_APP_PHYSICAL_SIZE
    if ap_end > partition_end:
        raise ValueError("Pebble AP payload exceeds the factory AP partition")
    # Some vendor QIO artifacts end at the last used AP block rather than at the
    # partition boundary. Extend such an artifact with the checked Pebble payload;
    # when the base is longer, preserve every byte after the overlay unchanged.
    output = (base_qio[:PRIMARY_AP_APP_PHYSICAL_OFFSET] + protected_ap
              + base_qio[ap_end:])
    if output[:PRIMARY_AP_APP_PHYSICAL_OFFSET] != base_qio[:PRIMARY_AP_APP_PHYSICAL_OFFSET]:
        raise AssertionError("QIO packaging changed the bootloader/CP prefix")
    return output



def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="raw logical application binary")
    parser.add_argument("output", type=Path, help="34:32 protected application binary")
    args = parser.parse_args()

    validate_pinned_ap_layout()
    encoded = encode_pebble_application(args.input.read_bytes())
    args.output.write_bytes(encoded)
    verify_protected_blocks(encoded)
    return 0



if __name__ == "__main__":
    raise SystemExit(main())
