# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Compare complete T5AI and stock pixel units using production bitmap layouts."""

import unittest

from tools.tests.t5ai_graphics_harness import run_graphics


class PixelFastTest(unittest.TestCase):
    def test_stock_equivalence(self):
        print(
            run_graphics(
                "tools/tests/fixtures/t5ai_graphics/pixel.c",
                "soc/t5ai/pixel_fast.c",
                stock_pixel=True,
            ),
            end="",
        )
