/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef void (*T5AIHCIReceiveCallback)(const uint8_t *data, size_t length);

typedef struct {
  T5AIHCIReceiveCallback event;
  T5AIHCIReceiveCallback acl;
} T5AIHCICallbacks;

/*
 * Controller-only interface to the CP Bluetooth firmware. Receive buffers are
 * borrowed from CP IPC and are valid only for the duration of the callback.
 * The implementation must serialize requests on the shared mailbox and must
 * not start a vendor host stack on either core.
 */
int t5ai_hci_ipc_start(const T5AIHCICallbacks *callbacks);
int t5ai_hci_ipc_stop(void);
int t5ai_hci_ipc_send_command(const uint8_t *data, size_t length);
int t5ai_hci_ipc_send_acl(const uint8_t *data, size_t length);
void t5ai_hci_ipc_poll(void);
