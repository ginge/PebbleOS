/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/display_profile.h>

#include <pbl/soc/t5ai/memory.h>

#include <pbl/drivers/display/display.h>
#include <pbl/drivers/display/t5ai.h>

#include "board/board.h"
#include <pbl/soc/t5ai/delay.h>
#include <pbl/drivers/gpio/t5ai.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include "system/passert.h"
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
#endif

#include <pbl/kernel/mutex.h>

#include <cmsis_core.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(driver_display_co5300, LOG_LEVEL_DEBUG);

#define DISPLAY_CACHE_LINE_MASK (T5AI_SHARED_ALIGNMENT - 1u)
#define PANEL_RGB565_BYTES_PER_PIXEL 2u
#define PANEL_RESET_PRECHARGE_US 20000u
#define PANEL_RESET_ASSERT_US 200000u
#define PANEL_RESET_SETTLE_US 120000u
#define PANEL_SLEEP_OUT_SETTLE_US 200000u
#define PANEL_DCS_SLEEP_OUT 0x11u
#define PANEL_DCS_NORMAL_MODE 0x13u
#define PANEL_DCS_INVERSION_OFF 0x20u
#define PANEL_DCS_DISPLAY_OFF 0x28u
#define PANEL_DCS_DISPLAY_ON 0x29u
#define PANEL_DCS_COLUMN_ADDRESS 0x2au
#define PANEL_DCS_ROW_ADDRESS 0x2bu
#define PANEL_DCS_MEMORY_WRITE 0x2cu
#define PANEL_DCS_BRIGHTNESS 0x51u
#define PANEL_DCS_PIXEL_FORMAT 0x3au
#define PANEL_DCS_TEARING_ON 0x35u
#define PANEL_DCS_CONTROL_DISPLAY 0x53u
#define PANEL_PIXEL_FORMAT_RGB565 0x55u
#define PANEL_CONTROL_BRIGHTNESS_ENABLE 0x20u
#define PANEL_SPI_REGISTER_WRITE 0x02u
#define PANEL_SPI_QUAD_WRITE 0x32u
#define QSPI_COMMAND_BYTE_SHIFT 16u
#define QSPI_COMMAND_C_FIFO_CONFIG 0x300u
#define QSPI_COMMAND_A_LINE_CONFIG 0x3u
#define QSPI_COMMAND_LENGTH_SHIFT 2u
#define QSPI_COMMAND_POLL_COUNT 10000u
#define QSPI_COMMAND_POLL_DELAY_US 1u
#define QSPI_CLOCK_DIVIDER_SHIFT 6u
#define QSPI_CLOCK_DIVIDER_MASK (0x0fu << QSPI_CLOCK_DIVIDER_SHIFT)
#define QSPI_CLOCK_DIVIDER_80MHZ 5u
#define QSPI_CLOCK_SOURCE_PLL (1u << 10)
#define QSPI0_CLOCK_ENABLE (1u << 20)
#define QSPI0_INTERRUPT_ENABLE (1u << 20)
#define QSPI_RESET_RELEASE (1u << 0)
#define DISPLAY_POWER_SETTLE_US 10u
#define DISPLAY_BACKUP_POWER_DOWN (1u << 4)
#define DISPLAY_GDMA_POWER_DOWN (1u << 30)
#define DISPLAY_QSPI_POWER_DOWN (1u << 6)
#define DISPLAY_GPIO_CONFIG_MASK 0x3fffu
#define DISPLAY_GPIO_OUTPUT_DISABLE (1u << 3)
#define DISPLAY_GPIO_PULL_UP (1u << 4)
#define DISPLAY_GPIO_PULL_ENABLE (1u << 5)
#define DISPLAY_GPIO_SECOND_FUNCTION (1u << 6)
#define DISPLAY_GPIO_DRIVE_MAX (3u << 8)
#define DISPLAY_GPIO_FUNCTION_MASK 0x0fu
#define DISPLAY_GPIO_FUNCTION_BITS 4u
#define DISPLAY_GPIO_PINS_PER_FUNCTION_WORD 8u
#define DISPLAY_QSPI_GPIO_FUNCTION 3u
#define DISPLAY_QSPI_CLOCK_PIN 22u
#define DISPLAY_QSPI_LAST_PIN 27u
#define DISPLAY_DRAW_LOG_FRAME_MASK 15u  // One diagnostic sample per 16 frames.
#define DISPLAY_SCB_DCCMVAC (*(volatile uint32_t *)0xe000ef68u)
#define DISPLAY_SCB_DCIMVAC (*(volatile uint32_t *)0xe000ef5cu)
// GColor8 stores three two-bit components; the panel consumes RGB565.
#define DISPLAY_COLOR_MASK 0x3fu
#define DISPLAY_COLOR_COUNT 64u
#define DISPLAY_COLOR_COMPONENT_MASK 3u
#define DISPLAY_COLOR_RED_SHIFT 4u
#define DISPLAY_COLOR_GREEN_SHIFT 2u
#define DISPLAY_RGB565_RED_SHIFT 11u
#define DISPLAY_RGB565_GREEN_SHIFT 5u
#define DISPLAY_RGB565_GREEN_STEP 21u
#define DISPLAY_DMA_BUFFER_COUNT 2u
#define DISPLAY_DMA_TEST_WORDS 8u
// Fixed pattern exercises different bytes and every word of one cache line.
#define DISPLAY_DMA_TEST_TAG 0x5a000000u
#define DISPLAY_DMA_TEST_STEP 0x010101u
#define DISPLAY_DMA_TEST_SEED 0x1234u
// DMA length is encoded as byte count minus one in a 16-bit field.
#define DISPLAY_DMA_MAX_TRANSFER_BYTES 65536u
#define DISPLAY_DMA_POLL_COUNT 1000000u
#define DISPLAY_DMA_LENGTH_SHIFT 16u
#define DISPLAY_DMA_ENABLE (1u << 0)
#define DISPLAY_DMA_RESET_RELEASE (1u << 0)
#define DISPLAY_DMA_CLOCK_GATE_BYPASS (1u << 1)
#define DISPLAY_DMA_CHANNEL0_ATTRIBUTE (1u << 0)
#define DISPLAY_DMA_SRC_WIDTH_32 (2u << 4)
#define DISPLAY_DMA_DEST_WIDTH_32 (2u << 6)
#define DISPLAY_DMA_SRC_INCREMENT (1u << 8)
#define DISPLAY_DMA_DEST_INCREMENT (1u << 9)
#define DISPLAY_DMA_SRC_LOOP (1u << 10)
#define DISPLAY_DMA_DEST_LOOP (1u << 11)
#define DISPLAY_DMA_SRC_SECURE (1u << 20)
#define DISPLAY_DMA_DEST_SECURE (1u << 21)
#define DISPLAY_DMA_SRC_BURST_INC16 (3u << 24)
#define DISPLAY_DMA_DEST_BURST_INC16 (3u << 26)
#define DISPLAY_DMA_STATUS_FINISH (1u << 18)
#define DISPLAY_DMA_STATUS_HALF_FINISH (1u << 19)
#define DISPLAY_DMA_STATUS_BUS_ERROR (1u << 20)
#define DISPLAY_DMA_STATUS_CLEAR \
  (DISPLAY_DMA_STATUS_FINISH | DISPLAY_DMA_STATUS_HALF_FINISH | DISPLAY_DMA_STATUS_BUS_ERROR)
