# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Verify fix-only CP packaging and strict input/partition guards."""

import hashlib
import unittest
from unittest.mock import patch

from tools import patch_unhygienix_cp_latency as fix
from tools.t5ai_image import encode_protected_blocks, verify_protected_blocks


class CPLatencyFixTest(unittest.TestCase):
    def test_only_two_scheduler_calls_change(self):
        image = bytearray(fix.DATA_TX_XIP - fix.CP_XIP + fix.DATA_TX_BYTES)
        for call in fix.INTERVAL_CALLS:
            offset = call - fix.CP_XIP
            image[offset : offset + 4] = fix.thumb_bl(call, fix.MAX_LAT_XIP)
        image = bytes(image)
        tx = fix.DATA_TX_XIP - fix.CP_XIP
        sched = fix.SCHED_XIP - fix.CP_XIP
        address = fix.CP_XIP + ((len(image) + 15) & ~15)
        with (
            patch.object(
                fix,
                "DATA_TX_SHA256",
                hashlib.sha256(image[tx : tx + fix.DATA_TX_BYTES]).hexdigest(),
            ),
            patch.object(
                fix,
                "SCHED_SHA256",
                hashlib.sha256(image[sched : sched + fix.SCHED_BYTES]).hexdigest(),
            ),
        ):
            result = fix.install_fix(image, b"12345678", address)
            for bad_address, bad_hook in (
                (address + 2, b"1234"),
                (address, b""),
                (address, b"x" * 0x100000),
            ):
                with self.assertRaises(ValueError):
                    fix.install_fix(image, bad_hook, bad_address)
        allowed = {
            i
            for call in fix.INTERVAL_CALLS
            for i in range(call - fix.CP_XIP, call - fix.CP_XIP + 4)
        }
        self.assertEqual(
            {i for i in range(len(image)) if image[i] != result[i]}, allowed
        )
        self.assertEqual(
            result[tx : tx + fix.DATA_TX_BYTES], image[tx : tx + fix.DATA_TX_BYTES]
        )
        self.assertEqual(
            verify_protected_blocks(encode_protected_blocks(result))[: len(result)],
            result,
        )
        with self.assertRaisesRegex(ValueError, "function"):
            fix.install_fix(image, b"12345678", address)
        with self.assertRaisesRegex(ValueError, "SHA256"):
            fix.verify_base(image)

    def test_scheduler_call_guard_is_independent(self):
        image = b"\x00" * 0x98000
        tx = fix.DATA_TX_XIP - fix.CP_XIP
        sched = fix.SCHED_XIP - fix.CP_XIP
        with (
            patch.object(
                fix,
                "DATA_TX_SHA256",
                hashlib.sha256(image[tx : tx + fix.DATA_TX_BYTES]).hexdigest(),
            ),
            patch.object(
                fix,
                "SCHED_SHA256",
                hashlib.sha256(image[sched : sched + fix.SCHED_BYTES]).hexdigest(),
            ),
            self.assertRaisesRegex(ValueError, "call"),
        ):
            fix.verify_controller(image)
