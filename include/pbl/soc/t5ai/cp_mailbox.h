/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "system/status_codes.h"

typedef struct {
  uintptr_t address;
  size_t length;
  /* Every frame is an AP-owned snapshot. Foreign CP storage is invalidated and
   * copied once, then never modified, cleaned, or republished. */
  uint8_t snapshot[16];
} T5AICPMailboxFrame;

typedef void (*T5AICPMailboxReceiveCallback)(uint8_t logical_channel,
                                             const T5AICPMailboxFrame *frame, void *context);

#define T5AI_CP_MAILBOX_TRACE_CAPACITY 32u
#define T5AI_CP_MAILBOX_FAILURE_TRACE_CAPACITY 8u

typedef enum {
  T5AICPMailboxTraceEvent_TxQueued = 1,
  T5AICPMailboxTraceEvent_TxPublished,
  T5AICPMailboxTraceEvent_TxRetired,
  T5AICPMailboxTraceEvent_Rx,
  T5AICPMailboxTraceEvent_Routed,
  T5AICPMailboxTraceEvent_Unrouted,
  T5AICPMailboxTraceEvent_Error,
  T5AICPMailboxTraceEvent_AckMismatch,
} T5AICPMailboxTraceEvent;

typedef struct {
  uint32_t ticks;
  uint32_t header;
  uint32_t parameter;
  int32_t status;
  uint8_t event;
  uint8_t tx_depth;
  uint16_t reserved;
} T5AICPMailboxTraceEntry;

typedef struct {
  uint32_t tx_queued;
  uint32_t tx_published;
  uint32_t tx_retired;
  uint32_t tx_ack_bypass;
  uint32_t rx_frames;
  uint32_t rx_transport_acks;
  uint32_t rx_routed;
  uint32_t rx_unrouted;
  uint32_t send_failures;
  uint32_t receive_timeouts;
  uint32_t tx_ack_mismatches;
  uint32_t trace_count;
  uint8_t tx_depth;
  uint8_t tx_high_water;
  uint16_t reserved;
  T5AICPMailboxTraceEntry trace[T5AI_CP_MAILBOX_TRACE_CAPACITY];
} T5AICPMailboxDiagnostics;

typedef struct {
  uint32_t context;
  int32_t status;
  uint32_t ticks;
  uint32_t tx_queued;
  uint32_t tx_published;
  uint32_t tx_retired;
  uint32_t tx_ack_bypass;
  uint32_t rx_frames;
  uint32_t rx_transport_acks;
  uint32_t rx_routed;
  uint32_t rx_unrouted;
  uint32_t send_failures;
  uint32_t receive_timeouts;
  uint32_t tx_ack_mismatches;
  uint32_t trace_count;
  uint32_t tx_head_header;
  uint32_t tx_head_parameter;
  uint32_t mailbox_control;
  uint32_t tx_fifo_status;
  uint8_t tx_depth;
  uint8_t tx_high_water;
  uint8_t tx_pending;
  uint8_t rx_pending;
  T5AICPMailboxTraceEntry trace[T5AI_CP_MAILBOX_FAILURE_TRACE_CAPACITY];
} T5AICPMailboxFailureDiagnostics;

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
void t5ai_cp_mailbox_heartbeat(void);
status_t t5ai_cp_mailbox_enable_uart0_clock(void);
status_t t5ai_cp_mailbox_enable_qspi0(void);
status_t t5ai_cp_mailbox_enable_psram(void);
status_t t5ai_cp_mailbox_vote_app_sleep(bool allow_sleep);
status_t t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency frequency);
status_t t5ai_cp_mailbox_log(const void *data, size_t length);
void t5ai_cp_mailbox_get_diagnostics(T5AICPMailboxDiagnostics *diagnostics);
void t5ai_cp_mailbox_snapshot_failure(uint32_t context, status_t status);
bool t5ai_cp_mailbox_get_failure_diagnostics(T5AICPMailboxFailureDiagnostics *diagnostics);
void t5ai_cp_mailbox_clear_failure_diagnostics(void);
