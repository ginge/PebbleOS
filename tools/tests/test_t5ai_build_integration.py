# SPDX-License-Identifier: Apache-2.0
"""Exercise real CMake backend selection and final-ELF boundary validation."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.check_unhygienix_build_integration import validate_symbols

ROOT = Path(__file__).resolve().parents[2]


class BuildIntegrationTest(unittest.TestCase):
    def configure_backends(self, fault="", flash=True):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = (
                "pebble/process_management/process_regions.c",
                "subsys/logging/backends/flash_storage.c",
                "soc/t5ai/process_regions.c",
                "soc/t5ai/log_storage.c",
                "other.c",
            )
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("/* Configure-only fixture. */\n")
            stock = "other.c" if fault == "missing-stock" else names[0]
            sources = f'"${{PBL_BASE}}/{stock}"'
            if fault == "duplicate-stock":
                sources += f" {sources}"
            backend = "other.c" if fault == "missing-backend" else names[2]
            target = (
                "wrong_target"
                if fault == "missing-target"
                else "pebble__process_management"
            )
            (root / "CMakeLists.txt").write_text(
                f'''cmake_minimum_required(VERSION 3.21)
project(backend_selection C)
set(PBL_BASE "${{CMAKE_SOURCE_DIR}}")
set(CONFIG_LOG_BACKEND_FLASH {"ON" if flash else "OFF"})
add_library({target} OBJECT {sources} other.c)
add_library(subsys__logging OBJECT "${{PBL_BASE}}/{names[1]}" other.c)
add_library(soc__t5ai OBJECT "${{PBL_BASE}}/{backend}" "${{PBL_BASE}}/{names[3]}")
include("{ROOT / "soc/t5ai/cmake/backends.cmake"}")
function(report)
  foreach(target pebble__process_management subsys__logging soc__t5ai)
    get_target_property(sources ${{target}} SOURCES)
    file(WRITE "${{CMAKE_BINARY_DIR}}/${{target}}.txt" "${{sources}}")
  endforeach()
  get_source_file_property(suppressed "${{PBL_BASE}}/{names[0]}" HEADER_FILE_ONLY)
  if(suppressed)
    message(FATAL_ERROR "Backend selection must not mutate source suppression")
  endif()
endfunction()
cmake_language(DEFER CALL report)
'''
            )
            result = subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "build")],
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
            )
            if fault:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("T5AI", result.stdout + result.stderr)
            else:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                process = (root / "build/pebble__process_management.txt").read_text()
                logging = (root / "build/subsys__logging.txt").read_text()
                self.assertNotIn(names[0], process)
                self.assertIn("other.c", process)
                self.assertEqual(names[1] in logging, not flash)
                local = (root / "build/soc__t5ai.txt").read_text()
                self.assertIn(names[2], local)
                self.assertIn(names[3], local)

    def test_backend_selection_preserves_other_sources_and_disabled_flash(self):
        for flash in (False, True):
            with self.subTest(flash=flash):
                self.configure_backends(flash=flash)

    def test_upstream_source_or_backend_drift_fails_configuration(self):
        for fault in (
            "missing-stock",
            "duplicate-stock",
            "missing-backend",
            "missing-target",
        ):
            with self.subTest(fault=fault):
                self.configure_backends(fault)

    def test_link_guard_requires_selected_boundaries_and_rejects_hidden_wrappers(self):
        wraps = {
            "pbl_console_lock",
            "pbl_console_unlock",
            "pfs_init",
            "event_put",
            "event_take_timeout",
            "pbl_msgq_put",
            "pbl_msgq_get",
            "recognizer_manager_handle_touch_event",
        }
        symbols = {"__wrap_" + name for name in wraps}
        symbols.update(("ftl_read", "ftl_write", "t5ai_touch_sample_ticks"))
        config = "CONFIG_CONSOLE=y\nCONFIG_T5AI_SD_PFS=y\nCONFIG_T5AI_TOUCH_QUEUE_COALESCE=y\n"

        def check(selected, declared=wraps):
            output = "\n".join("02120000 T " + name for name in selected)
            return validate_symbols(output, config, "\n".join(declared))

        self.assertEqual(check(symbols), [])
        for name in symbols:
            with self.subTest(missing=name):
                self.assertIn(
                    "missing production boundary: " + name, check(symbols - {name})
                )
        self.assertIn(
            "undeclared linker wrapper: surprise", check(symbols | {"__wrap_surprise"})
        )
        self.assertEqual(
            validate_symbols("02120000 T unrelated.constprop.0", "", ""), []
        )


if __name__ == "__main__":
    unittest.main()
