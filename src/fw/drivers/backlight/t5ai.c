/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/backlight.h>

// Backlight service uses percent; panel brightness remains fixed at full scale.
#define T5AI_BACKLIGHT_FULL_PERCENT 100u

static uint8_t s_brightness;

void backlight_init(void) {
  s_brightness = T5AI_BACKLIGHT_FULL_PERCENT;
}

uint8_t backlight_get_level(uint8_t brightness) {
  (void)brightness;
  return T5AI_BACKLIGHT_FULL_PERCENT;
}

void backlight_set_brightness(uint8_t brightness) {
  (void)brightness;
  // Keep the AMOLED at the maximum level programmed by panel initialization.
  // Do not issue QSPI commands for the stock light service's fade steps: panel
  // register traffic is not serialized with framebuffer updates yet.
  s_brightness = T5AI_BACKLIGHT_FULL_PERCENT;
}

void backlight_refresh(void) {
  backlight_set_brightness(s_brightness);
}
