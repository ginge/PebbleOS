#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the pinned Tuya RTOS and Pebble SVC bridge in an isolated source copy.

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

from build_tuya_hardware import ROOT, SOURCE, verify_sdk

KERNEL = Path("t5_os/ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel")
CONFIG = Path("t5_os/ap/components/bk_rtos/freertos/FreeRTOSConfig.h")
BUILD = Path("t5_os/build/bk7258/tuya_app/bk7258_ap")
PATCHES = (
    "0001-fix-mpu-kernel-compilation.patch",
    "0002-pebble-svc-and-restricted-task-affinity.patch",
    "0003-pebble-task-memory.patch",
)
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


def make_config(text, enabled, pebble=False):
    text, count = re.subn(
        r"(?m)^#define configENABLE_MPU\s+0\s*$",
        f"#define configENABLE_MPU {enabled}\n#define configTOTAL_MPU_REGIONS 16",
        text,
    )
    if count != 1:
        raise ValueError("Unexpected vendor MPU configuration; re-audit the SDK")
    if pebble:
        if enabled != 1:
            raise ValueError("Pebble requires MPU support")
        text, count = re.subn(
            r"(?m)^#define configNUM_THREAD_LOCAL_STORAGE_POINTERS[ \t]+3[ \t]*$",
            "#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5",
            text,
        )
        if count != 2:
            raise ValueError("Unexpected vendor TLS configuration; re-audit the SDK")
        text += (
            "\n#define configPEBBLE_SYSCALL_BRIDGE 1\n#define configPEBBLE_CORE_ID 1\n"
        )
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
    for name in PATCHES:
        patch = SOURCE / "patches" / name
        subprocess.run(["git", "apply", "--check", str(patch)], cwd=shadow, check=True)
        subprocess.run(["git", "apply", str(patch)], cwd=shadow, check=True)
    records = []
    links = []
    for mode, enabled, pebble in (
        ("mpu0", 0, False),
        ("mpu1", 1, False),
        ("pebble", 1, True),
    ):
        build = output / mode
        build.mkdir(exist_ok=True)
        (build / "FreeRTOSConfig.h").write_text(
            make_config((platform / CONFIG).read_text(), enabled, pebble)
        )
        includes = [
            build,
            shadow / KERNEL / "include/freertos",
            ROOT / "include",
            ROOT / "src/fw",
        ]
        objects = []
        (build / "kernel-bridge.rel.o").unlink(missing_ok=True)
        selected = [
            (entry, relative, shadow / KERNEL / relative) for entry, relative in entries
        ]
        if pebble:
            includes += [ROOT / "include", SOURCE / "kernel"]
            port_entry = next(
                entry for entry, relative in entries if relative.name == "port.c"
            )
            selected += [
                (port_entry, Path(name), SOURCE / "kernel" / name)
                for name in (
                    "svc_bridge.c",
                    "rtos_bridge.c",
                    "task_memory.c",
                    "task_memory_pebble.c",
                )
            ]
        port_entry = next(
            entry for entry, relative in entries if relative.name == "port.c"
        )
        probe = build / "syscall_abi_probe.c"
        probe.write_text(
            '#include "syscall/syscall_internal.h"\n'
            f'_Static_assert(PBL_SYSCALL_SVC == {64 if pebble else 2}, "SVC ABI");\n'
            f'_Static_assert(PBL_SYSCALL_TLS_SP == {4 if pebble else 1}, "TLS ABI");\n'
            "DEFINE_SYSCALL(int, pebble_abi_probe, int value) { return value; }\n"
        )
        selected.append((port_entry, Path(probe.name), probe))
        for entry, relative, source in selected:
            obj = build / str(relative).replace("/", "_").replace(".c", ".o")
            obj.unlink(missing_ok=True)
            objects.append(obj)
            command = compile_command(entry, source, obj, includes)
            if pebble:
                command.insert(1, "-DPBL_RTOS_TUYA=1")
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
                    "mode": mode,
                    "source": str(relative),
                    "exit_code": result.returncode,
                }
            )
            print(f"{mode} {relative}: {'PASS' if result.returncode == 0 else 'FAIL'}")
        # A relocatable link checks duplicate symbols and bridge resolution only.
        # It intentionally retains unresolved Pebble service and vendor symbols.
        if pebble and all(
            record["exit_code"] == 0 for record in records if record["mode"] == mode
        ):
            compiler = Path(command[0])
            linker = compiler.with_name(compiler.name.replace("gcc", "ld"))
            nm = compiler.with_name(compiler.name.replace("gcc", "nm"))
            linked = build / "kernel-bridge.rel.o"
            linked.unlink(missing_ok=True)
            result = subprocess.run(
                [str(linker), "-r", "-o", str(linked), *map(str, objects)],
                check=False,
                capture_output=True,
                text=True,
            )
            (build / "partial-link.log").write_text(result.stdout + result.stderr)
            unresolved = []
            if result.returncode == 0:
                symbols = subprocess.check_output(
                    [str(nm), "-u", str(linked)], text=True
                )
                unresolved = [
                    name
                    for name in (
                        "pebble_tuya_svc_dispatch",
                        "tuya_svc_dispatch",
                        "xTaskCreateRestrictedPinnedToCore",
                    )
                    if name in symbols
                ]
            links.append(
                {
                    "mode": mode,
                    "exit_code": result.returncode,
                    "unresolved_bridge_symbols": unresolved,
                }
            )
            print(
                f"{mode} partial link: {'PASS' if result.returncode == 0 and not unresolved else 'FAIL'}"
            )
    report = {
        "firmware": False,
        "hardware_tested": False,
        "compilation": records,
        "partial_links": links,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return (
        all(record["exit_code"] == 0 for record in records)
        and len(links) == 1
        and all(
            item["exit_code"] == 0 and not item["unresolved_bridge_symbols"]
            for item in links
        )
    )


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
