# SPDX-License-Identifier: Apache-2.0
"""Complete recorder and real scheduler signature: retain the interrupt-lock key."""

import unittest

from tools.tests.t5ai_graphics_harness import run_graphics


class DrawProfileTest(unittest.TestCase):
    def test_sched_wrapper_forwards_key_in_bypass_and_render_paths(self):
        run_graphics(
            "tools/tests/fixtures/t5ai_graphics/draw_profile.c",
            None,
            core_stub=True,
            config_values={"T5AI_DRAW_PROFILE": "y"},
        )
