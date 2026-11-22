/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdint.h>

#include <pbl/drivers/panel/co5300.h>
#include <pbl/kernel/time.h>

static struct pbl_panel_co5300_state panel_state = {.lock = PBL_MUTEX_INITIALIZER};
const struct pbl_panel_co5300 CO5300 = {.state = &panel_state};
static unsigned gpio_calls, transfers, busy_calls, wait_count, fail_gpio, fail_transfer;
static uint64_t wall_us, last_wait_start, sleep_ticks;
static uint32_t partial_tick_us;
static uint8_t commands[32];
static const uint32_t prepare_us[] = {20000, 200000, 120000, 200000};
static bool sleeping_in;

static uint32_t next_wait_us(void) {
  assert(wait_count < (sleeping_in ? 1u : 4u));
  return sleeping_in ? 200000u : prepare_us[wait_count];
}
static void observe_sleep(pbl_timeout_t timeout) {
  const uint32_t minimum_us = next_wait_us();
  // Complete kernel/time.c must include a tick for the fractional tick already in progress.
  const uint64_t expected = ((uint64_t)minimum_us * PBL_TICK_HZ + 999999u) / 1000000u + 1u;
  assert(timeout.ticks == expected);
  const uint64_t duration_us = (uint64_t)timeout.ticks * 1000000u / PBL_TICK_HZ - partial_tick_us;
  assert(duration_us >= minimum_us);
  last_wait_start = wall_us;
  wall_us += duration_us;
  sleep_ticks += timeout.ticks;
  partial_tick_us = 0;
  ++wait_count;
  // The transaction lock stays held while sleeping; unrelated work may run.
  assert(panel_state.lock.count == 1);
}
void delay_us(uint32_t us) {
  assert(!kernel_started && us == next_wait_us());
  ++busy_calls;
  ++wait_count;
  last_wait_start = wall_us;
  wall_us += us;
}
int pbl_gpio_configure(const struct pbl_gpio *gpio, uint32_t flags) {
  return 0;
}
int pbl_spi_setup(const struct pbl_spi_dev *spi) {
  return 0;
}
int pbl_mutex_lock_lr(struct pbl_mutex *mutex, pbl_timeout_t timeout, uintptr_t lr) {
  assert(mutex == &panel_state.lock && !mutex->count);
  ++mutex->count;
  return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *mutex) {
  assert(mutex->count == 1);
  --mutex->count;
}
int pbl_gpio_set(const struct pbl_gpio *gpio, bool value) {
  assert(value == (gpio_calls != 1));
  if (gpio_calls) {
    assert(wait_count == gpio_calls);
    assert(wall_us - last_wait_start >= prepare_us[gpio_calls - 1]);
  }
  return ++gpio_calls == fail_gpio ? -EIO : 0;
}
int pbl_spi_transfer(const struct pbl_spi_dev *spi, const struct pbl_spi_xfer *xfers,
                     size_t count) {
  static const uint8_t expected[] = {0xfe, 0x19, 0x1c, 0xfe, 0xc4, 0x3a, 0x35,
                                     0x53, 0x51, 0x63, 0x11, 0x20, 0x13};
  assert(count == 1 && transfers < sizeof(commands));
  uint8_t command = ((const uint8_t *)xfers->tx_buf)[2];
  if (!sleeping_in) {
    assert(command == expected[transfers]);
    assert(wait_count == (transfers < 11 ? 3u : 4u));
    assert(wall_us - last_wait_start >= (transfers < 11 ? 120000u : 200000u));
  } else {
    assert(command == 0x10 && !wait_count);
  }
  commands[transfers++] = command;
  return transfers == fail_transfer ? -EIO : 0;
}
static void reset_case(bool started) {
  kernel_started = started;
  gpio_calls = transfers = busy_calls = wait_count = sleeps = 0;
  wall_us = last_wait_start = sleep_ticks = 0;
  sleeping_in = false;
  sleep_observer = observe_sleep;
  partial_tick_us = 999; // Worst fractional phase at the board's 1 kHz tick.
  panel_state.lock.count = 1;
}
