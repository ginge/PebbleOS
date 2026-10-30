/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "board/board.h"

#include <pbl/device.h>
#include <pbl/drivers/backlight/t5ai.h>
#include <pbl/drivers/display/t5ai.h>
#include <pbl/drivers/dma/t5ai.h>
#include <pbl/drivers/input/gpio_keys.h>
#include <pbl/drivers/panel/co5300.h>
#include <pbl/drivers/pinctrl/t5ai.h>
#include <pbl/drivers/spi/t5ai.h>
#include <pbl/drivers/touch/cst92xx_t5ai.h>
#include <pbl/input/input.h>
#include <pbl/kernel/assert.h>
#include <pbl/services/boot_splash/t5ai.h>
#ifdef CONFIG_T5AI_SD_PFS
#include <pbl/drivers/t5ai/sd.h>
#endif
#include <pbl/drivers/battery/t5ai.h>
#include <pbl/drivers/gpio/t5ai.h>
#include <pbl/drivers/i2c/t5ai.h>
#include <pbl/drivers/imu/qmi8658.h>
#include <pbl/drivers/mic/t5ai/definitions.h>
#include <pbl/drivers/watchdog.h>
#include <pbl/kernel/irq.h>
#include <pbl/logging/logging.h>
#include <pbl/mcu/mpu.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/interrupt_profile.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/power.h>
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/state_trace.h>

#include <cmsis_core.h>

PBL_LOG_MODULE_DEFINE(board_unhygienix, LOG_LEVEL_DEBUG);
#ifdef CONFIG_T5AI_SD_PFS
PBL_IRQ_CONNECT(SDIO, 6, t5ai_sd_irq, , 0);
#endif

#define T5AI_GPIO_BASE             0x44000400u
#define T5AI_GPIO_OUTPUT           (1u << 1)
#define T5AI_GPIO_INPUT_ENABLE     (1u << 2)
#define T5AI_GPIO_OUTPUT_DISABLE   (1u << 3)
#define T5AI_GPIO_PULL_ENABLE      (1u << 5)
#define T5AI_GPIO_SECOND_FUNCTION  (1u << 6)
#define T5AI_GPIO_DRIVE_STRENGTH_1 (1u << 8)

static const MpuRegion s_display_psram_region = {
  .region_num = T5AI_DISPLAY_PSRAM_MPU_REGION,
  .enabled = true,
  .executable = false,
  .base_address = T5AI_DISPLAY_PSRAM_BASE,
  .size = T5AI_DISPLAY_PSRAM_SIZE,
  .cache_policy = MpuCachePolicy_WriteBackWriteAllocate,
  .permissions = MpuPermissions_PrivRW,
};

#ifdef CONFIG_T5AI_SD_PFS
static const MpuRegion s_pfs_flags_psram_region = {
  .region_num = T5AI_PFS_FLAGS_MPU_REGION,
  .enabled = true,
  .executable = false,
  .base_address = T5AI_PFS_FLAGS_PSRAM_BASE,
  .size = T5AI_PFS_FLAGS_PSRAM_SIZE,
  .cache_policy = MpuCachePolicy_WriteBackWriteAllocate,
  .permissions = MpuPermissions_PrivRW,
};
#endif

static const MpuRegion s_display_dma_psram_region = {
  .region_num = T5AI_DISPLAY_DMA_PSRAM_MPU_REGION,
  .enabled = true,
  .executable = false,
  .base_address = T5AI_DISPLAY_DMA_PSRAM_BASE,
  .size = T5AI_DISPLAY_DMA_PSRAM_SIZE,
  .cache_policy = MpuCachePolicy_WriteBackWriteAllocate,
  .permissions = MpuPermissions_PrivRW,
};

static void prv_enable_display_psram_cache(void) {
  // AP-private PSRAM windows have not been accessed yet. Display GDMA
  // cleans its staging range before transfer; PFS flags are CPU-only.
  mpu_set_region(&s_display_psram_region);
  mpu_set_region(&s_display_dma_psram_region);
#ifdef CONFIG_T5AI_SD_PFS
  mpu_set_region(&s_pfs_flags_psram_region);
#endif
  __DSB();
  __ISB();
}

// Validate only display-owned NOLOAD ranges; adjacent caches remain untouched.
extern uint32_t __t5ai_framebuffer_start__[];
extern uint32_t __t5ai_framebuffer_end__[];
extern uint32_t __t5ai_display_dma_start__[];
extern uint32_t __t5ai_display_dma_end__[];

