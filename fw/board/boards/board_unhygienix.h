/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#define BT_VENDOR_ID   0x0EEA
#define BT_VENDOR_NAME "Core Devices LLC"

extern MicDevice *const MIC;
extern UARTDevice *const DBG_UART;
extern const struct pbl_i2c_dev *const I2C_PCF85063;
extern const struct pbl_i2c_dev *const I2C_CST92XX;
extern const BoardConfig BOARD_CONFIG;
extern const BoardConfigButton BOARD_CONFIG_BUTTON;
extern const BoardConfigPower BOARD_CONFIG_POWER;
extern const BoardConfigActuator BOARD_CONFIG_VIBE;
static const BoardConfigAccel BOARD_CONFIG_ACCEL = {.default_motion_sensitivity = 55U};

extern const struct pbl_i2c_t5ai T5AI_I2C0;
