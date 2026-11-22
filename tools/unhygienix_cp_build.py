#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build isolated Unhygienix CP off/on profiles; never writes a device."""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/unhygienix/tuya/cp/variants"
SDK = ROOT / "third_party/hal_tuya/TuyaOpen-T5AI"
OPEN = ROOT / "third_party/hal_tuya/TuyaOpen"
LOCK = ROOT / "boards/unhygienix/tuya/sdk.lock.json"
PROTOCOL = ROOT / "include/pbl/soc/t5ai/sleep_protocol.h"


def contract_fields(path=BOARD / "contract.def"):
    """Read the same ordered probe definition included by the vendor C unit."""
    fields = {}
    for line in path.read_text().splitlines():
        if line.startswith(("/*", "//")) or not line.strip():
            continue
        match = re.fullmatch(
            r"CP_CONTRACT\(([a-z][a-z0-9_]*), (0x[0-9a-f]+), (.+)\)", line
        )
        if not match or match[1] in fields:
            raise ValueError("Invalid or duplicate CP contract field")
        fields[match[1]] = int(match[2], 16)
    if not fields:
        raise ValueError("Empty CP contract")
    return fields


def native_alarm_contract(path=PROTOCOL):
    """Use the scalar definitions shared by AP and CP, without a second copy."""
    text = path.read_text()
    values = []
    for field in ("ABI", "COMMAND", "WAKE", "MIN_TICKS", "MAX_TICKS"):
        matches = re.findall(
            rf"^#define T5AI_PM_NATIVE_ALARM_{field}\s+(0x[0-9a-fA-F]+|[0-9]+)u$",
            text,
            re.MULTILINE,
        )
        if len(matches) != 1:
            raise ValueError(f"Missing or ambiguous native alarm field: {field}")
        values.append(int(matches[0], 0))
    return values


# Keep the recipe attributes used by the package verifier, derived from the header.
(
    NATIVE_ALARM_ABI,
    NATIVE_ALARM_COMMAND,
    NATIVE_ALARM_WAKE,
    NATIVE_ALARM_MIN_TICKS,
    NATIVE_ALARM_MAX_TICKS,
) = native_alarm_contract()

CONTRACT_FIELDS = contract_fields()
# Preserve the existing manifest's ordered array representation.
CONTRACT = list(CONTRACT_FIELDS.values())
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
    values = dict(re.findall(r"^CONFIG_([A-Z0-9_]+)=([yn])$", text, re.MULTILINE))
    if not values or values.get("WIFI_ENABLE") != ("y" if wifi == "on" else "n"):
        raise ValueError("Invalid CP feature profile")
    return values


def load_overlay(name):
    """Expand readable replacement fragments, preserving the pinned recipe order."""
    entries = json.loads((BOARD / name).read_text())
    for entry in entries:
        for i, change in enumerate(entry["replacements"]):
            if "new_file" not in change:
                continue
            if "new" in change:
                raise ValueError("Ambiguous overlay replacement")
            relative = Path(change["new_file"])
            path = BOARD / relative
            if (
                relative.is_absolute()
                or ".." in relative.parts
                or not path.resolve().is_relative_to(BOARD.resolve())
            ):
                raise ValueError("Invalid overlay fragment path")
            replacement = path.read_bytes().decode("utf-8")
            entry["replacements"][i] = dict(
                ("new", replacement) if key == "new_file" else (key, value)
                for key, value in change.items()
            )
    return entries


def overlay_digest(name):
    # Keep existing reviewed pins over the original, fully expanded recipe.
    return digest((json.dumps(load_overlay(name), indent=2) + "\n").encode())


def board_inputs():
    return {
        str(path.relative_to(ROOT)): digest(path.read_bytes())
        for path in sorted(BOARD.rglob("*"))
        if path.is_file()
    }


def overlay(stage, sleep_contract=False):
    entries = load_overlay("overlay.json")
    if sleep_contract:
        entries += load_overlay("sleep-overlay.json")
    for entry in entries:
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
        (BOARD / "contract.def", "unhygienix_cp_contract.def"),
        (BOARD / "flash_contract.S", "unhygienix_flash_contract.S"),
        (BOARD / "mbedtls.h", "unhygienix_mbedtls.h"),
        (BOARD / "build.cmake", "unhygienix.cmake"),
        (BOARD.parent / "ble_rf_init.c", "unhygienix_ble_rf_init.c"),
    ]:
        shutil.copy2(source, target / name)

    if sleep_contract:
        shutil.copy2(
            BOARD.parent / "native_alarm.c",
            stage / "t5_os/cp/middleware/driver/pwr_clk/unhygienix_native_alarm.c",
        )
        shutil.copy2(
            ROOT / "include/pbl/soc/t5ai/sleep_protocol.h",
            stage / "t5_os/cp/include/driver/unhygienix_sleep_protocol.h",
        )
        with (target / "unhygienix.cmake").open("a") as handle:
            handle.write(
                '\ntarget_link_libraries(${CMAKE_PROJECT_NAME}.elf "-Wl,--undefined=unhygienix_native_alarm_contract")\n'
            )


