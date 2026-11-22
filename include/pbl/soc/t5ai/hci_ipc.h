/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Return false to retain this AP-owned packet for a nonblocking retry.
typedef bool (*T5AIHCIReceiveCallback)(const uint8_t *data, size_t length);

typedef struct {
  T5AIHCIReceiveCallback event;
  T5AIHCIReceiveCallback acl;
  bool (*discardable_event)(const uint8_t *data, size_t length);
} T5AIHCICallbacks;

#ifdef CONFIG_T5AI_HCI_PROFILE
typedef struct {
  uint32_t work_notifications;
  uint32_t work_timeouts;
  uint32_t send_busy_retries;
  uint32_t send_timeouts;
  uint32_t exchange_timeouts;
  uint32_t frames_received;
  uint32_t transport_acks_received;
  uint32_t commands_received;
  uint32_t events_received;
  uint32_t acl_received;
  uint32_t responses_sent;
  uint32_t mipc_acks_sent;
  uint32_t request_exhaustions;
  uint32_t response_exhaustions;
  uint32_t pending_queue_exhaustions;
  uint8_t pending_queue_high_water;
  uint8_t request_slots_in_use;
  uint8_t response_slots_in_use;
  bool started;
  bool faulted;
} T5AIHCIIpcDiagnostics;
#endif

/*
 * Controller-only interface to the CP Bluetooth firmware. Receive buffers are
 * AP-owned copies, borrowed only for the duration of each callback invocation.
 * The implementation serializes synchronous HCI requests independently from
 * asynchronous controller callbacks; physical mailbox ownership is internal
 * to the T5AI dispatcher. It must not start a vendor host stack on either core.
 */
int t5ai_hci_ipc_start(const T5AIHCICallbacks *callbacks);
int t5ai_hci_ipc_stop(void);
int t5ai_hci_ipc_send_command(const uint8_t *data, size_t length);
int t5ai_hci_ipc_send_acl(const uint8_t *data, size_t length);
int t5ai_hci_ipc_request_system_reboot(void);
bool t5ai_hci_ipc_faulted(void);
void t5ai_hci_ipc_poll(void);
#ifdef CONFIG_T5AI_HCI_PROFILE
void t5ai_hci_ipc_get_diagnostics(T5AIHCIIpcDiagnostics *diagnostics);
#endif

#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
// Nonblocking final idle checks include logical leases beyond physical mailbox ACK.
bool t5ai_hci_ipc_sleep_ready(void);
bool t5ai_hci_sleep_ready(void);
#endif

// Latched work notification; a signal before the worker waits is retained.
void t5ai_hci_notify_work(void);
