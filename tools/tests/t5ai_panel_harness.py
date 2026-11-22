# SPDX-License-Identifier: Apache-2.0
"""Compile the complete CO5300 unit with production types and board configuration."""

import subprocess
import tempfile
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]


def run_panel(prefix, main, defines=()):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        config = load_config("unhygienix")
        config.syms["T5AI_AMOLED_SLEEP_ON_DISABLE"].set_value("n")
        config.write_autoconf(str(path / "config.h"))
        (path / "test.c").write_text(
            prefix + '\n#include "drivers/panel/co5300.c"\n' + main
        )
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
                "-I",
                str(ROOT),
                "-I",
                str(ROOT / "include"),
                "-I",
                str(ROOT / "kernel/arch/posix/include"),
                "-include",
                str(path / "config.h"),
                *["-D" + define for define in defines],
                str(path / "test.c"),
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True, timeout=30)
