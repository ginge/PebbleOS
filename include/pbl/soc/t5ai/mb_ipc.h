/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "system/status_codes.h"

typedef struct T5AIMbIpcSocket T5AIMbIpcSocket;

status_t t5ai_mb_ipc_init(void);
status_t t5ai_mb_ipc_open(uint8_t client_port, uint8_t server_port,
                          T5AIMbIpcSocket **socket);
status_t t5ai_mb_ipc_send(T5AIMbIpcSocket *socket, uint8_t command, const void *data,
                          size_t length, uint32_t timeout_ms);
status_t t5ai_mb_ipc_receive(T5AIMbIpcSocket *socket, uint8_t *command, void *data,
                             size_t length, uint32_t timeout_ms);
// AON timestamp before acknowledging the last received service completion.
uint32_t t5ai_mb_ipc_last_receive_aon(const T5AIMbIpcSocket *socket);
void t5ai_mb_ipc_discard(T5AIMbIpcSocket *socket);
void t5ai_mb_ipc_poison(T5AIMbIpcSocket *socket);
