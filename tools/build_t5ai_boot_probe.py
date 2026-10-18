#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

"""Build the dependency-free T5AI CPU1 reset-entry probe."""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.unhygienix_image import encode_pebble_application, verify_protected_blocks


PROBE_DIR = ROOT / "tools" / "t5ai_boot_probe"


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def tool(name: str) -> str:
    resolved = shutil.which(name)
    if resolved:
        return resolved
    for entry in os.environ.get("PATH", "").split(os.pathsep):
        candidate = Path(entry).expanduser() / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    raise FileNotFoundError(f"required tool is not on PATH: {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "build" / "t5ai_boot_probe",
    )
    parser.add_argument("--tool-prefix", default="arm-none-eabi-")
    args = parser.parse_args()

    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    elf = output / "t5ai_boot_probe.elf"
    raw = output / "t5ai_boot_probe.bin"
    protected = output / "t5ai_boot_probe.t5ai-crc.bin"
    map_file = output / "t5ai_boot_probe.map"

    gcc = tool(f"{args.tool_prefix}gcc")
    objcopy = tool(f"{args.tool_prefix}objcopy")
    run([
        gcc,
        "-mcpu=cortex-m33",
        "-mthumb",
        "-nostdlib",
        "-nostartfiles",
        f"-Wl,-Map={map_file}",
        f"-Wl,-T,{PROBE_DIR / 'linker.ld'}",
        str(PROBE_DIR / "startup.S"),
        "-o",
        str(elf),
    ])
    run([objcopy, "-O", "binary", str(elf), str(raw)])

    encoded = encode_pebble_application(raw.read_bytes())
    protected.write_bytes(encoded)
    verify_protected_blocks(encoded)
    print(protected)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
