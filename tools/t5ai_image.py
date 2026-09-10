#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Encode a logical BK7258/T5AI image into protected flash blocks.

The pinned T5AI SDK stores 32 data bytes followed by a big-endian Beken
CRC-16.  A final short block is padded with erased-flash bytes before its CRC
is calculated.  This is a protected payload only; it is not a complete Tuya
bootloader/update image.
"""

import argparse
import hashlib
from pathlib import Path

LOGICAL_BLOCK_SIZE = 32
PHYSICAL_BLOCK_SIZE = 34
CRC_POLYNOMIAL = 0x8005
CRC_INITIAL = 0xFFFFFFFF
PADDING_BYTE = 0xFF

# Pinned BK7258 tuya_app partition geometry. The CSV sizes are physical flash
# occupancy; executable capacity is reduced by the 34:32 protection ratio.
T5AI_XIP_BASE = 0x02000000
PRIMARY_BOOTLOADER_PHYSICAL_SIZE = 0x00011000
PRIMARY_TUYABOOT_PHYSICAL_SIZE = 0x00011000
PRIMARY_CP_APP_PHYSICAL_SIZE = 0x00110000
PRIMARY_AP_APP_PHYSICAL_SIZE = 0x003B8000
PRIMARY_AP_APP_PHYSICAL_OFFSET = (
    PRIMARY_BOOTLOADER_PHYSICAL_SIZE
    + PRIMARY_TUYABOOT_PHYSICAL_SIZE
    + PRIMARY_CP_APP_PHYSICAL_SIZE
)
PRIMARY_AP_APP_LOGICAL_OFFSET = 0x00120000
PRIMARY_AP_APP_LOGICAL_SIZE = 0x00380000
PRIMARY_AP_APP_XIP_BASE = T5AI_XIP_BASE + PRIMARY_AP_APP_LOGICAL_OFFSET

# Pebble intentionally links only the first part of the factory AP partition.
# This is not a second factory/OTA partition and must not be packaged as one.
PEBBLE_EXPERIMENTAL_SLOT_SIZE = 0x001F0000
PEBBLE_EXPERIMENTAL_SLOT_PHYSICAL_SIZE = (
    PEBBLE_EXPERIMENTAL_SLOT_SIZE * PHYSICAL_BLOCK_SIZE // LOGICAL_BLOCK_SIZE
)


def crc16(block: bytes) -> int:
    crc = CRC_INITIAL
    for byte in block:
        crc ^= byte << 8
        for _ in range(8):
            crc = (crc << 1) ^ CRC_POLYNOMIAL if crc & 0x8000 else crc << 1
    return crc & 0xFFFF


def encode_protected_blocks(data: bytes) -> bytes:
    if not data:
        return b""

    output = bytearray()
    for offset in range(0, len(data), LOGICAL_BLOCK_SIZE):
        block = data[offset : offset + LOGICAL_BLOCK_SIZE]
        block += bytes([PADDING_BYTE]) * (LOGICAL_BLOCK_SIZE - len(block))
        output.extend(block)
        output.extend(crc16(block).to_bytes(2, "big"))
    return bytes(output)


def encode_pebble_application(data: bytes) -> bytes:
    """Encode a bounded AP-only payload, never a QIO/UA/UG image."""
    if len(data) > PEBBLE_EXPERIMENTAL_SLOT_SIZE:
        raise ValueError(
            "Pebble application exceeds the experimental T5AI slot: "
            f"{len(data)} > {PEBBLE_EXPERIMENTAL_SLOT_SIZE} bytes"
        )
    return encode_protected_blocks(data)


def validate_pinned_ap_layout() -> None:
    if PRIMARY_AP_APP_PHYSICAL_OFFSET * LOGICAL_BLOCK_SIZE != (
        PRIMARY_AP_APP_LOGICAL_OFFSET * PHYSICAL_BLOCK_SIZE
    ):
        raise ValueError("pinned T5AI AP partition offset is not 34:32 aligned")
    if PRIMARY_AP_APP_PHYSICAL_SIZE * LOGICAL_BLOCK_SIZE != (
        PRIMARY_AP_APP_LOGICAL_SIZE * PHYSICAL_BLOCK_SIZE
    ):
        raise ValueError("pinned T5AI AP partition size is not 34:32 aligned")
    if PEBBLE_EXPERIMENTAL_SLOT_SIZE > PRIMARY_AP_APP_LOGICAL_SIZE:
        raise ValueError("experimental Pebble slot exceeds the factory AP partition")


def verify_protected_blocks(data: bytes) -> bytes:
    if len(data) % PHYSICAL_BLOCK_SIZE:
        raise ValueError("protected image length is not a multiple of 34 bytes")

    output = bytearray()
    for offset in range(0, len(data), PHYSICAL_BLOCK_SIZE):
        block = data[offset : offset + LOGICAL_BLOCK_SIZE]
        stored_crc = int.from_bytes(
            data[offset + LOGICAL_BLOCK_SIZE : offset + PHYSICAL_BLOCK_SIZE],
            "big",
        )
        expected_crc = crc16(block)
        if stored_crc != expected_crc:
            raise ValueError(
                f"CRC mismatch in protected block {offset // PHYSICAL_BLOCK_SIZE}: "
                f"stored 0x{stored_crc:04x}, expected 0x{expected_crc:04x}"
            )
        output.extend(block)
    return bytes(output)


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
    if not protected_ap or len(protected_ap) > PEBBLE_EXPERIMENTAL_SLOT_PHYSICAL_SIZE:
        raise ValueError("protected AP payload is empty or exceeds the experimental slot")

    logical = verify_protected_blocks(protected_ap)
    initial_sp = int.from_bytes(logical[0:4], "little")
    reset_vector = int.from_bytes(logical[4:8], "little")
    if not 0x28010000 <= initial_sp <= 0x28066000 or initial_sp & 7:
        raise ValueError(f"invalid T5AI initial stack pointer 0x{initial_sp:08x}")
    if not (reset_vector & 1) or not (
        PRIMARY_AP_APP_XIP_BASE
        <= reset_vector & ~1
        < PRIMARY_AP_APP_XIP_BASE + PEBBLE_EXPERIMENTAL_SLOT_SIZE
    ):
        raise ValueError(f"invalid T5AI reset vector 0x{reset_vector:08x}")

    ap_end = PRIMARY_AP_APP_PHYSICAL_OFFSET + len(protected_ap)
    if ap_end > len(base_qio):
        raise ValueError("Pebble AP payload overlaps the required Tuya AP trailer")
    # The factory AP file ends with boot metadata consumed by the pinned CP.
    # Preserve it byte-for-byte and replace only the protected executable prefix.
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
