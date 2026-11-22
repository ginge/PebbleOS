# SPDX-License-Identifier: Apache-2.0
"""Complete flash, SARADC and MIPC units with real headers and modeled CP/kernel endpoints."""

import subprocess
import tempfile
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]


def run_init(checks, flash_source=ROOT / "drivers/flash/t5ai_flash_ipc.c"):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        config = load_config("unhygienix")
        config.syms["LOG_HASHED"].set_value("n")
        config.write_autoconf(str(path / "config.h"))
        model = (ROOT / "tools/tests/fixtures/t5ai_ipc_init/model.c").read_text()
        model = model.replace("FLASH_SOURCE", f'"{flash_source}"')
        (path / "test.c").write_text(
            model + "\nint main(void) {\n" + checks + "\nreturn 0; }\n"
        )
        includes = [
            ROOT,
            ROOT / "include",
            ROOT / "include/pbl",
            ROOT / "pebble",
            ROOT / "kernel/arch/posix/include",
        ]
        subprocess.run(
            [
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
                *[arg for include in includes for arg in ("-I", str(include))],
                str(path / "test.c"),
                str(ROOT / "kernel/tick.c"),
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True, timeout=30)