#define DISPLAY_DMA_TRANSFER_CONTROL                                                  \
  (DISPLAY_DMA_SRC_WIDTH_32 | DISPLAY_DMA_DEST_WIDTH_32 | DISPLAY_DMA_SRC_INCREMENT | \
   DISPLAY_DMA_DEST_INCREMENT | DISPLAY_DMA_SRC_LOOP | DISPLAY_DMA_DEST_LOOP)

#define PANEL_WIDTH 466u
#define PANEL_HEIGHT 466u
#define PANEL_ROW_BYTES (PANEL_WIDTH * PANEL_RGB565_BYTES_PER_PIXEL)
#define PANEL_DMA_ROWS ((PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT)
#define PANEL_DMA_BUFFER_BYTES \
  ((PANEL_ROW_BYTES * PANEL_DMA_ROWS + DISPLAY_CACHE_LINE_MASK) & ~DISPLAY_CACHE_LINE_MASK)
#define PANEL_X_OFFSET 6u
#define PANEL_RESET_PIN 29u
#define QSPI_BASE 0x46040000u
#define QSPI_GLB_CTRL 0x08u
#define QSPI_CMD_A_L 0x20u
#define QSPI_CMD_A_H 0x24u
#define QSPI_CMD_A_CFG1 0x28u
#define QSPI_CMD_A_CFG2 0x2cu
#define QSPI_CMD_C_L 0x40u
#define QSPI_CMD_C_H 0x44u
#define QSPI_CMD_C_CFG1 0x48u
#define QSPI_CMD_C_CFG2 0x4cu
#define QSPI_CONFIG 0x60u
#define QSPI_RST_CFG 0x64u
#define QSPI_STATUS_CLR 0x6cu
#define QSPI_STATUS 0x70u
#define QSPI_FIFO_DATA 0x100u

#define QSPI_CMD_DONE (1u << 2)
#define QSPI_CMD_START (1u << 0)
#define QSPI_DATA_LINE_4 (2u << 14)
#define QSPI_ENABLE (1u << 0)
#define QSPI_IO23_ENABLE (1u << 3)
#define QSPI_FORCE_CS_LOW (1u << 6)
#define QSPI_DISABLE_CMD_SCK (1u << 16)
#define QSPI_IO_CPU_MEM_SELECT (1u << 22)
#define QSPI_CS_HIGH_WAIT (8u << 24)
#define QSPI_DATA_APERTURE ((volatile uint32_t *)0x64000000u)

#define DMA_BASE 0x45020000u
#define DMA_GLOBAL_CTRL (*(volatile uint32_t *)(DMA_BASE + 0x08u))
#define DMA_SECURE_ATTR (*(volatile uint32_t *)(DMA_BASE + 0x10u))
#define DMA_PRIVILEGED_ATTR (*(volatile uint32_t *)(DMA_BASE + 0x14u))
#define DMA_CTRL (*(volatile uint32_t *)(DMA_BASE + 0x40u))
#define DMA_DEST (*(volatile uint32_t *)(DMA_BASE + 0x44u))
#define DMA_SRC (*(volatile uint32_t *)(DMA_BASE + 0x48u))
#define DMA_DEST_LOOP_END (*(volatile uint32_t *)(DMA_BASE + 0x4cu))
#define DMA_DEST_LOOP_START (*(volatile uint32_t *)(DMA_BASE + 0x50u))
#define DMA_SRC_LOOP_END (*(volatile uint32_t *)(DMA_BASE + 0x54u))
#define DMA_SRC_LOOP_START (*(volatile uint32_t *)(DMA_BASE + 0x58u))
#define DMA_REQ (*(volatile uint32_t *)(DMA_BASE + 0x5cu))
#define DMA_STATUS (*(volatile uint32_t *)(DMA_BASE + 0x70u))

#define SYSTEM_CLK_MODE2 (*(volatile uint32_t *)0x44010024u)
#define SYSTEM_DEVICE_CLK_ENABLE (*(volatile uint32_t *)0x44010030u)
#define SYSTEM_DEVICE_MEM_CTRL1 (*(volatile uint32_t *)0x44010038u)
#define SYSTEM_DEVICE_MEM_CTRL2 (*(volatile uint32_t *)0x4401003cu)
#define SYSTEM_POWER_SLEEP_WAKEUP (*(volatile uint32_t *)0x44010040u)
#define SYSTEM_CPU1_INTERRUPT_ENABLE (*(volatile uint32_t *)0x44010088u)
#define SYSTEM_GPIO_FUNCTION_BASE ((volatile uint32_t *)0x440100c0u)
#define GPIO_BASE ((volatile uint32_t *)0x44000400u)

