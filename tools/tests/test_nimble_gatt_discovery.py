# SPDX-License-Identifier: Apache-2.0
"""Run the complete production NimBLE discovery engine against scripted ATT callbacks."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = (
    "incremental",
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
                "comm/bt_conn_mgr.h",
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
            for lease in (0, 120):
                binary = tmp / f"discovery-{lease}"
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
                        f"-DCONFIG_BT_GATT_DISCOVERY_LEASE_SECONDS={lease}",
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
                    with self.subTest(lease=lease, scenario=scenario):
                        subprocess.run([str(binary), scenario], check=True, timeout=10)

    def test_lease_defaults_preserve_stock_and_t5ai_policy(self):
        for board, revision, expected in (
            ("qemu_gabbro", "_", 0),
            ("getafix", "dvt2", 0),
            ("unhygienix", "_", 120),
        ):
            with (
                self.subTest(board=board),
                patch.dict(
                    os.environ,
                    {
                        "srctree": str(ROOT),
                        "BOARD": board,
                        "BOARD_REVISION": revision,
                    },
                ),
            ):
                config = kconfiglib.Kconfig(str(ROOT / "Kconfig"), warn=False)
                paths = [f"boards/{board}/defconfig"]
                if revision != "_":
                    paths.append(f"boards/{board}/defconfig.{revision}")
                paths += ["fw/prj.conf", "fw/prj_normal.conf"]
                for index, path in enumerate(paths):
                    config.load_config(str(ROOT / path), replace=index == 0)
                self.assertEqual(
                    config.syms["BT_GATT_DISCOVERY_LEASE_SECONDS"].str_value,
                    str(expected),
                )


if __name__ == "__main__":
    unittest.main()
