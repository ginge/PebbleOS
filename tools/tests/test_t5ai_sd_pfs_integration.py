# SPDX-License-Identifier: Apache-2.0
"""Complete cache-enabled PFS and SD translation units with real public headers."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/tests/fixtures/t5ai_sd_pfs"


class SDPFSIntegrationTests(unittest.TestCase):
    def run_pfs(self, scenario, lto=False):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            config = load_config("unhygienix")
            for name in ("LOG", "LOG_HASHED", "ANALYTICS", "TASK_WDT", "SHELL"):
                config.syms[name].set_value("n")
            if scenario not in ("guard", "existing"):
                config.syms["T5AI_SD_PFS_REQUIRE_EXISTING"].set_value("n")
            if scenario == "nor":
                config.syms["T5AI_SD_PFS"].set_value("n")
            config.write_autoconf(str(path / "config.h"))
            includes = [
                path,
                FIXTURE,
                ROOT,
                ROOT / "include",
                ROOT / "include/pbl",
                ROOT / "subsys",
                ROOT / "subsys/fs/pfs",
                ROOT / "pebble",
                ROOT / "kernel/arch/posix/include",
            ]
            flags = [
                "clang-21",
                "-std=gnu23",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-ffunction-sections",
                "-fdata-sections",
                "-fno-pie",
                "-fsanitize=undefined" if lto else "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all",
                "-include",
                str(path / "config.h"),
                *[arg for include in includes for arg in ("-I", str(include))],
            ]
            units = [
                FIXTURE / "main.c",
                FIXTURE / "endpoints.c",
                ROOT / "subsys/fs/pfs/pfs.c",
                ROOT / "subsys/crc/crc32_legacy.c",
                ROOT / "subsys/crc/crc8.c",
            ]
            if scenario == "nor":
                flags += ["-DMODEL_NOR"]
                units[0] = FIXTURE / "nor.c"
                units.append(ROOT / "subsys/fs/pfs/flash_translation.c")
            objects = []
            for i, unit in enumerate(units):
                obj = path / f"{i}.o"
                extra = ["-flto"] if lto and unit.name != "pfs.c" else []
                subprocess.run(
                    [*flags, *extra, "-c", str(unit), "-o", str(obj)], check=True
                )
                objects.append(str(obj))
            executable = path / "test"
            subprocess.run(
                [
                    "clang-21",
                    *(["-flto"] if lto else []),
                    "-fsanitize=undefined" if lto else "-fsanitize=address,undefined",
                    "-no-pie",
                    *objects,
                    "-Wl,--gc-sections"
                    if scenario == "nor"
                    else "-Wl,--wrap=pfs_init,--gc-sections",
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable), scenario], check=True, timeout=30)

    def test_real_pfs_provision_format_reopen_cache_and_faults(self):
        self.run_pfs("provision")

    def test_real_pfs_existing_header_and_volume(self):
        self.run_pfs("existing")

    def test_unknown_header_refused_before_any_writes(self):
        self.run_pfs("guard")

    def test_real_pfs_lto_backend_selection(self):
        self.run_pfs("provision", lto=True)

    def test_backend_selection(self):
        config = load_config("unhygienix")
        for name in ("FS_PFS_CUSTOM_FTL", "FS_PFS_CUSTOM_PAGE_FLAGS"):
            self.assertEqual(config.syms[name].str_value, "y")
        config.syms["T5AI_SD_PFS"].set_value("n")
        for name in ("FS_PFS_CUSTOM_FTL", "FS_PFS_CUSTOM_PAGE_FLAGS"):
            self.assertEqual(config.syms[name].str_value, "n")
        section = (
            (ROOT / "soc/t5ai/cmake/production.cmake")
            .read_text()
            .split("if(CONFIG_T5AI_SD_PFS)", 1)[1]
            .split("# Reject mailbox-owner", 1)[0]
        )
        self.assertNotIn("--wrap=ftl_", section)
        self.assertNotIn("pbl_malloc_check=", section)
        self.assertIn("--wrap=pfs_init", section)

    def test_real_nor_cache_and_partition_behavior(self):
        self.run_pfs("nor")


if __name__ == "__main__":
    unittest.main()
