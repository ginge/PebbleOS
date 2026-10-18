# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Hash-gated CP flash diagnostics and ITCM deadline guard for the pinned ABI."""

import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from tools.patch_unhygienix_cp_rf import (
    CP_XIP,
    ROOT,
    install_hook,
    thumb_bl,
    verify_stock,
)
from tools.unhygienix_image import PRIMARY_CP_APP_LOGICAL_SIZE

ELF_SHA256 = "268c159b32ab9dc4b63b0b284371856972a6b5722d7f35d33613a16d04accb50"
SOURCES = [
    ROOT / "boards/unhygienix/tuya/cp" / name
    for name in ("flash_guard.c", "flash_guard.S", "flash_volatile.c", "flash_spi.c")
]
ITCM_START, ITCM_END, ITCM_LIMIT = 0x20, 0x3EA0, 0x4000
IRAM_START, IRAM_END, IRAM_LOAD, IRAM_COPY = (
    0x08066000,
    0x08067308,
    0x0210A2AC,
    0x02102E04,
)
IRAM_LIMIT, STATE = 0x08067400, 0x28067360
ITCM_LOAD, COPY_ENTRY = 0x02102E4C, 0x02102E1C
# address, base register, result register, shift-to-zero vs full-register result
WAITS = [
    (0x207EC36, 3, 2, True),
    (0x207EC72, 3, 2, False),
    (0x207EC8A, 3, 2, False),
    (0x207ECAA, 3, 2, False),
    (0x207ECE0, 3, 2, True),
    (0x207ECE8, 3, 2, True),
    (0x207ECFE, 3, 2, False),
    (0x207ED16, 3, 2, False),
    (0x207EDD4, 3, 2, True),
    (0x207EDFC, 3, 0, True),
    (0x207EEF0, 2, 3, False),
    (0x207EF0E, 2, 3, False),
    (0x207F00A, 2, 5, True),
    (0x207F01E, 2, 5, False),
    (0x207F03E, 2, 5, False),
    (0x207F30E, 4, 3, False),
    (0x207F32C, 4, 3, True),
]
# Enter the launch shim BEFORE making flash busy; it calls the RAM-only
# launch/wait routine. These are the complete original LDR/ORR/STR sequences.
LAUNCHES = [
    (0x207EC5E, 3, 2),
    (0x207EC82, 3, 2),
    (0x207ECA2, 3, 2),
    (0x207ECD8, 3, 2),
    (0x207ED0E, 3, 2),
    (0x207EDF4, 3, 2),
    (0x207EF06, 2, 3),
    (0x207F036, 2, 5),
    (0x207F324, 4, 3),
]
SYMBOLS = {
    "stock_flash_write": 0x207F380,
    "stock_bypass_write": 0x08066594,
    "stock_protect": 0x207ED34,
    "stock_status": 0x207EC24,
    "stock_mode": 0x207EDA4,
    "stock_prepare": 0x2082F74,
    "stock_finish": 0x2082F8A,
    "stock_cpu_off": 0x208183C,
    "stock_irq_disable": 0x2074A30,
    "stock_irq_enable": 0x207538C,
    "bk_printf": 0x206AFBC,
}
# Only these verified calls change; original controller/protection code stays intact.
CALLS = [
    (0x207FAE4, "stock_flash_write", "pebble_flash_write", False),
    (0x207EEC2, "stock_protect", "pebble_flash_protect", False),
    (0x207EED4, "stock_protect", "pebble_flash_protect", False),
    (0x207EF92, "stock_protect", "pebble_flash_protect", False),
    (0x207EFA0, "stock_protect", "pebble_flash_protect", False),
    (0x207ED8A, "stock_status", "pebble_flash_status", True),
    (0x207EE3C, "stock_status", "pebble_flash_status", False),
    (0x207EE9C, "stock_mode", "pebble_flash_mode", False),
    (0x207EEC8, "stock_mode", "pebble_flash_mode", False),
    (0x207EF6A, "stock_mode", "pebble_flash_mode", False),
    (0x207EF9A, "stock_mode", "pebble_flash_mode", False),
    (0x207EC02, "stock_prepare", "pebble_flash_prepare", True),
    (0x207EC16, "stock_finish", "pebble_flash_finish", False),
    (0x207EC28, "stock_irq_disable", "pebble_flash_irq_enter", False),
    (0x207EDA8, "stock_irq_disable", "pebble_flash_irq_enter", False),
    (0x207EEE8, "stock_irq_disable", "pebble_flash_irq_enter", False),
    (0x207F002, "stock_irq_disable", "pebble_flash_program_enter", False),
    (0x207F306, "stock_irq_disable", "pebble_flash_irq_enter", False),
    (0x207ECFA, "stock_irq_enable", "pebble_flash_irq_exit", True),
    (0x207EDC6, "stock_irq_enable", "pebble_flash_irq_exit", False),
    (0x207EF14, "stock_irq_enable", "pebble_flash_irq_exit", False),
    (0x207F344, "stock_irq_enable", "pebble_flash_irq_exit", False),
    (0x207F044, "stock_irq_enable", "pebble_flash_program_exit", False),
]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_hashes():
    return {p.name: digest(p.read_bytes()) for p in SOURCES}


