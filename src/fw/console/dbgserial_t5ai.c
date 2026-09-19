/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "dbgserial.h"

#include <pbl/mcu/interrupts.h>
#include <pbl/soc/t5ai/cp_mailbox.h>

#include <stdarg.h>
#include <stdio.h>

#define T5AI_DBG_SERIAL_BUFFER_SIZE 159u

static uint8_t s_t5ai_buffer[T5AI_DBG_SERIAL_BUFFER_SIZE];
static size_t s_t5ai_buffer_length;

void dbgserial_init(void) {
}

void dbgserial_change_baud_rate(uint32_t new_baud) {
  (void)new_baud;
}

void dbgserial_restore_baud_rate(void) {
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
  // The CP mailbox transport cannot take its task mutex or wait for an ACK
  // from an interrupt. Do not disturb a partially assembled task message.
  if (mcu_state_is_isr()) {
    return;
  }
  if (s_t5ai_buffer_length == sizeof(s_t5ai_buffer)) {
    dbgserial_flush();
  }
  s_t5ai_buffer[s_t5ai_buffer_length++] = c;
}

void dbgserial_flush(void) {
  if (mcu_state_is_isr()) {
    return;
  }
  if (s_t5ai_buffer_length != 0u) {
    (void)t5ai_cp_mailbox_log(s_t5ai_buffer, s_t5ai_buffer_length);
    s_t5ai_buffer_length = 0u;
  }
}

void dbgserial_putstr_fmt(char* buffer, unsigned int buffer_size, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsniprintf(buffer, buffer_size, fmt, ap);
  va_end(ap);

  dbgserial_putstr(buffer);
}
