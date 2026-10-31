#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify and package experimental Unhygienix CP links; never flashes hardware."""

import argparse
from io import BytesIO
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import unhygienix_cp_build as recipe  # noqa: E402
from tools.patch_unhygienix_cp_rf import CP_XIP, STOCK_SHA256, thumb_bl  # noqa: E402
from tools.t5ai_image import encode_protected_blocks  # noqa: E402
from tools.unhygienix_image import PRIMARY_CP_APP_LOGICAL_SIZE  # noqa: E402

CONTROLLER = recipe.SDK / (
    "t5_os/cp/components/bk_libs/bk7258/libs/"
    "libbluetooth_controller_controller_only_ble.a"
)
CONTROLLER_SHA256 = "a6dd54ec9e6a85be90528138b7a8f6df64cf1edd8590d35fa2e1f20fa0184443"
FUNCTIONS = {
    "lld_con_sched": 0x2B4,
    "lld_con_data_tx": 0xCC,
    "lld_con_max_lat_calc": 0x80,
}
SOURCE = ROOT / "boards/unhygienix/tuya/cp/ble_latency_fix.c"


def symbol(elf, name):
    if not hasattr(elf, "_unhygienix_symbols"):
        index = {}
        for entry in elf.get_section_by_name(".symtab").iter_symbols():
            if isinstance(entry["st_shndx"], int) or entry["st_shndx"] == "SHN_ABS":
                index.setdefault(entry.name, []).append(entry)
        elf._unhygienix_symbols = index
    symbols = elf._unhygienix_symbols.get(name, [])
    if len(symbols) != 1:
        raise ValueError(f"Missing or ambiguous defined symbol: {name}")
    return symbols[0]


def memory(elf, address, size):
    for section in elf.iter_sections():
        start = section["sh_addr"]
        if (
            section["sh_flags"] & 2
            and section["sh_type"] != "SHT_NOBITS"
            and (start <= address and address + size <= start + section["sh_size"])
        ):
            return section.data()[address - start : address - start + size]
    raise ValueError(
        f"Address outside initialized allocated ELF sections: {address:#x}"
    )


def controller_object():
    if recipe.digest(CONTROLLER.read_bytes()) != CONTROLLER_SHA256:
        raise ValueError("Bluetooth controller archive differs from pinned input")
    return subprocess.check_output(["ar", "p", str(CONTROLLER), "lld_con.c.obj"])


def verify_controller(elf, image, object_bytes=None):
    """Match every instruction and validate every relocation against pinned input."""
    obj = ELFFile(
        BytesIO(controller_object() if object_bytes is None else object_bytes)
    )
    obj_symbols = obj.get_section_by_name(".symtab")
    calls = []
    proof = {}
    for name, size in FUNCTIONS.items():
        source = symbol(obj, name)
        target = symbol(elf, name)
        if source["st_size"] != size or target["st_size"] != size:
            raise ValueError(f"Controller function size changed: {name}")
        section = obj.get_section(source["st_shndx"])
        start = source["st_value"] & ~1
        expected = section.data()[start : start + size]
        address = target["st_value"] & ~1
        actual = memory(elf, address, size)
        if actual != image[address - CP_XIP : address - CP_XIP + size]:
            raise ValueError(f"CP raw image disagrees with ELF: {name}")
        normalized = bytearray(actual)
        rel = obj.get_section_by_name(".rel" + section.name)
        if rel is None:
            raise ValueError(f"Missing pinned relocation section: {name}")
        relocations = []
        for item in rel.iter_relocations():
            offset = item["r_offset"] - start
            if not 0 <= offset <= size - 4:
                raise ValueError(f"Unexpected relocation extent: {name}")
            referent = obj_symbols.get_symbol(item["r_info_sym"])
            referent_name = referent.name
            if referent["st_info"]["type"] == "STT_SECTION":
                referent_name = obj.get_section(referent["st_shndx"]).name
            original = expected[offset : offset + 4]
            kind = item["r_info_type"]
            if kind in (10, 30):  # R_ARM_THM_CALL / R_ARM_THM_JUMP24
                placeholder = bytearray(thumb_bl(0, 0))
                if kind == 30:
                    placeholder[3] &= ~0x40
                if original != placeholder:
                    raise ValueError("Unsupported Thumb relocation addend")
                destination = symbol(elf, referent_name)["st_value"] & ~1
                relocated = bytearray(thumb_bl(address + offset, destination))
                if kind == 30:
                    relocated[3] &= ~0x40
                if name == "lld_con_sched" and referent_name == "lld_con_max_lat_calc":
                    calls.append(address + offset)
            elif kind == 2:  # R_ARM_ABS32
                addend = struct.unpack("<I", original)[0]
                if referent_name.startswith(".rodata.__func__."):
                    # Local function-name symbols are duplicated across objects.
                    literal_section = obj.get_section(referent["st_shndx"])
                    literal = literal_section.data()[referent["st_value"] + addend :]
                    literal = literal[: literal.index(b"\0") + 1]
                    pointer = struct.unpack("<I", actual[offset : offset + 4])[0]
                    if memory(elf, pointer, len(literal)) != literal:
                        raise ValueError(
                            "Controller diagnostic string relocation changed"
                        )
                    relocated = struct.pack("<I", pointer)
                else:
                    lookup = (
                        "lld_con_env"
                        if referent_name == ".bss.lld_con_env"
                        else referent_name
                    )
                    relocated = struct.pack(
                        "<I", symbol(elf, lookup)["st_value"] + addend
                    )
            else:
                raise ValueError(f"Unsupported controller relocation type: {kind}")
            if actual[offset : offset + 4] != relocated:
                raise ValueError(
                    f"Controller relocation mismatch: {name}+{offset:#x} {referent_name}"
                )
            normalized[offset : offset + 4] = original
            relocations.append(
                {"offset": offset, "type": kind, "target": referent_name}
            )
        if bytes(normalized) != expected:
            raise ValueError(f"Controller instruction bytes changed: {name}")
        proof[name] = {
            "address": address,
            "size": size,
            "normalized_sha256": recipe.digest(expected),
            "relocations": relocations,
        }
    sched = proof["lld_con_sched"]["address"]
    if calls != [sched + 0xEE, sched + 0x278]:
        raise ValueError("Unexpected interval-application call sites")
    return calls, proof