static bool s_initialized;
static bool s_enabled;
static bool s_updating;
static bool s_rotated;
static uint8_t s_brightness;
static PBL_MUTEX_DEFINE(s_panel_mutex);
static bool s_panel_mutex_ready;
static bool s_brightness_dirty;
static uint8_t s_panel_row[PANEL_ROW_BYTES] __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static uint32_t s_dma_test_source[DISPLAY_DMA_TEST_WORDS]
    __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static uint32_t s_dma_test_destination[DISPLAY_DMA_TEST_WORDS]
    __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static uint16_t s_panel_source_x[PANEL_WIDTH];
static uint8_t s_dma_rows[DISPLAY_DMA_BUFFER_COUNT][PANEL_DMA_BUFFER_BYTES]
    __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static bool s_dma_pending;
static unsigned s_dma_buffer;

#ifdef CONFIG_T5AI_DISPLAY_PROFILE
// Opt-in counters and per-update UART timing diagnostics.
volatile T5AIDrawStats g_t5ai_draw_stats;
static uint32_t s_last_frame_start;
static bool s_have_frame_start;
static T5AITransferProfile s_transfer;
static uint32_t s_dma_started_cycles;
#define TRANSFER_BEGIN(name)      const uint32_t name = DWT->CYCCNT
#define TRANSFER_END(field, name) s_transfer.field += (uint32_t)(DWT->CYCCNT - (name))
#else
#define TRANSFER_BEGIN(name)
#define TRANSFER_END(field, name)
#endif
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
static uint32_t prv_draw_ticks(void) {
  return T5AI_AON_RTC_COUNT_LOW;
}
#endif
static uint16_t s_rgb565[DISPLAY_COLOR_COUNT];
static bool s_dma_display_enabled;
static bool s_dma_display_failed;

static volatile uint32_t *prv_qspi(uint32_t offset) {
  return (volatile uint32_t *)(QSPI_BASE + offset);
}

static void prv_dma_init(void) {
  // GDMA resides in the backup power domain. PebbleOS leaves that domain
  // powered down, whereas Tuya's platform power manager enables it before
  // peripheral drivers run.
  SYSTEM_POWER_SLEEP_WAKEUP &= ~DISPLAY_BACKUP_POWER_DOWN;
  SYSTEM_DEVICE_MEM_CTRL1 &= ~DISPLAY_GDMA_POWER_DOWN;
  SYSTEM_DEVICE_MEM_CTRL2 &= ~DISPLAY_GDMA_POWER_DOWN;
  delay_us(DISPLAY_POWER_SETTLE_US);
  DMA_GLOBAL_CTRL = DISPLAY_DMA_RESET_RELEASE | DISPLAY_DMA_CLOCK_GATE_BYPASS;
  // PebbleOS runs in the secure privileged AP world. Tuya's CONFIG_SPE DMA
  // initialization marks allocated channels secure and privileged.
  DMA_SECURE_ATTR |= DISPLAY_DMA_CHANNEL0_ATTRIBUTE;
  DMA_PRIVILEGED_ATTR |= DISPLAY_DMA_CHANNEL0_ATTRIBUTE;
  DMA_CTRL = 0u;
}

static void prv_power_up(void) {
  SYSTEM_DEVICE_MEM_CTRL1 &= ~DISPLAY_QSPI_POWER_DOWN;
  SYSTEM_DEVICE_MEM_CTRL2 &= ~DISPLAY_QSPI_POWER_DOWN;
  SYSTEM_DEVICE_CLK_ENABLE |= QSPI0_CLOCK_ENABLE;
}

static bool prv_wait_command(void) {
  for (unsigned i = 0; i < QSPI_COMMAND_POLL_COUNT; ++i) {
    const uint32_t status = *prv_qspi(QSPI_STATUS);
    if (status & QSPI_CMD_DONE) {
      *prv_qspi(QSPI_STATUS_CLR) = QSPI_CMD_DONE;
      *prv_qspi(QSPI_STATUS_CLR) = 0;
      return true;
    }
    delay_us(QSPI_COMMAND_POLL_DELAY_US);
  }
  PBL_LOG_ERR("QSPI0 command timeout status=0x%08lx", (unsigned long)*prv_qspi(QSPI_STATUS));
  return false;
}

static bool prv_command(uint8_t command, const uint8_t *data, size_t length, bool quad_data) {
  TRANSFER_BEGIN(command_start);
  PBL_ASSERTN(length <= sizeof(s_panel_row));
  prv_power_up();
  // CO5300 framing uses 02h for registers and 32h for quad pixel data,
  // followed by 00h, register, 00h.
  *prv_qspi(QSPI_CMD_C_L) = 0;
  *prv_qspi(QSPI_CMD_C_H) = (quad_data ? PANEL_SPI_QUAD_WRITE : PANEL_SPI_REGISTER_WRITE) |
                            ((uint32_t)command << QSPI_COMMAND_BYTE_SHIFT);
  *prv_qspi(QSPI_CMD_C_CFG1) = 0u;
  *prv_qspi(QSPI_CMD_C_CFG2) = 0u;

  // The preserved known-visible native AP artifact sends every payload through
  // the FIFO, including one-byte register values. Match the executed machine
  // code rather than the nearby source revision, whose short-command path does
  // not describe that artifact.
  volatile uint32_t *fifo = prv_qspi(QSPI_FIFO_DATA);
  for (size_t offset = 0; offset < length; offset += sizeof(uint32_t)) {
    uint32_t word = 0;
    const size_t remaining = length - offset;
    memcpy(&word, data + offset, remaining < sizeof(word) ? remaining : sizeof(word));
    fifo[offset / sizeof(word)] = word;
  }
  *prv_qspi(QSPI_CMD_C_CFG1) = QSPI_COMMAND_C_FIFO_CONFIG;
  *prv_qspi(QSPI_CMD_C_CFG2) =
      ((uint32_t)length << QSPI_COMMAND_LENGTH_SHIFT) | (quad_data ? QSPI_DATA_LINE_4 : 0u);
  *prv_qspi(QSPI_CMD_C_CFG2) |= QSPI_CMD_START;
  const bool result = prv_wait_command();
  *prv_qspi(QSPI_CMD_C_CFG1) = 0;
  *prv_qspi(QSPI_CMD_C_CFG2) = 0;
  TRANSFER_END(command_cycles, command_start);
  return result;
}

