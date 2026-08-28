#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the pinned Tuya RTOS with MPU off/on in an isolated source copy.

This is a compiler compatibility check, not a PebbleOS firmware build or proof
of task isolation. The original SDK and its generated build are never modified.
"""

import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

from build_tuya_hardware import SOURCE, verify_sdk

KERNEL = Path("t5_os/ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel")
CONFIG = Path("t5_os/ap/components/bk_rtos/freertos/FreeRTOSConfig.h")
BUILD = Path("t5_os/build/bk7258/tuya_app/bk7258_ap")
PATCH = "0001-fix-mpu-kernel-compilation.patch"
EXPECTED_SOURCES = {
    "croutine.c",
    "event_groups.c",
    "list.c",
    "queue.c",
    "tasks.c",
    "stream_buffer.c",
    "timers.c",
    "portable/MemMang/heap_6.c",
    "portable/GCC/ARM_CM33_NTZ/non_secure/port.c",
    "portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c",
}


def make_config(text, enabled):
    text, count = re.subn(
        r"(?m)^#define configENABLE_MPU\s+0\s*$",
        f"#define configENABLE_MPU {enabled}\n#define configTOTAL_MPU_REGIONS 16",
        text,
    )
    if count != 1:
        raise ValueError("Unexpected vendor MPU configuration; re-audit the SDK")
    return text


def compile_command(entry, source, output, include_dirs):
    # Never execute a compilation database's command via a shell.
    args = entry.get("arguments") or shlex.split(entry["command"])
    args = list(args)
    if "-c" not in args or "-o" not in args:
        raise ValueError("Expected a single compiler invocation with -c and -o")
    if args.count("-c") != 1 or args.count("-o") != 1:
        raise ValueError("Ambiguous compiler command")
    args[args.index("-c") + 1] = str(source)
    args[args.index("-o") + 1] = str(output)
    for flag, suffix in (("-MF", ".d"), ("-MT", ".o"), ("-MQ", ".o")):
        if flag in args:
            args[args.index(flag) + 1] = str(output.with_suffix(suffix))
    args[1:1] = [f"-I{path}" for path in include_dirs]
    return args


def prepare_output(output):
    marker = output / ".pebble-tuya-kernel-check"
    if output.exists() and not marker.is_file():
        raise ValueError(f"Refusing to overwrite unowned directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    marker.write_text("Compiler check only; not a firmware image.\n")


def check(sdk, output):
    verify_sdk(sdk, json.loads((SOURCE / "sdk.lock.json").read_text()))
    if output.is_relative_to(sdk) or sdk.is_relative_to(output):
        raise ValueError("Kernel-check output must not overlap the SDK")
    platform = sdk / "platform/T5AI"
    pristine = subprocess.run(
        ["git", "diff", "--quiet", "HEAD", "--", str(KERNEL), str(CONFIG)],
        cwd=platform,
        check=False,
    )
    if pristine.returncode:
        raise ValueError("SDK kernel/config differs from the pinned revision")
    database = platform / BUILD / "compile_commands.json"
    if not database.is_file():
        raise ValueError(
            "Build the bring-up app first to generate the SDK compilation database"
        )
    entries = []
    for entry in json.loads(database.read_text()):
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        try:
            relative = source.resolve().relative_to(platform / KERNEL)
        except ValueError:
            continue
        entries.append((entry, relative))
    if (
        len(entries) != len(EXPECTED_SOURCES)
        or {str(path) for _, path in entries} != EXPECTED_SOURCES
    ):
        raise ValueError(
            "Unexpected RTOS source selection; re-audit the generated build"
        )
    prepare_output(output)
    (output / "report.json").unlink(missing_ok=True)
    shadow = output / "platform"
    shutil.copytree(platform / KERNEL, shadow / KERNEL, dirs_exist_ok=True)
    patch = SOURCE / "patches" / PATCH
    # Copying pristine files above makes repeated checks independent of old patches.
    subprocess.run(["git", "apply", "--check", str(patch)], cwd=shadow, check=True)
    subprocess.run(["git", "apply", str(patch)], cwd=shadow, check=True)
    records = []
    for enabled in (0, 1):
        build = output / f"mpu{enabled}"
        build.mkdir(exist_ok=True)
        (build / "FreeRTOSConfig.h").write_text(
            make_config((platform / CONFIG).read_text(), enabled)
        )
        includes = [build, shadow / KERNEL / "include/freertos"]
        for entry, relative in entries:
            obj = build / str(relative).replace("/", "_").replace(".c", ".o")
            obj.unlink(missing_ok=True)
            command = compile_command(entry, shadow / KERNEL / relative, obj, includes)
            result = subprocess.run(
                command,
                cwd=entry["directory"],
                capture_output=True,
                text=True,
                check=False,
            )
            obj.with_suffix(".log").write_text(result.stdout + result.stderr)
            records.append(
                {
                    "mpu": enabled,
                    "source": str(relative),
                    "exit_code": result.returncode,
                }
            )
            print(
                f"MPU={enabled} {relative}: {'PASS' if result.returncode == 0 else 'FAIL'}"
            )
    report = {"firmware": False, "hardware_tested": False, "compilation": records}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return all(record["exit_code"] == 0 for record in records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        return 0 if check(args.sdk.resolve(), args.out.resolve()) else 1
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Kernel compatibility check failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
