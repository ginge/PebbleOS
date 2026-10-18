# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Verify Bluetooth-only CP manifests and rejection of retired diagnostics."""

import json
from pathlib import Path
import tempfile
import unittest

from tools import unhygienix_cp as cp


class CPManifestTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.output = Path(directory.name)
        artifacts = {}
        for name in ("pebbleos.t5ai-cp.bin", "pebbleos.t5ai-cp-crc.bin"):
            data = name.encode()
            (self.output / name).write_bytes(data)
            artifacts[name] = cp.digest(data)
        self.manifest = {
            "schema": 1,
            "board": "unhygienix",
            "stock_sha256": cp.STOCK_SHA256,
            "vendor_revisions": json.loads(cp.LOCK.read_text()),
            "hook_sha256": cp.digest(cp.SOURCE.read_bytes()),
            "artifacts": artifacts,
        }

    def validate(self):
        (self.output / "pebbleos.t5ai-cp.json").write_text(json.dumps(self.manifest))
        cp.validate_manifest(self.output)

    def test_bluetooth_only_and_legacy_null_guard_are_accepted(self):
        self.validate()
        self.manifest["flash_guard"] = None
        self.validate()

    def test_retired_diagnostics_are_rejected(self):
        for guard in ({}, {"schema": 1, "volatile_status": False}):
            with self.subTest(guard=guard):
                self.manifest["flash_guard"] = guard
                with self.assertRaisesRegex(ValueError, "flash guard images are retired"):
                    self.validate()

    def test_modified_artifact_is_rejected(self):
        (self.output / "pebbleos.t5ai-cp.bin").write_bytes(b"modified")
        with self.assertRaisesRegex(ValueError, "CP manifest hash mismatch"):
            self.validate()