static bool prv_register(uint8_t command, const uint8_t *data, size_t length) {
  return prv_command(command, data, length, false);
}

static void prv_apply_brightness(void) {
  if (s_initialized && !s_updating && s_brightness_dirty &&
      prv_register(PANEL_DCS_BRIGHTNESS, &s_brightness, 1u)) {
    s_brightness_dirty = false;
  }
}

void display_co5300_set_brightness(uint8_t level) {
  // Before display_init(), only cache the requested level.
  if (s_panel_mutex_ready) {
    pbl_mutex_lock(&s_panel_mutex, PBL_FOREVER);
  }
  s_brightness = level;
  s_brightness_dirty = true;
  prv_apply_brightness();
  if (s_panel_mutex_ready) {
    pbl_mutex_unlock(&s_panel_mutex);
  }
}

static bool prv_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  const uint8_t columns[] = {
      (uint8_t)(x0 >> 8),
      (uint8_t)x0,
      (uint8_t)(x1 >> 8),
      (uint8_t)x1,
  };
  const uint8_t rows[] = {
      (uint8_t)(y0 >> 8),
      (uint8_t)y0,
      (uint8_t)(y1 >> 8),
      (uint8_t)y1,
  };
  return prv_register(PANEL_DCS_COLUMN_ADDRESS, columns, sizeof(columns)) &&
         prv_register(PANEL_DCS_ROW_ADDRESS, rows, sizeof(rows));
}

static void prv_init_conversion_tables(void) {
  // Rounded expansion of 2-bit red/blue to 5 bits: 0, 10, 21, 31.
  static const uint8_t red_blue[4] = {0u, 10u, 21u, 31u};
  for (unsigned framebuffer_pixel = 0u; framebuffer_pixel < DISPLAY_COLOR_COUNT;
       ++framebuffer_pixel) {
    const uint16_t red =
        red_blue[(framebuffer_pixel >> DISPLAY_COLOR_RED_SHIFT) & DISPLAY_COLOR_COMPONENT_MASK];
    const uint16_t green =
        ((framebuffer_pixel >> DISPLAY_COLOR_GREEN_SHIFT) & DISPLAY_COLOR_COMPONENT_MASK) *
        DISPLAY_RGB565_GREEN_STEP;
    const uint16_t blue = red_blue[framebuffer_pixel & DISPLAY_COLOR_COMPONENT_MASK];
    const uint16_t rgb565 =
        (red << DISPLAY_RGB565_RED_SHIFT) | (green << DISPLAY_RGB565_GREEN_SHIFT) | blue;
    // Store in the byte order consumed by the panel so the conversion loop
    // can emit one aligned halfword instead of two byte stores.
    s_rgb565[framebuffer_pixel] = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
  }
  for (unsigned x = 0u; x < PANEL_WIDTH; ++x) {
    s_panel_source_x[x] = x * PBL_DISPLAY_WIDTH / PANEL_WIDTH;
  }
}

static void __attribute__((section(".t5ai_ramfunc"), noinline, optimize("O3")))
prv_convert_row(uint8_t *destination, const uint8_t *source, bool reverse,
                bool invert_colors) {
  TRANSFER_BEGIN(convert_start);
  uint16_t *output = (uint16_t *)destination;
  unsigned previous_source_x = PBL_DISPLAY_WIDTH;
  uint16_t converted_pixel = 0u;
  for (unsigned x = 0u; x < PANEL_WIDTH; ++x) {
    unsigned source_x = s_panel_source_x[x];
    if (reverse) {
      source_x = PBL_DISPLAY_WIDTH - 1u - source_x;
    }
    // Scaling 260 pixels to 466 means most logical pixels are emitted twice.
    // The framebuffer is in PSRAM, so avoid rereading and reconverting a pixel
    // while the nearest-neighbour mapping still selects the same source x.
    if (source_x != previous_source_x) {
      unsigned framebuffer_pixel = source[source_x] & DISPLAY_COLOR_MASK;
      if (invert_colors) {
        framebuffer_pixel ^= DISPLAY_COLOR_MASK;
      }
      converted_pixel = s_rgb565[framebuffer_pixel];
      previous_source_x = source_x;
    }
    output[x] = converted_pixel;
  }
  TRANSFER_END(convert_cycles, convert_start);
}

static bool prv_pixel_stream_begin(void) {
  *prv_qspi(QSPI_CONFIG) |= QSPI_FORCE_CS_LOW;
  if (!prv_command(PANEL_DCS_MEMORY_WRITE, NULL, 0u, true)) {
    *prv_qspi(QSPI_CONFIG) &= ~QSPI_FORCE_CS_LOW;
    return false;
  }
  *prv_qspi(QSPI_CONFIG) |= QSPI_IO_CPU_MEM_SELECT;
  *prv_qspi(QSPI_CONFIG) |= QSPI_DISABLE_CMD_SCK;
  return true;
}

static void prv_pixel_stream_end(void) {
  *prv_qspi(QSPI_CONFIG) &= ~QSPI_DISABLE_CMD_SCK;
  *prv_qspi(QSPI_CONFIG) &= ~QSPI_IO_CPU_MEM_SELECT;
  *prv_qspi(QSPI_CONFIG) &= ~QSPI_FORCE_CS_LOW;
}

static size_t __attribute__((section(".t5ai_ramfunc"), noinline, optimize("O3")))
prv_pixel_stream_write(const uint8_t *data, size_t length, size_t aperture_word) {
  TRANSFER_BEGIN(cpu_start);
  PBL_ASSERTN((length % sizeof(uint32_t)) == 0u);
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  s_transfer.bytes += length;
  s_transfer.panel_rows += length / PANEL_ROW_BYTES;
#endif

  for (size_t offset = 0u; offset < length; offset += sizeof(uint32_t)) {
    uint32_t word;
    memcpy(&word, data + offset, sizeof(word));
    QSPI_DATA_APERTURE[aperture_word++] = word;
  }
  TRANSFER_END(cpu_cycles, cpu_start);
  return aperture_word;
}

