# SPDX-License-Identifier: Apache-2.0
"""Compare reference configuration with production without copying driver layouts."""

import re
import unittest
from pathlib import Path

from tools.tests.t5ai_touch_harness import run_touch

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "boards/unhygienix/tuya/src"


def scalar(text, pattern):
    values = re.findall(pattern, text)
    if len(values) != 1:
        raise AssertionError(f"Expected one configuration value: {pattern}: {values}")
    return int(values[0], 0)


def initializer(text, name):
    return text.split(name + " = {", 1)[1].split("\n};", 1)[0]


class ReferenceComparisonTest(unittest.TestCase):
    def test_reference_and_native_pin_contract(self):
        reference = (REFERENCE / "hardware.c").read_text()
        native = (ROOT / "boards/unhygienix/unhygienix.c").read_text()
        touch = initializer(native, "T5AI_TOUCH")
        reset = scalar(touch, r"\.reset = PBL_GPIO\(&T5AI_GPIO, (\d+),")
        self.assertEqual(reset, 43)
        self.assertEqual(
            scalar(reference, r"touch.rst_pin = TUYA_GPIO_NUM_(\d+);"), reset
        )
        self.assertEqual(
            scalar(touch, r"\.interrupt_input = PBL_GPIO\(&T5AI_GPIO, (\d+),"), 42
        )
        for name, expected in (
            ("POWER_ENABLE", 19),
            ("POWER_KEY", 18),
            ("USER_KEY", 12),
            ("CHARGING", 30),
            ("SPEAKER_ENABLE", 28),
        ):
            self.assertEqual(
                scalar(reference, rf"#define {name} TUYA_GPIO_NUM_(\d+)"), expected
            )
        for label, pin in (("Back", 18), ("Select", 12)):
            self.assertEqual(
                scalar(
                    native,
                    rf'PBL_GPIO_KEYS_KEY\("{label}", PBL_GPIO\(&T5AI_GPIO, (\d+),',
                ),
                pin,
            )
        self.assertEqual(
            scalar(native, r"s_amplifier_enable = PBL_GPIO\(&T5AI_GPIO, (\d+),"), 28
        )
        battery = initializer(native, "T5AI_BATTERY_CONFIG")
        self.assertEqual(
            scalar(battery, r"\.charge = PBL_GPIO\(&T5AI_GPIO, (\d+),"), 30
        )
        panel = initializer(native, "CO5300")
        self.assertEqual(
            scalar(reference, r"panel.rst_pin = TUYA_GPIO_NUM_(\d+);"),
            scalar(panel, r"\.reset = PBL_GPIO\(&T5AI_GPIO, (\d+),"),
        )
        for name, pin in (("scl", 20), ("sda", 21)):
            self.assertEqual(
                scalar(reference, rf"touch.i2c_cfg.{name}_pin = TUYA_GPIO_NUM_(\d+);"),
                pin,
            )
        self.assertRegex(native, r"100000u, 20, 21, &s_i2c0_pins")
        self.assertEqual(scalar(reference, r"#define RTC_ADDRESS (0x[0-9a-f]+)"), 0x51)
        self.assertIn("PBL_I2C_DEV(&T5AI_I2C0.bus, 0x51)", native)

    def test_geometry_bus_format_and_orientation_contract(self):
        reference = (REFERENCE / "hardware.c").read_text()
        native = (ROOT / "boards/unhygienix/unhygienix.c").read_text()
        touch = initializer(native, "T5AI_TOUCH")
        for axis in ("width", "height"):
            self.assertEqual(scalar(touch, rf"\.native_{axis} = (\d+),"), 466)
            self.assertIn(f"panel.{axis} = TUYA_PANEL_SIZE;", reference)
        self.assertIn(".inverted_x = true, .inverted_y = true", touch)
        self.assertIn("panel.rotation = TUYA_DISPLAY_ROTATION_0;", reference)
        self.assertIn("panel.pixel_fmt = TUYA_PIXEL_FMT_RGB565;", reference)
        self.assertIn("PBL_PANEL_BUS_FORMAT_RGB565", native)
        self.assertIn("PBL_DISPLAY_FORMAT_ARGB2222", native)
        self.assertEqual(scalar(reference, r"panel.spi_clk = (\d+);"), 80000000)
        self.assertIn("80000000u, PBL_SPI_MODE_0", native)
        self.assertEqual(scalar(reference, r"panel.x_offset = (\d+);"), 6)
        display = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()
        self.assertIn("{0x02, 0, 0x2a, 0, 0, 6, 1, 0xd7}", display)
        for axis in ("WIDTH", "HEIGHT"):
            self.assertEqual(scalar(display, rf"#define PANEL_{axis}\s+(\d+)u"), 466)

    def test_calibration_policy_is_explicitly_distinct(self):
        reference = (REFERENCE / "hardware.c").read_text()
        native = (ROOT / "boards/unhygienix/unhygienix.c").read_text()
        battery = initializer(native, "T5AI_BATTERY_CONFIG")
        expected = {
            "channel": 15,
            "samples": 16,
            "cal_low": 2716,
            "cal_span": 2854,
            "divider_numerator": 2510,
            "divider_denominator": 510,
        }
        for field, value in expected.items():
            self.assertEqual(scalar(battery, rf"\.{field} = (\d+),"), value)
        self.assertIn("cfg.ch_list.data = 1u << 15", reference)
        self.assertIn(
            "tkl_adc_read_single_channel(TUYA_ADC_NUM_0, 15, &raw)", reference
        )
        self.assertIn("i < 16;", reference)
        self.assertIn("if (calibration)", reference)
        self.assertIn("sample.calibrated = true;", reference)
        self.assertIn(
            "tuya_hardware_battery_read(&battery, NULL)",
            (REFERENCE / "main.c").read_text(),
        )

    def test_complete_touch_implementations_preserve_rounding_difference(self):
        run_touch(
            r"""
#include "boards/unhygienix/tuya/src/hardware_core.h"
static_assert(TUYA_PANEL_SIZE == 466 && TUYA_LOGICAL_SIZE == 260);
run_one();
unsigned different = 0;
for (unsigned x = 0; x < TUYA_PANEL_SIZE; ++x) {
  unsigned y = TUYA_PANEL_SIZE - 1 - x;
  uint16_t reference_x, reference_y;
  assert(tuya_map_touch(x, y, &reference_x, &reference_y));
  report(true, x, y);
  if (x) fire_timer(); else fire_irq();
  run_one();
  const struct Sample actual = output[samples-1];
  assert(actual.down);
  // Native mirrors after scaling; the reference mirrors before scaling.
  assert(actual.x >= reference_x && actual.x - reference_x <= 1);
  assert(actual.y >= reference_y && actual.y - reference_y <= 1);
  different += actual.x != reference_x;
}
assert(different != 0);
uint16_t x, y;
assert(!tuya_map_touch(TUYA_PANEL_SIZE, 0, &x, &y));
assert(!tuya_map_touch(0, TUYA_PANEL_SIZE, &x, &y));
""",
            extra_sources=("boards/unhygienix/tuya/src/hardware_core.c",),
        )
