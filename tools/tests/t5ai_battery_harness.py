# SPDX-License-Identifier: Apache-2.0
"""Complete battery driver, production headers/config/ticks, modeled execution and hardware."""

import subprocess
import tempfile
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]


def run_battery(
    checks,
    profile=False,
    quiet=False,
    battery_profile=False,
    source=ROOT / "drivers/battery/t5ai.c",
    hooks="",
):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        config = load_config("unhygienix")
        # The umbrella selects quiet success; use its real control opt-out for the matrix.
        config.syms["T5AI_POWER_SAVINGS"].set_value("n")
        config.syms["T5AI_TICK_PROFILE"].set_value("y" if profile else "n")
        for name, value in {
            "LOG_HASHED": False,
            "T5AI_POWER_PROFILE": profile,
            "T5AI_BATTERY_QUIET_SUCCESS": quiet,
            "T5AI_BATTERY_PROFILE": battery_profile,
        }.items():
            config.syms[name].set_value("y" if value else "n")
            assert config.syms[name].str_value == ("y" if value else "n"), name
        config.write_autoconf(str(path / "config.h"))
        (path / "font_resource_keys.auto.h").write_text("/* No font IDs used. */\n")
        model = (ROOT / "tools/tests/fixtures/t5ai_battery/model.c").read_text()
        (path / "test.c").write_text(
            model
            + f'\n#include "{source}"\n'
            + (ROOT / "tools/tests/fixtures/t5ai_battery/helpers.c").read_text()
            + hooks
            + "\nint main(void) { context = 1;\n"
            + checks
            + "\nreturn 0; }\n"
        )
        includes = [
            path,
            ROOT,
            ROOT / "include",
            ROOT / "include/pbl",
            ROOT / "pebble",
            ROOT / "subsys",
            ROOT / "kernel",
            ROOT / "kernel/arch",
            ROOT / "kernel/arch/posix/include",
            ROOT / "tests/overrides/default",
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