static bool prv_validate_psram_range(volatile uint32_t *words, const uint32_t *end) {
  const size_t word_count = ((uintptr_t)end - (uintptr_t)words) / sizeof(*words);
  const uint32_t seed = 0x3ca5c3a5u;

  for (size_t i = 0; i < word_count; ++i) {
    words[i] = seed ^ (uint32_t)i;
  }
  __DSB();
  for (size_t i = 0; i < word_count; ++i) {
    if (words[i] != (seed ^ (uint32_t)i)) {
      return false;
    }
    words[i] = ~(seed ^ (uint32_t)i);
  }
  __DSB();
  for (size_t i = 0; i < word_count; ++i) {
    if (words[i] != ~(seed ^ (uint32_t)i)) {
      return false;
    }
  }
  return true;
}

static void prv_assert_power_latch(void) {
  volatile uint32_t *reg = (volatile uint32_t *)(T5AI_GPIO_BASE + 19u * sizeof(uint32_t));
  uint32_t value = *reg;
  value &= ~(T5AI_GPIO_INPUT_ENABLE | T5AI_GPIO_OUTPUT_DISABLE | T5AI_GPIO_PULL_ENABLE |
             T5AI_GPIO_SECOND_FUNCTION);
  value &= ~(3u << 8);
  *reg = value | T5AI_GPIO_OUTPUT | T5AI_GPIO_DRIVE_STRENGTH_1;
}

/* The module amplifier enable is active-low. It remains deasserted until a
 * native Pebble audio path owns the codec, clocks, and sample transport. */
static const struct pbl_gpio s_amplifier_enable = PBL_GPIO(&T5AI_GPIO, 28, PBL_GPIO_ACTIVE_LOW);

#ifdef CONFIG_MIC
static MicDeviceState s_mic_state;
static const MicDevice s_mic_device = {
  .state = &s_mic_state,
  .base = 0x47800000u,
  .irq = GDMA_IRQn,
  .irq_priority = 6u,
  .analog_gain = 8u,
};
MicDevice *const MIC = &s_mic_device;
#endif
PBL_IRQ_CONNECT(GDMA, 6, pbl_dma_t5ai_irq_handler, , 0);
PBL_IRQ_CONNECT(QSPI0, 6, pbl_spi_t5ai_irq_handler, , 0);

#if defined(CONFIG_T5AI_SD_PFS) && defined(CONFIG_T5AI_SD_4BIT)
#define UART0_PINS nullptr
#else
PBL_PINCTRL_STATE_DEFINE(s_uart0_pins, &T5AI_PINCTRL, T5AI_PIN(11, 0, 0), T5AI_PIN(10, 0, 0));
#define UART0_PINS (&s_uart0_pins)
#endif
#ifdef CONFIG_T5AI_SD_PFS
PBL_PINCTRL_STATE_DEFINE(s_sd_pins, &T5AI_PINCTRL,
                         T5AI_PIN(2, 1, T5AI_PIN_PULL_UP | T5AI_PIN_DRIVE(3)),
                         T5AI_PIN(3, 1, T5AI_PIN_PULL_UP | T5AI_PIN_DRIVE(3)),
                         T5AI_PIN(4, 1, T5AI_PIN_PULL_UP | T5AI_PIN_DRIVE(3))
#ifdef CONFIG_T5AI_SD_4BIT
                             ,
                         T5AI_PIN(5, 1, T5AI_PIN_PULL_UP | T5AI_PIN_DRIVE(3)),
                         T5AI_PIN(10, 1, T5AI_PIN_PULL_UP | T5AI_PIN_DRIVE(3)),
                         T5AI_PIN(11, 1, T5AI_PIN_PULL_UP | T5AI_PIN_DRIVE(3))
#endif
);
const struct pbl_gpio T5AI_SD_DETECT =
    PBL_GPIO(&T5AI_GPIO, 8, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP);
const struct pbl_pinctrl T5AI_SD_PINCTRL = PBL_PINCTRL(&s_sd_pins, nullptr, 0);
#endif
PBL_UART_T5AI_DEFINE(s_dbg_uart, 0x44820000u, UART0_IRQn, 5, UART0_PINS, 26000000u, 460800u);
const struct pbl_uart *const DBG_UART = &s_dbg_uart.uart;
#ifdef CONFIG_T5AI_DRAW_PROFILE
static void prv_profile_uart_irq(void) {
  const uint32_t start = t5ai_interrupt_profile_irq_enter();
  pbl_uart_t5ai_irq_handler(&s_dbg_uart);
  t5ai_interrupt_profile_irq_exit(4u, start);
}
PBL_IRQ_CONNECT(UART0, 5, prv_profile_uart_irq, , 0);
#else
PBL_IRQ_CONNECT(UART0, 5, pbl_uart_t5ai_irq_handler, &s_dbg_uart, 0);
#endif

