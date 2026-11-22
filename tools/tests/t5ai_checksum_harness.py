# SPDX-License-Identifier: Apache-2.0
"""Complete 32-bit AP/vendor checksum units with actual headers and configuration."""

import json
import re
import shlex
import subprocess
from pathlib import Path

from tools import unhygienix_cp_build as cp
from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]


def build_checksums(output, vendor_flags, sanitize=True):
    output = Path(output)
    revisions = cp.dependencies()
    flags_text = Path(vendor_flags).read_text()
    includes = shlex.split(
        re.search(r"^C_INCLUDES = (.*)$", flags_text, re.MULTILINE)[1]
    )
    # Use pinned production SDK headers; retain only the generated CP config path.
    includes = [
        "-I" + str(cp.SDK / token.split("/sdk/", 1)[1])
        if token.startswith("-I") and "/sdk/" in token
        else token
        for token in includes
    ]
    common = [
        "clang-21",
        "-m32",
        "-O2",
        "-ffunction-sections",
        "-fdata-sections",
        "-fvisibility=hidden",
    ]
    if sanitize:
        common += [
            "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all",
            "-fno-pie",
        ]
    commands = []
    objects = []
    for name, relative, call in (
        ("vendor_flash", "middleware/driver/flash/flash_server.c", "calc_crc32(0,p,n)"),
        (
            "vendor_saradc",
            "middleware/driver/saradc/saradc_server.c",
            "calc_crc32(0,p,n)",
        ),
        ("vendor_mipc", "components/bk_common/bk_crc.c", "hnd_crc8(p,n,seed)"),
    ):
        path = cp.SDK / "t5_os/cp" / relative
        prototype = (
            f"uint8_t {name}(uint8_t *p, size_t n, uint8_t seed)"
            if name == "vendor_mipc"
            else f"uint32_t {name}(const void *p, size_t n)"
        )
        source = output / f"{name}.c"
        source.write_text(
            f'#include <stddef.h>\n#include "{path}"\n'
            + prototype
            + " { return "
            + call
            + "; }\n"
        )
        obj = output / f"{name}.o"
        command = [
            *common,
            "-std=gnu11",
            "-Wno-error=implicit-function-declaration",
            *includes,
            "-c",
            str(source),
            "-o",
            str(obj),
        ]
        subprocess.run(command, check=True)
        commands.append(command)
        objects.append(obj)
    config = load_config("unhygienix")
    config.syms["LOG_HASHED"].set_value("n")
    assert config.syms["CRC_HW_ACCEL"].str_value == "n"
    config.write_autoconf(str(output / "config.h"))
    ap = [*common, "-std=gnu23", "-include", str(output / "config.h")]
    for path in (
        ROOT,
        ROOT / "include",
        ROOT / "include/pbl",
        ROOT / "pebble",
        ROOT / "subsys",
        ROOT / "lib",
        ROOT / "kernel/arch/posix/include",
    ):
        ap += ["-I", str(path)]
    for name, relative, function in (
        ("ap_flash", "drivers/flash/t5ai_flash_ipc.c", "prv_crc32"),
        ("ap_saradc", "drivers/t5ai/saradc_ipc.c", "prv_saradc_crc32"),
    ):
        source = output / f"{name}.c"
        source.write_text(
            f'#include "{relative}"\nuint32_t {name}(const void *p, size_t n)'
            + " { return "
            + function
            + "(p,n); }\n"
        )
        obj = output / f"{name}.o"
        command = [*ap, "-c", str(source), "-o", str(obj)]
        subprocess.run(command, check=True)
        commands.append(command)
        objects.append(obj)
    if sanitize:
        ap += ["-no-pie"]
    binary = output / "checksums"
    command = [
        *ap,
        str(ROOT / "tools/tests/fixtures/t5ai_checksums.c"),
        str(ROOT / "subsys/crc/crc32.c"),
        str(ROOT / "drivers/t5ai/mipc.c"),
        *map(str, objects),
        "-Wl,--gc-sections",
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True)
    commands.append(command)
    (output / "vendor-revisions.json").write_text(
        json.dumps(revisions, indent=2) + "\n"
    )
    (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    (output / "vendor-flags.make").write_text(flags_text)
    return binary
