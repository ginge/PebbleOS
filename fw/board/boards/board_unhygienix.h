/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#define BT_VENDOR_ID 0x0EEA
#define BT_VENDOR_NAME "Core Devices LLC"

extern MicDevice *const MIC;
extern UARTDevice *const DBG_UART;
extern I2CSlavePort *const I2C_PCF85063;
extern I2CSlavePort *const I2C_CST92XX;
extern I2CSlavePort *const I2C_QMI8658;
extern const BoardConfig BOARD_CONFIG;
extern const BoardConfigButton BOARD_CONFIG_BUTTON;
extern const BoardConfigPower BOARD_CONFIG_POWER;
extern const BoardConfigActuator BOARD_CONFIG_VIBE;
static const BoardConfigAccel BOARD_CONFIG_ACCEL = {.default_motion_sensitivity = 55U};

// Waveshare T5-E1 Touch AMOLED 1.75 schematic: QMI_INT2 -> IO9.
static const ExtiConfig BOARD_QMI8658_INT2 = {.pin = 9, .pull = GPIO_PuPd_NOPULL};
