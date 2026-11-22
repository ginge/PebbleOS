# SPDX-License-Identifier: Apache-2.0
"""Complete native touch driver and class, real types/config/tick conversion, modeled endpoints."""

import subprocess
import tempfile
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]


def run_touch(checks, gesture_hook="", setup=True, extra_sources=()):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        config = load_config("unhygienix")
        config.syms["LOG_HASHED"].set_value("n")
        config.syms["T5AI_DRAW_PROFILE"].set_value("n")
        config.syms["T5AI_IO_PROFILE"].set_value("n")
        config.write_autoconf(str(path / "config.h"))
        model = (ROOT / "tools/tests/fixtures/t5ai_touch/model.c").read_text()
        model = model.replace("/* GESTURE_HOOK */", gesture_hook)
        (path / "test.c").write_text(
            model
            + "\nint main(void){\n"
            + ("setup();\n" if setup else "")
            + checks
            + "\nreturn 0;}\n"
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
                "-ffunction-sections",
                "-fdata-sections",
                "-Wl,--gc-sections",
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
                str(path / "test.c"),
                str(ROOT / "kernel/tick.c"),
                *[str(ROOT / source) for source in extra_sources],
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True, timeout=30)
