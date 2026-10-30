# SPDX-License-Identifier: Apache-2.0
"""Exercise the production native display through modeled SPI transfers."""

import unittest

from tools.tests.native_t5ai_harness import DISPLAY_MAIN, DISPLAY_PREFIX, run


class DisplayRotationTest(unittest.TestCase):
    def test_pixel_mapping_partial_ranges_and_rotation(self):
        run(
            "drivers/display/t5ai/display_co5300.c",
            DISPLAY_PREFIX,
            DISPLAY_MAIN,
            ("CASE=0", "CONFIG_T5AI_DISPLAY_RECOVERY=1"),
        )
