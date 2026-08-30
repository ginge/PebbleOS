# SPDX-License-Identifier: Apache-2.0

import unittest
from pathlib import Path

from tools.t5ai_image import (
    PEBBLE_EXPERIMENTAL_SLOT_SIZE,
    PRIMARY_AP_APP_LOGICAL_OFFSET,
    PRIMARY_AP_APP_LOGICAL_SIZE,
    PRIMARY_AP_APP_PHYSICAL_OFFSET,
    PRIMARY_AP_APP_PHYSICAL_SIZE,
    PRIMARY_AP_APP_XIP_BASE,
    crc16,
    encode_pebble_application,
    encode_protected_blocks,
    validate_pinned_ap_layout,
    verify_protected_blocks,
)

ROOT = Path(__file__).resolve().parents[2]


class T5AIImagePackagingTest(unittest.TestCase):
    def test_system_resource_pack_ceiling(self):
        wscript = (ROOT / "wscript").read_text()
        t5ai_branch = wscript.split("elif ctx.env.CONFIG_SOC_T5AI:", 1)[1]
        t5ai_branch = t5ai_branch.split("elif ctx.env.CONFIG_SOC_SF32LB52:", 1)[0]
        self.assertIn("max_size = 1024 * 1024", t5ai_branch)

    def test_vendor_crc_vectors(self):
        self.assertEqual(crc16(bytes(32)), 0x8029)
        self.assertEqual(crc16(bytes(range(32))), 0x259D)
        self.assertEqual(crc16(b"\xff" * 32), 0x000C)

    def test_encoding_uses_big_endian_crc(self):
        encoded = encode_protected_blocks(bytes(range(32)))
        self.assertEqual(encoded[:32], bytes(range(32)))
        self.assertEqual(encoded[32:], b"\x25\x9d")

    def test_short_block_is_ff_padded(self):
        encoded = encode_protected_blocks(b"123456789")
        self.assertEqual(len(encoded), 34)
        self.assertEqual(encoded[:9], b"123456789")
        self.assertEqual(encoded[9:32], b"\xff" * 23)
        self.assertEqual(encoded[32:], b"\x00\xb9")

    def test_round_trip_and_corruption_detection(self):
        raw = bytes(range(255))
        encoded = encode_protected_blocks(raw)
        decoded = verify_protected_blocks(encoded)
        self.assertEqual(decoded[: len(raw)], raw)
        self.assertEqual(decoded[len(raw) :], b"\xff")

        corrupted = bytearray(encoded)
        corrupted[35] ^= 1
        with self.assertRaisesRegex(
            ValueError, "CRC mismatch in protected block 1"
        ):
            verify_protected_blocks(corrupted)

    def test_empty_image(self):
        self.assertEqual(encode_protected_blocks(b""), b"")
        self.assertEqual(verify_protected_blocks(b""), b"")

    def test_rejects_truncated_protected_image(self):
        with self.assertRaisesRegex(ValueError, "multiple of 34"):
            verify_protected_blocks(b"\xff" * 33)

    def test_pinned_factory_ap_partition_geometry(self):
        validate_pinned_ap_layout()
        self.assertEqual(PRIMARY_AP_APP_PHYSICAL_OFFSET, 0x132000)
        self.assertEqual(PRIMARY_AP_APP_LOGICAL_OFFSET, 0x120000)
        self.assertEqual(PRIMARY_AP_APP_XIP_BASE, 0x02120000)
        self.assertEqual(PRIMARY_AP_APP_PHYSICAL_SIZE, 0x3B8000)
        self.assertEqual(PRIMARY_AP_APP_LOGICAL_SIZE, 0x380000)

    def test_pebble_payload_is_bounded_below_factory_ap_capacity(self):
        self.assertLess(PEBBLE_EXPERIMENTAL_SLOT_SIZE, PRIMARY_AP_APP_LOGICAL_SIZE)
        with self.assertRaisesRegex(ValueError, "exceeds the experimental T5AI slot"):
            encode_pebble_application(
                b"\xff" * (PEBBLE_EXPERIMENTAL_SLOT_SIZE + 1)
            )


if __name__ == "__main__":
    unittest.main()
