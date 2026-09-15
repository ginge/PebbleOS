/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/display/display.h>
#include <pbl/drivers/display/t5ai.h>

#include "board/board.h"
#include "console/dbgserial.h"
#include "kernel/util/delay.h"
#include <pbl/drivers/gpio.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include "system/passert.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(driver_display_co5300, CONFIG_DRIVER_DISPLAY_LOG_LEVEL);

#define PANEL_WIDTH 466u
#define PANEL_HEIGHT 466u
#define PANEL_ROW_BYTES (PANEL_WIDTH * 2u)
#define DMA_CHUNK_PANEL_ROWS 32u
#define PANEL_X_OFFSET 6u
#define PANEL_RESET_PIN 29u
#define PANEL_QSPI_CLK_PIN 22u
#define PANEL_QSPI_CS_PIN 23u
#define PANEL_QSPI_IO0_PIN 24u

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
#define QSPI_CMD_D_L 0x50u
#define QSPI_CMD_D_H 0x54u
#define QSPI_CMD_D_CFG1 0x58u
#define QSPI_CMD_D_CFG2 0x5cu
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
#define SYSTEM_ANALOG_BASE ((volatile uint32_t *)0x44010100u)
#define GPIO_BASE ((volatile uint32_t *)0x44000400u)

static bool s_initialized;
static bool s_enabled;
static bool s_updating;
static status_t s_qspi_power_status;
static bool s_rotated;
static uint8_t s_brightness = 0xffu;
static bool s_brightness_dirty;
static uint8_t s_panel_row[PANEL_ROW_BYTES] __attribute__((aligned(32)));
static uint8_t s_dma_chunk[PANEL_ROW_BYTES * DMA_CHUNK_PANEL_ROWS]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));
static uint32_t s_dma_test_source[8] __attribute__((aligned(32)));
static uint32_t s_dma_test_destination[8] __attribute__((aligned(32)));
static uint16_t s_panel_source_x[PANEL_WIDTH];
static uint16_t s_rgb565[64];
static char s_trace_buffer[160];
static uint32_t s_last_stream_words;
static bool s_dma_display_enabled;
static bool s_dma_display_failed;

static void prv_trace(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(s_trace_buffer, sizeof(s_trace_buffer), format, args);
  va_end(args);

  const size_t bounded_length = length < 0 ? 0u
                                : (size_t)length < sizeof(s_trace_buffer)
                                    ? (size_t)length
                                    : sizeof(s_trace_buffer) - 1u;
  s_trace_buffer[bounded_length] = '\0';
  dbgserial_putstr(s_trace_buffer);
}

static volatile uint32_t *prv_qspi(uint32_t offset) {
  return (volatile uint32_t *)(QSPI_BASE + offset);
}

static void prv_dma_init(void) {
  // GDMA resides in the backup power domain. PebbleOS leaves that domain
  // powered down, whereas Tuya's platform power manager enables it before
  // peripheral drivers run.
  SYSTEM_POWER_SLEEP_WAKEUP &= ~(1u << 4);
  SYSTEM_DEVICE_MEM_CTRL1 &= ~(1u << 30);
  SYSTEM_DEVICE_MEM_CTRL2 &= ~(1u << 30);
  delay_us(10);
  DMA_GLOBAL_CTRL = (1u << 0) | (1u << 1);
  // PebbleOS runs in the secure privileged AP world. Tuya's CONFIG_SPE DMA
  // initialization marks allocated channels secure and privileged.
  DMA_SECURE_ATTR |= 1u;
  DMA_PRIVILEGED_ATTR |= 1u;
  DMA_CTRL = 0u;
}

static void prv_power_up(void) {
  SYSTEM_DEVICE_MEM_CTRL1 &= ~(1u << 6);
  SYSTEM_DEVICE_MEM_CTRL2 &= ~(1u << 6);
  SYSTEM_DEVICE_CLK_ENABLE |= 1u << 20;
}

static bool prv_wait_command(void) {
  for (unsigned i = 0; i < 10000u; ++i) {
    const uint32_t status = *prv_qspi(QSPI_STATUS);
    if (status & QSPI_CMD_DONE) {
      *prv_qspi(QSPI_STATUS_CLR) = QSPI_CMD_DONE;
      *prv_qspi(QSPI_STATUS_CLR) = 0;
      return true;
    }
    delay_us(1);
  }
  PBL_LOG_ERR("QSPI0 command timeout status=0x%08lx", (unsigned long)*prv_qspi(QSPI_STATUS));
  return false;
}

