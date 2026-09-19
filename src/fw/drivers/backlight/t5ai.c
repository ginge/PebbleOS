/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/backlight.h>

static uint8_t s_brightness;

void backlight_init(void) {
  s_brightness = 100u;
}

uint8_t backlight_get_level(uint8_t brightness) {
  (void)brightness;
  return 100u;
}

void backlight_set_brightness(uint8_t brightness) {
  (void)brightness;
  // Keep the AMOLED at the maximum level programmed by panel initialization.
  // Do not issue QSPI commands for the stock light service's fade steps: panel
  // register traffic is not serialized with framebuffer updates yet.
  s_brightness = 100u;
}

void backlight_refresh(void) {
  backlight_set_brightness(s_brightness);
}
