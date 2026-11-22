/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <pbl/drivers/panel/co5300.h>
#include <pbl/soc/t5ai/delay.h>

// No CO5300 singleton is defined: every operation must use its argument.
PBL_PANEL_CO5300_STATE_DEFINE(first);
PBL_PANEL_CO5300_STATE_DEFINE(second);
const struct pbl_device_class pbl_panel_class = {.name = "panel"};
static const struct pbl_spi_bus buses[2];
static const struct pbl_gpio_port ports[2];
static const struct pbl_panel_timing timings[2] = {
  {.hactive = 466, .vactive = 466},
  {.hactive = 320, .vactive = 240},
};
static const struct pbl_panel_co5300 devices[] = {
  {
    .panel = PBL_PANEL_INIT(first, "first", pbl_panel_co5300_init, &pbl_panel_co5300_ops, nullptr,
                            &timings[0], PBL_PANEL_BUS_FORMAT_RGB565),
    .spi = PBL_SPI_DEV(&buses[0], 0, 80000000, PBL_SPI_MODE_0),
    .reset = PBL_GPIO(&ports[0], 29, 0),
    .state = &first_panel_state,
  },
  {
    .panel = PBL_PANEL_INIT(second, "second", pbl_panel_co5300_init, &pbl_panel_co5300_ops, nullptr,
                            &timings[1], PBL_PANEL_BUS_FORMAT_RGB565),
    .spi = PBL_SPI_DEV(&buses[1], 1, 20000000, PBL_SPI_MODE_3),
    .reset = PBL_GPIO(&ports[1], 7, PBL_GPIO_ACTIVE_LOW),
    .state = &second_panel_state,
  },
};
static const struct pbl_panel_co5300 *active;
static uint8_t commands[64], values[64];
static uint32_t delays[8];
static unsigned transfers, delay_count, resets, setups, configurations, locks;
static unsigned fail_transfer, fail_reset;
static int configure_error, setup_error;

