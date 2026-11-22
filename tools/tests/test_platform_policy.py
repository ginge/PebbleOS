# SPDX-License-Identifier: Apache-2.0
"""Recovery boot semantics and board-owned pairing controls using production definitions."""

import os
import subprocess
import tempfile
import unittest
from itertools import product
from pathlib import Path
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]


def config_for(board, revision="_", variant="normal"):
    with patch.dict(
        os.environ, {"srctree": str(ROOT), "BOARD": board, "BOARD_REVISION": revision}
    ):
        config = kconfiglib.Kconfig(str(ROOT / "pebble/Kconfig"), warn=False)
        paths = [f"boards/{board}/defconfig"]
        if revision != "_" and (ROOT / f"boards/{board}/defconfig.{revision}").exists():
            paths.append(f"boards/{board}/defconfig.{revision}")
        paths += ["pebble/prj.conf", f"pebble/prj_{variant}.conf"]
        for n, name in enumerate(paths):
            config.load_config(str(ROOT / name), replace=n == 0)
        config.syms["LOG_HASHED"].set_value("n")
        return config


def flags(path):
    result = [
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
        "-Wno-missing-field-initializers",
        "-ffunction-sections",
        "-fdata-sections",
        "-include",
        str(path / "config.h"),
    ]
    for include in (
        path,
        ROOT,
        ROOT / "include",
        ROOT / "include/pbl",
        ROOT / "pebble",
        ROOT / "subsys",
        ROOT / "lib",
        ROOT / "boards",
        ROOT / "kernel/arch/posix/include",
        ROOT / "third_party/upng/upng",
    ):
        result.extend(("-I", str(include)))
    return result


def run_factory(boot, update, pblboot, recovery, shutdown, soc=True, source=None):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        config_for("unhygienix").write_autoconf(str(path / "config.h"))
        values = {
            "RECOVERY_BOOT_SUPPORTED": boot,
            "RECOVERY_UPDATE_SUPPORTED": update,
            "PBLBOOT": pblboot,
            "RECOVERY_FW": recovery,
            "SOC_T5AI": soc,
        }
        (path / "scenario.h").write_text(
            "".join(
                f"#undef CONFIG_{name}\n"
                + (f"#define CONFIG_{name} 1\n" if enabled else "")
                for name, enabled in values.items()
            )
        )
        source = source or ROOT / "pebble/kernel/util/factory_reset.c"
        model = (ROOT / "tools/tests/fixtures/platform_policy/factory.c").read_text()
        model = model.replace("FACTORY_SOURCE", f'"{source}"')
        expected = "LBPWCSRTDDZ"
        if not recovery and boot:
            expected += "F" + ("01" if pblboot else "")
        expected += "E" if recovery else "X"
        expected += ("H" if shutdown else "") + "QN"
        model += f"""
int main(void) {{
 assert(!factory_reset_ongoing());
 if (!setjmp(rebooted)) factory_reset({int(shutdown)});
 assert(factory_reset_ongoing());assert(!strcmp(calls,"{expected}"));
}}
"""
        (path / "test.c").write_text(model)
        subprocess.run(
            [
                *flags(path),
                "-include",
                str(path / "scenario.h"),
                "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all",
                "-fno-pie",
                "-no-pie",
                "-Wl,--gc-sections",
                str(path / "test.c"),
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True, timeout=5)


class PlatformPolicyTest(unittest.TestCase):
    def test_recovery_boot_defaults_and_hidden_t5ai_guard(self):
        for board, revision in (
            ("unhygienix", "_"),
            ("obelix", "_"),
            ("asterix", "_"),
            ("getafix", "dvt"),
            ("getafix", "dvt2"),
        ):
            for variant in ("normal", "prf"):
                with self.subTest(board=board, revision=revision, variant=variant):
                    config = config_for(board, revision, variant)
                    self.assertEqual(
                        config.syms["RECOVERY_BOOT_SUPPORTED"].str_value,
                        "n" if board == "unhygienix" else "y",
                    )
                    if board == "unhygienix":
                        config.syms["RECOVERY_BOOT_SUPPORTED"].set_value(2)
                        self.assertEqual(
                            config.syms["RECOVERY_BOOT_SUPPORTED"].str_value, "n"
                        )

    def test_factory_reset_order_capabilities_and_recovery_variant(self):
        for values in product((False, True), repeat=5):
            with self.subTest(
                boot=values[0],
                update=values[1],
                pblboot=values[2],
                recovery=values[3],
                shutdown=values[4],
            ):
                run_factory(*values)

    def test_factory_reset_guard_uses_capability_without_soc_identity(self):
        # Generic normal builds must obey the same capability regardless of SoC identity.
        for boot in (False, True):
            run_factory(boot, False, True, False, False, soc=False)

    def test_pairing_controls_from_complete_unit_preprocessing(self):
        for board, revision in (
            ("unhygienix", "_"),
            ("obelix", "_"),
            ("asterix", "_"),
            ("getafix", "dvt"),
            ("getafix", "dvt2"),
        ):
            for variant in ("normal", "prf"):
                with (
                    self.subTest(board=board, revision=revision, variant=variant),
                    tempfile.TemporaryDirectory() as temporary,
                ):
                    path = Path(temporary)
                    config_for(board, revision, variant).write_autoconf(
                        str(path / "config.h")
                    )
                    # Preprocessing does not use resource IDs; full ARM builds generate the real header.
                    (path / "resource").mkdir()
                    (path / "resource/resource_ids.auto.h").write_text("")
                    (path / "font_resource_keys.auto.h").write_text("")
                    macros = subprocess.check_output(
                        [
                            *flags(path),
                            "-E",
                            "-dM",
                            str(ROOT / "pebble/popups/bluetooth_pairing_ui.c"),
                        ],
                        text=True,
                    )
                    definitions = "\n".join(
                        line
                        for line in macros.splitlines()
                        if line.startswith(
                            (
                                "#define BT_PAIRING_CONFIRM_BUTTON ",
                                "#define BT_PAIRING_DECLINE_BUTTON ",
                            )
                        )
                    )
                    confirm, decline = (
                        ("SELECT", "BACK") if board == "unhygienix" else ("UP", "DOWN")
                    )
                    (path / "buttons.c").write_text(f"""
#include <applib/ui/click.h>
{definitions}
static_assert(BT_PAIRING_CONFIRM_BUTTON == BUTTON_ID_{confirm});
static_assert(BT_PAIRING_DECLINE_BUTTON == BUTTON_ID_{decline});
static_assert(BT_PAIRING_CONFIRM_BUTTON != BT_PAIRING_DECLINE_BUTTON);
int main(void) {{ return 0; }}
""")
                    subprocess.run(
                        [
                            *flags(path),
                            str(path / "buttons.c"),
                            "-o",
                            str(path / "buttons"),
                        ],
                        check=True,
                    )
                    subprocess.run([str(path / "buttons")], check=True, timeout=5)