// Shared touch/RTC/IMU bus is qualified at 100 kHz.
PBL_PINCTRL_STATE_DEFINE(s_i2c0_pins, &T5AI_PINCTRL, T5AI_PIN(20, 0, T5AI_PIN_PULL_UP),
                         T5AI_PIN(21, 0, T5AI_PIN_PULL_UP));
PBL_I2C_T5AI_DEFINE(T5AI_I2C0, 0x45850000u, I2C0_IRQn, 100000u, 20, 21, &s_i2c0_pins);
static void prv_i2c_irq_handler(void) {
  const uint32_t irq_start = t5ai_interrupt_profile_irq_enter();
  pbl_i2c_t5ai_irq_handler(&T5AI_I2C0);
  t5ai_interrupt_profile_irq_exit(0, irq_start);
}
PBL_IRQ_CONNECT(I2C0, 5, prv_i2c_irq_handler, , 0);
PBL_QMI8658_DEFINE(s_qmi8658, &T5AI_I2C0.bus, 0x6b, &T5AI_GPIO, &T5AI_GPIO_INTC, 9);
const struct pbl_accel *const ACCEL = &s_qmi8658.accel;
static const struct pbl_i2c_dev s_pcf85063 = PBL_I2C_DEV(&T5AI_I2C0.bus, 0x51);
static const struct pbl_i2c_dev s_cst92xx = PBL_I2C_DEV(&T5AI_I2C0.bus, 0x5a);
const struct pbl_i2c_dev *const I2C_PCF85063 = &s_pcf85063;
const struct pbl_i2c_dev *const I2C_CST92XX = &s_cst92xx;

const struct pbl_backlight *const BACKLIGHT = &pbl_backlight_t5ai;