def branch(source, target, tail=False):
    data = thumb_bl(source, target)
    if tail:
        a, b = struct.unpack("<HH", data)
        data = struct.pack("<HH", a, b & ~0x4000)
    return data


def wait_bytes(base, out, shift):
    # 16-bit LDR Rt,[Rn,#16]; LSRS Rt,Rt,#31 or CMP Rt,#0; BNE/BLT -8.
    return struct.pack(
        "<HHH",
        0x6900 | base << 3 | out,
        (0x0FC0 | out << 3 | out) if shift else (0x2800 | out << 8),
        0xD1FC if shift else 0xDBFC,
    )


def compile_guard(address, compiler, objcopy, folder, volatile_status=False):
    folder = Path(folder)
    shims = [".syntax unified", ".thumb"]
    for i, (base, out, shift) in enumerate(dict.fromkeys(site[1:] for site in WAITS)):
        # Preserve all live registers, including IRQ flags in r0. LR is saved by
        # every original containing function. The shim restores loop result/flags.
        name = f"pebble_wait_{i}"
        shims += [
            '.section .guard_shims,"ax",%progbits',
            f".global {name}",
            f".type {name},%function",
            ".thumb_func",
            f"{name}:",
            "push {r0-r4, lr}",
            f"mov r0, r{base}",
            "ldr r4, =pebble_flash_wait",
            "blx r4",
        ]
        shims += [f"str r0, [sp, #{out * 4}]"] if out <= 4 else [f"mov r{out}, r0"]
        shims += [
            "pop {r0-r4, lr}",
            f"lsrs r{out}, r{out}, #31" if shift else f"cmp r{out}, #0",
            "bx lr",
            ".ltorg",
        ]
    for i, (base, out) in enumerate(dict.fromkeys(site[1:] for site in LAUNCHES)):
        name = f"pebble_launch_{i}"
        shims += [
            '.section .text.launch_shims,"ax",%progbits',
            f".global {name}",
            f".type {name},%function",
            ".thumb_func",
            f"{name}:",
            "push {r0-r4, lr}",
            f"mov r0, r{base}",
            "ldr r4, =pebble_flash_launch",
            "blx r4",
        ]
        shims += [f"str r0, [sp, #{out * 4}]"] if out <= 4 else [f"mov r{out}, r0"]
        shims += ["pop {r0-r4, pc}", ".ltorg"]
    (folder / "shims.S").write_text("\n".join(shims) + "\n")
    ld = f"""ENTRY(pebble_flash_write)
SECTIONS {{
 .guard 0x{ITCM_END:x} : {{ *(.guard) *(.guard_shims) }}
 .spi_guard 0x080665a0 : {{ *(.spi_guard) }}
 .hook 0x{address:x} : {{ *(.text*) *(.rodata*) }}
 /DISCARD/ : {{ *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.eh_frame*) }}
}}
ASSERT(SIZEOF(.guard) <= 0x{ITCM_LIMIT - ITCM_END:x}, "ITCM guard exceeds unused tail")
ASSERT(SIZEOF(.spi_guard) <= 0x238, "SPI replacement exceeds pinned bypass body")
ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "Unexpected writable CP hook data")
"""
    ld += (
        "\n".join(f"{name} = 0x{value | 1:x};" for name, value in SYMBOLS.items())
        + "\n"
    )
    (folder / "guard.ld").write_text(ld)
    elf = folder / "guard.elf"
    subprocess.run(
        [
            compiler,
            "-Os",
            *(["-DPEBBLE_FLASH_VOLATILE_STATUS=1"] if volatile_status else []),
            "-mthumb",
            "-mcpu=cortex-m33",
            "-mfloat-abi=soft",
            "-fno-unwind-tables",
            "-fno-asynchronous-unwind-tables",
            "-nostdlib",
            "-Wl,--build-id=none",
            "-T",
            str(folder / "guard.ld"),
            *map(str, SOURCES),
            str(folder / "shims.S"),
            "-o",
            str(elf),
        ],
        check=True,
    )
    result = {}
    for section in ("hook", "guard", "spi_guard"):
        raw = folder / f"{section}.bin"
        subprocess.run(
            [objcopy, "-O", "binary", "-j", "." + section, str(elf), str(raw)],
            check=True,
        )
        result[section] = raw.read_bytes()
    nm = str(Path(compiler).with_name("arm-none-eabi-nm"))
    rows = subprocess.run(
        [nm, "--defined-only", str(elf)], check=True, capture_output=True, text=True
    ).stdout
    result["symbols"] = {
        parts[2]: int(parts[0], 16)
        for line in rows.splitlines()
        if len(parts := line.split()) == 3
    }
    return result


