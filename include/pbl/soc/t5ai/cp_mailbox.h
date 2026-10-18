/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pbl/kernel/types.h>
#include <pbl/soc/t5ai/mailbox_channel.h>

typedef struct {
  uintptr_t address;
  size_t length;
  /* Every frame is an AP-owned snapshot. Foreign CP storage is invalidated and
   * copied once, then never modified, cleaned, or republished. */
  uint8_t snapshot[T5AI_MAILBOX_FRAME_SIZE];
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

/* Serializes only the T5AI control/PM client's reusable request state. Physical
 * FIFO ownership is internal to send/receive and is never held while a logical
 * client waits for its CP response. New logical clients should use their own
 * transaction mutex rather than this compatibility lock. */
status_t t5ai_cp_mailbox_lock(void);
void t5ai_cp_mailbox_unlock(void);
status_t t5ai_cp_mailbox_send(const void *data, size_t length);
status_t t5ai_cp_mailbox_send_priority(const void *data, size_t length);
status_t t5ai_cp_mailbox_register(uint8_t logical_channel, T5AICPMailboxReceiveCallback callback,
                                  void *context);
void t5ai_cp_mailbox_unregister(uint8_t logical_channel);
status_t t5ai_cp_mailbox_acknowledge_frame(const T5AICPMailboxFrame *frame, bool handled);
status_t t5ai_cp_mailbox_process_pending(void);
void t5ai_cp_mailbox_enable_rx_interrupt(void);
status_t t5ai_cp_mailbox_receive(uint8_t logical_channel, T5AICPMailboxFrame *frame);
status_t t5ai_cp_mailbox_try_receive(uint8_t logical_channel, T5AICPMailboxFrame *frame);
void t5ai_cp_mailbox_discard_pending(void);
void t5ai_cp_mailbox_announce_power_up(void);
/* Mailbox-task only: service a due heartbeat and return ticks until the next one. */
pbl_tick_t t5ai_cp_mailbox_heartbeat_poll(void);
status_t t5ai_cp_mailbox_enable_uart0_clock(void);
status_t t5ai_cp_mailbox_enable_qspi0(void);
status_t t5ai_cp_mailbox_enable_psram(void);
status_t t5ai_cp_mailbox_set_audio_power(bool enabled);
status_t t5ai_cp_mailbox_set_audio_clock(bool enabled);
status_t t5ai_cp_mailbox_vote_app_sleep(bool allow_sleep);
status_t t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency frequency);
status_t t5ai_cp_mailbox_log(const void *data, size_t length);
bool t5ai_cp_mailbox_shared_range_valid(uintptr_t address, size_t length);
