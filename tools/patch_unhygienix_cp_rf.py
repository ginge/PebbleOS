#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Build the Unhygienix RF initialization and interval-transition fixes in pinned CP.

Refuses unknown images and verifies the original complete reset function.
Compile the source hook into unused CP flash and redirect only the reset's
Bluetooth-init call, then add the guarded interval-transition correction.
Preserve every existing code/data address and AP ABI.
"""
import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.t5ai_image import encode_protected_blocks
from tools.unhygienix_image import PRIMARY_CP_APP_LOGICAL_SIZE

STOCK_SHA256 = "0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856"
CP_XIP = 0x02020000
RESET_OFFSET = 0x542B8
STOCK_RESET = bytes.fromhex("08b505f09ffa642000f0b6f905f046fa002008bd")
INIT_CALL_OFFSET = 0x542C4
INIT_XIP = 0x02079754
RF_CONFIG_XIP = 0x020CE93C
PRINTF_XIP = 0x0206AFBC
RF_CONFIG_RAM = 0x2806813A
SOURCE = ROOT / "boards/unhygienix/tuya/cp/ble_rf_init.c"


def thumb_bl(source: int, target: int) -> bytes:
    delta = (target & ~1) - (source + 4)
    if source & 1 or delta & 1 or not -(1 << 24) <= delta < (1 << 24):
        raise ValueError("BL target is unaligned or outside Thumb branch range")
    sign = (delta >> 24) & 1
    j1 = (1 ^ ((delta >> 23) & 1)) ^ sign
    j2 = (1 ^ ((delta >> 22) & 1)) ^ sign
    return struct.pack("<HH", 0xF000 | sign << 10 | ((delta >> 12) & 0x3FF),
                       0xD000 | j1 << 13 | j2 << 11 | ((delta >> 1) & 0x7FF))


def verify_stock(image: bytes):
    if hashlib.sha256(image).hexdigest() != STOCK_SHA256:
        raise ValueError("CP image does not match the pinned stock SHA256")
    if image[RESET_OFFSET:RESET_OFFSET + len(STOCK_RESET)] != STOCK_RESET:
        raise ValueError("CP reset function does not match the verified ELF")


def install_hook(image: bytes, hook: bytes) -> bytes:
    verify_stock(image)
    if not hook or len(hook) > 512:
        raise ValueError("Unexpected CP hook size")
    offset = (len(image) + 15) & ~15
    if offset + len(hook) > PRIMARY_CP_APP_LOGICAL_SIZE:
        raise ValueError("CP hook exceeds the existing CP partition")
    result = bytearray(image)
    result[INIT_CALL_OFFSET:INIT_CALL_OFFSET + 4] = thumb_bl(CP_XIP + INIT_CALL_OFFSET, CP_XIP + offset)
    result.extend(b"\xff" * (offset - len(result)))
    result.extend(hook)
    return bytes(result)


def compile_hook(address: int, compiler: str, objcopy_tool: str | None = None) -> bytes:
    gcc = shutil.which(compiler)
    if gcc is None:
        raise ValueError("ARM compiler unavailable")
    objcopy = objcopy_tool or Path(gcc).with_name("arm-none-eabi-objcopy")
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        linker = folder / "hook.ld"
        linker.write_text(f'''ENTRY(pebble_ble_init_bt_pll)
SECTIONS {{ . = 0x{address:x}; .hook : {{ *(.text*) *(.rodata*) }}
/DISCARD/ : {{ *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.eh_frame*) }} }}
bk_bluetooth_init = 0x{INIT_XIP | 1:x};
rwnx_cal_set_rfconfig = 0x{RF_CONFIG_XIP | 1:x};
bk_printf = 0x{PRINTF_XIP | 1:x};
rwnx_rfconfig = 0x{RF_CONFIG_RAM:x};
''')
        elf, raw = folder / "hook.elf", folder / "hook.bin"
        subprocess.run([gcc, "-Os", "-mthumb", "-mcpu=cortex-m33", "-mfloat-abi=soft", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-nostdlib", "-Wl,--build-id=none", "-T", str(linker), str(SOURCE), "-o", str(elf)], check=True)
        subprocess.run([str(objcopy), "-O", "binary", "-j", ".hook", str(elf), str(raw)], check=True)
        return raw.read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cc", default="arm-none-eabi-gcc")
    args = parser.parse_args()
    if args.stock.resolve() == args.out.resolve():
        parser.error("output must not overwrite the stock CP image")
    stock = args.stock.read_bytes()
    verify_stock(stock)
    address = CP_XIP + ((len(stock) + 15) & ~15)
    hook = compile_hook(address, args.cc)
    image = install_hook(stock, hook)
    from tools.patch_unhygienix_cp_latency import add_fix

    image = add_fix(image, args.cc)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image)
    args.out.with_suffix(".crc.bin").write_bytes(encode_protected_blocks(image))
    print(f"BLE PLL CP: {len(image)} bytes sha256={hashlib.sha256(image).hexdigest()}")
    print(f"Source hook: {len(hook)} bytes at 0x{address:x}; RF init and interval-transition correction installed")


if __name__ == "__main__":
    main()
