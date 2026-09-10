/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "dbgserial.h"

#include "board/board.h"
#ifdef CONFIG_SOC_T5AI
#include <pbl/soc/t5ai/cp_mailbox.h>
#else
#include <pbl/drivers/uart.h>
#endif

#include <stdarg.h>
#include <stdio.h>

#ifdef CONFIG_SOC_T5AI
#define T5AI_DBG_SERIAL_BUFFER_SIZE 159u
static uint8_t s_t5ai_buffer[T5AI_DBG_SERIAL_BUFFER_SIZE];
static size_t s_t5ai_buffer_length;
#endif

#ifdef CONFIG_PULSE_EVERYWHERE
#define DEFAULT_SERIAL_BAUD_RATE 1000000
#elif defined(CONFIG_SOC_T5AI)
#define DEFAULT_SERIAL_BAUD_RATE 460800
#else
#define DEFAULT_SERIAL_BAUD_RATE 115200
#endif

void dbgserial_init(void) {
#ifndef CONFIG_SOC_T5AI
#if !defined(CONFIG_RELEASE) || defined(CONFIG_MFG)
  uart_init(DBG_UART);
#else
  uart_init_tx_only(DBG_UART);
#endif
  dbgserial_restore_baud_rate();
#endif
}

void dbgserial_change_baud_rate(uint32_t new_baud) {
#ifdef CONFIG_SOC_T5AI
  (void)new_baud;
#else
  uart_set_baud_rate(DBG_UART, new_baud);
#endif
}

void dbgserial_restore_baud_rate(void) {
  dbgserial_change_baud_rate(DEFAULT_SERIAL_BAUD_RATE);
}

void dbgserial_putstr(const char* str) {
  while (*str) {
    dbgserial_putchar_lazy(*str);
    ++str;
  }
  dbgserial_putchar_lazy('\r');
  dbgserial_putchar_lazy('\n');
  dbgserial_flush();
}

void dbgserial_putchar(uint8_t c) {
  dbgserial_putchar_lazy(c);
  dbgserial_flush();
}

void dbgserial_putchar_lazy(uint8_t c) {
#ifdef CONFIG_SOC_T5AI
  if (s_t5ai_buffer_length == sizeof(s_t5ai_buffer)) {
    dbgserial_flush();
  }
  s_t5ai_buffer[s_t5ai_buffer_length++] = c;
#else
  uart_write_byte(DBG_UART, c);
#endif
}

void dbgserial_flush(void) {
#ifdef CONFIG_SOC_T5AI
  if (s_t5ai_buffer_length != 0u) {
    (void)t5ai_cp_mailbox_log(s_t5ai_buffer, s_t5ai_buffer_length);
    s_t5ai_buffer_length = 0u;
  }
#else
  uart_wait_for_tx_complete(DBG_UART);
#endif
}

void dbgserial_putstr_fmt(char* buffer, unsigned int buffer_size, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsniprintf(buffer, buffer_size, fmt, ap);
  va_end(ap);

  dbgserial_putstr(buffer);
}
