# SPDX-License-Identifier: Apache-2.0
"""Owned touch transport with real queue, pan, recognizer and tick units."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/tests/fixtures/t5ai_touch"


class TouchQueueTest(unittest.TestCase):
    def check_transport(self, lto):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            config = load_config("unhygienix")
            config.syms["LOG_HASHED"].set_value("n")
            config.write_autoconf(str(path / "config.h"))
            core = path / "pbl/soc/t5ai/core.h"
            core.parent.mkdir(parents=True)
            core.write_text(
                "unsigned test_control(void);\n#define __get_CONTROL() test_control()\n"
            )
            # Generated font IDs are unused; public syscall declarations include this file.
            (path / "font_resource_keys.auto.h").write_text(
                "/* No font resources used. */\n"
            )
            includes = [
                path,
                FIXTURE,
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
            sanitizer = (
                "-fsanitize=undefined" if lto else "-fsanitize=address,undefined"
            )
            flags = [
                "clang-21",
                "-std=gnu23",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                sanitizer,
                "-fno-sanitize-recover=all",
                "-fno-pie",
                "-ffunction-sections",
                "-fdata-sections",
                "-include",
                str(path / "config.h"),
                *[arg for include in includes for arg in ("-I", str(include))],
            ]
            units = [
                FIXTURE / "main.c",
                FIXTURE / "endpoints.c",
                ROOT / "kernel/msgq.c",
                ROOT / "kernel/tick.c",
                ROOT / "pebble/applib/ui/recognizer/pan.c",
                ROOT / "pebble/applib/ui/recognizer/recognizer.c",
                ROOT / "pebble/applib/graphics/gtypes.c",
            ]
            objects = []
            for index, unit in enumerate(units):
                args = flags.copy()
                # Match the production non-LTO queue/event/manager boundaries.
                if lto and unit.name not in ("endpoints.c", "msgq.c"):
                    args += ["-flto"]
                if unit.name == "pan.c":
                    args += ["-Dsys_get_ticks=t5ai_touch_sample_ticks"]
                obj = path / f"{index}.o"
                subprocess.run([*args, "-c", str(unit), "-o", str(obj)], check=True)
                objects.append(str(obj))
            integration = (
                (ROOT / "soc/t5ai/cmake/production.cmake")
                .read_text()
                .split("if(CONFIG_T5AI_TOUCH_QUEUE_COALESCE)", 1)[1]
                .split("  endfunction()", 1)[0]
            )
            wraps = (
                re.search(r"foreach\(symbol ([^)]*)\)", integration).group(1).split()
            )
            self.assertIn("event_take_timeout", wraps)
            subprocess.run(
                [
                    "clang-21",
                    *(["-flto"] if lto else []),
                    sanitizer,
                    "-no-pie",
                    "-Wl,--gc-sections",
                    *["-Wl,--wrap=" + symbol for symbol in wraps],
                    *objects,
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True, timeout=15)

    def test_owned_samples_queue_pressure_and_pan_velocity(self):
        self.check_transport(False)

    def test_lto_public_boundary_integration(self):
        self.check_transport(True)

    def test_no_private_queue_or_spare_event_metadata(self):
        source = (ROOT / "soc/t5ai/touch_queue.c").read_text()
        header = (ROOT / "soc/t5ai/touch_queue.h").read_text()
        self.assertNotRegex(source + header, r"->(?:backend|buf|max_msgs)\b")
        self.assertNotIn("t5_touch_meta", source + header)
        self.assertNotIn("T5_TOUCH_TIME_MASK", source + header)


if __name__ == "__main__":
    unittest.main()
