# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "boards/getafix/tuya"
spec = importlib.util.spec_from_file_location("build_tuya_hardware", ROOT / "tools/build_tuya_hardware.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class TuyaHardwareTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_hardware_core(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "hardware_test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(SOURCE / "src"), str(SOURCE / "src/hardware_core.c"),
                str(SOURCE / "tests/test_hardware_core.c"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_staging_does_not_overwrite_unowned_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                builder.stage_app(SOURCE, Path(directory))

    def test_staging_is_repeatable(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "app"
            builder.stage_app(SOURCE, output)
            builder.stage_app(SOURCE, output)
            self.assertEqual((output / "src/hardware.c").read_bytes(),
                             (SOURCE / "src/hardware.c").read_bytes())
            self.assertFalse((output / "tests").exists())


if __name__ == "__main__":
    unittest.main()
