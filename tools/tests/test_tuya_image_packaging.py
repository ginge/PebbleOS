# SPDX-License-Identifier: Apache-2.0

import unittest
import hashlib
from pathlib import Path

from tools.t5ai_image import (
    PEBBLE_EXPERIMENTAL_SLOT_SIZE,
    PEBBLE_EXPERIMENTAL_SLOT_PHYSICAL_SIZE,
    PRIMARY_AP_APP_LOGICAL_OFFSET,
    PRIMARY_AP_APP_LOGICAL_SIZE,
    PRIMARY_AP_APP_PHYSICAL_OFFSET,
    PRIMARY_AP_APP_PHYSICAL_SIZE,
    PRIMARY_AP_APP_XIP_BASE,
    crc16,
    encode_pebble_application,
    encode_protected_blocks,
    package_ap_qio,
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

    def test_qio_packaging_preserves_boot_prefix_and_ap_trailer(self):
        prefix = bytes((index * 17) & 0xFF for index in range(
            PRIMARY_AP_APP_PHYSICAL_OFFSET
        ))
        trailer = bytes((index * 29) & 0xFF for index in range(4096))
        base = prefix + trailer
        digest = hashlib.sha256(base).hexdigest()
        vector = (
            (0x28020000).to_bytes(4, "little")
            + (PRIMARY_AP_APP_XIP_BASE + 0x101).to_bytes(4, "little")
            + b"Pebble"
        )
        protected = encode_pebble_application(vector)
        packaged = package_ap_qio(base, protected, digest)
        self.assertEqual(packaged[:PRIMARY_AP_APP_PHYSICAL_OFFSET], prefix)
        self.assertEqual(len(packaged), len(base))
        decoded = verify_protected_blocks(packaged[
            PRIMARY_AP_APP_PHYSICAL_OFFSET:
            PRIMARY_AP_APP_PHYSICAL_OFFSET + len(protected)])
        self.assertEqual(decoded[: len(vector)], vector)
        self.assertEqual(packaged[PRIMARY_AP_APP_PHYSICAL_OFFSET + len(protected):],
                         trailer[len(protected):])

    def test_qio_packaging_rejects_wrong_base_and_bad_vectors(self):
        base = b"\xaa" * (PRIMARY_AP_APP_PHYSICAL_OFFSET + 1)
        digest = hashlib.sha256(base).hexdigest()
        valid_vector = (
            (0x28020000).to_bytes(4, "little")
            + (PRIMARY_AP_APP_XIP_BASE + 0x101).to_bytes(4, "little")
        )
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            package_ap_qio(base, encode_protected_blocks(valid_vector), "0" * 64)
        with self.assertRaisesRegex(ValueError, "stack pointer"):
            package_ap_qio(
                base,
                encode_protected_blocks(b"\0" * 4 + valid_vector[4:]),
                digest,
            )
        with self.assertRaisesRegex(ValueError, "reset vector"):
            package_ap_qio(
                base,
                encode_protected_blocks(valid_vector[:4] + b"\0" * 4),
                digest,
            )


if __name__ == "__main__":
    unittest.main()
