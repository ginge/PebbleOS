/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/display/display.h>
#include <pbl/drivers/display/t5ai.h>

#include "board/board.h"
#include <pbl/drivers/gpio.h>
#include "kernel/util/delay.h"
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include "system/passert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DEFINE(driver_display_co5300, CONFIG_DRIVER_DISPLAY_LOG_LEVEL);

#define PANEL_WIDTH 466u
#define PANEL_HEIGHT 466u
#define PANEL_X_OFFSET 6u
#define PANEL_RESET_PIN 29u
#define QSPI_FIFO_SIZE 256u

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

#define SYSTEM_CLK_MODE2 (*(volatile uint32_t *)0x44010024u)
#define SYSTEM_DEVICE_CLK_ENABLE (*(volatile uint32_t *)0x44010030u)
#define SYSTEM_DEVICE_MEM_CTRL1 (*(volatile uint32_t *)0x44010038u)
#define SYSTEM_DEVICE_MEM_CTRL2 (*(volatile uint32_t *)0x4401003cu)
#define SYSTEM_GPIO_FUNCTION_BASE ((volatile uint32_t *)0x440100c0u)
#define GPIO_BASE ((volatile uint32_t *)0x44000400u)

static bool s_initialized;
static bool s_enabled;
static bool s_updating;
static bool s_rotated;
static uint8_t s_brightness;
static bool s_brightness_dirty;
static uint8_t s_panel_row[PANEL_WIDTH * 2u] __attribute__((aligned(4)));

static volatile uint32_t *prv_qspi(uint32_t offset) {
  return (volatile uint32_t *)(QSPI_BASE + offset);
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

  if (length <= sizeof(uint32_t)) {
    uint32_t word = 0;
    if (length != 0u) {
      memcpy(&word, data, length);
    }
    *prv_qspi(QSPI_CMD_C_L) = word;
  } else {
    volatile uint32_t *fifo = prv_qspi(QSPI_FIFO_DATA);
    for (size_t offset = 0; offset < length; offset += sizeof(uint32_t)) {
      uint32_t word = 0;
      const size_t remaining = length - offset;
      memcpy(&word, data + offset, remaining < sizeof(word) ? remaining : sizeof(word));
      fifo[offset / sizeof(word)] = word;
    }
  }
  *prv_qspi(QSPI_CMD_C_CFG1) = length <= sizeof(uint32_t) ? 0x3u << ((length + 4u) * 2u) : 0x300u;
  *prv_qspi(QSPI_CMD_C_CFG2) = length <= sizeof(uint32_t)
                                   ? 0u
                                   : ((uint32_t)length << 2) | (quad_data ? QSPI_DATA_LINE_4 : 0u);
  *prv_qspi(QSPI_CMD_C_CFG2) |= QSPI_CMD_START;
  const bool result = prv_wait_command();
  *prv_qspi(QSPI_CMD_C_CFG1) = 0;
  *prv_qspi(QSPI_CMD_C_CFG2) = 0;
  return result;
}

static bool prv_register(uint8_t command, const uint8_t *data, size_t length) {
  return prv_command(command, data, length, false);
}

static void prv_apply_brightness(void) {
  if (s_initialized && !s_updating && s_brightness_dirty &&
      prv_register(0x51u, &s_brightness, 1u)) {
    s_brightness_dirty = false;
  }
}

