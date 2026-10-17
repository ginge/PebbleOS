#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the Unhygienix peripheral bring-up image, not the PebbleOS kernel."""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "boards/unhygienix/tuya"


DEFAULT_SDK = ROOT / "third_party/hal_tuya/TuyaOpen"
DEFAULT_PLATFORM = ROOT / "third_party/hal_tuya/TuyaOpen-T5AI"


def platform_path(sdk):
    return DEFAULT_PLATFORM if sdk.resolve() == DEFAULT_SDK.resolve() else sdk / "platform/T5AI"


def verify_sdk(sdk, revisions):
    for name, path in (("tuyaopen", sdk), ("t5ai", platform_path(sdk))):
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout.strip() != revisions[name]:
            raise ValueError(f"{name}: expected SDK commit {revisions[name]}")
        dirty = subprocess.run(
            ["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"],
            check=True, capture_output=True, text=True,
        )
        if dirty.stdout:
            raise ValueError(f"{name}: tracked SDK source is modified")
    modules = subprocess.run(
        ["git", "-C", str(sdk), "submodule", "status"],
        check=True, capture_output=True, text=True,
    )
    if any(line.startswith(("-", "+", "U")) for line in modules.stdout.splitlines()):
        raise ValueError("Initialize pinned TuyaOpen dependencies with git submodule update --init --recursive")
    if not (sdk / "tos.py").is_file():
        raise ValueError("Not a TuyaOpen SDK directory")


def stage_app(source, output):
    marker = output / ".pebble-tuya-build"
    if output.exists() and not marker.is_file():
        raise ValueError(f"Refusing to overwrite unowned directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    marker.write_text("unhygienix hardware bring-up\n")
    for name in ("CMakeLists.txt", "app_default.config", "sdk.lock.json"):
        shutil.copy2(source / name, output / name)
    # Recreate only marker-owned staging trees, including retired adapter files.
    for name in ("src", "pebble_include"):
        target = output / name
        if target.is_symlink():
            target.unlink()
        elif target.exists():
            shutil.rmtree(target)
    shutil.copytree(source / "src", output / "src")
    for name in ("drivers/button_id.h", "services/touch/touch_event.h"):
        target = output / "pebble_include/pbl" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "include/pbl" / name, target)


def clone_sdk(source, destination):
    # Tuya tooling needs Git metadata for its revision and submodule checks.
    subprocess.run(["git", "-c", "advice.detachedHead=false", "clone", "--quiet", "--no-hardlinks", str(source), str(destination)],
                   check=True)
    origin = subprocess.run(["git", "-C", str(source), "remote", "get-url", "origin"],
                            capture_output=True, text=True)
    if origin.returncode == 0:
        subprocess.run(["git", "-C", str(destination), "remote", "set-url", "origin",
                        origin.stdout.strip()], check=True)
    modules = source / ".gitmodules"
    if modules.is_file():
        paths = subprocess.run(["git", "config", "-f", str(modules), "--get-regexp",
                                r"submodule\..*\.path"], capture_output=True, text=True)
        if paths.returncode not in (0, 1):
            raise ValueError("Cannot read vendor submodule paths")
        for line in paths.stdout.splitlines():
            key, relative = line.split(None, 1)
            policy = subprocess.run(["git", "config", "-f", str(modules), "--get",
                                     key[:-4] + "update"], capture_output=True, text=True)
            if policy.stdout.strip() == "none":
                continue
            child = source / relative
            if not child.resolve().is_relative_to(source.resolve()):
                raise ValueError("Vendor submodule path escapes its source tree")
            clone_sdk(child, destination / relative)
        subprocess.run(["git", "-C", str(destination), "submodule", "init"], check=True)


def stage_sdk(sdk, output):
    # Vendor generators write into their SDK. Keep pinned submodules pristine.
    for source in (sdk, platform_path(sdk)):
        if output.is_relative_to(source) or source.is_relative_to(output):
            raise ValueError("Build output must not overlap vendor source")
    staged_sdk = output / "sdk"
    if staged_sdk.is_symlink():
        raise ValueError("Refusing a symlinked SDK staging directory")
    if staged_sdk.exists():
        shutil.rmtree(staged_sdk)
    clone_sdk(sdk, staged_sdk)
    clone_sdk(platform_path(sdk), staged_sdk / "platform/T5AI")
    return staged_sdk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--sdk", default=DEFAULT_SDK, type=Path, help="Pinned TuyaOpen checkout"
    )
    parser.add_argument(
        "--python",
        default=sys.executable,
        type=Path,
        help="Python from a venv containing TuyaOpen build dependencies",
    )
    parser.add_argument("--out", type=Path, default=ROOT / "build/unhygienix_hw")
    args = parser.parse_args()
    sdk, output = args.sdk.resolve(), args.out.resolve()
    python = args.python.absolute()  # Preserve a venv's interpreter symlink.
    try:
        verify_sdk(sdk, json.loads((SOURCE / "sdk.lock.json").read_text()))
        for source in (sdk, platform_path(sdk)):
            if output.is_relative_to(source) or source.is_relative_to(output):
                raise ValueError("Build output must not overlap vendor source")
        stage_app(SOURCE, output)
        staged_sdk = stage_sdk(sdk, output)
        env = os.environ.copy()
        env["PATH"] = str(python.parent) + os.pathsep + env.get("PATH", "")
        env["OPEN_SDK_PYTHON"] = str(python)
        print(
            "Building hardware bring-up only; PebbleOS kernel is not included.",
            flush=True,
        )
        subprocess.run(
            [str(python), str(staged_sdk / "tos.py"), "build"], cwd=output, env=env, check=True
        )
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Tuya build failed: {error}", file=sys.stderr)
        return 1
    print(
        f"Inspect vendor build artifacts in {output / 'dist'}; no device was flashed."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
