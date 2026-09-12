/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/backlight.h>
#include <pbl/drivers/display/t5ai.h>

static uint8_t s_brightness;

void backlight_init(void) {
  s_brightness = 100u;
  display_co5300_set_brightness(0xffu);
}

uint8_t backlight_get_level(uint8_t brightness) {
  (void)brightness;
  return 100u;
}

void backlight_set_brightness(uint8_t brightness) {
  (void)brightness;
  // Keep the AMOLED at maximum brightness throughout panel bring-up. Dynamic
  // brightness, including the light service's zero/off request, is restored
  // only after the native panel transport produces reliable visible output.
  s_brightness = 100u;
  display_co5300_set_brightness(0xffu);
}

void backlight_refresh(void) {
  backlight_set_brightness(s_brightness);
}
