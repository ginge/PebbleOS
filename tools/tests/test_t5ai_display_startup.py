# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Validate the actual board display against native compositor startup."""

import unittest
from pathlib import Path

from tools.tests.test_t5ai_native_objects import body
from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]


class DisplayStartupTests(unittest.TestCase):
    def test_native_compositor_accepts_board_display_and_preserves_rectangular_rows(self):
        board = (ROOT / "boards/unhygienix/unhygienix.c").read_text()
        declaration = board[board.index("static const uint8_t s_round_mask") : board.index("PBL_DEVICE_REGISTER(T5AI_DISPLAY")]
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define nullptr NULL
#define PBL_DISPLAY_WIDTH 260
#define PBL_DISPLAY_HEIGHT 260
#define PBL_BW 0
#define PBL_ROUND 1
#define PBL_ASSERTN(x) assert(x)
enum pbl_display_format {PBL_DISPLAY_FORMAT_MONO, PBL_DISPLAY_FORMAT_ARGB2222};
struct pbl_display {
  uint16_t width, height; enum pbl_display_format format;
  const uint8_t *round_mask;
};
#define PBL_DISPLAY_INIT(sym,name,ops,deps,w,h,fmt,rotated,...) \
  {.width=w,.height=h,.format=fmt,__VA_ARGS__}
"""
        harness += declaration
        harness += "\n#define DISPLAY (&T5AI_DISPLAY)\n"
        harness += body((ROOT / "pebble/services/compositor/compositor_display.c").read_text(), "compositor_display_init")
        harness += r"""
int main(void) {
  compositor_display_init();
  assert(sizeof(s_round_mask) == DISPLAY->height / 2);
  for (unsigned y=0; y<DISPLAY->height; ++y) {
    unsigned half_y = y<DISPLAY->height/2 ? y : DISPLAY->height-1-y;
    assert(DISPLAY->round_mask[half_y] == 0);
  }
  return 0;
}
"""
        run_c(harness, {})


if __name__ == "__main__":
    unittest.main()
