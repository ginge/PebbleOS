# SPDX-License-Identifier: Apache-2.0
"""Differential compositor validation using complete stock bitmap units."""

import unittest

from tools.tests.t5ai_graphics_harness import run_graphics


class NativeCopyTest(unittest.TestCase):
    def test_complete_units_rows_padding_clipping_and_blending(self):
        print(
            run_graphics(
                "tools/tests/fixtures/t5ai_graphics/compositor.c",
                "soc/t5ai/compositor_fast.c",
            ),
            end="",
        )

    @unittest.expectedFailure
    def test_stock_legacy_wide_signed_shift(self):
        # Stock 1-bit-to-8-bit bitblt shifts signed 1 by 31; retain the reproducer.
        run_graphics(
            "tools/tests/fixtures/t5ai_graphics/compositor.c",
            "soc/t5ai/compositor_fast.c",
            defines=("LEGACY_WIDE",),
        )
