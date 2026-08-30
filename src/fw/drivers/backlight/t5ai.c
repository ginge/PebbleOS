/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/backlight.h>
#include <pbl/drivers/display/t5ai.h>

static uint8_t s_brightness;

void backlight_init(void) {
  display_co5300_set_brightness(0u);
}

uint8_t backlight_get_level(uint8_t brightness) {
  return brightness > 100u ? 100u : brightness;
}

void backlight_set_brightness(uint8_t brightness) {
  s_brightness = backlight_get_level(brightness);
  display_co5300_set_brightness(
      (uint8_t)(((uint32_t)s_brightness * 255u) / 100u));
}

void backlight_refresh(void) {
  backlight_set_brightness(s_brightness);
}
