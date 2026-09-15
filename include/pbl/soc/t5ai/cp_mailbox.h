/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "system/status_codes.h"

typedef struct {
  uintptr_t address;
  size_t length;
} T5AICPMailboxFrame;

typedef void (*T5AICPMailboxReceiveCallback)(uint8_t logical_channel,
                                             const T5AICPMailboxFrame *frame, void *context);

typedef enum {
  T5AICpuFrequency_26MHz = 0,
  T5AICpuFrequency_60MHz = 1,
  T5AICpuFrequency_80MHz = 2,
  T5AICpuFrequency_120MHz = 3,
  T5AICpuFrequency_240MHz = 4,
  T5AICpuFrequency_320MHz = 5,
  T5AICpuFrequency_480MHz = 6,
} T5AICpuFrequency;

/* One owner for the AP<->CP physical mailbox FIFO. Callers hold the lock for
 * an entire logical transaction, not merely an individual frame. */
status_t t5ai_cp_mailbox_lock(void);
void t5ai_cp_mailbox_unlock(void);
status_t t5ai_cp_mailbox_send(const void *data, size_t length);
status_t t5ai_cp_mailbox_register(uint8_t logical_channel, T5AICPMailboxReceiveCallback callback,
                                  void *context);
void t5ai_cp_mailbox_unregister(uint8_t logical_channel);
status_t t5ai_cp_mailbox_acknowledge(uint32_t header, uint32_t parameter1, uint32_t parameter2,
                                     bool handled);
status_t t5ai_cp_mailbox_process_pending(void);
void t5ai_cp_mailbox_enable_rx_interrupt(void);
status_t t5ai_cp_mailbox_receive(uint8_t logical_channel, T5AICPMailboxFrame *frame);
status_t t5ai_cp_mailbox_try_receive(uint8_t logical_channel, T5AICPMailboxFrame *frame);
void t5ai_cp_mailbox_discard_pending(void);
void t5ai_cp_mailbox_announce_power_up(void);
void t5ai_cp_mailbox_heartbeat(void);
status_t t5ai_cp_mailbox_enable_uart0_clock(void);
status_t t5ai_cp_mailbox_enable_qspi0(void);
status_t t5ai_cp_mailbox_enable_psram(void);
status_t t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency frequency);
status_t t5ai_cp_mailbox_log(const void *data, size_t length);