def feature_config(path):
    return dict(
        re.findall(r"^(CONFIG_[A-Z0-9_]+)=(.*)$", Path(path).read_text(), re.MULTILINE)
    )


CP_FLASH_XIP_BASE = 0x02000000
CP_FLASH_XIP_LIMIT = 0x03000000
NATIVE_WAKE_ITCM_ROOTS = (
    "unhygienix_native_sleep_wait",
    "unhygienix_native_flash_ready",
    "unhygienix_native_flash_resume",
)


def audit_itcm_calls(
    symbols, low, high, disassemble, roots=NATIVE_WAKE_ITCM_ROOTS, ranges=None
):
    """Reject XIP calls, veneers and unresolved indirect calls before flash is ready."""
    pending = list(roots)
    checked = set()
    while pending:
        name = pending.pop()
        if name in checked:
            continue
        if name not in symbols or not low <= symbols[name] < high:
            raise ValueError(f"Native wake function is outside ITCM: {name}")
        assembly = disassemble(name)
        if not re.search(r"^\s*[0-9a-fA-F]+:\s+\S", assembly, re.MULTILINE):
            raise ValueError(f"Empty native wake disassembly: {name}")
        if re.search(r"\b(?:tbb|tbh)\b", assembly):
            raise ValueError(f"Indirect native wake branch: {name}")
        if re.search(r"\b(?:blx|bx)\s+(?:r[0-9]+|ip)\b", assembly):
            raise ValueError(f"Indirect native wake branch: {name}")
        for literal in re.findall(r"\.word\s+0x([0-9a-fA-F]+)", assembly):
            if CP_FLASH_XIP_BASE <= int(literal, 16) < CP_FLASH_XIP_LIMIT:
                raise ValueError(f"XIP literal in native wake closure: {name}")
        checked.add(name)
        for address, callee in re.findall(
            r"\b(?:b[a-z]{0,3}(?:\.w|\.n)?|cbn?z)\s+(?:r[0-9]+,\s*)?([0-9a-fA-F]+)\s+<([^>+]+)",
            assembly,
        ):
            if not low <= int(address, 16) < high:
                raise ValueError(f"Native wake branch leaves ITCM: {name} -> {callee}")
            if ranges is not None:
                target = int(address, 16)
                start, end = ranges[name]
                if start <= target < end:
                    continue
                callee = next(
                    (
                        owner
                        for owner, (start, end) in ranges.items()
                        if start <= target < end
                    ),
                    None,
                )
                if callee is None:
                    raise ValueError(f"Native wake target has no function: {target:#x}")
            if callee != name:
                pending.append(callee)
    return sorted(checked)


def audit_masked_wfi_leaf(assembly):
    instructions = re.findall(
        r"^\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{4}\s+){1,2}([a-z][a-z0-9.]*)[ \t]*([^\n]*)",
        assembly,
        re.MULTILINE,
    )
    instructions = [
        (op, re.sub(r"\s+", "", operands).split(";")[0])
        for op, operands in instructions
    ]
    expected = [
        ("str", "r1,[r0,#0]"),
        ("dsb", "sy"),
        ("wfi", ""),
        ("isb", "sy"),
        ("bx", "lr"),
    ]
    if instructions != expected:
        raise ValueError("Masked WFI leaf must store the mask, DSB, WFI, ISB, return")
    return [op for op, _ in instructions]


