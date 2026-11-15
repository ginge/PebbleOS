# SPDX-License-Identifier: Apache-2.0
"""Unsupported shutdown must return before touching hardware or storage."""

import unittest

from tools.tests.native_t5ai_harness import ROOT, run

PREFIX = r"""
#include <assert.h>
#include <errno.h>
typedef unsigned RebootReasonCode;
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_WRN(...) (++warnings)
static unsigned warnings;
"""


class UnhygienixStandbyTest(unittest.TestCase):
    def test_every_reason_returns_without_teardown(self):
        run(
            "boards/unhygienix/unhygienix_standby.c",
            PREFIX,
            "int main(void){for(unsigned i=0;i<256;++i)assert(enter_standby(i)==-ENOTSUP);"
            "assert(warnings==256);return 0;}",
        )

    def test_board_selects_only_local_implementation(self):
        kernel = (ROOT / "pebble/kernel/CMakeLists.txt").read_text()
        board = (ROOT / "pebble/board/CMakeLists.txt").read_text()
        self.assertIn(
            "if(CONFIG_BOARD_UNHYGIENIX)\n  pbl_filter_sources(sources EXCLUDE util/standby.c)",
            kernel,
        )
        self.assertIn("boards/unhygienix_standby.c", board)
