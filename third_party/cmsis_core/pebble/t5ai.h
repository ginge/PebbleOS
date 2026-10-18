/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#define __CM33_REV 0x0000U
#define __NVIC_PRIO_BITS 3U
#define __Vendor_SysTickConfig 0U
#define __MPU_PRESENT 1U
#define __VTOR_PRESENT 1U
#define __FPU_PRESENT 1U
#define __DSP_PRESENT 1U
#define __SAUREGION_PRESENT 0U
// BK7258's STAR-MC1 application core has an L1 instruction cache. The AP
// vendor startup enables it during SystemInit, before the MPU and D-cache are
// configured. Keep D-cache disabled until all shared-memory and DMA regions
// have an explicit coherency policy.
#define __ICACHE_PRESENT 1U

typedef enum IRQn {
  NonMaskableInt_IRQn = -14,
  HardFault_IRQn = -13,
  MemoryManagement_IRQn = -12,
  BusFault_IRQn = -11,
  UsageFault_IRQn = -10,
  SecureFault_IRQn = -9,
  SVCall_IRQn = -5,
  DebugMonitor_IRQn = -4,
  PendSV_IRQn = -2,
  SysTick_IRQn = -1,
  DMA0_NSEC_IRQn = 0,
  ENCP_SEC_IRQn = 1,
  ENCP_NSEC_IRQn = 2,
  TIMER0_IRQn = 3,
  UART0_IRQn = 4,
  PWM0_IRQn = 5,
  I2C0_IRQn = 6,
  SPI0_IRQn = 7,
  SARADC_IRQn = 8,
  IRDA_IRQn = 9,
  SDIO_IRQn = 10,
  GDMA_IRQn = 11,
  LA_IRQn = 12,
  TIMER1_IRQn = 13,
  I2C1_IRQn = 14,
  UART1_IRQn = 15,
  UART2_IRQn = 16,
  SPI1_IRQn = 17,
  CAN_IRQn = 18,
  USB_IRQn = 19,
  QSPI0_IRQn = 20,
  CKMN_IRQn = 21,
  SBC_IRQn = 22,
  AUDIO_IRQn = 23,
  I2S0_IRQn = 24,
  JPEG_ENC_IRQn = 25,
  JPEG_DEC_IRQn = 26,
  DISPLAY_IRQn = 27,
  DMA2D_IRQn = 28,
  PHY_MBP_IRQn = 29,
  PHY_RIU_IRQn = 30,
  MAC_INT_TX_RX_TIMER_IRQn = 31,
  MAC_INT_TX_RX_MISC_IRQn = 32,
  MAC_INT_RX_TRIGGER_IRQn = 33,
  MAC_INT_TX_TRIGGER_IRQn = 34,
  MAC_INT_PORT_TRIGGER_IRQn = 35,
  MAC_INT_GEN_IRQn = 36,
  HSU_IRQn = 37,
  INT_MAC_WAKEUP_IRQn = 38,
  DM_IRQn = 39,
  BLE_IRQn = 40,
  BT_IRQn = 41,
  QSPI1_IRQn = 42,
  PWM1_IRQn = 43,
  I2S1_IRQn = 44,
  I2S2_IRQn = 45,
  H264_IRQn = 46,
  SDMADC_IRQn = 47,
  ETHERNET_IRQn = 48,
  SCAL0_IRQn = 49,
  BMC64_IRQn = 50,
  DPLL_UNLOCK_IRQn = 51,
  TOUCH_IRQn = 52,
  USB_PLUG_IRQn = 53,
  RTC_IRQn = 54,
  GPIO_IRQn = 55,
  DMA1_SEC_IRQn = 56,
  DMA1_NSEC_IRQn = 57,
  YUV_BUF_IRQn = 58,
  ROTT_IRQn = 59,
  BK7816_IRQn = 60,
  LIN_IRQn = 61,
  SCAL1_IRQn = 62,
  MAILBOX_IRQn = 63,
} IRQn_Type;
#include "core_cm33.h"
// core_cm33.h describes the compatible system-control register layout but
// does not expose the optional L1 cache helpers. STAR-MC1 uses the same CMSIS
// L1 cache operations, as confirmed by the BK7258 vendor core header.
#include "cachel1_armv7.h"
#ifndef SCB_CCR_STKALIGN_Msk
#define SCB_CCR_STKALIGN_Msk (1UL << 9)
#endif
extern uint32_t SystemCoreClock;