def inspect_native_wake_itcm(elf_path, objdump):
    from elftools.elf.elffile import ELFFile

    with Path(elf_path).open("rb") as handle:
        elf = ELFFile(handle)
        section = elf.get_section_by_name(".itcm")
        if section is None:
            raise ValueError("Missing native wake ITCM section")
        low, high = section["sh_addr"], section["sh_addr"] + section["sh_size"]
        functions = [
            s
            for s in elf.get_section_by_name(".symtab").iter_symbols()
            if s["st_info"]["type"] == "STT_FUNC"
            and s["st_shndx"] != "SHN_UNDEF"
            and low <= s["st_value"] & ~1 < high
        ]
        symbols = {s.name: s["st_value"] & ~1 for s in functions}
        ranges = {}
        for function in functions:
            start = symbols[function.name]
            # Hand-written assembly may omit .size; bound it by the next function.
            end = (
                start + function["st_size"]
                if function["st_size"]
                else min(
                    [address for address in symbols.values() if address > start]
                    + [high]
                )
            )
            if end > high:
                raise ValueError(
                    f"Native wake function extends outside ITCM: {function.name}"
                )
            ranges[function.name] = (start, end)

    def disassemble(name):
        start, end = ranges[name]
        return subprocess.check_output(
            [
                str(objdump),
                "-d",
                "-j",
                ".itcm",
                f"--start-address={start}",
                f"--stop-address={end}",
                str(elf_path),
            ],
            text=True,
        )

    checked = audit_itcm_calls(symbols, low, high, disassemble, ranges=ranges)
    leaf = "unhygienix_cp_masked_wfi"
    if leaf not in ranges:
        raise ValueError("Missing stack-free masked WFI leaf")
    audit_masked_wfi_leaf(disassemble(leaf))
    return {
        "elf_sha256": digest(Path(elf_path).read_bytes()),
        "itcm_start": low,
        "itcm_end": high,
        "functions": checked,
    }


def inspect(elf_path, config_path, wifi, sleep_contract=False):
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
        if sleep_contract:
            for name in [
                "AON_RTC",
                "AON_RTC_64BIT",
                "AON_RTC_MANY_USERS",
                "AON_WDT",
                "NMI_WDT_EN",
            ]:
                if cfg.get("CONFIG_" + name) != "y":
                    raise ValueError(
                        "Native alarm requires pinned RTC and watchdog services"
                    )
            matches = (
                symtab.get_symbol_by_name("unhygienix_native_alarm_contract") or []
            )
            if len(matches) != 1 or matches[0]["st_size"] != 20:
                raise ValueError("Missing native alarm ABI contract")
            item = matches[0]
            section = elf.get_section(item["st_shndx"])
            offset = item["st_value"] - section["sh_addr"]
            if (
                list(struct.unpack("<5I", section.data()[offset : offset + 20]))
                != native_alarm_contract()
            ):
                raise ValueError("Native alarm ABI contract changed")
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
        verify_contract(data)
    return {
        "wifi": wifi,
        "abi_contract": CONTRACT,
        "required_symbols": sorted(REQUIRED),
        "wifi_symbols_present": sorted(WIFI & defined),
        "bluetooth_only_calibration": "bk_cal_if_init" in defined,
    }


def verify_contract(data):
    if len(data) != 4 * len(CONTRACT_FIELDS):
        raise ValueError("Compiled CP mailbox/HCI/PM ABI mismatch: size")
    import struct

    actual = struct.unpack(f"<{len(CONTRACT_FIELDS)}I", data)
    for (name, expected), value in zip(CONTRACT_FIELDS.items(), actual, strict=True):
        if value != expected:
            raise ValueError(f"Compiled CP mailbox/HCI/PM ABI mismatch: {name}")


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


def build(work, wifi, compiler_root, python, cmake, sleep_contract=False):
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
    overlay(stage, sleep_contract)
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
    inputs = board_inputs()
    inputs[str((BOARD.parent / "ble_rf_init.c").relative_to(ROOT))] = digest(
        (BOARD.parent / "ble_rf_init.c").read_bytes()
    )
    if sleep_contract:
        for path in [
            BOARD.parent / "native_alarm.c",
            ROOT / "include/pbl/soc/t5ai/sleep_protocol.h",
        ]:
            inputs[str(path.relative_to(ROOT))] = digest(path.read_bytes())
    provenance = {
        "schema": 1,
        "board": "unhygienix",
        "wifi": wifi,
        "sleep_contract": sleep_contract,
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
    report = inspect(output / "app.elf", output / "sdkconfig", wifi, sleep_contract)
    if sleep_contract:
        report["native_wake_itcm"] = inspect_native_wake_itcm(
            output / "app.elf", compiler.with_name("arm-none-eabi-objdump")
        )
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
    parser.add_argument(
        "--sleep-contract",
        action="store_true",
        help="Add the isolated native wake-alarm protocol; never enables sleep",
    )
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--wifi", choices=["off", "on"], default="off")
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--python", type=Path, default=Path(sys.executable))
    parser.add_argument("--cmake", type=Path, required=True)
    args = parser.parse_args()
    try:
        build(
            args.work,
            args.wifi,
            args.toolchain,
            args.python,
            args.cmake,
            args.sleep_contract,
        )
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"CP build rejected: {error}\n")


if __name__ == "__main__":
    main()