int pbl_gpio_configure(const struct pbl_gpio *gpio, uint32_t flags) {
  assert(gpio == &active->reset && flags == PBL_GPIO_OUTPUT_INACTIVE);
  ++configurations;
  return configure_error;
}
int pbl_spi_setup(const struct pbl_spi_dev *spi) {
  assert(spi == &active->spi);
  ++setups;
  return setup_error;
}
int pbl_gpio_set(const struct pbl_gpio *gpio, bool high) {
  assert(gpio == &active->reset);
  assert(high == (resets % 3 != 1));
  return ++resets == fail_reset ? -EIO : 0;
}
void delay_us(uint32_t us) {
  assert(delay_count < sizeof(delays) / sizeof(delays[0]));
  delays[delay_count++] = us;
}
int pbl_spi_transfer(const struct pbl_spi_dev *spi, const struct pbl_spi_xfer *xfers,
                     size_t count) {
  assert(spi == &active->spi && count == 1 && transfers < sizeof(commands));
  const uint8_t *bytes = xfers[0].tx_buf;
  assert(bytes[0] == 2 && !bytes[1] && !bytes[3]);
  assert(xfers[0].len == 4 || xfers[0].len == 5);
  commands[transfers] = bytes[2];
  values[transfers] = xfers[0].len == 5 ? bytes[4] : 0;
  return ++transfers == fail_transfer ? -EIO : 0;
}
int pbl_mutex_lock_lr(struct pbl_mutex *mutex, pbl_timeout_t timeout, uintptr_t lr) {
  (void)lr;
  assert(mutex == &active->state->lock && !mutex->count);
  assert(timeout.ticks == PBL_TICK_FOREVER);
  ++mutex->count;
  ++locks;
  return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *mutex) {
  assert(mutex == &active->state->lock && mutex->count == 1);
  --mutex->count;
}
static void select_device(unsigned index) {
  active = &devices[index];
  transfers = delay_count = resets = fail_transfer = fail_reset = 0;
}
static int brightness(uint8_t value) {
  const unsigned before = locks;
  const int ret = pbl_panel_co5300_set_brightness(&active->panel, value);
  assert(locks == before + 1 && !active->state->lock.count);
  return ret;
}
static void prepare(uint8_t value) {
  static const uint8_t expected[] = {0xfe, 0x19, 0x1c, 0xfe, 0xc4, 0x3a, 0x35,
                                     0x53, 0x51, 0x63, 0x11, 0x20, 0x13};
  static const uint32_t timing[] = {20000, 200000, 120000, 200000};
  assert(active->panel.ops->prepare(&active->panel) == 0);
  assert(transfers == sizeof(expected) && !memcmp(commands, expected, sizeof(expected)));
  assert(values[8] == value && delay_count == 4 && !memcmp(delays, timing, sizeof(timing)));
  assert(active->state->prepared && active->state->brightness_valid);
}
int main(void) {
  for (unsigned index = 0; index < 2; ++index) {
    select_device(index);
    assert(active->panel.dev.init(&active->panel.dev) == 0);
  }
  assert(configurations == 2 && setups == 2);
  configure_error = -ENODEV;
  assert(active->panel.dev.init(&active->panel.dev) == -ENODEV && setups == 2);
  configure_error = 0;
  setup_error = -EIO;
  assert(active->panel.dev.init(&active->panel.dev) == -EIO && setups == 3);
  setup_error = 0;
  select_device(0);
  assert(brightness(35) == 0 && !transfers);
  prepare(35);
  assert(active->panel.ops->enable(&active->panel) == 0 && transfers == 14);
  select_device(1);
  assert(brightness(77) == 0 && !transfers);
  prepare(77);
  assert(first_panel_state.prepared && first_panel_state.applied_brightness == 35);
  assert(active->panel.ops->enable(&active->panel) == 0 && transfers == 14);
  assert(brightness(88) == 0 && transfers == 15 && values[14] == 88);
  select_device(0);
  assert(brightness(88) == 0 && transfers == 1 && values[0] == 88);
  assert(brightness(88) == 0 && transfers == 1);
  // A held first-device lock must not block or satisfy the second device's lock.
  first_panel_state.lock.count = 1;
  select_device(1);
  assert(brightness(89) == 0 && transfers == 1);
  first_panel_state.lock.count = 0;
  fail_transfer = transfers + 1;
  assert(active->panel.ops->disable(&active->panel) == -EIO);
  assert(second_panel_state.display == CO5300_DISPLAY_UNKNOWN);
  assert(first_panel_state.display == CO5300_DISPLAY_ON);
  select_device(0);
  assert(active->panel.ops->enable(&active->panel) == 0 && !transfers);
  fail_transfer = 1;
  assert(brightness(99) == -EIO && !first_panel_state.brightness_valid);
  assert(second_panel_state.brightness_valid && second_panel_state.brightness == 89);
  fail_transfer = 0;
  assert(brightness(99) == 0 && transfers == 2 && first_panel_state.brightness_valid);
  select_device(1);
  fail_reset = 2;
  assert(active->panel.ops->prepare(&active->panel) == -EIO && !transfers);
  assert(!second_panel_state.prepared && !second_panel_state.brightness_valid);
  assert(first_panel_state.prepared && first_panel_state.brightness_valid);
  select_device(1);
  prepare(89);
  transfers = delay_count = 0;
  assert(active->panel.ops->unprepare(&active->panel) == 0);
  assert(transfers == 1 && commands[0] == 0x10 && !second_panel_state.prepared);
#ifdef CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE
  assert(delay_count == 1 && delays[0] == 200000);
#else
  assert(!delay_count);
#endif
  assert(first_panel_state.prepared && first_panel_state.display == CO5300_DISPLAY_ON);
  assert(!first_panel_state.lock.count && !second_panel_state.lock.count && locks == 8);
}
