#pragma once
#include <pbl/drivers/audio.h>
#include <pbl/drivers/display.h>
#include <pbl/drivers/flash.h>
#include <pbl/drivers/imu/qmi8658.h>
#include <pbl/drivers/rtc.h>
#include <pbl/drivers/touch.h>
#include <pbl/drivers/uart.h>
extern const struct pbl_qmi8658 T5AI_QMI8658;
extern const struct pbl_touch T5AI_TOUCH;
extern const struct pbl_display T5AI_DISPLAY;
extern const struct pbl_flash T5AI_FLASH;
extern const struct pbl_rtc T5AI_RTC;
extern const struct pbl_uart T5AI_CONSOLE_UART;
extern const struct pbl_audio T5AI_MIC;
