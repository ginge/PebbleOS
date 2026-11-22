# SPDX-License-Identifier: Apache-2.0
"""Real layout freshness and render ownership for the T5AI text cache."""

import unittest

from tools.tests.t5ai_graphics_harness import run_graphics


class TextNodeCacheTest(unittest.TestCase):
    def test_complete_layout_freshness_and_render_ownership(self):
        print(
            run_graphics(
                "tools/tests/fixtures/t5ai_graphics/text_cache.c",
                "soc/t5ai/text_node_cache.c",
                text_cache=True,
            ),
            end="",
        )
