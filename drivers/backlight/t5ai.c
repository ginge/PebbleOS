/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/backlight.h>
#include <pbl/drivers/backlight/t5ai.h>
#include <pbl/drivers/display/t5ai.h>

static uint8_t prv_get_level(const struct pbl_backlight *bl, uint8_t brightness) {
  const uint8_t level = ((uint16_t)brightness * UINT8_MAX + 99u) / 100u;
  return level < T5AI_AMOLED_MIN_BRIGHTNESS ? T5AI_AMOLED_MIN_BRIGHTNESS : level;
}

static int prv_set_brightness(const struct pbl_backlight *bl, uint8_t brightness) {
  display_co5300_set_brightness(prv_get_level(bl, brightness));
  return 0;
}

static int prv_init(const struct pbl_backlight *bl) {
  return prv_set_brightness(bl, 0u);
}

static int prv_refresh(const struct pbl_backlight *bl) {
  return prv_set_brightness(bl, bl->state->brightness);
}

static const struct pbl_backlight_ops s_ops = {
  .init = prv_init,
  .set_brightness = prv_set_brightness,
  .get_level = prv_get_level,
  .refresh = prv_refresh,
};

PBL_BACKLIGHT_STATE_DEFINE(pbl_backlight_t5ai);
const struct pbl_backlight pbl_backlight_t5ai =
    PBL_BACKLIGHT_INIT(pbl_backlight_t5ai, "backlight", nullptr, &s_ops);
PBL_DEVICE_REGISTER(pbl_backlight_t5ai, &pbl_backlight_t5ai.dev);
