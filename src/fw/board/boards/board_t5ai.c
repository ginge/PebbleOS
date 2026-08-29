/* SPDX-License-Identifier: Apache-2.0 */
#include "board/board.h"
#include <cmsis_core.h>
#include <pbl/drivers/watchdog.h>
#include <pbl/drivers/i2c.h>

static UARTDeviceState s_dbg_uart_state;
static UARTDevice s_dbg_uart_device = {
    .state = &s_dbg_uart_state,
    .base = 0x44820000u,
    .irqn = UART0_IRQn,
    .irq_priority = 5,
    .tx_pin = 11,
    .rx_pin = 10,
    .clock_hz = 26000000u,
    .baud_rate = 460800u,
};

UARTDevice *const DBG_UART = &s_dbg_uart_device;
IRQ_MAP(UART0, uart_irq_handler, DBG_UART)

static I2CBusState s_i2c0_state;
static const I2CBusHal s_i2c0_hal = {
    .base = 0x45850000u,
    .irqn = I2C0_IRQn,
    .frequency = 400000u,
    .scl_pin = 20u,
    .sda_pin = 21u,
};
static I2CBus s_i2c0_bus = {
    .state = &s_i2c0_state,
    .hal = &s_i2c0_hal,
    .name = "i2c0",
};
I2CBus *const I2C0_BUS = &s_i2c0_bus;
IRQ_MAP(I2C0, i2c_irq_handler, I2C0_BUS)

const BoardConfig BOARD_CONFIG = {.backlight_on_percent = 20};
const BoardConfigButton BOARD_CONFIG_BUTTON = {
    .buttons =
        {
            [BUTTON_ID_BACK] = {.name = "Back", .pin = 18, .pull = GPIO_PuPd_UP,
                                .active_high = false},
            [BUTTON_ID_UP] = {.name = "Up", .pin = UINT8_MAX},
            [BUTTON_ID_SELECT] = {.name = "Select", .pin = 12,
                                  .pull = GPIO_PuPd_UP, .active_high = false},
            [BUTTON_ID_DOWN] = {.name = "Down", .pin = UINT8_MAX},
        },
};
const BoardConfigPower BOARD_CONFIG_POWER = {.low_power_threshold = 5};
const BoardConfigActuator BOARD_CONFIG_VIBE = {.ctl = {.pin = UINT8_MAX}};
void board_early_init(void) {
  watchdog_init();
  watchdog_start();
}
void board_init(void) {
  i2c_init(I2C0_BUS);
}
