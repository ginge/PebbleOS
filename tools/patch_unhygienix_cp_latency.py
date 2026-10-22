#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Package the board-local CP interval-transition fix without AP instrumentation."""

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.patch_unhygienix_cp_rf import CP_XIP, STOCK_SHA256, thumb_bl
from tools.t5ai_image import encode_protected_blocks
from tools.unhygienix_image import PRIMARY_CP_APP_LOGICAL_SIZE

SOURCE = ROOT / "boards/unhygienix/tuya/cp/ble_latency_fix.c"
INSTALLED_SHA256 = "63e6a7f37acfa2cfd9ad827e0fee61199c54c611538f6e4f12e60ef16c6b7e04"
DATA_TX_XIP = 0x020B8C84
DATA_TX_BYTES = 0xCC
DATA_TX_SHA256 = "34367efddbbdabf8d7b796921e49ce0873d0a41d312cfa5e08ffa27e71f3cc93"
SCHED_XIP = 0x020B7B68
SCHED_BYTES = 0x2B4
SCHED_SHA256 = "374eb6d5f1287c5e232fc505132f7a846da97db49b26d6fd43d3597f8251f64b"
INTERVAL_CALLS = (0x020B7C56, 0x020B7DE0)
MAX_LAT_XIP = 0x020B7760
ENV_XIP = 0x2806E628


def verify_controller(image):
    for address, size, expected in (
        (DATA_TX_XIP, DATA_TX_BYTES, DATA_TX_SHA256),
        (SCHED_XIP, SCHED_BYTES, SCHED_SHA256),
    ):
        offset = address - CP_XIP
        if hashlib.sha256(image[offset : offset + size]).hexdigest() != expected:
            raise ValueError("controller function does not match reviewed bytes")
    for call in INTERVAL_CALLS:
        offset = call - CP_XIP
        if image[offset : offset + 4] != thumb_bl(call, MAX_LAT_XIP):
            raise ValueError("interval application call does not match reviewed ELF")


def verify_base(image):
    if hashlib.sha256(image).hexdigest() not in (STOCK_SHA256, INSTALLED_SHA256):
        raise ValueError("CP base does not match a reviewed SHA256")
    verify_controller(image)


def install_fix(image, hook, address):
    # Callers verify the complete input before composing RF and latency hooks.
    verify_controller(image)
    offset = (len(image) + 15) & ~15
    if (
        address != CP_XIP + offset
        or not hook
        or offset + len(hook) > PRIMARY_CP_APP_LOGICAL_SIZE
    ):
        raise ValueError("latency hook address/size exceeds the CP partition")
    result = bytearray(image)
    for call in INTERVAL_CALLS:
        offset_call = call - CP_XIP
        result[offset_call : offset_call + 4] = thumb_bl(call, address)
    result.extend(b"\xff" * (offset - len(result)))
    result.extend(hook)
    return bytes(result)


def compile_fix(address, compiler, objcopy=None):
    compiler = Path(compiler)
    tools = compiler.parent
    objcopy = objcopy or tools / "arm-none-eabi-objcopy"
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        linker = tmp / "fix.ld"
        linker.write_text(f"""ENTRY(pebble_latency_interval_applied)
SECTIONS {{ . = 0x{address:x}; .hook : {{ *(.text*) *(.rodata*) }}
/DISCARD/ : {{ *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.eh_frame*) }} }}
lld_con_max_lat_calc = 0x{MAX_LAT_XIP | 1:x};
pebble_lld_con_env = 0x{ENV_XIP:x};
""")
        elf, raw = tmp / "fix.elf", tmp / "fix.bin"
        subprocess.run(
            [
                str(compiler),
                "-Os",
                "-std=c11",
                "-mthumb",
                "-mcpu=cortex-m33",
                "-mfloat-abi=soft",
                "-ffreestanding",
                "-fno-builtin",
                "-fno-unwind-tables",
                "-fno-asynchronous-unwind-tables",
                "-nostdlib",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wl,--build-id=none",
                "-T",
                str(linker),
                str(SOURCE),
                "-o",
                str(elf),
            ],
            check=True,
        )
        headers = subprocess.check_output(
            [str(tools / "arm-none-eabi-objdump"), "-h", str(elf)], text=True
        )
        for row in headers.splitlines():
            fields = row.split()
            if (
                len(fields) > 2
                and fields[1] in {".bss", ".data"}
                and int(fields[2], 16)
            ):
                raise ValueError("CP fix requires unreserved data/BSS")
        symbols = subprocess.check_output(
            [str(tools / "arm-none-eabi-nm"), str(elf)], text=True
        )
        if not any(
            row.split() == [f"{address:08x}", "T", "pebble_latency_interval_applied"]
            for row in symbols.splitlines()
        ):
            raise ValueError("CP fix entry is not at the appended hook address")
        subprocess.run(
            [str(objcopy), "-O", "binary", "-j", ".hook", str(elf), str(raw)],
            check=True,
        )
        return raw.read_bytes()


def add_fix(image, compiler, objcopy=None):
    address = CP_XIP + ((len(image) + 15) & ~15)
    return install_fix(image, compile_fix(address, compiler, objcopy), address)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    if args.base.resolve().is_relative_to(args.output.resolve()):
        parser.error("output must not contain the input base")
    base = args.base.read_bytes()
    verify_base(base)
    image = add_fix(base, args.cc)
    args.output.mkdir(parents=True, exist_ok=True)
    files = {
        "cp-latency.bin": image,
        "cp-latency.crc.bin": encode_protected_blocks(image),
    }
    for name, data in files.items():
        (args.output / name).write_bytes(data)
    manifest = {
        "schema": 1,
        "board": "unhygienix",
        "fix": "invalidate old-interval latency rewind",
        "base_sha256": hashlib.sha256(base).hexdigest(),
        "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        "calls_changed": [hex(call) for call in INTERVAL_CALLS],
        "ap_dependency": None,
        "tracing": False,
        "outputs": {
            name: hashlib.sha256(data).hexdigest() for name, data in files.items()
        },
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"CP fix: {len(image)} bytes, no AP buffer or trace hooks")


if __name__ == "__main__":
    main()