const BoardConfig BOARD_CONFIG = {.backlight_on_percent = 100};
PBL_GPIO_KEYS_DEFINE(
    s_buttons, "buttons",
    PBL_GPIO_KEYS_KEY("Back", PBL_GPIO(&T5AI_GPIO, 18, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
                      PBL_INPUT_KEY_BACK, 40),
    PBL_GPIO_KEYS_KEY("Select", PBL_GPIO(&T5AI_GPIO, 12, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
                      PBL_INPUT_KEY_SELECT, 40));

PBL_TOUCH_STATE_DEFINE(s_touch);
static const struct pbl_touch s_touch =
    PBL_TOUCH_INIT(s_touch, "cst92xx", &T5AI_I2C0.bus.dev, &pbl_touch_cst92xx_t5ai_ops,
                   .size_x = PBL_DISPLAY_WIDTH, .size_y = PBL_DISPLAY_HEIGHT);
PBL_DEVICE_REGISTER(s_touch, &s_touch.dev);
const struct pbl_touch *const TOUCH = &s_touch;

PBL_PINCTRL_STATE_DEFINE(s_qspi0_pins, &T5AI_PINCTRL, T5AI_PIN(22, 3, T5AI_PIN_DRIVE(3)),
                         T5AI_PIN(23, 3, T5AI_PIN_DRIVE(3) | T5AI_PIN_PULL_UP),
                         T5AI_PIN(24, 3, T5AI_PIN_DRIVE(3) | T5AI_PIN_PULL_UP),
                         T5AI_PIN(25, 3, T5AI_PIN_DRIVE(3) | T5AI_PIN_PULL_UP),
                         T5AI_PIN(26, 3, T5AI_PIN_DRIVE(3) | T5AI_PIN_PULL_UP),
                         T5AI_PIN(27, 3, T5AI_PIN_DRIVE(3) | T5AI_PIN_PULL_UP));
PBL_SPI_BUS_STATE_DEFINE(s_qspi0, PBL_GPIO(nullptr, 0, 0));
static const struct pbl_spi_t5ai s_qspi0 = {
  .bus = PBL_SPI_BUS_INIT(s_qspi0, "qspi0", &pbl_spi_t5ai_ops, PBL_DEVICE_DEPS(&T5AI_DMA.dev)),
  .pinctrl = PBL_PINCTRL(&s_qspi0_pins, nullptr, 0),
  .dma = PBL_DMA_CHAN(&T5AI_DMA, 0, 0, T5AI_DMA_PERIPHERAL_INCREMENT | T5AI_DMA_BURST_INC16),
};
PBL_DEVICE_REGISTER(s_qspi0, &s_qspi0.bus.dev);
static const struct pbl_panel_timing s_panel_timing = {.hactive = 466, .vactive = 466};
PBL_DEVICE_STATE_DEFINE(CO5300);
const struct pbl_panel_co5300 CO5300 = {
  .panel = PBL_PANEL_INIT(CO5300, "co5300-panel", pbl_panel_co5300_init, &pbl_panel_co5300_ops,
                          PBL_DEVICE_DEPS(&s_qspi0.bus.dev, &T5AI_GPIO.dev), &s_panel_timing,
                          PBL_PANEL_BUS_FORMAT_RGB565),
  .spi = PBL_SPI_DEV(&s_qspi0.bus, 0, 80000000u, PBL_SPI_MODE_0),
  .reset = PBL_GPIO(&T5AI_GPIO, 29, 0),
};
PBL_DEVICE_REGISTER(CO5300, &CO5300.panel.dev);
PBL_DISPLAY_STATE_DEFINE(s_display);
static const struct pbl_display s_display = PBL_DISPLAY_INIT(
    s_display, "co5300", &pbl_display_co5300_ops, PBL_DEVICE_DEPS(&CO5300.panel.dev),
    PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT, PBL_DISPLAY_FORMAT_ARGB2222, false);
PBL_DEVICE_REGISTER(s_display, &s_display.dev);
const struct pbl_display *const DISPLAY = &s_display;

// P13/channel 15, 2M/510K divider and meter calibration from the Waveshare board.
const T5AIBatteryConfig T5AI_BATTERY_CONFIG = {
  .charge = PBL_GPIO(&T5AI_GPIO, 30, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
  .interrupt = PBL_IRQ_SPEC(&T5AI_GPIO_INTC, 30, PBL_IRQ_TYPE_EDGE_BOTH),
  .channel = 15,
  .samples = 16,
  .cal_low = 2716,
  .cal_span = 2854,
  .divider_numerator = 2510,
  .divider_denominator = 510,
};
const BoardConfigPower BOARD_CONFIG_POWER = {.low_power_threshold = 5};
const BoardConfigActuator BOARD_CONFIG_VIBE = {};
void board_early_init(void) {
  // CP supervision starts before the scheduler; service it before storage startup.
  t5ai_reboot_trace_start();
  t5ai_cp_mailbox_enable_rx_interrupt();
  prv_enable_display_psram_cache();
  // P19 holds the module power rail on. Preload HIGH in the same write that
  // enables output so initialization cannot glitch the rail low.
  prv_assert_power_latch();
  PBL_ASSERTN(pbl_device_init(&T5AI_GPIO.dev) == 0);
  PBL_ASSERTN(pbl_gpio_configure(&s_amplifier_enable, PBL_GPIO_OUTPUT_INACTIVE) == 0);
  /* The T5AI CP owns reset supervision and monitors the AP heartbeat. The
   * vendor AP adapter deliberately leaves both local watchdogs disabled. */
  watchdog_init();
  t5ai_state_trace_start();
}
void board_init(void) {
  // Keep CP awake until the AP implements the low-voltage suspend/resume contract.
  const status_t status = t5ai_cp_mailbox_vote_app_sleep(false);
  if (FAILED(status)) {
    PBL_LOG_ERR("App sleep vote failed: %ld", (long)status);
  }

  // Vote after the scheduler and CP mailbox are ready; keep the active rate at 240 MHz.
  t5ai_power_init();

  // The boot stages map PSRAM; verify the handoff before any framebuffer use.
  (void)t5ai_cp_mailbox_enable_psram();
  PBL_ASSERTN(prv_validate_psram_range(__t5ai_framebuffer_start__, __t5ai_framebuffer_end__));
  PBL_ASSERTN(prv_validate_psram_range(__t5ai_display_dma_start__, __t5ai_display_dma_end__));
  // Device initialization precedes PSRAM validation; render only after it is ready.
  t5ai_boot_splash_render();
  PBL_ASSERTN(pbl_backlight_set_brightness(BACKLIGHT, BOARD_CONFIG.backlight_on_percent) == 0);
}
