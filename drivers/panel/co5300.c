/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/panel/co5300.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
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

// Panel transactions run in thread context with interrupts/preemption enabled.
static void prv_delay(uint32_t us) {
  if (!pbl_kernel_is_started()) {
    delay_us(us);
    return;
  }
  PBL_ASSERTN(!pbl_in_isr() && !pbl_irq_is_locked() && pbl_kernel_is_running());
  pbl_sleep_us(us);
}

static const struct pbl_panel_co5300 *prv_co5300(const struct pbl_panel *panel) {
  return container_of(panel, const struct pbl_panel_co5300, panel);
}
static int prv_command(const struct pbl_panel_co5300 *co5300, uint8_t command, const uint8_t *data,
                       size_t length) {
  uint8_t bytes[CO5300_QSPI_HEADER_BYTES + CO5300_REGISTER_VALUE_BYTES] = {
    CO5300_QSPI_WRITE_COMMAND, CO5300_QSPI_RESERVED, command,
    CO5300_QSPI_RESERVED,      length ? *data : 0u,
  };
  const struct pbl_spi_xfer xfer = {.tx_buf = bytes, .len = CO5300_QSPI_HEADER_BYTES + length};
  return pbl_spi_transfer(&co5300->spi, &xfer, 1);
}
int pbl_panel_co5300_init(const struct pbl_device *dev) {
  const struct pbl_panel_co5300 *co5300 =
      container_of(dev, const struct pbl_panel_co5300, panel.dev);
  int ret = pbl_gpio_configure(&co5300->reset, PBL_GPIO_OUTPUT_INACTIVE);
  return ret != 0 ? ret : pbl_spi_setup(&co5300->spi);
}
static int prv_prepare(const struct pbl_panel *panel) {
  const struct pbl_panel_co5300 *co5300 = prv_co5300(panel);
  struct pbl_panel_co5300_state *state = co5300->state;
  state->prepared = state->brightness_valid = false;
  state->display = CO5300_DISPLAY_UNKNOWN;
  int ret = pbl_gpio_set(&co5300->reset, true);
  if (ret != 0)
    return ret;
  prv_delay(CO5300_RESET_PRE_DELAY_US);
  ret = pbl_gpio_set(&co5300->reset, false);
  if (ret != 0)
    return ret;
  prv_delay(CO5300_RESET_PULSE_US);
  ret = pbl_gpio_set(&co5300->reset, true);
  if (ret != 0)
    return ret;
  prv_delay(CO5300_RESET_SETTLE_US);
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
    {CO5300_CMD_WRITE_BRIGHTNESS, state->brightness},
    {CO5300_CMD_WRITE_HBM_BRIGHTNESS, CO5300_HBM_BRIGHTNESS_MAX},
  };
  for (unsigned i = 0; i < ARRAY_LENGTH(init); ++i) {
    int ret = prv_command(co5300, init[i].command, &init[i].value, CO5300_REGISTER_VALUE_BYTES);
    if (ret != 0)
      return ret;
  }
  ret = prv_command(co5300, CO5300_CMD_SLEEP_OUT, nullptr, 0);
  if (ret != 0)
    return ret;
  prv_delay(CO5300_SLEEP_OUT_DELAY_US);
  ret = prv_command(co5300, CO5300_CMD_INVERSION_OFF, nullptr, 0);
  if (ret == 0)
    ret = prv_command(co5300, CO5300_CMD_NORMAL_MODE_ON, nullptr, 0);
  state->prepared = ret == 0;
  if (state->prepared) {
    state->applied_brightness = state->brightness;
    state->brightness_valid = true;
  }
  return ret;
}
static int prv_enable(const struct pbl_panel *panel) {
  const struct pbl_panel_co5300 *co5300 = prv_co5300(panel);
  struct pbl_panel_co5300_state *state = co5300->state;
  if (state->display == CO5300_DISPLAY_ON)
    return 0;
  const int ret = prv_command(co5300, CO5300_CMD_DISPLAY_ON, nullptr, 0);
  state->display = ret == 0 ? CO5300_DISPLAY_ON : CO5300_DISPLAY_UNKNOWN;
  return ret;
}
static int prv_disable(const struct pbl_panel *panel) {
  const struct pbl_panel_co5300 *co5300 = prv_co5300(panel);
  struct pbl_panel_co5300_state *state = co5300->state;
  if (state->display == CO5300_DISPLAY_OFF)
    return 0;
  const int ret = prv_command(co5300, CO5300_CMD_DISPLAY_OFF, nullptr, 0);
  state->display = ret == 0 ? CO5300_DISPLAY_OFF : CO5300_DISPLAY_UNKNOWN;
  return ret;
}
static int prv_unprepare(const struct pbl_panel *panel) {
  const struct pbl_panel_co5300 *co5300 = prv_co5300(panel);
  struct pbl_panel_co5300_state *state = co5300->state;
  state->display = CO5300_DISPLAY_UNKNOWN;
  state->brightness_valid = false;
  int ret = prv_command(co5300, CO5300_CMD_SLEEP_IN, nullptr, 0);
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  if (ret == 0)
    prv_delay(CO5300_SLEEP_IN_DELAY_US);
#endif
  state->prepared = false;
  return ret;
}
int pbl_panel_co5300_set_brightness(const struct pbl_panel *panel, uint8_t level) {
  const struct pbl_panel_co5300 *co5300 = prv_co5300(panel);
  struct pbl_panel_co5300_state *state = co5300->state;
  pbl_mutex_lock(&state->lock, PBL_FOREVER);
  state->brightness = level;
  int ret = 0;
  if (state->prepared && (!state->brightness_valid || level != state->applied_brightness)) {
    ret = prv_command(co5300, CO5300_CMD_WRITE_BRIGHTNESS, &level, CO5300_REGISTER_VALUE_BYTES);
    state->brightness_valid = ret == 0;
    if (ret == 0)
      state->applied_brightness = level;
  }
  pbl_mutex_unlock(&state->lock);
  return ret;
}
const struct pbl_panel_ops pbl_panel_co5300_ops = {
  .prepare = prv_prepare,
  .enable = prv_enable,
  .disable = prv_disable,
  .unprepare = prv_unprepare,
};
