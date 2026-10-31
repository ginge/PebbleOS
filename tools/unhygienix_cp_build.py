#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build isolated Unhygienix CP off/on profiles; never writes a device."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/unhygienix/tuya/cp/variants"
SDK = ROOT / "third_party/hal_tuya/TuyaOpen-T5AI"
OPEN = ROOT / "third_party/hal_tuya/TuyaOpen"
LOCK = ROOT / "boards/unhygienix/tuya/sdk.lock.json"
CONTRACT = [
    0x55435031,
    16,
    20,
    4,
    12,
    0,
    1,
    2,
    2,
    20,
    22,
    30,
    91,
    93,
    101,
    122,
    9,
    12,
    38,
    4,
    5,
    6,
    64,
    66,
    78,
    75,
]
REQUIRED = {
    "get_info_item",
    "save_info_item",
    "unhygienix_cp_flash_wait",
    "unhygienix_cp_flash_launch",
    "unhygienix_cp_flash_fatal",
    "bk_bluetooth_init",
    "pebble_ble_init_bt_pll",
    "rwnx_cal_set_rfconfig",
    "tkl_hci_reset",
    "tuya_ipc_init",
    "tkl_sys_ipc_func",
    "tkl_hci_ipc_func",
    "mb_ipc_heartbeat_init",
    "mb_ipc_heartbeat_notify",
    "ipc_heartbeat_timeout",
    "lld_con_sched",
    "lld_con_data_tx",
    "lld_con_max_lat_calc",
    "lld_con_env",
}
WIFI = {"app_wifi_init", "wifi_init", "cif_init", "tcpip_init"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked(command, **kwargs):
    return subprocess.check_output([str(x) for x in command], **kwargs)


def dependencies():
    revisions = json.loads(LOCK.read_text())
    for path, key in [(SDK, "t5ai"), (OPEN, "tuyaopen")]:
        if (
            checked(["git", "-C", path, "rev-parse", "HEAD"], text=True).strip()
            != revisions[key]
        ):
            raise ValueError(f"{path.name}: revision mismatch")
        if checked(
            ["git", "-C", path, "status", "--porcelain", "--untracked-files=no"]
        ):
            raise ValueError(f"{path.name}: modified tracked vendor inputs")
    return revisions


def profile(wifi):
    text = (BOARD / f"{wifi}.config").read_text()
    values = dict(re.findall(r"^CONFIG_([A-Z0-9_]+)=([yn])$", text, re.M))
    if not values or values.get("WIFI_ENABLE") != ("y" if wifi == "on" else "n"):
        raise ValueError("Invalid CP feature profile")
    return values


def overlay(stage):
    for entry in json.loads((BOARD / "overlay.json").read_text()):
        relative = Path(entry["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Invalid overlay path")
        path = stage / relative
        raw = path.read_bytes()
        if digest(raw) != entry["vendor_sha256"]:
            raise ValueError(f"Overlay input changed: {relative}")
        text = raw.decode().replace("\r\n", "\n")
        for change in entry["replacements"]:
            if text.count(change["old"]) != change["count"]:
                raise ValueError(f"Overlay context changed: {relative}")
            text = text.replace(change["old"], change["new"])
        if b"\r\n" in raw:
            text = text.replace("\n", "\r\n")
        path.write_bytes(text.encode())
    target = stage / "t5_os/projects/tuya_app/cp"
    for source, name in [
        (BOARD / "contract.c", "unhygienix_cp_contract.c"),
        (BOARD / "flash_contract.S", "unhygienix_flash_contract.S"),
        (BOARD / "mbedtls.h", "unhygienix_mbedtls.h"),
        (BOARD / "build.cmake", "unhygienix.cmake"),
        (BOARD.parent / "ble_rf_init.c", "unhygienix_ble_rf_init.c"),
    ]:
        shutil.copy2(source, target / name)


def feature_config(path):
    return dict(re.findall(r"^(CONFIG_[A-Z0-9_]+)=(.*)$", Path(path).read_text(), re.M))


def inspect(elf_path, config_path, wifi):
    import struct
    from elftools.elf.elffile import ELFFile

    cfg = feature_config(config_path)
    for key, value in profile(wifi).items():
        if (cfg.get("CONFIG_" + key) == "y") != (value == "y"):
            raise ValueError(f"Effective CP config disagrees: {key}")
    for key in [
        "BLUETOOTH",
        "BTDM_CONTROLLER_ONLY",
        "BLUETOOTH_SUPPORT_IPC",
        "SLAVE_HEART_BEAT",
    ]:
        if cfg.get("CONFIG_" + key) != "y":
            raise ValueError(f"Required CP service disabled: {key}")
    with Path(elf_path).open("rb") as handle:
        elf = ELFFile(handle)
        symtab = elf.get_section_by_name(".symtab")
        defined = {
            s.name for s in symtab.iter_symbols() if s["st_shndx"] != "SHN_UNDEF"
        }
        missing = REQUIRED - defined
        if missing:
            raise ValueError(f"Missing CP service symbols: {sorted(missing)}")
        if wifi == "off":
            if (
                WIFI & defined
                or not {"bk_cal_if_init", "phy_wakeup_for_bluetooth"} <= defined
            ):
                raise ValueError("Wi-Fi exclusion or Bluetooth-only PHY check failed")
        elif not (WIFI - {"app_wifi_init"}) <= defined:
            raise ValueError("Wi-Fi-on control is missing its stack")
        for name in (
            "unhygienix_cp_flash_wait",
            "unhygienix_cp_flash_launch",
            "unhygienix_cp_flash_fatal",
        ):
            symbols = symtab.get_symbol_by_name(name) or []
            if len(symbols) != 1:
                raise ValueError("Missing or ambiguous flash contract symbol")
            section = elf.get_section(symbols[0]["st_shndx"])
            if (
                section.name != ".itcm"
                or not 0x20 <= symbols[0]["st_value"] & ~1 < 0x4000
            ):
                raise ValueError("CP flash launch/wait/fail-stop must execute in ITCM")
        phy = symtab.get_symbol_by_name("g_phy_os_funcs") or []
        if len(phy) != 1:
            raise ValueError("Missing or ambiguous PHY callback table")
        table = phy[0]
        section = elf.get_section(table["st_shndx"])
        offset = table["st_value"] - section["sh_addr"]
        for position, name in ((60, "save_info_item"), (64, "get_info_item")):
            functions = symtab.get_symbol_by_name(name) or []
            if (
                len(functions) != 1
                or struct.unpack_from("<I", section.data(), offset + position)[0]
                != functions[0]["st_value"]
            ):
                raise ValueError(
                    f"Missing or incorrect Bluetooth calibration callback: {name}"
                )
        contracts = symtab.get_symbol_by_name("unhygienix_cp_contract") or []
        if len(contracts) != 1:
            raise ValueError("Missing or ambiguous compiled CP ABI contract")
        symbol = contracts[0]
        section = elf.get_section(symbol["st_shndx"])
        offset = symbol["st_value"] - section["sh_addr"]
        data = section.data()[offset : offset + symbol["st_size"]]
        if (
            len(data) != 4 * len(CONTRACT)
            or list(struct.unpack("<26I", data)) != CONTRACT
        ):
            raise ValueError("Compiled CP mailbox/HCI/PM ABI mismatch")
    return {
        "wifi": wifi,
        "abi_contract": CONTRACT,
        "required_symbols": sorted(REQUIRED),
        "wifi_symbols_present": sorted(WIFI & defined),
        "bluetooth_only_calibration": "bk_cal_if_init" in defined,
    }


def inspect_libraries(output, wifi):
    link = (output / "CMakeFiles/app.elf.dir/link.txt").read_text()
    # bk_phy is also the adapter component; inspect proprietary archives only.
    libraries = {
        Path(token).name
        for token in shlex.split(link)
        if "/components/bk_libs/" in token and token.endswith(".a")
    }
    if wifi == "off" and any(
        name.startswith(("libwifi", "libble_wifi_exchange")) or name == "libbk_phy.a"
        for name in libraries
    ):
        raise ValueError("Wi-Fi-off link includes an excluded radio/network library")
    selected = "libcom_phy.a" if wifi == "off" else "libbk_phy.a"
    if selected not in libraries:
        raise ValueError("Unexpected selected PHY library")
    return selected


def canonical_link(work, compiler):
    output = work / "build/bk7258/tuya_app/bk7258"
    original = shlex.split((output / "CMakeFiles/app.elf.dir/link.txt").read_text())
    # Normalize staging paths and duplicate runtime groups from CMake reconfigure.
    prefix = str(work) + "/"
    relative = os.path.relpath(work, output) + "/"
    command = [token.replace(prefix, relative) for token in original]
    runtime = ["-lm", "-lgcc", "-lc", "-lnosys"]
    i = 0
    while i <= len(command) - 2 * len(runtime):
        if command[i : i + 8] == runtime * 2:
            del command[i + 4 : i + 8]
        else:
            i += 1
    if Path(command[0]).resolve() != Path(compiler).resolve():
        raise ValueError("CP link command uses a different compiler")
    with (work / "canonical-link.log").open("w") as log:
        subprocess.run(
            command, cwd=output, stdout=log, stderr=subprocess.STDOUT, check=True
        )
        subprocess.run(
            [
                str(Path(compiler).with_name("arm-none-eabi-objcopy")),
                "-O",
                "binary",
                "app.elf",
                "app.bin",
            ],
            cwd=output,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )
    (work / "canonical-link.json").write_text(json.dumps(command, indent=2) + "\n")


def build(work, wifi, compiler_root, python, cmake):
    revisions = dependencies()
    work, compiler_root, python, cmake = [
        Path(x).resolve() for x in [work, compiler_root, python, cmake]
    ]
    if work.is_relative_to(ROOT) or work.exists():
        raise ValueError("Use a new work directory outside the production tree")
    compiler = compiler_root / "bin/arm-none-eabi-gcc"
    version = checked([compiler, "--version"], text=True).splitlines()[0]
    if "10.3-2021.10" not in version:
        raise ValueError("The pinned GCC 10.3-2021.10 toolchain is required")
    cmake_version = checked([cmake, "--version"], text=True).splitlines()[0]
    if cmake_version != "cmake version 3.31.6":
        raise ValueError("This recipe is qualified with CMake 3.31.6")
    work.mkdir(parents=True)
    stage = work / "sdk"
    stage.mkdir()
    archive = subprocess.Popen(
        ["git", "-C", str(SDK), "archive", revisions["t5ai"]], stdout=subprocess.PIPE
    )
    with tarfile.open(fileobj=archive.stdout, mode="r|") as files:
        files.extractall(stage, filter="data")
    archive.stdout.close()
    if archive.wait():
        raise ValueError("Pinned SDK archive failed")
    overlay(stage)
    config = stage / "t5_os/projects/tuya_app/cp/config/bk7258/config"
    values = profile(wifi)
    lines = [
        line
        for line in config.read_text().splitlines()
        if not any(line.startswith("CONFIG_" + key + "=") for key in values)
    ]
    config.write_text(
        "\n".join(lines)
        + "\n"
        + "\n".join("CONFIG_" + key + "=" + value for key, value in values.items())
        + "\n"
    )
    generated = work / "tuyaopen"
    generated.mkdir()
    params = {
        "OPEN_VERSION": "v1.9.0",
        "OPEN_COMMIT": revisions["tuyaopen"],
        "PLATFORM_NAME": "T5AI",
        "PLATFORM_COMMIT": revisions["t5ai"],
        "PLATFORM_CHIP": "T5AI",
        "PLATFORM_BOARD": "WAVESHARE_T5AI_TOUCH_AMOLED_1_75",
    }
    subprocess.run(
        [
            str(python),
            str(OPEN / "tools/kconfiglib/conf2h.py"),
            "-c",
            str(BOARD / "tuyaopen.config"),
            "-i",
            str(OPEN / "tools/kconfiglib/config.h.in"),
            "-o",
            str(generated / "tuya_kconfig.h"),
            "-p",
            "***".join(k + ":::" + v for k, v in params.items()),
        ],
        check=True,
    )
    host = work / "host-bin"
    host.mkdir()
    for name in ["python", "python3"]:
        (host / name).symlink_to(python)
    env = os.environ.copy()
    env.update(
        TUYA_TOOLCHAIN_PATH=str(compiler_root),
        python=str(python),
        UNHYGIENIX_CP_WIFI=wifi,
        UNHYGIENIX_CP_STAGE_ROOT=str(stage),
        SOURCE_DATE_EPOCH=checked(
            ["git", "-C", SDK, "show", "-s", "--format=%ct", revisions["t5ai"]],
            text=True,
        ).strip(),
        TUYA_HEADER_DIR=" ".join(
            str(x)
            for x in [
                generated,
                OPEN / "src/common/include",
                stage / "tuyaos/tuyaos_adapter/include",
            ]
        ),
    )
    env["PATH"] = os.pathsep.join(
        [str(host), str(cmake.parent), str(compiler.parent), env.get("PATH", "")]
    )
    command = [
        "make",
        "-C",
        str(stage / "t5_os"),
        "bk7258_cp",
        "PROJECT=tuya_app",
        "BUILD_DIR=" + str(work / "build"),
        "APP_VERSION=1.0.0",
    ]
    inputs = {
        str(p.relative_to(ROOT)): digest(p.read_bytes())
        for p in sorted(BOARD.iterdir())
        if p.is_file()
    }
    inputs[str((BOARD.parent / "ble_rf_init.c").relative_to(ROOT))] = digest(
        (BOARD.parent / "ble_rf_init.c").read_bytes()
    )
    provenance = {
        "schema": 1,
        "board": "unhygienix",
        "wifi": wifi,
        "vendor_revisions": revisions,
        "board_inputs": inputs,
        "recipe_sha256": digest(Path(__file__).read_bytes()),
        "compiler": version,
        "compiler_sha256": digest(compiler.read_bytes()),
        "cmake": cmake_version,
        "source_date_epoch": env["SOURCE_DATE_EPOCH"],
        "command": command,
        "flash_qualified": False,
    }
    (work / "inputs.json").write_text(json.dumps(provenance, indent=2) + "\n")
    with (work / "build.log").open("w") as log:
        subprocess.run(
            command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True
        )
    canonical_link(work, compiler)
    output = work / "build/bk7258/tuya_app/bk7258"
    report = inspect(output / "app.elf", output / "sdkconfig", wifi)
    report["selected_phy"] = inspect_libraries(output, wifi)
    report["artifacts"] = {
        name: digest((output / name).read_bytes())
        for name in ["app.elf", "app.bin", "app.map", "sdkconfig"]
    }
    report["flash_qualified"] = False
    (work / "inspection.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--wifi", choices=["off", "on"], default="off")
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--python", type=Path, default=Path(sys.executable))
    parser.add_argument("--cmake", type=Path, required=True)
    args = parser.parse_args()
    try:
        build(args.work, args.wifi, args.toolchain, args.python, args.cmake)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"CP build rejected: {error}\n")


if __name__ == "__main__":
    main()