void display_co5300_set_brightness(uint8_t level) {
  s_brightness = level;
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

static uint16_t prv_rgb565(uint8_t pixel) {
  static const uint8_t red_blue[4] = {0u, 10u, 21u, 31u};
  const uint16_t red = red_blue[(pixel >> 4) & 3u];
  const uint16_t green = ((pixel >> 2) & 3u) * 21u;
  const uint16_t blue = red_blue[pixel & 3u];
  return (red << 11) | (green << 5) | blue;
}

static void prv_convert_row(const uint8_t *source, bool reverse) {
  for (unsigned x = 0; x < PANEL_WIDTH; ++x) {
    unsigned source_x = x * PBL_DISPLAY_WIDTH / PANEL_WIDTH;
    if (reverse) {
      source_x = PBL_DISPLAY_WIDTH - 1u - source_x;
    }
    const uint16_t pixel = prv_rgb565(source[source_x]);
    // The panel consumes RGB565 most-significant byte first.
    s_panel_row[x * 2u] = (uint8_t)(pixel >> 8);
    s_panel_row[x * 2u + 1u] = (uint8_t)pixel;
  }
}

static bool prv_send_panel_row(void) {
  for (size_t offset = 0; offset < sizeof(s_panel_row); offset += QSPI_FIFO_SIZE) {
    const size_t remaining = sizeof(s_panel_row) - offset;
    const size_t chunk_length = remaining < QSPI_FIFO_SIZE ? remaining : QSPI_FIFO_SIZE;
    if (!prv_command(offset == 0u ? 0x2cu : 0x3cu, s_panel_row + offset, chunk_length, true)) {
      return false;
    }
  }
  return true;
}

static bool prv_send_logical_row(uint16_t logical_y, const uint8_t *data) {
  const uint16_t mapped_y = s_rotated ? PBL_DISPLAY_HEIGHT - 1u - logical_y : logical_y;
  const uint16_t y0 = (mapped_y * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT;
  const uint16_t y1 =
      ((mapped_y + 1u) * PANEL_HEIGHT + PBL_DISPLAY_HEIGHT - 1u) / PBL_DISPLAY_HEIGHT - 1u;
  prv_convert_row(data, s_rotated);
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

static void prv_qspi_init(void) {
  // BK7258 gates QSPI with both the AHB-peripheral power domain and its
  // individual peripheral clock. A cleared power-down bit turns AHBP on.
  (void)t5ai_cp_mailbox_enable_qspi0();
  prv_power_up();
  delay_us(10);
  SYSTEM_CLK_MODE2 = (SYSTEM_CLK_MODE2 & ~((0x0fu << 6) | (1u << 10))) | (11u << 6) |
                     (1u << 10);  // 480 MHz / (11 + 1) = 40 MHz.

  for (unsigned pin = 22; pin <= 27; ++pin) {
    // Match Tuya's T5AI adapter: secondary function, maximum drive on every
    // QSPI signal, pull-up on CS and IO0-IO3 (but not clock).
    GPIO_BASE[pin] = (GPIO_BASE[pin] & ~0x3fffu) | (1u << 3) | (1u << 6) | (3u << 8);
    if (pin != 22u) {
      GPIO_BASE[pin] |= (1u << 4) | (1u << 5);
    }
    volatile uint32_t *function = &SYSTEM_GPIO_FUNCTION_BASE[pin / 8u];
    const unsigned shift = (pin % 8u) * 4u;
    *function = (*function & ~(0x0fu << shift)) | (3u << shift);
  }

  *prv_qspi(QSPI_GLB_CTRL) = 1u;
  *prv_qspi(QSPI_GLB_CTRL) = 0;
  *prv_qspi(QSPI_CONFIG) = QSPI_ENABLE | QSPI_IO23_ENABLE;
  *prv_qspi(QSPI_CMD_A_L) = 0;
  *prv_qspi(QSPI_CMD_A_H) = 0;
  *prv_qspi(QSPI_CMD_A_CFG1) = 0x3u;
  *prv_qspi(QSPI_CMD_A_CFG2) = QSPI_DATA_LINE_4;
  *prv_qspi(QSPI_RST_CFG) |= 1u << 1;
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
      {0x3a, 0x55}, {0x35, 0x00}, {0x53, 0x20}, {0x63, 0xff},
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
  return prv_register(0x51u, &s_brightness, 1u) && prv_register(0x29u, NULL, 0);
}

void display_init(void) {
  prv_qspi_init();
  s_initialized = prv_panel_init();
  s_enabled = s_initialized;
  s_brightness_dirty = false;
  if (!s_initialized) {
    PBL_LOG_ERR("CO5300 initialization failed");
  }
}

void display_clear(void) {
  if (!s_initialized) {
    return;
  }
  memset(s_panel_row, 0, sizeof(s_panel_row));
  for (uint16_t y = 0; y < PANEL_HEIGHT; ++y) {
    if (!prv_set_window(PANEL_X_OFFSET, y, PANEL_X_OFFSET + PANEL_WIDTH - 1u, y) ||
        !prv_send_panel_row()) {
      return;
    }
  }
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
  while (s_initialized && nrcb(&row)) {
    if (row.address >= PBL_DISPLAY_HEIGHT || !prv_send_logical_row(row.address, row.data)) {
      break;
    }
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
  for (uint16_t y = 0; y < PBL_DISPLAY_HEIGHT; ++y) {
    if (!prv_send_logical_row(y, framebuffer + y * PBL_DISPLAY_WIDTH)) {
      return;
    }
  }
}
