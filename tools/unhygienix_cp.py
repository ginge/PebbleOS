# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Reproducible source-hook packaging for the validated stock CP ABI."""

import hashlib
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.patch_unhygienix_cp_latency import SOURCE as LATENCY_SOURCE
from tools.patch_unhygienix_cp_latency import add_fix
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


def build_cp(stock_path, output, compiler, objcopy):
    revisions = verify_dependencies()
    stock = Path(stock_path).read_bytes()
    verify_stock(stock)
    hook = compile_hook(CP_XIP + ((len(stock) + 15) & ~15), compiler, objcopy)
    image = install_hook(stock, hook)
    image = add_fix(image, compiler, objcopy)
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
        "schema": 1,
        "board": "unhygienix",
        "vendor_revisions": revisions,
        "stock_sha256": STOCK_SHA256,
        "hook_sha256": digest(SOURCE.read_bytes()),
        "latency_fix_sha256": digest(LATENCY_SOURCE.read_bytes()),
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
        or manifest.get("latency_fix_sha256") != digest(LATENCY_SOURCE.read_bytes())
    ):
        raise ValueError("CP manifest does not match the pinned Unhygienix inputs")
    if manifest.get("flash_guard") is not None:
        raise ValueError(
            "CP flash guard images are retired; rebuild the Bluetooth-only CP"
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
    args = parser.parse_args()
    if not args.stock:
        parser.error("configure T5AI_CP_STOCK with the pinned stock CP image")
    build_cp(args.stock, args.output, args.compiler, args.objcopy)