static void prv_dcache_clean(const void *address, size_t size) {
  if ((SCB->CCR & SCB_CCR_DC_Msk) == 0u) {
    return;
  }
  const uintptr_t first = (uintptr_t)address & ~(uintptr_t)DISPLAY_CACHE_LINE_MASK;
  const uintptr_t end =
      ((uintptr_t)address + size + DISPLAY_CACHE_LINE_MASK) & ~(uintptr_t)DISPLAY_CACHE_LINE_MASK;
  __asm volatile("dsb 0xf" ::: "memory");
  for (uintptr_t line = first; line < end; line += T5AI_SHARED_ALIGNMENT) {
    DISPLAY_SCB_DCCMVAC = (uint32_t)line;  // SCB DCCMVAC
  }
  __asm volatile("dsb 0xf\nisb 0xf" ::: "memory");
}

static void prv_dcache_invalidate(const void *address, size_t size) {
  if ((SCB->CCR & SCB_CCR_DC_Msk) == 0u) {
    return;
  }
  const uintptr_t first = (uintptr_t)address & ~(uintptr_t)DISPLAY_CACHE_LINE_MASK;
  const uintptr_t end =
      ((uintptr_t)address + size + DISPLAY_CACHE_LINE_MASK) & ~(uintptr_t)DISPLAY_CACHE_LINE_MASK;
  __asm volatile("dsb 0xf" ::: "memory");
  for (uintptr_t line = first; line < end; line += T5AI_SHARED_ALIGNMENT) {
    DISPLAY_SCB_DCIMVAC = (uint32_t)line;  // SCB DCIMVAC
  }
  __asm volatile("dsb 0xf\nisb 0xf" ::: "memory");
}

static uint32_t prv_dma_request_configuration(void) {
  // CONFIG_SPE builds of TuyaOpen mark each channel's source and destination
  // transactions secure in addition to the controller-wide channel security.
  // PebbleOS executes from secure SRAM and accesses QSPI through its secure AP
  // aperture, so omitting these bits silently targets the wrong bus aliases.
  return DISPLAY_DMA_SRC_SECURE | DISPLAY_DMA_DEST_SECURE | DISPLAY_DMA_SRC_BURST_INC16 |
         DISPLAY_DMA_DEST_BURST_INC16;
}

static bool prv_dma_self_test(void) {
  for (unsigned i = 0u; i < DISPLAY_DMA_TEST_WORDS; ++i) {
    s_dma_test_source[i] =
        DISPLAY_DMA_TEST_TAG | (i * DISPLAY_DMA_TEST_STEP + DISPLAY_DMA_TEST_SEED);
    s_dma_test_destination[i] = 0u;
  }
  prv_dcache_clean(s_dma_test_source, sizeof(s_dma_test_source));
  prv_dcache_clean(s_dma_test_destination, sizeof(s_dma_test_destination));

  const uint32_t source = (uint32_t)(uintptr_t)s_dma_test_source;
  const uint32_t destination = (uint32_t)(uintptr_t)s_dma_test_destination;
  DMA_CTRL = 0u;
  DMA_STATUS = DISPLAY_DMA_STATUS_CLEAR;
  DMA_DEST = destination;
  DMA_SRC = source;
  DMA_DEST_LOOP_START = destination;
  DMA_DEST_LOOP_END = destination + sizeof(s_dma_test_destination) + sizeof(uint32_t);
  DMA_SRC_LOOP_START = source;
  DMA_SRC_LOOP_END = source + sizeof(s_dma_test_source) + sizeof(uint32_t);
  DMA_REQ = prv_dma_request_configuration();
  DMA_CTRL = ((uint32_t)(sizeof(s_dma_test_source) - 1u) << DISPLAY_DMA_LENGTH_SHIFT) |
             DISPLAY_DMA_TRANSFER_CONTROL;
  __asm volatile("dsb 0xf" ::: "memory");
  DMA_CTRL |= DISPLAY_DMA_ENABLE;

  unsigned timeout = DISPLAY_DMA_POLL_COUNT;
  while ((DMA_CTRL & DISPLAY_DMA_ENABLE) && --timeout) {
  }
  const uint32_t status = DMA_STATUS;
  DMA_CTRL = 0u;
  prv_dcache_invalidate(s_dma_test_destination, sizeof(s_dma_test_destination));
  const bool matches = memcmp(s_dma_test_source, s_dma_test_destination,
                              sizeof(s_dma_test_source)) == 0;
  return matches && timeout != 0u && (status & DISPLAY_DMA_STATUS_BUS_ERROR) == 0u;
}

static void prv_pixel_stream_start_dma(const uint8_t *data, size_t length) {
  TRANSFER_BEGIN(kick_start);
  PBL_ASSERTN(!s_dma_pending);
  PBL_ASSERTN(length != 0u && length <= DISPLAY_DMA_MAX_TRANSFER_BYTES);
  PBL_ASSERTN((length % sizeof(uint32_t)) == 0u);
  const uint32_t source = (uint32_t)(uintptr_t)data;
  // The QSPI mapping is a streaming aperture, not a framebuffer. Match
  // Tuya's LCD DMA path by starting every transaction at its fixed base;
  // advancing the DMA destination between row-sized transactions leaks the
  // aperture address into the pixel stream at the panel edge.
  const uint32_t destination = (uint32_t)(uintptr_t)QSPI_DATA_APERTURE;

  DMA_CTRL = 0u;
  DMA_STATUS = DISPLAY_DMA_STATUS_CLEAR;
  DMA_DEST = destination;
  DMA_SRC = source;
  DMA_DEST_LOOP_START = destination;
  DMA_DEST_LOOP_END = destination + length + sizeof(uint32_t);
  DMA_SRC_LOOP_START = source;
  DMA_SRC_LOOP_END = source + length + sizeof(uint32_t);
  // DTCM is Tuya's generic memory request (mux zero). INC16 bursts match its
  // production QSPI display path.
  DMA_REQ = prv_dma_request_configuration();
  DMA_CTRL = ((uint32_t)(length - 1u) << DISPLAY_DMA_LENGTH_SHIFT) | DISPLAY_DMA_TRANSFER_CONTROL;
  __asm volatile("dsb 0xf" ::: "memory");
  DMA_CTRL |= DISPLAY_DMA_ENABLE;

  s_dma_pending = true;
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  s_dma_started_cycles = DWT->CYCCNT;
  s_transfer.bytes += length;
  s_transfer.panel_rows += length / PANEL_ROW_BYTES;
  ++s_transfer.dma_submits;
#endif
  TRANSFER_END(kick_cycles, kick_start);
}

