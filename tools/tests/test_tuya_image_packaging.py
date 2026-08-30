# SPDX-License-Identifier: Apache-2.0

import unittest

from tools.t5ai_image import (
    crc16,
    encode_protected_blocks,
    verify_protected_blocks,
)


class T5AIImagePackagingTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
