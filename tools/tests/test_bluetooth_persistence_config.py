# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Check normal/recovery Bluetooth backend selection on stock and T5AI boards."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class BluetoothPersistenceConfigTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cmake"), "CMake required")
    def test_build_selects_one_backend(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "selection.cmake"
            script.write_text(
                "macro(pbl_library)\nendmacro()\n"
                "macro(pbl_library_sources)\nendmacro()\n"
                "macro(pbl_library_sources_ifdef)\nendmacro()\n"
                f'include("{ROOT}/fw/services/bluetooth/CMakeLists.txt")\n'
                'message("SELECTED=${sources}")\n'
            )
            for t5ai, recovery, expected in (
                (False, False, "normal"),
                (False, True, "prf"),
                (True, False, "normal"),
                (True, True, "prf"),
            ):
                with self.subTest(t5ai=t5ai, recovery=recovery):
                    result = subprocess.run(
                        [
                            "cmake",
                            f"-DCONFIG_SOC_T5AI={int(t5ai)}",
                            f"-DCONFIG_RECOVERY_FW={int(recovery)}",
                            "-P",
                            str(script),
                        ],
                        capture_output=True,
                        text=True,
                        check=True,
                        timeout=10,
                    )
                    selected = result.stderr.split("SELECTED=", 1)[1].strip().split(";")
                    backends = [
                        name
                        for name in selected
                        if name.startswith("bluetooth_persistent_storage_")
                        and not name.endswith("debug.c")
                    ]
                    self.assertEqual(
                        backends, [f"bluetooth_persistent_storage_{expected}.c"]
                    )


if __name__ == "__main__":
    unittest.main()