static bool prv_command(uint8_t command, const uint8_t *data, size_t length, bool quad_data) {
  PBL_ASSERTN(length <= sizeof(s_panel_row));
  prv_power_up();
  // CO5300 framing uses 02h for registers and 32h for quad pixel data,
  // followed by 00h, register, 00h.
  *prv_qspi(QSPI_CMD_C_L) = 0;
  *prv_qspi(QSPI_CMD_C_H) = (quad_data ? 0x32u : 0x02u) | ((uint32_t)command << 16);
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
  *prv_qspi(QSPI_CMD_C_CFG1) = 0x300u;
  *prv_qspi(QSPI_CMD_C_CFG2) = ((uint32_t)length << 2) | (quad_data ? QSPI_DATA_LINE_4 : 0u);
  *prv_qspi(QSPI_CMD_C_CFG2) |= QSPI_CMD_START;
  const bool result = prv_wait_command();
  *prv_qspi(QSPI_CMD_C_CFG1) = 0;
  *prv_qspi(QSPI_CMD_C_CFG2) = 0;
  return result;
}

static bool prv_register(uint8_t command, const uint8_t *data, size_t length) {
  return prv_command(command, data, length, false);
}

static bool prv_read_register(uint8_t command, uint32_t *raw_data) {
  prv_power_up();
  *prv_qspi(QSPI_CMD_D_L) = 0u;
  *prv_qspi(QSPI_CMD_D_H) = 0x03u | ((uint32_t)command << 16);
  *prv_qspi(QSPI_CMD_D_CFG1) = 0x300u;
  *prv_qspi(QSPI_CMD_D_CFG2) = (1u << 2) | QSPI_CMD_START;
  const bool result = prv_wait_command();
  *raw_data = *prv_qspi(QSPI_FIFO_DATA);
  *prv_qspi(QSPI_CMD_D_CFG1) = 0u;
  *prv_qspi(QSPI_CMD_D_CFG2) = 0u;
  return result;
}

static void prv_apply_brightness(void) {
  if (s_initialized && !s_updating && s_brightness_dirty &&
      prv_register(0x51u, &s_brightness, 1u)) {
    s_brightness_dirty = false;
  }
}

void display_co5300_set_brightness(uint8_t level) {
  (void)level;
  s_brightness = 0xffu;
  s_brightness_dirty = true;
  prv_apply_brightness();
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
  return prv_register(0x2au, columns, sizeof(columns)) && prv_register(0x2bu, rows, sizeof(rows));
}

static void prv_init_conversion_tables(void) {
  static const uint8_t red_blue[4] = {0u, 10u, 21u, 31u};
  for (unsigned pixel = 0u; pixel < 64u; ++pixel) {
    const uint16_t red = red_blue[(pixel >> 4) & 3u];
    const uint16_t green = ((pixel >> 2) & 3u) * 21u;
    const uint16_t blue = red_blue[pixel & 3u];
    const uint16_t rgb565 = (red << 11) | (green << 5) | blue;
    // Store in the byte order consumed by the panel so the conversion loop
    // can emit one aligned halfword instead of two byte stores.
    s_rgb565[pixel] = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
  }
  for (unsigned x = 0u; x < PANEL_WIDTH; ++x) {
    s_panel_source_x[x] = x * PBL_DISPLAY_WIDTH / PANEL_WIDTH;
  }
}

static void __attribute__((section(".t5ai_ramfunc"), noinline, optimize("O3")))
prv_convert_row(uint8_t *destination, const uint8_t *source, bool reverse) {
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
      converted_pixel = s_rgb565[source[source_x] & 0x3fu];
      previous_source_x = source_x;
    }
    output[x] = converted_pixel;
  }
}

