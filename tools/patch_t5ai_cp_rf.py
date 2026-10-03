#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Select the BLE BT PLL in the exact pinned vendor CP image.

The board override is boards/getafix/tuya/cp/ble_rf_mode.c. Install its leaf
function at the existing get_rf_mode address without relinking the vendor CP;
all other code, data and addresses remain stock. A whole-image digest and the
original function bytes guard this replacement. Another CP release requires
matching ELF analysis and a newly reviewed integration.
"""
import argparse
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools.t5ai_image import encode_protected_blocks

STOCK_SHA256 = "0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856"
# Matching app.elf: .text.get_rf_mode VMA/LMA 0x02079998;
# CP XIP starts at 0x02020000. Compile ble_rf_mode.c with -Os -mthumb
# -mcpu=cortex-m33: movs r0, #0; bx lr (AAPCS, no stack or callee-save use).
RF_MODE_OFFSET = 0x59998
STOCK_RF_MODE = bytes.fromhex("064b1b6803f00f00012805d003f0f003102b14bf00200220704700bf3a810628")
BLE_RF_MODE = bytes.fromhex("00207047")


def select_ble_pll(image: bytes) -> bytes:
    if hashlib.sha256(image).hexdigest() != STOCK_SHA256:
        raise ValueError("CP image does not match the pinned stock SHA256")
    if image[RF_MODE_OFFSET:RF_MODE_OFFSET + len(STOCK_RF_MODE)] != STOCK_RF_MODE:
        raise ValueError("CP RF callback does not match the verified ELF")
    result = bytearray(image)
    result[RF_MODE_OFFSET:RF_MODE_OFFSET + len(BLE_RF_MODE)] = BLE_RF_MODE
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="New raw CP image; .crc.bin is also written")
    args = parser.parse_args()
    if args.stock.resolve() == args.out.resolve():
        parser.error("output must not overwrite the stock CP image")
    image = select_ble_pll(args.stock.read_bytes())
    protected = encode_protected_blocks(image)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image)
    args.out.with_suffix(".crc.bin").write_bytes(protected)
    print(f"BLE PLL CP: {len(image)} bytes sha256={hashlib.sha256(image).hexdigest()}")
    print("Configuration candidate; validate boot selection and radio stability on hardware.")


if __name__ == "__main__":
    main()
