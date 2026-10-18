#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

"""Encode a logical BK7258/T5AI image into protected flash blocks.

The pinned T5AI SDK stores 32 data bytes followed by a big-endian Beken
CRC-16.  A final short block is padded with erased-flash bytes before its CRC
is calculated.  This is a protected payload only; it is not a complete Tuya
bootloader/update image.
"""

LOGICAL_BLOCK_SIZE = 32
PHYSICAL_BLOCK_SIZE = 34
CRC_POLYNOMIAL = 0x8005
CRC_INITIAL = 0xFFFFFFFF
PADDING_BYTE = 0xFF

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
