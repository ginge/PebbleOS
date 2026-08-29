/* SPDX-License-Identifier: Apache-2.0 */
#include "board/board.h"
#include <cmsis_core.h>
#include <pbl/drivers/watchdog.h>

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

const BoardConfig BOARD_CONFIG = {.backlight_on_percent = 20};
const BoardConfigButton BOARD_CONFIG_BUTTON = {
    .buttons =
        {
            [BUTTON_ID_BACK] = {.name = "Back", .pin = 18, .active_high = false},
            [BUTTON_ID_SELECT] = {.name = "Select", .pin = 12, .active_high = false},
        },
};
const BoardConfigPower BOARD_CONFIG_POWER = {.low_power_threshold = 5};
const BoardConfigActuator BOARD_CONFIG_VIBE = {.ctl = {.pin = UINT8_MAX}};
void board_early_init(void) {
  watchdog_init();
  watchdog_start();
}
void board_init(void) {}