static bool prv_pixel_stream_finish_dma(size_t *aperture_word, size_t length) {
  if (!s_dma_pending) {
    return true;
  }
  TRANSFER_BEGIN(wait_start);
  unsigned timeout = DISPLAY_DMA_POLL_COUNT;
  while ((DMA_CTRL & DISPLAY_DMA_ENABLE) && --timeout) {
  }
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  const uint32_t waited = DWT->CYCCNT - wait_start;
  s_transfer.wait_cycles += waited;
  if (waited > s_transfer.wait_max_cycles)
    s_transfer.wait_max_cycles = waited;
  s_transfer.observed_cycles += (uint32_t)(DWT->CYCCNT - s_dma_started_cycles);
#endif
  const bool complete = timeout != 0u && (DMA_STATUS & DISPLAY_DMA_STATUS_BUS_ERROR) == 0u;
  DMA_CTRL = 0u;
  s_dma_pending = false;
  if (complete) {
    *aperture_word += length / sizeof(uint32_t);
  } else {
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
    ++g_t5ai_draw_stats.dma_failures;
#endif
  }
  return complete;
}

static size_t s_dma_pending_bytes;

static bool prv_finish_row_dma(size_t *aperture_word) {
  return prv_pixel_stream_finish_dma(aperture_word, s_dma_pending_bytes);
}

static bool prv_send_panel_row(void) {
  if (!prv_pixel_stream_begin()) {
    return false;
  }
  size_t aperture_word = 0u;
  aperture_word = prv_pixel_stream_write(s_panel_row, sizeof(s_panel_row), aperture_word);
  prv_pixel_stream_end();
  return true;
}

