/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/backlight.h>
#include <pbl/drivers/display/t5ai.h>
#include <pbl/services/boot_splash/t5ai.h>
#include "board/board.h"

static uint8_t s_brightness;

void backlight_init(void) {
  s_brightness = 0u;
  backlight_refresh();
  t5ai_boot_splash_render();
  backlight_set_brightness(BOARD_CONFIG.backlight_on_percent);
}

uint8_t backlight_get_level(uint8_t brightness) {
  if (brightness > 100u) {
    brightness = 100u;
  }
  const uint8_t level = ((uint16_t)brightness * UINT8_MAX + 99u) / 100u;
  return level < T5AI_AMOLED_MIN_BRIGHTNESS ? T5AI_AMOLED_MIN_BRIGHTNESS : level;
}

void backlight_set_brightness(uint8_t brightness) {
  s_brightness = brightness;
  display_co5300_set_brightness(backlight_get_level(brightness));
}

void backlight_refresh(void) {
  backlight_set_brightness(s_brightness);
}
