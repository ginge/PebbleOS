# SPDX-License-Identifier: Apache-2.0

import tempfile
import unittest
from unittest import mock

from tools import t5ai_trace


class T5AITraceTest(unittest.TestCase):
    def test_discover_runtime_tty_requires_one_match(self):
        with mock.patch.object(t5ai_trace.glob, "glob", return_value=[]):
            self.assertIsNone(t5ai_trace.discover_runtime_tty("ignored"))
        with mock.patch.object(t5ai_trace.glob, "glob", return_value=["/dev/runtime"]):
            self.assertEqual(t5ai_trace.discover_runtime_tty("ignored"), "/dev/runtime")
        with mock.patch.object(
            t5ai_trace.glob, "glob", return_value=["/dev/runtime-b", "/dev/runtime-a"]
        ):
            with self.assertRaisesRegex(RuntimeError, "multiple runtime UARTs"):
                t5ai_trace.discover_runtime_tty("ignored")

    def test_wait_accepts_explicit_tty(self):
        with tempfile.NamedTemporaryFile() as tty:
            self.assertEqual(
                t5ai_trace.wait_for_runtime_tty(tty.name, "ignored", 0.01), tty.name
            )

    def test_wait_timeout_names_target(self):
        with mock.patch.object(t5ai_trace.os.path, "exists", return_value=False), mock.patch.object(
            t5ai_trace.time, "sleep"
        ):
            with self.assertRaisesRegex(TimeoutError, "/dev/missing"):
                t5ai_trace.wait_for_runtime_tty("/dev/missing", "ignored", 0.0001)

    def test_cli_defaults_are_dvt3_safe(self):
        args = t5ai_trace.parse_args([])
        self.assertEqual(args.baud, 460800)
        self.assertIn("USB_Dual_Serial", args.tty_glob)
        self.assertTrue(args.tty_glob.endswith("-if02"))
        self.assertEqual(args.duration, 0)
        self.assertEqual(args.command_timeout, 5)
        self.assertEqual(args.reconnect_idle, 10)


if __name__ == "__main__":
    unittest.main()
