/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "dbgserial.h"

#include <pbl/mcu/interrupts.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sched.h>
#include <pbl/soc/t5ai/cp_mailbox.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#define T5AI_DBG_SERIAL_BUFFER_SIZE 159u

static uint8_t s_t5ai_buffer[T5AI_DBG_SERIAL_BUFFER_SIZE];
static size_t s_t5ai_buffer_length;
static PBL_MUTEX_DEFINE(s_t5ai_mutex);
static bool s_t5ai_mutex_ready;

static bool prv_can_write(void) {
  // Runtime mailbox output needs interrupts and a schedulable worker.
  return !mcu_state_is_isr() &&
         !(pbl_kernel_is_started() && (pbl_irq_is_locked() || pbl_sched_is_locked()));
}

static bool prv_lock(void) {
  if (!s_t5ai_mutex_ready || !pbl_kernel_is_running()) {
    return false;
  }
  pbl_mutex_lock(&s_t5ai_mutex, PBL_FOREVER);
  return true;
}

static void prv_unlock(bool locked) {
  if (locked) {
    pbl_mutex_unlock(&s_t5ai_mutex);
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
  s_t5ai_mutex_ready = true;
}

void dbgserial_change_baud_rate(uint32_t new_baud) {
  (void)new_baud;
}

void dbgserial_restore_baud_rate(void) {
}

void dbgserial_putstr(const char* str) {
  if (!prv_can_write()) {
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
  if (!prv_can_write()) {
    return;
  }
  const bool locked = prv_lock();
  prv_putchar(c);
  prv_flush();
  prv_unlock(locked);
}

void dbgserial_putchar_lazy(uint8_t c) {
  // Do not disturb a partially assembled task message when output cannot block.
  if (!prv_can_write()) {
    return;
  }
  const bool locked = prv_lock();
  prv_putchar(c);
  prv_unlock(locked);
}

void dbgserial_flush(void) {
  if (!prv_can_write()) {
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
