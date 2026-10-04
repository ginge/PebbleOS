# SPDX-License-Identifier: Apache-2.0
import importlib.util
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "boards/getafix/tuya"
spec = importlib.util.spec_from_file_location(
    "build_tuya_hardware", ROOT / "tools/build_tuya_hardware.py"
)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class TuyaHardwareTest(unittest.TestCase):
    def test_sdk_staging_uses_private_platform_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk, platform, output = root / "TuyaOpen", root / "T5AI", root / "output"
            sdk.mkdir()
            platform.mkdir()
            (sdk / "tos.py").write_text("reference")
            (platform / "source.c").write_text("stock")
            for repo in (sdk, platform):
                subprocess.run(["git", "init", "-q", str(repo)], check=True)
                subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
                subprocess.run(["git", "-C", str(repo), "-c", "user.name=Test",
                                "-c", "user.email=test@example.com", "commit", "-qm", "fixture"],
                               check=True)
            with patch.object(builder, "platform_path", return_value=platform):
                staged = builder.stage_sdk(sdk, output)
                self.assertEqual((staged / "platform/T5AI/source.c").read_text(), "stock")
                self.assertTrue((staged / "platform/T5AI/.git").is_dir())
                (staged / "platform/T5AI/source.c").write_text("generated")
                (staged / "obsolete.c").write_text("old")
                builder.stage_sdk(sdk, output)
                self.assertEqual((platform / "source.c").read_text(), "stock")
                self.assertFalse((staged / "obsolete.c").exists())
                with self.assertRaises(ValueError):
                    builder.stage_sdk(sdk, sdk / "output")

    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_hardware_core(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "hardware_test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(SOURCE / "src"),
                    str(SOURCE / "src/hardware_core.c"),
                    str(SOURCE / "tests/test_hardware_core.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_runtime_ports(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "runtime_test"
            sources = [
                "hardware_core.c",
                "rtc_port.c",
                "input_port.c",
                "battery_port.c",
            ]
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(SOURCE / "src"),
                    "-I",
                    str(ROOT / "include"),
                    *[str(SOURCE / "src" / source) for source in sources],
                    str(SOURCE / "tests/test_runtime.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_hci_port(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "hci_test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(SOURCE / "tests/mocks"),
                    "-I",
                    str(SOURCE / "src"),
                    str(SOURCE / "src/hci_port.c"),
                    str(SOURCE / "tests/test_hci.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            for scenario in ("overflow", "malformed", "init_failure", "send_failure"):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(binary), scenario], check=True)

    def test_staging_does_not_overwrite_unowned_directory(self):
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(ValueError):
            builder.stage_app(SOURCE, Path(directory))

    def test_staging_is_repeatable(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "app"
            builder.stage_app(SOURCE, output)
            builder.stage_app(SOURCE, output)
            self.assertEqual(
                (output / "src/hardware.c").read_bytes(),
                (SOURCE / "src/hardware.c").read_bytes(),
            )
            self.assertFalse((output / "tests").exists())
            self.assertEqual(
                (output / "pebble_include/pbl/os/mutex.h").read_bytes(),
                (ROOT / "include/pbl/os/mutex.h").read_bytes(),
            )
            self.assertTrue((output / "CMakeLists.txt").is_file())


if __name__ == "__main__":
    unittest.main()
