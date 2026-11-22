# SPDX-License-Identifier: Apache-2.0
"""Readable fragments preserve staged bytes, reviewed pins and fail-closed guards."""

import json
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import unhygienix_cp_build as build
from tools import unhygienix_cp_variant as variant

ROOT = Path(__file__).resolve().parents[2]


class OverlayTest(unittest.TestCase):
    def test_expanded_hashes_match_committed_recipe_pins(self):
        self.assertEqual(
            build.overlay_digest("overlay.json"),
            "6b8b9c7e689d2b060771506fd20ee40375ace0ffe087a358696f09228bd29681",
        )
        pins = json.loads((build.BOARD.parent / "sleep-variant-links.json").read_text())
        self.assertEqual(
            build.overlay_digest("sleep-overlay.json"), pins["sleep_overlay_sha256"]
        )

    def test_complete_staging_matches_frozen_committed_outputs(self):
        expected = json.loads(
            (ROOT / "tools/tests/fixtures/unhygienix_cp_overlay.json").read_text()
        )
        for sleep in (False, True):
            with self.subTest(sleep=sleep), tempfile.TemporaryDirectory() as tmp:
                stage = Path(tmp)
                entries = build.load_overlay("overlay.json")
                if sleep:
                    entries += build.load_overlay("sleep-overlay.json")
                for entry in entries:
                    target = stage / entry["path"]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(build.SDK / entry["path"], target)
                build.overlay(stage, sleep)
                actual = {
                    str(p.relative_to(stage)): build.digest(p.read_bytes())
                    for p in sorted(stage.rglob("*"))
                    if p.is_file()
                }
                self.assertEqual(actual, expected["sleep" if sleep else "base"])

    def test_fragment_paths_and_ambiguous_content_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            board = root / "board"
            board.mkdir()
            (root / "outside.inc").write_text("outside")
            (board / "link.inc").symlink_to(root / "outside.inc")
            for relative in ("../outside.inc", str(root / "outside.inc"), "link.inc"):
                recipe = [
                    {"replacements": [{"old": "x", "new_file": relative, "count": 1}]}
                ]
                (board / "recipe.json").write_text(json.dumps(recipe))
                with (
                    self.subTest(path=relative),
                    patch.object(build, "BOARD", board),
                    self.assertRaisesRegex(ValueError, "fragment path"),
                ):
                    build.load_overlay("recipe.json")
            recipe[0]["replacements"][0]["new"] = "ambiguous"
            (board / "recipe.json").write_text(json.dumps(recipe))
            with (
                patch.object(build, "BOARD", board),
                self.assertRaisesRegex(ValueError, "Ambiguous"),
            ):
                build.load_overlay("recipe.json")

    def test_vendor_hash_and_replacement_count_still_rejected(self):
        original = json.loads((build.BOARD / "overlay.json").read_text())
        for kind in ("hash", "count"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                board = root / "board"
                shutil.copytree(build.BOARD, board)
                entry = original[0].copy()
                entry["replacements"] = [c.copy() for c in original[0]["replacements"]]
                if kind == "hash":
                    entry["vendor_sha256"] = "bad"
                else:
                    entry["replacements"][0]["count"] += 1
                (board / "overlay.json").write_text(json.dumps([entry]))
                target = root / "stage" / entry["path"]
                target.parent.mkdir(parents=True)
                shutil.copy2(build.SDK / entry["path"], target)
                before = target.read_bytes()
                with (
                    patch.object(build, "BOARD", board),
                    self.assertRaisesRegex(ValueError, "input changed|context changed"),
                ):
                    build.overlay(root / "stage")
                self.assertEqual(target.read_bytes(), before)

    def test_every_referenced_fragment_is_in_physical_provenance(self):
        inputs = build.board_inputs()
        count = 0
        for name in ("overlay.json", "sleep-overlay.json"):
            for entry in json.loads((build.BOARD / name).read_text()):
                for change in entry["replacements"]:
                    if "new_file" in change:
                        count += 1
                        path = build.BOARD / change["new_file"]
                        self.assertEqual(
                            inputs[str(path.relative_to(ROOT))],
                            build.digest(path.read_bytes()),
                        )
        self.assertEqual(count, 67)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            board = root / "board"
            (board / "nested").mkdir(parents=True)
            path = board / "nested/fragment.inc"
            path.write_text("before")
            with patch.object(build, "BOARD", board), patch.object(build, "ROOT", root):
                before = build.board_inputs()
                path.write_text("after")
                self.assertNotEqual(build.board_inputs(), before)

    def test_package_pin_rejects_fragment_mutation(self):
        # Synthetic accepted metadata exercises the real package admission function.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cp = root / "boards/unhygienix/tuya/cp"
            board = cp / "variants"
            shutil.copytree(build.BOARD, board)
            protocol = root / "include/pbl/soc/t5ai/sleep_protocol.h"
            protocol.parent.mkdir(parents=True)
            shutil.copy2(build.PROTOCOL, protocol)
            shutil.copy2(build.BOARD.parent / "native_alarm.c", cp / "native_alarm.c")
            image, config = b"test-image", b"test-config"
            pins = {
                "schema": 1,
                "board": "unhygienix",
                "alarm_abi": build.NATIVE_ALARM_ABI,
                "stock_sha256": variant.STOCK_SHA256,
                "controller_sha256": variant.CONTROLLER_SHA256,
                "latency_source_sha256": build.digest(variant.SOURCE.read_bytes()),
                "native_alarm_sha256": build.digest(
                    (cp / "native_alarm.c").read_bytes()
                ),
                "sleep_protocol_sha256": build.digest(protocol.read_bytes()),
                "sleep_overlay_sha256": build.overlay_digest("sleep-overlay.json"),
                "profiles": {
                    "off": {
                        "app.bin": build.digest(image),
                        "sdkconfig": build.digest(config),
                    }
                },
            }
            (cp / "sleep-variant-links.json").write_text(json.dumps(pins))
            with (
                patch.object(variant, "ROOT", root),
                patch.object(build, "BOARD", board),
            ):
                variant.verify_sleep_link_hashes(image, config, "off")
                entries = json.loads((board / "sleep-overlay.json").read_text())
                change = next(
                    c for e in entries for c in e["replacements"] if "new_file" in c
                )
                path = board / change["new_file"]
                path.write_bytes(path.read_bytes() + b"\n// changed\n")
                with self.assertRaisesRegex(
                    ValueError, "Invalid reviewed native alarm"
                ):
                    variant.verify_sleep_link_hashes(image, config, "off")
