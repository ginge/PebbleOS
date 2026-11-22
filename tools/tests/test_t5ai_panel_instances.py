# SPDX-License-Identifier: Apache-2.0
"""Real native panel instances must isolate hardware routing, caches and locks."""

import unittest

from tools.tests.t5ai_panel_harness import ROOT, run_panel


class PanelInstancesTest(unittest.TestCase):
    def test_two_instances_and_failures_without_a_board_singleton(self):
        fixture = (ROOT / "tools/tests/fixtures/t5ai_panel/instances.c").read_text()
        for defines in ([], ["CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE"]):
            with self.subTest(defines=defines):
                run_panel(fixture, "", defines=defines)