static bool prv_pixel_stream_begin(void) {
  *prv_qspi(QSPI_CONFIG) |= QSPI_FORCE_CS_LOW;
  if (!prv_command(0x2cu, NULL, 0u, true)) {
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
  PBL_ASSERTN((length % sizeof(uint32_t)) == 0u);

  for (size_t offset = 0u; offset < length; offset += sizeof(uint32_t)) {
    uint32_t word;
    memcpy(&word, data + offset, sizeof(word));
    QSPI_DATA_APERTURE[aperture_word++] = word;
  }
  return aperture_word;
}

static void prv_dcache_clean(const void *address, size_t size) {
  const uintptr_t first = (uintptr_t)address & ~(uintptr_t)31u;
  const uintptr_t end = ((uintptr_t)address + size + 31u) & ~(uintptr_t)31u;
  __asm volatile("dsb 0xf" ::: "memory");
  for (uintptr_t line = first; line < end; line += 32u) {
    *(volatile uint32_t *)0xe000ef68u = (uint32_t)line;  // SCB DCCMVAC
  }
  __asm volatile("dsb 0xf\nisb 0xf" ::: "memory");
}

static void prv_dcache_invalidate(const void *address, size_t size) {
  const uintptr_t first = (uintptr_t)address & ~(uintptr_t)31u;
  const uintptr_t end = ((uintptr_t)address + size + 31u) & ~(uintptr_t)31u;
  __asm volatile("dsb 0xf" ::: "memory");
  for (uintptr_t line = first; line < end; line += 32u) {
    *(volatile uint32_t *)0xe000ef5cu = (uint32_t)line;  // SCB DCIMVAC
  }
  __asm volatile("dsb 0xf\nisb 0xf" ::: "memory");
}

static uint32_t prv_dma_request_configuration(void) {
  // CONFIG_SPE builds of TuyaOpen mark each channel's source and destination
  // transactions secure in addition to the controller-wide channel security.
  // PebbleOS executes from secure SRAM and accesses QSPI through its secure AP
  // aperture, so omitting these bits silently targets the wrong bus aliases.
  return (1u << 20) | (1u << 21) | (3u << 24) | (3u << 26);
}

static bool prv_dma_self_test(void) {
  for (unsigned i = 0u; i < 8u; ++i) {
    s_dma_test_source[i] = 0x5a000000u | (i * 0x010101u + 0x1234u);
    s_dma_test_destination[i] = 0u;
  }
  prv_dcache_clean(s_dma_test_source, sizeof(s_dma_test_source));
  prv_dcache_clean(s_dma_test_destination, sizeof(s_dma_test_destination));

  const uint32_t source = (uint32_t)(uintptr_t)s_dma_test_source;
  const uint32_t destination = (uint32_t)(uintptr_t)s_dma_test_destination;
  DMA_CTRL = 0u;
  DMA_STATUS = (1u << 18) | (1u << 19) | (1u << 20);
  DMA_DEST = destination;
  DMA_SRC = source;
  DMA_DEST_LOOP_START = destination;
  DMA_DEST_LOOP_END = destination + sizeof(s_dma_test_destination) + sizeof(uint32_t);
  DMA_SRC_LOOP_START = source;
  DMA_SRC_LOOP_END = source + sizeof(s_dma_test_source) + sizeof(uint32_t);
  DMA_REQ = prv_dma_request_configuration();
  DMA_CTRL = ((uint32_t)(sizeof(s_dma_test_source) - 1u) << 16) |
             (2u << 4) | (2u << 6) | (1u << 8) | (1u << 9);
  __asm volatile("dsb 0xf" ::: "memory");
  DMA_CTRL |= 1u;

  unsigned timeout = 1000000u;
  while ((DMA_CTRL & 1u) && --timeout) {
  }
  const uint32_t status = DMA_STATUS;
  const uint32_t final_source = DMA_SRC;
  const uint32_t final_destination = DMA_DEST;
  DMA_CTRL = 0u;
  prv_dcache_invalidate(s_dma_test_destination, sizeof(s_dma_test_destination));
  const bool matches = memcmp(s_dma_test_source, s_dma_test_destination,
                              sizeof(s_dma_test_source)) == 0;
  prv_trace("PBLDMATEST ok=%u timeout=%u status=%08lx req=%08lx src=%08lx/%08lx dst=%08lx/%08lx first=%08lx/%08lx\r\n",
            (unsigned)matches, (unsigned)(timeout == 0u), (unsigned long)status,
            (unsigned long)DMA_REQ, (unsigned long)source, (unsigned long)final_source,
            (unsigned long)destination, (unsigned long)final_destination,
            (unsigned long)s_dma_test_source[0],
            (unsigned long)s_dma_test_destination[0]);
  return matches && timeout != 0u && (status & (1u << 20)) == 0u;
}

static bool prv_pixel_stream_write_dma(const uint8_t *data, size_t length,
                                       size_t *aperture_word) {
  PBL_ASSERTN((length % sizeof(uint32_t)) == 0u);
  const uint32_t source = (uint32_t)(uintptr_t)data;
  const uint32_t destination = (uint32_t)(uintptr_t)&QSPI_DATA_APERTURE[*aperture_word];

  prv_dcache_clean(data, length);
  DMA_CTRL = 0u;
  DMA_STATUS = (1u << 18) | (1u << 19) | (1u << 20);
  DMA_DEST = destination;
  DMA_SRC = source;
  DMA_DEST_LOOP_START = destination;
  DMA_DEST_LOOP_END = destination + length + sizeof(uint32_t);
  DMA_SRC_LOOP_START = source;
  DMA_SRC_LOOP_END = source + length + sizeof(uint32_t);
  // DTCM is Tuya's generic memory request (mux zero). INC16 bursts match its
  // production QSPI display path.
  DMA_REQ = prv_dma_request_configuration();
  DMA_CTRL = ((uint32_t)(length - 1u) << 16) | (2u << 4) | (2u << 6) |
             (1u << 8) | (1u << 9);
  __asm volatile("dsb 0xf" ::: "memory");
  DMA_CTRL |= 1u;

  unsigned timeout = 1000000u;
  while ((DMA_CTRL & 1u) && --timeout) {
  }
  const bool complete = timeout != 0u && (DMA_STATUS & (1u << 20)) == 0u;
  DMA_CTRL = 0u;
  if (complete) {
    *aperture_word += length / sizeof(uint32_t);
  }
  return complete;
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

static bool prv_send_logical_row(uint16_t logical_y, const uint8_t *data) {
  const uint16_t mapped_y = s_rotated ? PBL_DISPLAY_HEIGHT - 1u - logical_y : logical_y;
  const uint16_t y0 = (mapped_y * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT;
  const uint16_t y1 =
      ((mapped_y + 1u) * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT - 1u;
  prv_convert_row(s_panel_row, data, s_rotated);
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

static bool prv_flush_dma_chunk(size_t *chunk_bytes, size_t *aperture_word) {
  if (*chunk_bytes == 0u) {
    return true;
  }
  if (!prv_pixel_stream_write_dma(s_dma_chunk, *chunk_bytes, aperture_word)) {
    return false;
  }
  *chunk_bytes = 0u;
  return true;
}

static bool prv_stream_logical_row(uint16_t logical_y, const uint8_t *data,
                                   size_t *aperture_word, bool use_dma,
                                   size_t *chunk_bytes) {
  const uint16_t y0 =
      (logical_y * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT;
  const uint16_t y1 =
      ((logical_y + 1u) * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT - 1u;
  if (!use_dma) {
    prv_convert_row(s_panel_row, data, false);
    for (uint16_t y = y0; y <= y1; ++y) {
      *aperture_word = prv_pixel_stream_write(s_panel_row, sizeof(s_panel_row), *aperture_word);
    }
    return true;
  }

  const size_t panel_rows = (size_t)y1 - y0 + 1u;
  const size_t required_bytes = panel_rows * PANEL_ROW_BYTES;
  if (*chunk_bytes + required_bytes > sizeof(s_dma_chunk) &&
      !prv_flush_dma_chunk(chunk_bytes, aperture_word)) {
    return false;
  }
  uint8_t *destination = &s_dma_chunk[*chunk_bytes];
  prv_convert_row(destination, data, false);
  for (size_t row = 1u; row < panel_rows; ++row) {
    memcpy(destination + row * PANEL_ROW_BYTES, destination, PANEL_ROW_BYTES);
  }
  *chunk_bytes += required_bytes;
  return true;
}

static uint16_t prv_panel_row_start(uint16_t logical_y) {
  return (logical_y * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT;
}

static bool prv_draw_diagnostic_pattern(void) {
  // Match Tuya's production frame transfer rather than splitting each row
  // into command-C FIFO transactions. It holds CS low across the 32 00 2c 00
  // preamble and every pixel, then streams incrementing 32-bit writes through
  // the BK7258 QSPI0 data aperture. Use saturated RGB bands so any accepted
  // pixel data is obvious.
  s_last_stream_words = 0u;
  if (!prv_register(0x13u, NULL, 0u)) {
    return false;
  }
  if (!prv_set_window(PANEL_X_OFFSET, 0u, PANEL_X_OFFSET + PANEL_WIDTH - 1u,
                      PANEL_HEIGHT - 1u)) {
    return false;
  }

  if (!prv_pixel_stream_begin()) {
    return false;
  }

  size_t aperture_word = 0u;
  for (uint16_t y = 0u; y < PANEL_HEIGHT; ++y) {
    const uint16_t color = y < PANEL_HEIGHT / 3u   ? 0xf800u
                           : y < 2u * PANEL_HEIGHT / 3u ? 0x07e0u
                                                        : 0x001fu;
    for (uint16_t x = 0u; x < PANEL_WIDTH; ++x) {
      s_panel_row[x * 2u] = (uint8_t)(color >> 8);
      s_panel_row[x * 2u + 1u] = (uint8_t)color;
    }
    aperture_word = prv_pixel_stream_write(s_panel_row, sizeof(s_panel_row), aperture_word);
  }

  prv_pixel_stream_end();
  s_last_stream_words = (uint32_t)aperture_word;
  return true;
}

static bool prv_qspi_init(void) {
  // BK7258 gates QSPI with both the AHB-peripheral power domain and its
  // individual peripheral clock. A cleared power-down bit turns AHBP on.
  const status_t status = t5ai_cp_mailbox_enable_qspi0();
  s_qspi_power_status = status;
  if (FAILED(status)) {
    // The CP power-management service is not guaranteed to be ready during
    // early display bring-up. The AP registers below are the validated native
    // fallback and were used by the original working display probe.
    PBL_LOG_WRN("QSPI0 CP power/clock request failed, using AP fallback: %ld", (long)status);
  }
  prv_power_up();
  delay_us(10);
  for (unsigned pin = 22; pin <= 27; ++pin) {
    // Match Tuya's gpio_dev_map(): bit 3 is the active-low GPIO output enable,
    // so set it to disable GPIO ownership before selecting the peripheral.
    // Use maximum drive on every signal and pull up CS/IO0-IO3.
    GPIO_BASE[pin] = (GPIO_BASE[pin] & ~0x3fffu) | (1u << 3) | (1u << 6) | (3u << 8);
    if (pin != 22u) {
      GPIO_BASE[pin] |= (1u << 4) | (1u << 5);
    }
    volatile uint32_t *function = &SYSTEM_GPIO_FUNCTION_BASE[pin / 8u];
    const unsigned shift = (pin % 8u) * 4u;
    *function = (*function & ~(0x0fu << shift)) | (3u << shift);
  }

  // Match the exact compiled Tuya ordering. Enable the controller before
  // selecting its source clock, program command-A, then release soft reset.
  // Tuya also opens the system QSPI0 interrupt route here. Completion remains
  // polled, so the NVIC line itself stays disabled.
  SYSTEM_CPU1_INTERRUPT_ENABLE |= 1u << 20;
  *prv_qspi(QSPI_CONFIG) = QSPI_CS_HIGH_WAIT | QSPI_ENABLE | QSPI_IO23_ENABLE;
  SYSTEM_CLK_MODE2 = (SYSTEM_CLK_MODE2 & ~((0x0fu << 6) | (1u << 10))) | (5u << 6) |
                     (1u << 10);  // 480 MHz / (5 + 1) = 80 MHz.
  *prv_qspi(QSPI_CMD_A_L) = 0;
  *prv_qspi(QSPI_CMD_A_H) = 0;
  *prv_qspi(QSPI_CMD_A_CFG1) = 0x3u;
  *prv_qspi(QSPI_CMD_A_CFG2) = QSPI_DATA_LINE_4;
  *prv_qspi(QSPI_RST_CFG) = 0u;
  *prv_qspi(QSPI_GLB_CTRL) |= 1u;
  return true;
}

static bool prv_panel_init(void) {
  const OutputConfig reset = {.pin = PANEL_RESET_PIN, .active_high = true};
  gpio_output_init(&reset, GPIO_OType_PP);
  gpio_output_set(&reset, true);
  delay_us(20000);
  gpio_output_set(&reset, false);
  delay_us(200000);
  gpio_output_set(&reset, true);
  delay_us(120000);

  static const struct {
    uint8_t command;
    uint8_t value;
  } init[] = {
      {0xfe, 0x20}, {0x19, 0x10}, {0x1c, 0xa0}, {0xfe, 0x00}, {0xc4, 0x80},
      {0x3a, 0x55}, {0x35, 0x00}, {0x53, 0x20}, {0x51, 0xff}, {0x63, 0xff},
  };
  for (unsigned i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
    if (!prv_register(init[i].command, &init[i].value, 1)) {
      return false;
    }
  }
  if (!prv_register(0x11u, NULL, 0)) {
    return false;
  }
  delay_us(200000);
  // Select normal display mode before enabling output. DCS 0x23 is the
  // all-pixels-on diagnostic and would mask every GRAM update as solid white.
  return prv_register(0x13u, NULL, 0) && prv_register(0x29u, NULL, 0);
}

static void prv_bitbang_write_pin(const OutputConfig *pin, bool level) {
  gpio_output_set(pin, level);
  delay_us(2);
}

static void prv_bitbang_byte(const OutputConfig *clock, const OutputConfig *data, uint8_t value) {
  for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1) {
    prv_bitbang_write_pin(data, (value & mask) != 0u);
    prv_bitbang_write_pin(clock, true);
    prv_bitbang_write_pin(clock, false);
  }
}

static void prv_bitbang_command(const OutputConfig *clock, const OutputConfig *chip_select,
                                const OutputConfig *data, uint8_t command, const uint8_t *payload,
                                size_t length) {
  prv_bitbang_write_pin(chip_select, false);
  prv_bitbang_byte(clock, data, 0x02u);
  prv_bitbang_byte(clock, data, 0x00u);
  prv_bitbang_byte(clock, data, command);
  prv_bitbang_byte(clock, data, 0x00u);
  for (size_t i = 0; i < length; ++i) {
    prv_bitbang_byte(clock, data, payload[i]);
  }
  prv_bitbang_write_pin(chip_select, true);
  delay_us(10);
}

static void prv_bitbang_read(const OutputConfig *clock, const OutputConfig *chip_select,
                             const OutputConfig *data_output, uint8_t command,
                             GPIOPuPd_TypeDef pull, uint8_t data0_result[4],
                             uint8_t data1_result[4]) {
  static const InputConfig data0_input = {.pin = PANEL_QSPI_IO0_PIN};
  static const InputConfig data1_input = {.pin = PANEL_QSPI_IO0_PIN + 1u};

  prv_bitbang_write_pin(chip_select, false);
  prv_bitbang_byte(clock, data_output, 0x03u);
  prv_bitbang_byte(clock, data_output, 0x00u);
  prv_bitbang_byte(clock, data_output, command);
  prv_bitbang_byte(clock, data_output, 0x00u);
  gpio_input_init_pull_up_down(&data0_input, pull);
  gpio_input_init_pull_up_down(&data1_input, pull);
  memset(data0_result, 0, 4u);
  memset(data1_result, 0, 4u);
  for (unsigned bit = 0; bit < 32u; ++bit) {
    prv_bitbang_write_pin(clock, true);
    data0_result[bit / 8u] =
        (uint8_t)((data0_result[bit / 8u] << 1) | (gpio_input_read(&data0_input) ? 1u : 0u));
    data1_result[bit / 8u] =
        (uint8_t)((data1_result[bit / 8u] << 1) | (gpio_input_read(&data1_input) ? 1u : 0u));
    prv_bitbang_write_pin(clock, false);
  }
  prv_bitbang_write_pin(chip_select, true);
  gpio_output_init(data_output, GPIO_OType_PP);
  prv_bitbang_write_pin(data_output, false);
}

void display_co5300_bitbang_probe(void) {
  static const OutputConfig clock = {.pin = PANEL_QSPI_CLK_PIN, .active_high = true};
  static const OutputConfig chip_select = {.pin = PANEL_QSPI_CS_PIN, .active_high = true};
  static const OutputConfig data = {.pin = PANEL_QSPI_IO0_PIN, .active_high = true};
  static const OutputConfig reset = {.pin = PANEL_RESET_PIN, .active_high = true};

  // This is a diagnostic boundary test, not the production transport. Stop
  // QSPI0, return its one-wire command pins to GPIO ownership, and clock the
  // exact CO5300 byte stream in mode 0 at approximately 125 kHz.
  *prv_qspi(QSPI_CONFIG) &= ~QSPI_ENABLE;
  gpio_output_init(&clock, GPIO_OType_PP);
  gpio_output_init(&chip_select, GPIO_OType_PP);
  gpio_output_init(&data, GPIO_OType_PP);
  prv_bitbang_write_pin(&clock, false);
  prv_bitbang_write_pin(&chip_select, true);
  prv_bitbang_write_pin(&data, false);

  gpio_output_init(&reset, GPIO_OType_PP);
  gpio_output_set(&reset, true);
  delay_us(20000);
  gpio_output_set(&reset, false);
  delay_us(200000);
  gpio_output_set(&reset, true);
  delay_us(120000);

  static const struct {
    uint8_t command;
    uint8_t value;
  } init[] = {
      {0xfe, 0x20}, {0x19, 0x10}, {0x1c, 0xa0}, {0xfe, 0x00}, {0xc4, 0x80},
      {0x3a, 0x55}, {0x35, 0x00}, {0x53, 0x20}, {0x51, 0xff}, {0x63, 0xff},
  };
  for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
    prv_bitbang_command(&clock, &chip_select, &data, init[i].command, &init[i].value, 1u);
  }
  prv_bitbang_command(&clock, &chip_select, &data, 0x11u, NULL, 0u);
  delay_us(200000);
  prv_bitbang_command(&clock, &chip_select, &data, 0x29u, NULL, 0u);
  prv_bitbang_command(&clock, &chip_select, &data, 0x23u, NULL, 0u);
  uint8_t data0_float[4];
  uint8_t data1_float[4];
  uint8_t data0_up[4];
  uint8_t data1_up[4];
  uint8_t data0_down[4];
  uint8_t data1_down[4];
  prv_bitbang_read(&clock, &chip_select, &data, 0x0au, GPIO_PuPd_NOPULL, data0_float,
                   data1_float);
  prv_bitbang_read(&clock, &chip_select, &data, 0x0au, GPIO_PuPd_UP, data0_up, data1_up);
  prv_bitbang_read(&clock, &chip_select, &data, 0x0au, GPIO_PuPd_DOWN, data0_down, data1_down);
  prv_trace(
      "PBLBIT done cfg=%08lx clk/cs/io0/io1/rst=%03lx/%03lx/%03lx/%03lx/%03lx "
      "float=%02x%02x%02x%02x/%02x%02x%02x%02x\r\n",
      (unsigned long)*prv_qspi(QSPI_CONFIG), (unsigned long)GPIO_BASE[PANEL_QSPI_CLK_PIN],
      (unsigned long)GPIO_BASE[PANEL_QSPI_CS_PIN], (unsigned long)GPIO_BASE[PANEL_QSPI_IO0_PIN],
      (unsigned long)GPIO_BASE[PANEL_QSPI_IO0_PIN + 1u],
      (unsigned long)GPIO_BASE[PANEL_RESET_PIN], data0_float[0], data0_float[1], data0_float[2],
      data0_float[3], data1_float[0], data1_float[1], data1_float[2], data1_float[3]);
  prv_trace("PBLBIT pull-up=%02x%02x%02x%02x/%02x%02x%02x%02x "
            "pull-down=%02x%02x%02x%02x/%02x%02x%02x%02x\r\n",
            data0_up[0], data0_up[1], data0_up[2], data0_up[3], data1_up[0], data1_up[1],
            data1_up[2], data1_up[3], data0_down[0], data0_down[1], data0_down[2],
            data0_down[3], data1_down[0], data1_down[1], data1_down[2], data1_down[3]);
}

void display_init(void) {
  prv_dma_init();
  s_dma_display_enabled = prv_dma_self_test();
  prv_init_conversion_tables();
  s_initialized = prv_qspi_init() && prv_panel_init();
  s_enabled = s_initialized;
  s_brightness_dirty = false;
  if (!s_initialized) {
    PBL_LOG_ERR("CO5300 initialization failed");
  }
}

void display_co5300_log_diagnostics(void) {
  const status_t boot_power_status = s_qspi_power_status;
  if (s_updating) {
    prv_trace("PBLDSP busy bootpm=%ld", (long)boot_power_status);
    return;
  }

  // The initial CP power vote can run before that service is ready. Retry it
  // on demand after the USART path proves the CP is fully up, then repeat the
  // vendor reset and initialization sequence against the known register state.
  const bool reinitialized = prv_qspi_init() && prv_panel_init();
  s_initialized = reinitialized;
  s_enabled = reinitialized;
  s_brightness_dirty = false;
  const bool pattern_written = reinitialized && prv_draw_diagnostic_pattern();
  prv_trace("PBLPIX continuous=%u words=%lu bytes=%lu cfg=%08lx st=%08lx\r\n",
            (unsigned)pattern_written, (unsigned long)s_last_stream_words,
            (unsigned long)(s_last_stream_words * sizeof(uint32_t)),
            (unsigned long)*prv_qspi(QSPI_CONFIG), (unsigned long)*prv_qspi(QSPI_STATUS));
  uint32_t power_mode_raw = 0u;
  const bool power_mode_read = prv_read_register(0x0au, &power_mode_raw);
  prv_trace(
      "PBLDSP bootpm=%ld nowpm=%ld reinit=%u pattern=%u rd=%u pm=%02lx raw=%08lx "
      "i/e/u=%u/%u/%u glb=%08lx "
      "cfg=%08lx rst=%08lx st=%08lx\r\n",
      (long)boot_power_status, (long)s_qspi_power_status, (unsigned)reinitialized,
      (unsigned)pattern_written, (unsigned)power_mode_read,
      (unsigned long)(power_mode_raw & 0xffu), (unsigned long)power_mode_raw,
      (unsigned)s_initialized, (unsigned)s_enabled, (unsigned)s_updating,
      (unsigned long)*prv_qspi(QSPI_GLB_CTRL), (unsigned long)*prv_qspi(QSPI_CONFIG),
      (unsigned long)*prv_qspi(QSPI_RST_CFG), (unsigned long)*prv_qspi(QSPI_STATUS));
  prv_trace("PBLPWR clk=%08lx gate=%08lx irq=%08lx mem=%08lx/%08lx sleep=%08lx p19=%08lx p29=%08lx\r\n",
            (unsigned long)SYSTEM_CLK_MODE2, (unsigned long)SYSTEM_DEVICE_CLK_ENABLE,
            (unsigned long)SYSTEM_CPU1_INTERRUPT_ENABLE, (unsigned long)SYSTEM_DEVICE_MEM_CTRL1,
            (unsigned long)SYSTEM_DEVICE_MEM_CTRL2, (unsigned long)SYSTEM_POWER_SLEEP_WAKEUP,
            (unsigned long)GPIO_BASE[19], (unsigned long)GPIO_BASE[PANEL_RESET_PIN]);
  prv_trace(
      "PBLPIN p22-27=%03lx/%03lx/%03lx/%03lx/%03lx/%03lx mux=%08lx/%08lx\r\n",
      (unsigned long)GPIO_BASE[22], (unsigned long)GPIO_BASE[23], (unsigned long)GPIO_BASE[24],
      (unsigned long)GPIO_BASE[25], (unsigned long)GPIO_BASE[26], (unsigned long)GPIO_BASE[27],
      (unsigned long)SYSTEM_GPIO_FUNCTION_BASE[2], (unsigned long)SYSTEM_GPIO_FUNCTION_BASE[3]);
  for (uint32_t offset = 0u; offset <= QSPI_STATUS; offset += 16u) {
    prv_trace("PBLREG %02lx=%08lx %02lx=%08lx %02lx=%08lx %02lx=%08lx\r\n", (unsigned long)offset,
              (unsigned long)*prv_qspi(offset), (unsigned long)(offset + 4u),
              (unsigned long)*prv_qspi(offset + 4u), (unsigned long)(offset + 8u),
              (unsigned long)*prv_qspi(offset + 8u), (unsigned long)(offset + 12u),
              (unsigned long)*prv_qspi(offset + 12u));
  }
  // These shared analog registers survive an AP-only replacement and are a
  // useful boundary check for the observation that the formerly visible AP
  // image stopped driving the panel after a complete USB power cycle. Keep
  // this probe read-only until a specific vendor startup write is identified.
  for (unsigned index = 0u; index < 28u; index += 4u) {
    prv_trace("PBLANA %02u=%08lx %02u=%08lx %02u=%08lx %02u=%08lx\r\n", index,
              (unsigned long)SYSTEM_ANALOG_BASE[index], index + 1u,
              (unsigned long)SYSTEM_ANALOG_BASE[index + 1u], index + 2u,
              (unsigned long)SYSTEM_ANALOG_BASE[index + 2u], index + 3u,
              (unsigned long)SYSTEM_ANALOG_BASE[index + 3u]);
  }
}

void display_clear(void) {
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

void display_set_enabled(bool enabled) {
  if (!s_initialized || enabled == s_enabled) {
    return;
  }
  if (prv_register(enabled ? 0x29u : 0x28u, NULL, 0)) {
    s_enabled = enabled;
  }
}

void display_set_rotated(bool rotated) {
  s_rotated = rotated;
}

void display_update(NextRowCallback nrcb, UpdateCompleteCallback uccb) {
  PBL_ASSERTN(!s_updating);
  s_updating = true;
  DisplayRow row;
  bool full_stream = false;
  size_t aperture_word = 0u;
  size_t chunk_bytes = 0u;
  uint16_t expected_row = 0u;
  const bool use_dma = s_dma_display_enabled && !s_dma_display_failed;

  while (s_initialized && nrcb(&row)) {
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
                                  &chunk_bytes)) {
        s_dma_display_failed = true;
        s_dma_display_enabled = false;
        break;
      }
      expected_row++;
    } else {
      if (full_stream) {
        if (!prv_flush_dma_chunk(&chunk_bytes, &aperture_word)) {
          s_dma_display_failed = true;
          s_dma_display_enabled = false;
          break;
        }
        prv_pixel_stream_end();
        full_stream = false;
      }
      if (!prv_send_logical_row(row.address, row.data)) {
        break;
      }
    }
  }
  if (full_stream) {
    if (!prv_flush_dma_chunk(&chunk_bytes, &aperture_word)) {
      s_dma_display_failed = true;
      s_dma_display_enabled = false;
    }
    prv_pixel_stream_end();
  }
  s_updating = false;
  prv_apply_brightness();
  uccb();
}

bool display_update_in_progress(void) {
  return s_updating;
}

void display_update_boot_frame(uint8_t *framebuffer) {
  if (!s_initialized || framebuffer == NULL) {
    return;
  }
  if (!prv_set_window(PANEL_X_OFFSET, 0u, PANEL_X_OFFSET + PANEL_WIDTH - 1u,
                      PANEL_HEIGHT - 1u) ||
      !prv_pixel_stream_begin()) {
    return;
  }
  size_t aperture_word = 0u;
  size_t chunk_bytes = 0u;
  for (uint16_t y = 0; y < PBL_DISPLAY_HEIGHT; ++y) {
    if (!prv_stream_logical_row(y, framebuffer + y * PBL_DISPLAY_WIDTH, &aperture_word, false,
                                &chunk_bytes)) {
      break;
    }
  }
  prv_pixel_stream_end();
}
