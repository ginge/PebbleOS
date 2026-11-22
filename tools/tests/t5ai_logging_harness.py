# SPDX-License-Identifier: Apache-2.0
"""Complete stock applib and T5AI logging units, real types, modeled kernel endpoints."""

import subprocess
import tempfile
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]


def run_logging(checks, adapter=ROOT / "soc/t5ai/mailbox_logging.c", baseline=False):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        config = load_config("unhygienix")
        config.syms["LOG_HASHED"].set_value("n")
        config.write_autoconf(str(path / "config.h"))
        flags = [
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
            "-ffunction-sections",
            "-fdata-sections",
            "-pthread",
            "-include",
            str(path / "config.h"),
        ]
        for include in (
            ROOT,
            ROOT / "include",
            ROOT / "include/pbl",
            ROOT / "pebble",
            ROOT / "subsys",
            ROOT / "lib",
            ROOT / "kernel/arch/posix/include",
        ):
            flags.extend(("-I", str(include)))
        subprocess.run(
            [
                *flags,
                "-Dkernel_applib_get_log_state=t5ai_stock_get_log_state",
                "-Dkernel_applib_release_log_state=t5ai_stock_release_log_state",
                "-Dkernel_applib_init=t5ai_stock_kernel_applib_init",
                "-c",
                str(ROOT / "pebble/kernel/kernel_applib_state.c"),
                "-o",
                str(path / "stock.o"),
            ],
            check=True,
        )
        model = (ROOT / "tools/tests/fixtures/t5ai_logging/model.c").read_text()
        model = model.replace("ADAPTER_SOURCE", f'"{adapter}"')
        if baseline:
            model += """
void kernel_applib_release_log_state(LogState *state) { t5ai_stock_release_log_state(state); }
void kernel_applib_init(void) { t5ai_stock_kernel_applib_init(); }
"""
        (path / "test.c").write_text(
            model + "\nint main(void) {\n" + checks + "\nreturn 0; }\n"
        )
        subprocess.run(
            [
                *flags,
                "-no-pie",
                "-Wl,--gc-sections",
                str(path / "test.c"),
                str(ROOT / "kernel/tick.c"),
                str(path / "stock.o"),
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True, timeout=10)