def verify_flash_copy(elf, image):
    start = symbol(elf, "__itcm_start__")["st_value"]
    end = symbol(elf, "__itcm_end__")["st_value"]
    load = symbol(elf, "__itcm_text")["st_value"]
    table = symbol(elf, "__copy_table_start__")["st_value"]
    table_end = symbol(elf, "__copy_table_end__")["st_value"]
    if not 0x20 <= start < end <= 0x4000 or (end - start) % 4:
        raise ValueError("Invalid CP ITCM extent")
    data = memory(elf, table, table_end - table)
    if len(data) % 12:
        raise ValueError("Invalid CP boot copy table")
    if (load, start, (end - start) // 4) not in list(struct.iter_unpack("<III", data)):
        raise ValueError("Boot copy table omits the CP flash contract")
    if image[load - CP_XIP : load - CP_XIP + end - start] != memory(
        elf, start, end - start
    ):
        raise ValueError("Raw CP image omits the ITCM load bytes")
    return {"load": load, "start": start, "end": end, "bytes": end - start}


def compile_fix(address, max_lat, env, compiler):
    if "10.3-2021.10" not in recipe.checked([compiler, "--version"], text=True):
        raise ValueError("Pinned GCC 10.3 toolchain required")
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        script, elf_path, raw_path = [
            folder / name for name in ("fix.ld", "fix.elf", "fix.bin")
        ]
        script.write_text(f"""ENTRY(pebble_latency_interval_applied)
SECTIONS {{ . = {address}; .hook : {{ *(.text*) *(.rodata*) }}
/DISCARD/ : {{ *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.eh_frame*) }} }}
lld_con_max_lat_calc = {max_lat | 1};
pebble_lld_con_env = {env};
""")
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
                str(script),
                str(SOURCE),
                "-o",
                str(elf_path),
            ],
            check=True,
        )
        with elf_path.open("rb") as handle:
            elf = ELFFile(handle)
            if (
                symbol(elf, "pebble_latency_interval_applied")["st_value"] & ~1
                != address
            ):
                raise ValueError("Latency hook entry differs from append address")
            if any(
                s["sh_size"] for s in elf.iter_sections() if s.name in (".bss", ".data")
            ):
                raise ValueError("Latency hook needs unreserved RAM")
        subprocess.run(
            [
                str(compiler.with_name("arm-none-eabi-objcopy")),
                "-O",
                "binary",
                "-j",
                ".hook",
                str(elf_path),
                str(raw_path),
            ],
            check=True,
        )
        return raw_path.read_bytes()


