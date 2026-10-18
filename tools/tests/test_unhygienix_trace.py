# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import tempfile
import unittest
from unittest import mock

from tools import unhygienix_trace


class UnhygienixTraceTest(unittest.TestCase):
    def test_discover_runtime_tty_requires_one_match(self):
        with mock.patch.object(unhygienix_trace.glob, "glob", return_value=[]):
            self.assertIsNone(unhygienix_trace.discover_runtime_tty("ignored"))
        with mock.patch.object(
            unhygienix_trace.glob, "glob", return_value=["/dev/runtime"]
        ):
            self.assertEqual(
                unhygienix_trace.discover_runtime_tty("ignored"), "/dev/runtime"
            )
        with mock.patch.object(
            unhygienix_trace.glob,
            "glob",
            return_value=["/dev/runtime-b", "/dev/runtime-a"],
        ):
            with self.assertRaisesRegex(RuntimeError, "multiple runtime UARTs"):
                unhygienix_trace.discover_runtime_tty("ignored")

    def test_wait_accepts_explicit_tty(self):
        with tempfile.NamedTemporaryFile() as tty:
            self.assertEqual(
                unhygienix_trace.wait_for_runtime_tty(tty.name, "ignored", 0.01),
                tty.name,
            )

    def test_wait_timeout_names_target(self):
        with (
            mock.patch.object(unhygienix_trace.os.path, "exists", return_value=False),
            mock.patch.object(unhygienix_trace.time, "sleep"),
        ):
            with self.assertRaisesRegex(TimeoutError, "/dev/missing"):
                unhygienix_trace.wait_for_runtime_tty("/dev/missing", "ignored", 0.0001)

    def test_cli_defaults_are_unhygienix_safe(self):
        args = unhygienix_trace.parse_args([])
        self.assertEqual(args.baud, 460800)
        self.assertIn("USB_Dual_Serial", args.tty_glob)
        self.assertTrue(args.tty_glob.endswith("-if02"))
        self.assertEqual(args.duration, 0)
        self.assertEqual(args.command_timeout, 5)
        self.assertEqual(args.reconnect_idle, 10)


if __name__ == "__main__":
    unittest.main()
