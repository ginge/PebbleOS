# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
import json
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from pbl.runners.core import RunnerConfig, RunnerError
from pbl.runners.tyutool import TyuToolRunner
from tools.unhygienix_image import (
    encode_pebble_application,
    encode_protected_blocks,
    PEBBLE_AP_PHYSICAL_OFFSET,
    PEBBLE_FILESYSTEM_PHYSICAL_OFFSET,
    PEBBLE_AP_SLOT_PHYSICAL_SIZE,
    PEBBLE_RESOURCES_SIZE,
)


class UnhygienixRunnerTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.raw = self.root / "pebbleos.bin"
        self.image = (
            (0x28066000).to_bytes(4, "little")
            + (0x02120141).to_bytes(4, "little")
            + bytes(64)
        )
        self.raw.write_bytes(self.image)
        self.protected = self.root / "pebbleos.t5ai-crc.bin"
        self.protected.write_bytes(encode_pebble_application(self.image))
        self.resources = self.root / "system_resources.pbpack"
        self.resources.write_bytes(b"resources")

    def runner(self, resources=False, **kwargs):
        cfg = RunnerConfig(
            board_dir="boards/unhygienix",
            soc="T5AI",
            bin_file=str(self.raw),
            resources_file=str(self.resources) if resources else None,
        )
        return TyuToolRunner(cfg, tty="/dev/test", **kwargs)

    def commands(self, runner):
        with patch.object(runner, "call", return_value=0) as call:
            runner.run("flash")
        return [args[0][0] for args in call.call_args_list]

    def test_cli_supplies_binary_to_board_runner(self):
        from types import SimpleNamespace
        from pbl.device import make_runner

        build = SimpleNamespace(
            board_spec=SimpleNamespace(name="unhygienix", runners=["tyutool"]),
            board="unhygienix",
            topdir=str(self.root),
            variant="normal",
            config=SimpleNamespace(CONFIG_SOC="T5AI"),
            hex=str(self.root / "pebbleos.hex"),
            elf=str(self.root / "pebbleos.elf"),
            bin=str(self.raw),
            pbpack=str(self.resources),
            tool=lambda name: None,
        )
        runner = make_runner(build, SimpleNamespace(tyutool_tty="/dev/test"))
        commands = self.commands(runner)
        self.assertEqual(len(commands), 1)
        self.assertIn("-s 0x132000", commands[0])

    def test_default_updates_only_firmware(self):
        commands = self.commands(self.runner())
        self.assertEqual(len(commands), 1)
        self.assertIn("-s 0x132000", commands[0])
        self.assertIn(
            "--end " + hex(PEBBLE_AP_PHYSICAL_OFFSET + self.protected.stat().st_size),
            commands[0],
        )
        self.assertFalse((self.root / "pebbleos.t5ai-pfs.bin").exists())

    def test_resources_preserve_pfs(self):
        commands = self.commands(self.runner(resources=True))
        self.assertEqual(len(commands), 1)
        self.assertIn(
            "--end "
            + hex(
                PEBBLE_AP_PHYSICAL_OFFSET
                + PEBBLE_AP_SLOT_PHYSICAL_SIZE
                + PEBBLE_RESOURCES_SIZE * 34 // 32
            ),
            commands[0],
        )
        self.assertFalse((self.root / "pebbleos.t5ai-pfs.bin").exists())

    def test_provision_is_explicit(self):
        runner = self.runner(provision=True)
        with patch.object(runner, "call") as call, self.assertRaises(RunnerError):
            runner.run("flash")
        call.assert_not_called()
        commands = self.commands(self.runner(resources=True, provision=True))
        self.assertEqual(len(commands), 2)
        self.assertIn("-s " + hex(PEBBLE_FILESYSTEM_PHYSICAL_OFFSET), commands[1])

    def test_corrupt_firmware_rejected_before_write(self):
        self.protected.write_bytes(b"corrupt")
        runner = self.runner(resources=True, provision=True)
        with patch.object(runner, "call") as call, self.assertRaises(RunnerError):
            runner.run("flash")
        call.assert_not_called()

    def test_missing_or_untrusted_cp_rejected_before_ap_write(self):
        runner = self.runner(cp=True)
        with patch.object(runner, "call") as call, self.assertRaises(RunnerError):
            runner.run("flash")
        call.assert_not_called()
        cp = (
            (0x28010000).to_bytes(4, "little")
            + (0x02020141).to_bytes(4, "little")
            + bytes(64)
        )
        (self.root / "pebbleos.t5ai-cp.bin").write_bytes(cp)
        (self.root / "pebbleos.t5ai-cp-crc.bin").write_bytes(
            encode_protected_blocks(cp)
        )
        (self.root / "pebbleos.t5ai-cp.json").write_text(json.dumps({"schema": 1}))
        with patch.object(runner, "call") as call, self.assertRaises(RunnerError):
            runner.run("flash")
        call.assert_not_called()

    def test_valid_cp_is_first_and_tampering_prevents_all_writes(self):
        from tools.unhygienix_cp import LOCK, SOURCE, STOCK_SHA256

        cp = (
            (0x28010000).to_bytes(4, "little")
            + (0x02020141).to_bytes(4, "little")
            + bytes(64)
        )
        artifacts = {
            "pebbleos.t5ai-cp.bin": cp,
            "pebbleos.t5ai-cp-crc.bin": encode_protected_blocks(cp),
        }
        for name, data in artifacts.items():
            (self.root / name).write_bytes(data)
        manifest = {
            "schema": 1,
            "board": "unhygienix",
            "stock_sha256": STOCK_SHA256,
            "vendor_revisions": json.loads(LOCK.read_text()),
            "hook_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
            "artifacts": {
                name: hashlib.sha256(data).hexdigest()
                for name, data in artifacts.items()
            },
        }
        (self.root / "pebbleos.t5ai-cp.json").write_text(json.dumps(manifest))
        commands = self.commands(self.runner(cp=True))
        self.assertEqual(len(commands), 2)
        self.assertIn("-s 0x22000", commands[0])
        self.assertIn("-s 0x132000", commands[1])
        # CRC-valid replacement still fails provenance before either write.
        replacement = cp + b"changed"
        (self.root / "pebbleos.t5ai-cp.bin").write_bytes(replacement)
        (self.root / "pebbleos.t5ai-cp-crc.bin").write_bytes(
            encode_protected_blocks(replacement)
        )
        runner = self.runner(cp=True)
        with patch.object(runner, "call") as call, self.assertRaises(RunnerError):
            runner.run("flash")
        call.assert_not_called()

    def test_dry_run_writes_no_artifacts(self):
        runner = self.runner(resources=True, provision=True)
        runner.cfg = runner.cfg._replace(dry_run=True)
        self.commands(runner)
        self.assertFalse((self.root / "pebbleos.t5ai-flash.bin").exists())
        self.assertFalse((self.root / "pebbleos.t5ai-pfs.bin").exists())

    def test_failure_stops_before_pfs(self):
        runner = self.runner(resources=True, provision=True)
        with (
            patch.object(runner, "call", return_value=1) as call,
            self.assertRaises(RunnerError),
        ):
            runner.run("flash")
        self.assertEqual(call.call_count, 1)


if __name__ == "__main__":
    unittest.main()
