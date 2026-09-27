/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "dbgserial.h"

#include <pbl/mcu/interrupts.h>
#include <pbl/os/mutex.h>
#include <pbl/soc/t5ai/cp_mailbox.h>

#include "FreeRTOS.h"
#include "task.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#define T5AI_DBG_SERIAL_BUFFER_SIZE 159u

static uint8_t s_t5ai_buffer[T5AI_DBG_SERIAL_BUFFER_SIZE];
static size_t s_t5ai_buffer_length;
static PebbleMutex *s_t5ai_mutex;

static bool prv_lock(void) {
  if (s_t5ai_mutex == NULL || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
    return false;
  }
  mutex_lock(s_t5ai_mutex);
  return true;
}

static void prv_unlock(bool locked) {
  if (locked) {
    mutex_unlock(s_t5ai_mutex);
  }
}

static void prv_flush(void) {
  if (s_t5ai_buffer_length != 0u) {
    (void)t5ai_cp_mailbox_log(s_t5ai_buffer, s_t5ai_buffer_length);
    s_t5ai_buffer_length = 0u;
  }
}

static void prv_putchar(uint8_t c) {
  if (s_t5ai_buffer_length == sizeof(s_t5ai_buffer)) {
    prv_flush();
  }
  s_t5ai_buffer[s_t5ai_buffer_length++] = c;
}

void dbgserial_init(void) {
  s_t5ai_mutex = mutex_create();
}

void dbgserial_change_baud_rate(uint32_t new_baud) {
  (void)new_baud;
}

void dbgserial_restore_baud_rate(void) {
}

void dbgserial_putstr(const char* str) {
  if (mcu_state_is_isr()) {
    return;
  }
  const bool locked = prv_lock();
  while (*str) {
    prv_putchar(*str);
    ++str;
  }
  prv_putchar('\r');
  prv_putchar('\n');
  prv_flush();
  prv_unlock(locked);
}

void dbgserial_putchar(uint8_t c) {
  if (mcu_state_is_isr()) {
    return;
  }
  const bool locked = prv_lock();
  prv_putchar(c);
  prv_flush();
  prv_unlock(locked);
}

void dbgserial_putchar_lazy(uint8_t c) {
  // The CP mailbox transport cannot take its task mutex or wait for an ACK
  // from an interrupt. Do not disturb a partially assembled task message.
  if (mcu_state_is_isr()) {
    return;
  }
  const bool locked = prv_lock();
  prv_putchar(c);
  prv_unlock(locked);
}

void dbgserial_flush(void) {
  if (mcu_state_is_isr()) {
    return;
  }
  const bool locked = prv_lock();
  prv_flush();
  prv_unlock(locked);
}

void dbgserial_putstr_fmt(char* buffer, unsigned int buffer_size, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsniprintf(buffer, buffer_size, fmt, ap);
  va_end(ap);

  dbgserial_putstr(buffer);
}
