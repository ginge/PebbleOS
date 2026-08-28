# SPDX-License-Identifier: Apache-2.0
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location(
    "check_tuya_kernel", ROOT / "tools/check_tuya_kernel.py"
)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class TuyaKernelCheckTest(unittest.TestCase):
    def test_only_expected_config_is_rewritten(self):
        config = "#define configENABLE_MPU                   0\n#define OTHER 7\n"
        for enabled in (0, 1):
            result = checker.make_config(config, enabled)
            self.assertIn(f"#define configENABLE_MPU {enabled}\n", result)
            self.assertIn("#define configTOTAL_MPU_REGIONS 16\n", result)
            self.assertIn("#define OTHER 7", result)
        with self.assertRaises(ValueError):
            checker.make_config("#define configENABLE_MPU 1\n", 1)
        with self.assertRaises(ValueError):
            checker.make_config(config + config, 1)

    def test_compiler_outputs_are_redirected_without_a_shell(self):
        entry = {
            "command": "'/sdk/tool chain/gcc' -DNAME='a;b' -I/sdk -MD -MF old.d -MT old.o -o old.o -c old.c"
        }
        result = checker.compile_command(
            entry, Path("/copy/tasks.c"), Path("/out/tasks.o"), [Path("/config")]
        )
        self.assertEqual(result[0:2], ["/sdk/tool chain/gcc", "-I/config"])
        self.assertIn("-DNAME=a;b", result)
        self.assertEqual(result[result.index("-c") + 1], "/copy/tasks.c")
        self.assertEqual(result[result.index("-o") + 1], "/out/tasks.o")
        self.assertEqual(result[result.index("-MF") + 1], "/out/tasks.d")
        self.assertEqual(result[result.index("-MT") + 1], "/out/tasks.o")

    def test_rejects_non_compiler_commands(self):
        for args in (["gcc", "x.c"], ["gcc", "-c", "a.c", "-c", "b.c", "-o", "a.o"]):
            with self.assertRaises(ValueError):
                checker.compile_command(
                    {"arguments": args}, Path("a.c"), Path("a.o"), []
                )

    def test_output_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                checker.prepare_output(root)
            output = root / "check"
            checker.prepare_output(output)
            checker.prepare_output(output)
            self.assertTrue((output / ".pebble-tuya-kernel-check").is_file())


if __name__ == "__main__":
    unittest.main()