def install_flash_guard(
    stock, image, stock_elf, compiler, objcopy, output, volatile_status=False
):
    # The AP notification handler ACKs only; it does not park XIP execution.
    # Never package a pin-mux bypass until a real RAM-resident handoff exists.
    if volatile_status:
        raise ValueError(
            "Volatile SPI bypass disabled: AP flash notification only ACKs; "
            "a verified RAM-resident AP ownership handoff is required"
        )
    verify_stock(stock)
    if digest(Path(stock_elf).read_bytes()) != ELF_SHA256:
        raise ValueError("CP flash guard requires the exact verified stock ELF")
    rf_offset = (len(stock) + 15) & ~15
    if image != install_hook(stock, image[rf_offset:]):
        raise ValueError("CP input contains changes outside the RF hook")
    with tempfile.TemporaryDirectory() as tmp:
        extracted = Path(tmp) / "stock.bin"
        subprocess.run(
            [objcopy, "-O", "binary", str(stock_elf), str(extracted)], check=True
        )
        if extracted.read_bytes() != stock:
            raise ValueError("Stock CP ELF load bytes do not match pinned input")
        address = CP_XIP + ((len(image) + 15) & ~15)
        built = compile_guard(address, compiler, objcopy, tmp, volatile_status)
        data = bytearray(image)
        data.extend(b"\xff" * (address - CP_XIP - len(data)))
        data.extend(built["hook"])
        patches = []

        def patch(address, expected, replacement):
            off = address - CP_XIP
            if data[off : off + len(expected)] != expected:
                raise ValueError(f"CP flash hook mismatch at {address:#x}")
            data[off : off + len(expected)] = replacement
            patches.append(
                {
                    "address": address,
                    "before": expected.hex(),
                    "after": replacement.hex(),
                }
            )

        for site, old, new, tail in CALLS:
            patch(
                site,
                branch(site, SYMBOLS[old], tail),
                branch(site, built["symbols"][new], tail),
            )
        kinds = list(dict.fromkeys(site[1:] for site in WAITS))
        for site, base, out, shift in WAITS:
            target = (
                built["symbols"][f"pebble_wait_{kinds.index((base, out, shift))}"] | 1
            )
            if target >= 0x10000:
                raise ValueError("Flash wait trampoline must fit MOVW")
            movw = struct.pack(
                "<HH",
                0xF240 | ((target >> 12) & 15) | ((target >> 11) & 1) << 10,
                ((target >> 8) & 7) << 12 | out << 8 | (target & 255),
            )
            # MOVW + BLX enter ITCM without fetching a flash literal or veneer.
            patch(
                site,
                wait_bytes(base, out, shift),
                movw + struct.pack("<H", 0x4780 | out << 3),
            )
        kinds = list(dict.fromkeys(site[1:] for site in LAUNCHES))
        for site, base, out in LAUNCHES:
            expected = struct.pack(
                "<HHHH",
                0x6900 | base << 3 | out,
                0xF040 | out,
                0x5000 | out << 8,
                0x6100 | base << 3 | out,
            )
            target = built["symbols"][f"pebble_launch_{kinds.index((base, out))}"]
            patch(site, expected, branch(site, target) + b"\x00\xbf" * 2)
        # Relocate the original ITCM load image intact, extending only its free
        # tail. Startup's existing copy loop initializes guard code and zero state.
        load = CP_XIP + ((len(data) + 15) & ~15)
        data.extend(b"\xff" * (load - CP_XIP - len(data)))
        original = stock[
            ITCM_LOAD - CP_XIP : ITCM_LOAD - CP_XIP + ITCM_END - ITCM_START
        ]
        if len(original) != ITCM_END - ITCM_START:
            raise ValueError("Truncated stock ITCM")
        tail = bytearray(ITCM_LIMIT - ITCM_END)
        tail[: len(built["guard"])] = built["guard"]
        data.extend(original + tail)
        patch(
            COPY_ENTRY,
            struct.pack("<III", ITCM_LOAD, ITCM_START, (ITCM_END - ITCM_START) // 4),
            struct.pack("<III", load, ITCM_START, (ITCM_LIMIT - ITCM_START) // 4),
        )
        iram_load = CP_XIP + ((len(data) + 15) & ~15)
        data.extend(b"\xff" * (iram_load - CP_XIP - len(data)))
        iram = stock[IRAM_LOAD - CP_XIP : IRAM_LOAD - CP_XIP + IRAM_END - IRAM_START]
        if len(iram) != IRAM_END - IRAM_START:
            raise ValueError("Truncated stock IRAM")
        if volatile_status:
            iram = bytearray(iram)
            offset = 0x08066594 - IRAM_START
            entry = bytes.fromhex("dff800f0") + struct.pack(
                "<I", built["symbols"]["pebble_spi_bypass_write"] | 1
            )
            iram[offset : offset + 8] = entry
            offset = 0x080665A0 - IRAM_START
            iram[offset : offset + len(built["spi_guard"])] = built["spi_guard"]
        data.extend(iram + bytes(IRAM_LIMIT - IRAM_END))
        patch(
            IRAM_COPY,
            struct.pack("<III", IRAM_LOAD, IRAM_START, (IRAM_END - IRAM_START) // 4),
            struct.pack("<III", iram_load, IRAM_START, (IRAM_LIMIT - IRAM_START) // 4),
        )
        if len(data) > PRIMARY_CP_APP_LOGICAL_SIZE:
            raise ValueError("CP flash guard exceeds the existing partition")
        output = Path(output)
        output.mkdir(parents=True, exist_ok=True)
        for name in ("guard.elf", "guard.ld", "shims.S"):
            (output / ("pebbleos.t5ai-flash-" + name)).write_bytes(
                (Path(tmp) / name).read_bytes()
            )
        metadata = {
            "schema": 1,
            "elf_sha256": ELF_SHA256,
            "sources": source_hashes(),
            "guard_address": ITCM_END,
            "guard_size": len(built["guard"]),
            "launch_sites": len(LAUNCHES),
            "state_address": STATE,
            "state_size": 144,
            "wait_sites": len(WAITS),
            "itcm_load": load,
            "iram_load": iram_load,
            "patches": patches,
            "timeout_aon_ticks": 160000,
            "diagnostics": True,
            "protection_policy": "experimental-volatile-unprotect-stock-restore-readback"
            if volatile_status
            else "unchanged",
            "volatile_status": volatile_status,
            "spi_replacement": {
                "entry": "08066594",
                "ram": "080665a0",
                "ram_size": len(built["spi_guard"]),
                "clock_hz_estimate": 26000000 // (2 * 7),
                "clock_basis": "CONFIG_SYSTEM_CTRL vendor HAL; not measured",
            }
            if volatile_status
            else None,
            "volatile_status_jedec_id": "c86517" if volatile_status else None,
        }
        (output / "pebbleos.t5ai-flash-guard.json").write_text(
            json.dumps(metadata, indent=2) + "\n"
        )
        return bytes(data), metadata
