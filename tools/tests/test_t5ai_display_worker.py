# SPDX-License-Identifier: Apache-2.0
"""The native display completes synchronously before releasing borrowed pixels."""

import unittest

from tools.tests.native_t5ai_harness import DISPLAY_MAIN, DISPLAY_PREFIX, ROOT, run


class DisplayOwnershipTest(unittest.TestCase):
    def test_native_completion_and_no_descriptor_adapter(self):
        source = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()
        for obsolete in (
            "DisplayRow",
            "NextRowCallback",
            "UpdateCompleteCallback",
            "s_worker",
            "s_class_buffer",
        ):
            self.assertNotIn(obsolete, source)
        run(
            "drivers/display/t5ai/display_co5300.c",
            DISPLAY_PREFIX,
            DISPLAY_MAIN,
            ("CASE=0",),
        )
