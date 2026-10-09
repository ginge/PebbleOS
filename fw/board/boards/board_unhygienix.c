/* SPDX-License-Identifier: Apache-2.0 */
#include "board/board.h"
#include <cmsis_core.h>
#include <pbl/drivers/mpu.h>
#include <pbl/drivers/watchdog.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/mic/t5ai/definitions.h>
#include <pbl/soc/t5ai/state_trace.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/logging/logging.h>
#include "system/passert.h"

PBL_LOG_MODULE_DEFINE(board_unhygienix, LOG_LEVEL_DEBUG);

#define T5AI_GPIO_BASE 0x44000400u
#define T5AI_GPIO_OUTPUT (1u << 1)
#define T5AI_GPIO_INPUT_ENABLE (1u << 2)
#define T5AI_GPIO_OUTPUT_DISABLE (1u << 3)
#define T5AI_GPIO_PULL_ENABLE (1u << 5)
#define T5AI_GPIO_SECOND_FUNCTION (1u << 6)
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

static void prv_enable_display_psram_cache(void) {
  // This slab contains only the compositor framebuffer and board-local DMA
  // staging. It has not been accessed yet, so there are no old cache lines to
  // reconcile when its explicit write-back mapping replaces the background
  // memory attributes. Display GDMA cleans its staging range before transfer.
  mpu_set_region(&s_display_psram_region);
  __DSB();
  __ISB();
}

// Only the NOLOAD framebuffer is tested; adjacent PSRAM caches remain untouched.
extern uint32_t __t5ai_framebuffer_start__[];
extern uint32_t __t5ai_framebuffer_end__[];

static bool prv_validate_framebuffer_ram(void) {
  volatile uint32_t *const words = __t5ai_framebuffer_start__;
  const size_t word_count =
      ((uintptr_t)__t5ai_framebuffer_end__ - (uintptr_t)words) / sizeof(*words);
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
static const OutputConfig s_amplifier_enable = {
    .pin = 28u,
    .active_high = false,
};

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
PBL_IRQ_CONNECT(GDMA, 6, t5ai_mic_dma_irq_handler, MIC, 0);
#endif

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
PBL_IRQ_CONNECT(UART0, 5, uart_irq_handler, DBG_UART, 0);

static I2CBusState s_i2c0_state;
static const I2CBusHal s_i2c0_hal = {
    .base = 0x45850000u,
    .irqn = I2C0_IRQn,
    // The Waveshare CST92xx board configuration qualifies this shared touch/RTC/IMU bus at
    // standard mode. Running it at 400 kHz causes intermittent CST address-write timeouts.
    .frequency = 100000u,
    .scl_pin = 20u,
    .sda_pin = 21u,
};
static I2CBus s_i2c0_bus = {
    .state = &s_i2c0_state,
    .hal = &s_i2c0_hal,
    .name = "i2c0",
};
I2CBus *const I2C0_BUS = &s_i2c0_bus;
PBL_IRQ_CONNECT(I2C0, 5, i2c_irq_handler, I2C0_BUS, 0);

static const I2CSlavePort s_i2c_pcf85063 = {
    .bus = &s_i2c0_bus,
    .address = 0x51u,
};
I2CSlavePort *const I2C_PCF85063 = &s_i2c_pcf85063;

static const I2CSlavePort s_i2c_cst92xx = {
    .bus = &s_i2c0_bus,
    .address = 0x5au,
};
I2CSlavePort *const I2C_CST92XX = &s_i2c_cst92xx;

static const I2CSlavePort s_i2c_qmi8658 = {
    .bus = &s_i2c0_bus,
    .address = 0x6bu,
};
I2CSlavePort *const I2C_QMI8658 = &s_i2c_qmi8658;

const BoardConfig BOARD_CONFIG = {.backlight_on_percent = 100};
const BoardConfigButton BOARD_CONFIG_BUTTON = {
    .buttons =
        {
            [BUTTON_ID_BACK] =
                {.name = "Back", .pin = 18, .pull = GPIO_PuPd_UP, .active_high = false},
            [BUTTON_ID_UP] = {.name = "Up", .pin = UINT8_MAX},
            [BUTTON_ID_SELECT] =
                {.name = "Select", .pin = 12, .pull = GPIO_PuPd_UP, .active_high = false},
            [BUTTON_ID_DOWN] = {.name = "Down", .pin = UINT8_MAX},
        },
};
const BoardConfigPower BOARD_CONFIG_POWER = {.low_power_threshold = 5};
const BoardConfigActuator BOARD_CONFIG_VIBE = {.ctl = {.pin = UINT8_MAX}};
void board_early_init(void) {
  // CP supervision starts before the scheduler; service it before storage startup.
  t5ai_cp_mailbox_enable_rx_interrupt();
  prv_enable_display_psram_cache();
  // P19 holds the module power rail on. Preload HIGH in the same write that
  // enables output so initialization cannot glitch the rail low.
  prv_assert_power_latch();
  gpio_output_init(&s_amplifier_enable, GPIO_OType_PP);
  gpio_output_set(&s_amplifier_enable, false);
  /* The T5AI CP owns reset supervision and monitors the AP heartbeat. The
   * vendor AP adapter deliberately leaves both local watchdogs disabled. */
  watchdog_init();
  t5ai_state_trace_start();
}
void board_init(void) {
  i2c_init(I2C0_BUS);

  // Keep CP awake until the AP implements the low-voltage suspend/resume contract.
  const status_t status = t5ai_cp_mailbox_vote_app_sleep(false);
  if (FAILED(status)) {
    PBL_LOG_ERR("App sleep vote failed: %ld", (long)status);
  }

  // The boot stages map PSRAM; verify the handoff before any framebuffer use.
  (void)t5ai_cp_mailbox_enable_psram();
  PBL_ASSERTN(prv_validate_framebuffer_ram());
}
