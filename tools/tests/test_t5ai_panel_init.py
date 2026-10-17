# SPDX-License-Identifier: Apache-2.0
"""Run the production panel sequence, including every register-write failure."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class PanelInitTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_panel_order_reset_timing_and_write_failure(self):
        display = (ROOT / "fw/drivers/display/t5ai/display_co5300.c").read_text()
        start = display.index("static bool prv_panel_init(void)")
        end = display.index("\nvoid display_init(void)", start)
        defines = "\n".join(line for line in display.splitlines()
                            if line.startswith("#define PANEL_") and not line.endswith("\\"))
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { unsigned pin; bool active_high; } OutputConfig;
#define GPIO_OType_PP 1
static unsigned calls, fail_at, reset_step, delays;
static uint8_t s_brightness;
static const uint8_t commands[] = {
  0xfe, 0x19, 0x1c, 0xfe, 0xc4, 0x3a, 0x35, 0x53, 0x51, 0x63,
  0x11, 0x20, 0x13, 0x29
};
static const uint8_t values[] = {0x20, 0x10, 0xa0, 0, 0x80, 0x55, 0, 0x20, 0, 0xff};
static void gpio_output_init(const OutputConfig *config, unsigned type) {
  assert(config->pin == 29 && config->active_high && type == GPIO_OType_PP);
  assert(reset_step++ == 0);
}
static void gpio_output_set(const OutputConfig *config, bool value) {
  assert(config->pin == 29 && reset_step >= 1 && reset_step <= 3);
  assert(value == (reset_step != 2));
  assert(delays == reset_step - 1);
  ++reset_step;
}
static void delay_us(unsigned us) {
  static const unsigned expected[] = {20000, 200000, 120000, 200000};
  assert(delays < 4 && us == expected[delays]);
  if (delays < 3) assert(reset_step == delays + 2);
  else assert(calls == 11);
  ++delays;
}
static bool prv_register(uint8_t command, const uint8_t *data, size_t size) {
  assert(calls < sizeof(commands) && reset_step == 4);
  assert(delays == (calls < 11 ? 3 : 4));
  assert(command == commands[calls]);
  if (calls < sizeof(values)) {
    assert(data && size == 1);
    assert(*data == (calls == 8 ? s_brightness : values[calls]));
  } else assert(!data && !size);
  return ++calls != fail_at;
}
""" + defines + "\n" + display[start:end] + r"""
int main(void) {
  for (unsigned brightness = 35; brightness <= 255; brightness += 110) {
    s_brightness = brightness;
    for (fail_at = 0; fail_at <= sizeof(commands); ++fail_at) {
      calls = reset_step = delays = 0;
      assert(prv_panel_init() == (fail_at == 0));
      assert(calls == (fail_at ? fail_at : sizeof(commands)));
      assert(delays == (fail_at && fail_at <= 11 ? 3 : 4));
    }
  }
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "panel.c"
            binary = Path(directory) / "panel"
            source.write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
