# SPDX-License-Identifier: Apache-2.0
"""Reproducible source-hook packaging for the validated stock CP ABI."""

import hashlib
import json
import subprocess
from pathlib import Path

from tools.patch_unhygienix_cp_rf import (
    CP_XIP,
    ROOT,
    SOURCE,
    STOCK_SHA256,
    compile_hook,
    install_hook,
    verify_stock,
)
from tools.t5ai_image import encode_protected_blocks

DEPENDENCIES = ROOT / "third_party/hal_tuya"
LOCK = ROOT / "boards/unhygienix/tuya/sdk.lock.json"


def verify_dependencies():
    revisions = json.loads(LOCK.read_text())
    for name, key in (("TuyaOpen-T5AI", "t5ai"),):
        path = DEPENDENCIES / name
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout.strip() != revisions[key]:
            raise ValueError(f"{name}: expected {revisions[key]}")
        dirty = subprocess.run(
            ["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"],
            check=True,
            capture_output=True,
            text=True,
        )
        if dirty.stdout:
            raise ValueError(f"{name}: tracked vendor source is modified")
    return revisions


def digest(data):
    return hashlib.sha256(data).hexdigest()


def build_cp(
    stock_path, output, compiler, objcopy, flash_guard_elf=None, volatile_status=False
):
    if volatile_status and not flash_guard_elf:
        raise ValueError("Volatile status requires hash-verified flash guard ELF")
    revisions = verify_dependencies()
    stock = Path(stock_path).read_bytes()
    verify_stock(stock)
    hook = compile_hook(CP_XIP + ((len(stock) + 15) & ~15), compiler, objcopy)
    image = install_hook(stock, hook)
    flash_guard = None
    if flash_guard_elf:
        from tools.unhygienix_cp_flash import install_flash_guard

        image, flash_guard = install_flash_guard(
            stock, image, flash_guard_elf, compiler, objcopy, output, volatile_status
        )
    protected = encode_protected_blocks(image)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    artifacts = {"pebbleos.t5ai-cp.bin": image, "pebbleos.t5ai-cp-crc.bin": protected}
    for name, data in artifacts.items():
        (output / name).write_bytes(data)
    compiler_version = subprocess.run(
        [compiler, "--version"], check=True, capture_output=True, text=True
    ).stdout.splitlines()[0]
    manifest = {
        "compiler": compiler_version,
        "flash_guard": flash_guard,
        "schema": 1,
        "board": "unhygienix",
        "vendor_revisions": revisions,
        "stock_sha256": STOCK_SHA256,
        "hook_sha256": digest(SOURCE.read_bytes()),
        "compatibility": "pinned Unhygienix CP mailbox/HCI ABI; BLE/BT PLL 0x0202",
        "artifacts": {name: digest(data) for name, data in artifacts.items()},
    }
    (output / "pebbleos.t5ai-cp.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Validated Unhygienix CP artifacts written to", output)


def validate_manifest(output):
    output = Path(output)
    manifest = json.loads((output / "pebbleos.t5ai-cp.json").read_text())
    if not isinstance(manifest, dict):
        raise ValueError("Invalid CP manifest")
    if (
        manifest.get("schema") != 1
        or manifest.get("board") != "unhygienix"
        or manifest.get("stock_sha256") != STOCK_SHA256
        or manifest.get("vendor_revisions") != json.loads(LOCK.read_text())
        or manifest.get("hook_sha256") != digest(SOURCE.read_bytes())
    ):
        raise ValueError("CP manifest does not match the pinned Unhygienix inputs")
    guard = manifest.get("flash_guard")
    if guard is not None:
        from tools.unhygienix_cp_flash import ELF_SHA256, source_hashes

        if (
            guard.get("schema") != 1
            or guard.get("elf_sha256") != ELF_SHA256
            or guard.get("sources") != source_hashes()
        ):
            raise ValueError(
                "CP flash guard manifest does not match source/verified ELF"
            )
    for name in ("pebbleos.t5ai-cp.bin", "pebbleos.t5ai-cp-crc.bin"):
        if manifest.get("artifacts", {}).get(name) != digest(
            (output / name).read_bytes()
        ):
            raise ValueError(f"CP manifest hash mismatch: {name}")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument(
        "--flash-guard-elf",
        help="Exact pinned stock ELF for CP flash diagnostics and deadlines",
    )
    parser.add_argument(
        "--volatile-status",
        action="store_true",
        help="EXPERIMENTAL: GD25WQ64E volatile unprotect; hardware readback failed, keep disabled",
    )
    args = parser.parse_args()
    if not args.stock:
        parser.error("configure T5AI_CP_STOCK with the pinned stock CP image")
    build_cp(
        args.stock,
        args.output,
        args.compiler,
        args.objcopy,
        args.flash_guard_elf,
        args.volatile_status,
    )
