/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/panel/co5300.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/util/misc.h>

#define CO5300_QSPI_WRITE_COMMAND   0x02u
#define CO5300_QSPI_RESERVED        0x00u
#define CO5300_QSPI_HEADER_BYTES    4u
#define CO5300_REGISTER_VALUE_BYTES 1u

#define CO5300_CMD_SLEEP_IN              0x10u
#define CO5300_CMD_SLEEP_OUT             0x11u
#define CO5300_CMD_NORMAL_MODE_ON        0x13u
#define CO5300_CMD_INVERSION_OFF         0x20u
#define CO5300_CMD_DISPLAY_OFF           0x28u
#define CO5300_CMD_DISPLAY_ON            0x29u
#define CO5300_CMD_TEARING_EFFECT_ON     0x35u
#define CO5300_CMD_PIXEL_FORMAT          0x3au
#define CO5300_CMD_WRITE_BRIGHTNESS      0x51u
#define CO5300_CMD_WRITE_CONTROL_DISPLAY 0x53u
#define CO5300_CMD_WRITE_HBM_BRIGHTNESS  0x63u
#define CO5300_CMD_SPI_MODE              0xc4u
#define CO5300_CMD_PAGE_SELECT           0xfeu

#define CO5300_PAGE_STANDARD       0x00u
#define CO5300_PAGE_CMD2_EXTENSION 0x20u
// Preserve the module vendor's page-20 settings; register functions are undocumented.
#define CO5300_PAGE20_REG_19             0x19u
#define CO5300_PAGE20_REG_19_INIT        0x10u
#define CO5300_PAGE20_REG_1C             0x1cu
#define CO5300_PAGE20_REG_1C_INIT        0xa0u
#define CO5300_SPI_RAM_WRITE_ENABLE      (1u << 7)
#define CO5300_PIXEL_FORMAT_RGB565       0x55u
#define CO5300_TEARING_VBLANK_ONLY       0x00u
#define CO5300_CONTROL_BRIGHTNESS_ENABLE (1u << 5)
#define CO5300_HBM_BRIGHTNESS_MAX        0xffu

#define CO5300_RESET_PRE_DELAY_US 20000u
#define CO5300_RESET_PULSE_US     200000u
#define CO5300_RESET_SETTLE_US    120000u
#define CO5300_SLEEP_OUT_DELAY_US 200000u
#define CO5300_SLEEP_IN_DELAY_US  200000u

static uint8_t s_brightness;
static bool s_prepared;
PBL_MUTEX_DEFINE(co5300_lock);
static int prv_command(uint8_t command, const uint8_t *data, size_t length) {
  uint8_t bytes[CO5300_QSPI_HEADER_BYTES + CO5300_REGISTER_VALUE_BYTES] = {
    CO5300_QSPI_WRITE_COMMAND, CO5300_QSPI_RESERVED, command,
    CO5300_QSPI_RESERVED,      length ? *data : 0u,
  };
  const struct pbl_spi_xfer xfer = {.tx_buf = bytes, .len = CO5300_QSPI_HEADER_BYTES + length};
  return pbl_spi_transfer(&CO5300.spi, &xfer, 1);
}
int pbl_panel_co5300_init(const struct pbl_device *dev) {
  (void)dev;
  int ret = pbl_gpio_configure(&CO5300.reset, PBL_GPIO_OUTPUT_INACTIVE);
  return ret != 0 ? ret : pbl_spi_setup(&CO5300.spi);
}
static int prv_prepare(const struct pbl_panel *panel) {
  (void)panel;
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  s_prepared = false;
#endif
  (void)pbl_gpio_set(&CO5300.reset, true);
  delay_us(CO5300_RESET_PRE_DELAY_US);
  (void)pbl_gpio_set(&CO5300.reset, false);
  delay_us(CO5300_RESET_PULSE_US);
  (void)pbl_gpio_set(&CO5300.reset, true);
  delay_us(CO5300_RESET_SETTLE_US);
  const struct {
    uint8_t command;
    uint8_t value;
  } init[] = {
    {CO5300_CMD_PAGE_SELECT, CO5300_PAGE_CMD2_EXTENSION},
    {CO5300_PAGE20_REG_19, CO5300_PAGE20_REG_19_INIT},
    {CO5300_PAGE20_REG_1C, CO5300_PAGE20_REG_1C_INIT},
    {CO5300_CMD_PAGE_SELECT, CO5300_PAGE_STANDARD},
    {CO5300_CMD_SPI_MODE, CO5300_SPI_RAM_WRITE_ENABLE},
    {CO5300_CMD_PIXEL_FORMAT, CO5300_PIXEL_FORMAT_RGB565},
    {CO5300_CMD_TEARING_EFFECT_ON, CO5300_TEARING_VBLANK_ONLY},
    {CO5300_CMD_WRITE_CONTROL_DISPLAY, CO5300_CONTROL_BRIGHTNESS_ENABLE},
    {CO5300_CMD_WRITE_BRIGHTNESS, s_brightness},
    {CO5300_CMD_WRITE_HBM_BRIGHTNESS, CO5300_HBM_BRIGHTNESS_MAX},
  };
  for (unsigned i = 0; i < ARRAY_LENGTH(init); ++i) {
    int ret = prv_command(init[i].command, &init[i].value, CO5300_REGISTER_VALUE_BYTES);
    if (ret != 0)
      return ret;
  }
  int ret = prv_command(CO5300_CMD_SLEEP_OUT, nullptr, 0);
  if (ret != 0)
    return ret;
  delay_us(CO5300_SLEEP_OUT_DELAY_US);
  ret = prv_command(CO5300_CMD_INVERSION_OFF, nullptr, 0);
  if (ret == 0)
    ret = prv_command(CO5300_CMD_NORMAL_MODE_ON, nullptr, 0);
  s_prepared = ret == 0;
  return ret;
}
static int prv_enable(const struct pbl_panel *panel) {
  (void)panel;
  return prv_command(CO5300_CMD_DISPLAY_ON, nullptr, 0);
}
static int prv_disable(const struct pbl_panel *panel) {
  (void)panel;
  return prv_command(CO5300_CMD_DISPLAY_OFF, nullptr, 0);
}
static int prv_unprepare(const struct pbl_panel *panel) {
  (void)panel;
  int ret = prv_command(CO5300_CMD_SLEEP_IN, nullptr, 0);
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  if (ret == 0)
    delay_us(CO5300_SLEEP_IN_DELAY_US);
#endif
  s_prepared = false;
  return ret;
}
int pbl_panel_co5300_set_brightness(uint8_t level) {
  pbl_mutex_lock(&co5300_lock, PBL_FOREVER);
  s_brightness = level;
  const int ret =
      s_prepared ? prv_command(CO5300_CMD_WRITE_BRIGHTNESS, &level, CO5300_REGISTER_VALUE_BYTES)
                 : 0;
  pbl_mutex_unlock(&co5300_lock);
  return ret;
}
const struct pbl_panel_ops pbl_panel_co5300_ops = {
  .prepare = prv_prepare,
  .enable = prv_enable,
  .disable = prv_disable,
  .unprepare = prv_unprepare,
};
