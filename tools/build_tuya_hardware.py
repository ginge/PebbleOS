#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the DVT3 peripheral bring-up image, not the PebbleOS kernel."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "boards/getafix/tuya"


def verify_sdk(sdk, revisions):
    for name, path in (("tuyaopen", sdk), ("t5ai", sdk / "platform/T5AI")):
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True,
        )
        if result.stdout.strip() != revisions[name]:
            raise ValueError(f"{name}: expected SDK commit {revisions[name]}")
    if not (sdk / "tos.py").is_file():
        raise ValueError("Not a TuyaOpen SDK directory")


def stage_app(source, output):
    marker = output / ".pebble-tuya-build"
    if output.exists() and not marker.is_file():
        raise ValueError(f"Refusing to overwrite unowned directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    marker.write_text("getafix@dvt3 hardware bring-up\n")
    for name in ("CMakeLists.txt", "app_default.config", "sdk.lock.json"):
        shutil.copy2(source / name, output / name)
    shutil.copytree(source / "src", output / "src", dirs_exist_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path, help="Pinned TuyaOpen checkout")
    parser.add_argument("--python", default=sys.executable, type=Path,
                        help="Python from a venv containing TuyaOpen build dependencies")
    parser.add_argument("--out", type=Path, default=ROOT / "build/getafix_dvt3_hw")
    args = parser.parse_args()
    sdk, output = args.sdk.resolve(), args.out.resolve()
    python = args.python.absolute()  # Preserve a venv's interpreter symlink.
    try:
        verify_sdk(sdk, json.loads((SOURCE / "sdk.lock.json").read_text()))
        stage_app(SOURCE, output)
        env = os.environ.copy()
        env["PATH"] = str(python.parent) + os.pathsep + env.get("PATH", "")
        env["OPEN_SDK_PYTHON"] = str(python)
        print("Building hardware bring-up only; PebbleOS kernel is not included.", flush=True)
        subprocess.run([str(python), str(sdk / "tos.py"), "build"],
                       cwd=output, env=env, check=True)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Tuya build failed: {error}", file=sys.stderr)
        return 1
    print(f"Inspect vendor build artifacts in {output / 'dist'}; no device was flashed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