def verify_link_hashes(image, config, wifi):
    path = ROOT / "boards/unhygienix/tuya/cp/variant-links.json"
    pins = json.loads(path.read_text())
    if (
        pins.get("schema") != 1
        or pins.get("board") != "unhygienix"
        or pins.get("stock_sha256") != STOCK_SHA256
        or pins.get("controller_sha256") != CONTROLLER_SHA256
        or pins.get("latency_source_sha256") != recipe.digest(SOURCE.read_bytes())
    ):
        raise ValueError("Invalid reviewed CP link contract")
    expected = pins.get("profiles", {}).get(wifi, {})
    if recipe.digest(image) != expected.get("app.bin") or recipe.digest(
        config
    ) != expected.get("sdkconfig"):
        raise ValueError("CP link/config does not match a reviewed profile SHA256")
    return recipe.digest(path.read_bytes())


def package(work, stock, output, compiler):
    if output.exists():
        raise ValueError("Use a new output directory; preserve earlier evidence")
    if recipe.digest(stock.read_bytes()) != STOCK_SHA256:
        raise ValueError("Stock CP does not match the pinned SHA256")
    revisions = recipe.dependencies()
    inputs = json.loads((work / "inputs.json").read_text())
    if inputs["board"] != "unhygienix" or inputs["vendor_revisions"] != revisions:
        raise ValueError("CP build provenance mismatch")
    for relative, digest in inputs["board_inputs"].items():
        if recipe.digest((ROOT / relative).read_bytes()) != digest:
            raise ValueError(f"Build recipe input changed: {relative}")
    wifi = inputs["wifi"]
    built = work / "build/bk7258/tuya_app/bk7258"
    elf_path, raw_path = built / "app.elf", built / "app.bin"
    inspection = recipe.inspect(elf_path, built / "sdkconfig", wifi)
    inspection["selected_phy"] = recipe.inspect_libraries(built, wifi)
    image = raw_path.read_bytes()
    pins_hash = verify_link_hashes(image, (built / "sdkconfig").read_bytes(), wifi)
    with elf_path.open("rb") as handle:
        elf = ELFFile(handle)
        calls, proof = verify_controller(elf, image)
        flash_copy = verify_flash_copy(elf, image)
        address = CP_XIP + ((len(image) + 15) & ~15)
        hook = compile_fix(
            address,
            symbol(elf, "lld_con_max_lat_calc")["st_value"],
            symbol(elf, "lld_con_env")["st_value"],
            compiler,
        )
    if not hook or address - CP_XIP + len(hook) > PRIMARY_CP_APP_LOGICAL_SIZE:
        raise ValueError("Latency hook exceeds the CP partition")
    fixed = bytearray(image)
    for call in calls:
        fixed[call - CP_XIP : call - CP_XIP + 4] = thumb_bl(call, address)
    fixed.extend(b"\xff" * (address - CP_XIP - len(fixed)))
    fixed.extend(hook)
    files = {
        "cp-variant.bin": bytes(fixed),
        "cp-variant.crc.bin": encode_protected_blocks(fixed),
    }
    manifest = {
        "schema": 2,
        "board": "unhygienix",
        "experimental": True,
        "flash_qualified": False,
        "wifi": wifi,
        "stock_sha256": STOCK_SHA256,
        "reviewed_links_sha256": pins_hash,
        "build_inputs_sha256": recipe.digest((work / "inputs.json").read_bytes()),
        "elf_sha256": recipe.digest(elf_path.read_bytes()),
        "base_sha256": recipe.digest(image),
        "controller_sha256": CONTROLLER_SHA256,
        "latency_source_sha256": recipe.digest(SOURCE.read_bytes()),
        "packager_sha256": recipe.digest(Path(__file__).read_bytes()),
        "inspection": inspection,
        "controller_proof": proof,
        "flash_contract_copy": flash_copy,
        "calls_changed": calls,
        "hook_address": address,
        "outputs": {name: recipe.digest(data) for name, data in files.items()},
    }
    output.mkdir(parents=True)
    for name, data in files.items():
        (output / name).write_bytes(data)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(
        f"Experimental {wifi} CP verified and packaged: {len(fixed)} bytes; no device write"
    )


def validate_package(work, stock, output, compiler):
    # Regenerate from reviewed source/ELF inputs; a manifest alone is insufficient.
    with tempfile.TemporaryDirectory() as directory:
        expected = Path(directory) / "expected"
        package(work, stock, expected, compiler)
        for name in ("manifest.json", "cp-variant.bin", "cp-variant.crc.bin"):
            if (expected / name).read_bytes() != (output / name).read_bytes():
                raise ValueError(f"CP variant package mismatch: {name}")
    from tools.flash_unhygienix_cp_firmware import validate_firmware

    return validate_firmware(output / "cp-variant.bin", output / "cp-variant.crc.bin")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("work", "stock", "output", "cc"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        package(args.work, args.stock, args.output, args.cc)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"CP variant rejected: {error}\n")


if __name__ == "__main__":
    main()