static bool prv_send_logical_row(uint16_t logical_y, const uint8_t *data,
                                 bool invert_colors) {
  const uint16_t mapped_y = s_rotated ? PBL_DISPLAY_HEIGHT - 1u - logical_y : logical_y;
  const uint16_t y0 = (mapped_y * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT;
  const uint16_t y1 =
      ((mapped_y + 1u) * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT - 1u;
  prv_convert_row(s_panel_row, data, s_rotated, invert_colors);
  if (!prv_set_window(PANEL_X_OFFSET, y0, PANEL_X_OFFSET + PANEL_WIDTH - 1u, y1)) {
    return false;
  }
  for (uint16_t y = y0; y <= y1; ++y) {
    if (!prv_send_panel_row()) {
      return false;
    }
  }
  return true;
}

static bool prv_stream_logical_row(uint16_t logical_y, const uint8_t *data,
                                   size_t *aperture_word, bool use_dma,
                                   bool invert_colors) {
  const uint16_t y0 =
      (logical_y * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT;
  const uint16_t y1 =
      ((logical_y + 1u) * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT - 1u;
  if (!use_dma) {
    prv_convert_row(s_panel_row, data, false, invert_colors);
    for (uint16_t y = y0; y <= y1; ++y) {
      *aperture_word = prv_pixel_stream_write(s_panel_row, sizeof(s_panel_row), *aperture_word);
    }
    return true;
  }

  // The other buffer remains owned by GDMA while this row is prepared.
  uint8_t *buffer = s_dma_rows[s_dma_buffer];
  const size_t length = (y1 - y0 + 1u) * PANEL_ROW_BYTES;
  prv_convert_row(buffer, data, false, invert_colors);
  TRANSFER_BEGIN(copy_start);
  for (uint16_t y = y0 + 1u; y <= y1; ++y) {
    memcpy(buffer + (y - y0) * PANEL_ROW_BYTES, buffer, PANEL_ROW_BYTES);
  }
  TRANSFER_END(copy_cycles, copy_start);
  TRANSFER_BEGIN(clean_start);
  prv_dcache_clean(buffer, length);
  TRANSFER_END(clean_cycles, clean_start);
  if (!prv_finish_row_dma(aperture_word)) {
    return false;
  }
  prv_pixel_stream_start_dma(buffer, length);
  s_dma_pending_bytes = length;
  s_dma_buffer ^= 1u;
  return true;
}

static uint16_t prv_panel_row_start(uint16_t logical_y) {
  return (logical_y * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT;
}

static bool prv_qspi_init(void) {
  // BK7258 gates QSPI with both the AHB-peripheral power domain and its
  // individual peripheral clock. A cleared power-down bit turns AHBP on.
  const status_t status = t5ai_cp_mailbox_enable_qspi0();
  if (FAILED(status)) {
    // The CP power-management service is not guaranteed to be ready during
    // early display bring-up. The AP registers below are the validated native
    // fallback and were used by the original working display probe.
    PBL_LOG_WRN("QSPI0 CP power/clock request failed, using AP fallback: %ld", (long)status);
  }
  prv_power_up();
  delay_us(DISPLAY_POWER_SETTLE_US);
  for (unsigned pin = DISPLAY_QSPI_CLOCK_PIN; pin <= DISPLAY_QSPI_LAST_PIN; ++pin) {
    // Match Tuya's gpio_dev_map(): bit 3 is the active-low GPIO output enable,
    // so set it to disable GPIO ownership before selecting the peripheral.
    // Use maximum drive on every signal and pull up CS/IO0-IO3.
    GPIO_BASE[pin] = (GPIO_BASE[pin] & ~DISPLAY_GPIO_CONFIG_MASK) | DISPLAY_GPIO_OUTPUT_DISABLE |
                     DISPLAY_GPIO_SECOND_FUNCTION | DISPLAY_GPIO_DRIVE_MAX;
    if (pin != DISPLAY_QSPI_CLOCK_PIN) {
      GPIO_BASE[pin] |= DISPLAY_GPIO_PULL_UP | DISPLAY_GPIO_PULL_ENABLE;
    }
    volatile uint32_t *function =
        &SYSTEM_GPIO_FUNCTION_BASE[pin / DISPLAY_GPIO_PINS_PER_FUNCTION_WORD];
    const unsigned shift = (pin % DISPLAY_GPIO_PINS_PER_FUNCTION_WORD) * DISPLAY_GPIO_FUNCTION_BITS;
    *function = (*function & ~(DISPLAY_GPIO_FUNCTION_MASK << shift)) |
                (DISPLAY_QSPI_GPIO_FUNCTION << shift);
  }

  // Match the exact compiled Tuya ordering. Enable the controller before
  // selecting its source clock, program command-A, then release soft reset.
  // Tuya also opens the system QSPI0 interrupt route here. Completion remains
  // polled, so the NVIC line itself stays disabled.
  SYSTEM_CPU1_INTERRUPT_ENABLE |= QSPI0_INTERRUPT_ENABLE;
  *prv_qspi(QSPI_CONFIG) = QSPI_CS_HIGH_WAIT | QSPI_ENABLE | QSPI_IO23_ENABLE;
  SYSTEM_CLK_MODE2 = (SYSTEM_CLK_MODE2 & ~(QSPI_CLOCK_DIVIDER_MASK | QSPI_CLOCK_SOURCE_PLL)) |
                     (QSPI_CLOCK_DIVIDER_80MHZ << QSPI_CLOCK_DIVIDER_SHIFT) |
                     QSPI_CLOCK_SOURCE_PLL;  // 480 MHz / (5 + 1) = 80 MHz.
  *prv_qspi(QSPI_CMD_A_L) = 0;
  *prv_qspi(QSPI_CMD_A_H) = 0;
  *prv_qspi(QSPI_CMD_A_CFG1) = QSPI_COMMAND_A_LINE_CONFIG;
  *prv_qspi(QSPI_CMD_A_CFG2) = QSPI_DATA_LINE_4;
  *prv_qspi(QSPI_RST_CFG) = 0u;
  *prv_qspi(QSPI_GLB_CTRL) |= QSPI_RESET_RELEASE;
  return true;
}

static bool prv_panel_init(void) {
  const struct pbl_gpio reset = PBL_GPIO(&T5AI_GPIO, PANEL_RESET_PIN, 0);
  (void)pbl_gpio_configure(&reset, PBL_GPIO_OUTPUT_INACTIVE);
  (void)pbl_gpio_set(&reset, true);
  delay_us(PANEL_RESET_PRECHARGE_US);
  (void)pbl_gpio_set(&reset, false);
  delay_us(PANEL_RESET_ASSERT_US);
  (void)pbl_gpio_set(&reset, true);
  delay_us(PANEL_RESET_SETTLE_US);

  const struct {
    uint8_t command;
    uint8_t value;
  } init[] = {
      // Pinned Waveshare CO5300 page/tuning sequence; vendor bytes remain table data.
      {0xfe, 0x20},
      {0x19, 0x10},
      {0x1c, 0xa0},
      {0xfe, 0x00},
      {0xc4, 0x80},
      {PANEL_DCS_PIXEL_FORMAT, PANEL_PIXEL_FORMAT_RGB565},
      {PANEL_DCS_TEARING_ON, 0x00},
      {PANEL_DCS_CONTROL_DISPLAY, PANEL_CONTROL_BRIGHTNESS_ENABLE},
      {PANEL_DCS_BRIGHTNESS, s_brightness},
      {0x63, 0xff},
  };
  for (unsigned i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
    if (!prv_register(init[i].command, &init[i].value, 1)) {
      return false;
    }
  }
  if (!prv_register(PANEL_DCS_SLEEP_OUT, NULL, 0)) {
    return false;
  }
  delay_us(PANEL_SLEEP_OUT_SETTLE_US);
  // Do not rely on the controller's retained/default inversion state across
  // an AP-only reboot. DCS 0x20 selects non-inverted pixels, while 0x13 exits
  // partial mode and 0x29 enables output.
  return prv_register(PANEL_DCS_INVERSION_OFF, NULL, 0) &&
         prv_register(PANEL_DCS_NORMAL_MODE, NULL, 0) &&
         prv_register(PANEL_DCS_DISPLAY_ON, NULL, 0);
}

void display_init(void) {
  s_panel_mutex_ready = true;
  pbl_mutex_lock(&s_panel_mutex, PBL_FOREVER);
  if (s_initialized) {
    pbl_mutex_unlock(&s_panel_mutex);
    return;
  }
  if (!s_brightness_dirty) {
    s_brightness = T5AI_AMOLED_MIN_BRIGHTNESS;
  }
  prv_dma_init();
  s_dma_display_enabled = prv_dma_self_test();
  prv_init_conversion_tables();
  s_initialized = prv_qspi_init() && prv_panel_init();
  s_enabled = s_initialized;
  s_brightness_dirty = false;
  if (!s_initialized) {
    PBL_LOG_ERR("CO5300 initialization failed");
  }
  pbl_mutex_unlock(&s_panel_mutex);
}

static void prv_display_clear(void) {
  if (!s_initialized) {
    return;
  }
  memset(s_panel_row, 0, sizeof(s_panel_row));
  if (!prv_set_window(PANEL_X_OFFSET, 0u, PANEL_X_OFFSET + PANEL_WIDTH - 1u,
                      PANEL_HEIGHT - 1u) ||
      !prv_pixel_stream_begin()) {
    return;
  }
  size_t aperture_word = 0u;
  for (uint16_t y = 0; y < PANEL_HEIGHT; ++y) {
    aperture_word = prv_pixel_stream_write(s_panel_row, sizeof(s_panel_row), aperture_word);
  }
  prv_pixel_stream_end();
}

void display_clear(void) {
  pbl_mutex_lock(&s_panel_mutex, PBL_FOREVER);
  prv_display_clear();
  pbl_mutex_unlock(&s_panel_mutex);
}

static void prv_display_set_enabled(bool enabled) {
  if (!s_initialized || enabled == s_enabled) {
    return;
  }
  if (prv_register(enabled ? PANEL_DCS_DISPLAY_ON : PANEL_DCS_DISPLAY_OFF, NULL, 0)) {
    s_enabled = enabled;
  }
}

void display_set_enabled(bool enabled) {
  pbl_mutex_lock(&s_panel_mutex, PBL_FOREVER);
  prv_display_set_enabled(enabled);
  pbl_mutex_unlock(&s_panel_mutex);
}

void display_set_rotated(bool rotated) {
  s_rotated = rotated;
}

static bool prv_profile_next_row(NextRowCallback callback, DisplayRow *row) {
  TRANSFER_BEGIN(next_start);
  const bool result = callback(row);
  TRANSFER_END(next_cycles, next_start);
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  s_transfer.rows += result;
#endif
  return result;
}

static void prv_display_update(NextRowCallback nrcb, UpdateCompleteCallback uccb,
                               bool use_dma, bool invert_colors) {
  TRANSFER_BEGIN(lock_start);
  pbl_mutex_lock(&s_panel_mutex, PBL_FOREVER);
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  memset(&s_transfer, 0, sizeof(s_transfer));
  s_transfer.dma_frames = use_dma && !s_rotated;
  s_transfer.cpu_frames = !use_dma || s_rotated;
#endif
  TRANSFER_END(lock_cycles, lock_start);
  PBL_ASSERTN(!s_updating);
  s_updating = true;
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  const uint32_t frame_start = prv_draw_ticks();
  if (s_have_frame_start) {
    g_t5ai_draw_stats.last_interval_ticks = frame_start - s_last_frame_start;
  }
  s_have_frame_start = true;
  s_last_frame_start = frame_start;
#endif
  DisplayRow row;
  bool full_stream = false;
  size_t aperture_word = 0u;
  uint16_t expected_row = 0u;
  while (s_initialized && prv_profile_next_row(nrcb, &row)) {
    if (row.address >= PBL_DISPLAY_HEIGHT) {
      break;
    }

    // Compositor updates commonly begin below row zero. Start one continuous
    // stream at the first dirty row instead of paying three polled register
    // commands and a separate pixel transaction for every logical row.
    if (!s_rotated && !full_stream) {
      const uint16_t panel_y = prv_panel_row_start(row.address);
      full_stream = prv_set_window(PANEL_X_OFFSET, panel_y,
                                   PANEL_X_OFFSET + PANEL_WIDTH - 1u,
                                   PANEL_HEIGHT - 1u) &&
                    prv_pixel_stream_begin();
      expected_row = row.address;
    }
    if (full_stream && row.address == expected_row) {
      if (!prv_stream_logical_row(row.address, row.data, &aperture_word, use_dma,
                                  invert_colors)) {
        s_dma_display_failed = true;
        s_dma_display_enabled = false;
        break;
      }
      expected_row++;
    } else {
      if (full_stream) {
        if (!prv_finish_row_dma(&aperture_word)) {
          s_dma_display_failed = true;
          s_dma_display_enabled = false;
          break;
        }
        prv_pixel_stream_end();
        full_stream = false;
      }
      if (!prv_send_logical_row(row.address, row.data, invert_colors)) {
        break;
      }
    }
  }
  if (full_stream) {
    if (!prv_finish_row_dma(&aperture_word)) {
      s_dma_display_failed = true;
      s_dma_display_enabled = false;
    }
    prv_pixel_stream_end();
  }
#ifdef CONFIG_T5AI_DISPLAY_PROFILE
  const uint32_t elapsed = prv_draw_ticks() - frame_start;
  ++g_t5ai_draw_stats.frames;
  g_t5ai_draw_stats.total_update_ticks += elapsed;
  g_t5ai_draw_stats.last_update_ticks = elapsed;
  if (elapsed > g_t5ai_draw_stats.max_update_ticks) {
    g_t5ai_draw_stats.max_update_ticks = elapsed;
  }
  t5ai_display_profile_frame(frame_start, elapsed, SystemCoreClock, g_t5ai_draw_stats.dma_failures,
                             &s_transfer);
#endif
  s_updating = false;
  prv_apply_brightness();
  pbl_mutex_unlock(&s_panel_mutex);
  uccb();
}

void display_update(NextRowCallback nrcb, UpdateCompleteCallback uccb) {
  const bool use_dma = s_dma_display_enabled && !s_dma_display_failed;
  prv_display_update(nrcb, uccb, use_dma, false);
}

void display_co5300_update_cpu(NextRowCallback nrcb, UpdateCompleteCallback uccb) {
  prv_display_update(nrcb, uccb, false, false);
}

bool display_update_in_progress(void) {
  return s_updating;
}

void display_update_boot_frame(uint8_t *framebuffer) {
  if (framebuffer == NULL) {
    return;
  }
  pbl_mutex_lock(&s_panel_mutex, PBL_FOREVER);
  if (!s_initialized) {
    goto done;
  }
  if (!prv_set_window(PANEL_X_OFFSET, 0u, PANEL_X_OFFSET + PANEL_WIDTH - 1u,
                      PANEL_HEIGHT - 1u) ||
      !prv_pixel_stream_begin()) {
    goto done;
  }
  size_t aperture_word = 0u;
  for (uint16_t y = 0; y < PBL_DISPLAY_HEIGHT; ++y) {
    if (!prv_stream_logical_row(y, framebuffer + y * PBL_DISPLAY_WIDTH, &aperture_word,
                                false, false)) {
      break;
    }
  }
  (void)prv_finish_row_dma(&aperture_word);
  prv_pixel_stream_end();
done:
  pbl_mutex_unlock(&s_panel_mutex);
}
