# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Run the complete production NimBLE discovery engine against scripted ATT callbacks."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = (
    "incremental",
    "large-database",
    "empty-services",
    "no-services",
    "all-empty",
    "range",
    "immediate-services",
    "immediate-chars",
    "immediate-descriptors",
    "cancel-services",
    "cancel-chars",
    "cancel-descriptors",
    "disconnect-error",
    "disconnect-complete",
    "timeout",
    "att-error",
    "subscribe-immediate",
    "subscribe-success",
    "subscribe-error",
    "subscribe-disconnected",
    "queued-disconnect",
)


class NimbleDiscoveryTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_discovery_lifetimes_and_completion(self):
        source = Path(
            os.environ.get(
                "GATT_DISCOVERY_TEST_SOURCE",
                ROOT / "subsys/bluetooth/gatt_client_discovery.c",
            )
        )
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / "discovery.c").write_bytes(source.read_bytes())
            fixture = ROOT / "tools/tests/fixtures/gatt_discovery"
            shutil.copy2(fixture / "fixture.h", tmp / "fixture.h")
            for name in (
                "nimble_gattc_op_queue.h",
                "nimble_type_conversions.h",
                "pbl/bluetooth/gatt.h",
                "pbl/kernel/sem.h",
                "comm/bt_lock.h",
                "host/ble_hs.h",
                "services/gatt/ble_svc_gatt.h",
            ):
                path = tmp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "fixture.h"\n')
            stubs = {
                "pbl/kernel/compiler.h": "#pragma once\n#define PBL_PACKED __attribute__((packed))\n",
                "pbl/logging/logging.h": """#define PBL_LOG_MODULE_DECLARE(...)
#define PBL_LOG_DBG(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define PBL_LOG_ERR(...) PBL_LOG_DBG(__VA_ARGS__)
#define PBL_LOG_WRN(...) PBL_LOG_DBG(__VA_ARGS__)
""",
                "pbl/util/assert.h": "#define UTIL_ASSERT(x) assert(x)\n",
                "pbl/util/logging.h": "#define UTIL_LOG(x) ((void)(x))\n",
                "pbl/util/math.h": "#define MIN(a,b) ((a) < (b) ? (a) : (b))\n",
            }
            for name, contents in stubs.items():
                path = tmp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
            binary = tmp / "discovery"
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-Dnullptr=NULL",
                    "-Dstatic_assert=_Static_assert",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-fno-omit-frame-pointer",
                    "-fno-pie",
                    "-no-pie",
                    "-g",
                    "-include",
                    str(tmp / "fixture.h"),
                    "-I",
                    str(tmp),
                    "-I",
                    str(ROOT / "include"),
                    str(fixture / "harness.c"),
                    str(ROOT / "lib/util/list.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            for scenario in SCENARIOS:
                with self.subTest(scenario=scenario):
                    subprocess.run([str(binary), scenario], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
