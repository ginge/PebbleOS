# SPDX-License-Identifier: Apache-2.0
"""Complete retained/bootbits/reboot units with real types and modeled ARM/cache endpoints."""

import subprocess
import tempfile
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]


def run_retained(checks, source=ROOT / "soc/t5ai/retained.c", expected=0):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        config = load_config("unhygienix")
        config.syms["LOG_HASHED"].set_value("n")
        config.write_autoconf(str(path / "config.h"))
        stub = path / "pbl/soc/t5ai/core.h"
        stub.parent.mkdir(parents=True)
        stub.write_bytes(
            (ROOT / "tools/tests/fixtures/t5ai_retained/core.h").read_bytes()
        )
        (path / "pbl_arch_cache.h").write_bytes(
            (ROOT / "tools/tests/fixtures/t5ai_retained/cache.h").read_bytes()
        )
        model = (ROOT / "tools/tests/fixtures/t5ai_retained/model.c").read_text()
        model = model.replace("RETAINED_SOURCE", f'"{source}"')
        (path / "test.c").write_text(
            model + "\nint main(void) {\n" + checks + "\nreturn 0; }\n"
        )
        command = [
            "clang-21",
            "-std=gnu23",
            "-O1",
            "-g",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-Wno-unused-variable",
            "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all",
            "-fno-pie",
            "-no-pie",
            "-ffunction-sections",
            "-fdata-sections",
            "-Wl,--gc-sections",
            "-include",
            str(path / "config.h"),
        ]
        for include in (
            path,
            ROOT,
            ROOT / "include",
            ROOT / "include/pbl",
            ROOT / "pebble",
            ROOT / "subsys",
            ROOT / "lib",
            ROOT / "kernel/arch/posix/include",
        ):
            command.extend(("-I", str(include)))
        subprocess.run(
            [*command, str(path / "test.c"), "-o", str(path / "test")], check=True
        )
        result = subprocess.run(
            [str(path / "test")], check=False, stdout=subprocess.PIPE, timeout=10
        )
        assert result.returncode == expected, result.returncode
        return result.stdout
