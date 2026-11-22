# SPDX-License-Identifier: Apache-2.0
"""Minimum panel timings through the real native sleep conversion and complete driver."""

import subprocess
import unittest

from tools.tests.t5ai_panel_harness import ROOT, run_panel

PREFIX = (ROOT / "tools/tests/fixtures/t5ai_panel/delays.c").read_text()
MAIN = r"""
int main(void) {
  assert(PBL_TICK_HZ == 1000);
  for (unsigned started = 0; started < 2; ++started) {
    for (unsigned error = 0; error <= 13; ++error) {
      reset_case(started);
      fail_transfer = error; fail_gpio = 0;
      assert((prv_prepare(&CO5300.panel) == 0) == !error);
      assert(transfers == (error ? error : 13));
      assert(wait_count == (error && error <= 11 ? 3u : 4u));
      assert(sleeps == (started ? wait_count : 0u));
      assert(busy_calls == (started ? 0u : wait_count));
      assert(panel_state.prepared == !error);
      if (!error) assert(wall_us >= 540000);
    }
    for (unsigned error = 1; error <= 3; ++error) {
      reset_case(started);
      fail_gpio = error; fail_transfer = 0;
      assert(prv_prepare(&CO5300.panel) == -EIO);
      assert(!transfers && wait_count == error - 1);
      assert(sleeps == (started ? wait_count : 0u));
      assert(!panel_state.prepared && !panel_state.brightness_valid);
    }
    for (unsigned error = 0; error < 2; ++error) {
      reset_case(started); sleeping_in = true;
      fail_gpio = 0; fail_transfer = error;
      assert((prv_unprepare(&CO5300.panel) == 0) == !error);
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
      assert(wait_count == (error ? 0u : 1u));
#else
      assert(!wait_count);
#endif
      assert(sleeps == (started ? wait_count : 0u));
      assert(busy_calls == (started ? 0u : wait_count));
      assert(transfers == 1 && !panel_state.prepared);
    }
  }
  return 0;
}
"""


class PanelDelaysTest(unittest.TestCase):
    def test_minimum_timing_sequence_and_each_error_in_both_contexts(self):
        for defines in ([], ["CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE"]):
            with self.subTest(defines=defines):
                run_panel(PREFIX, MAIN, defines=defines)

    def test_post_start_blocking_context_contract_is_enforced(self):
        for invalid in ("in_isr", "irq_locked", "sched_locked"):
            with self.subTest(context=invalid):
                main = (
                    "static void expected_panic(void) { _Exit(73); }\n"
                    "int main(void) { reset_case(true); panic_observer = expected_panic; "
                    + invalid
                    + " = true; prv_prepare(&CO5300.panel); return 0; }\n"
                )
                with self.assertRaises(subprocess.CalledProcessError) as raised:
                    run_panel(PREFIX, main)
                self.assertEqual(raised.exception.returncode, 73)
                self.assertTrue(raised.exception.cmd[0].endswith("/test"))
