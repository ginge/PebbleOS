/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define T5AI_MIPC_TX_LOGICAL_CHANNEL 0x17u
#define T5AI_MIPC_RX_LOGICAL_CHANNEL 0x47u
#define T5AI_MIPC_CRC_INITIAL 0xffu

enum {
  T5AI_MIPC_TYPE_RESERVED = 0,
  T5AI_MIPC_TYPE_COMMAND = 1,
  T5AI_MIPC_TYPE_ACK = 2,
  T5AI_MIPC_TYPE_SYSTEM = 3,
};

typedef struct {
  uint32_t next;
  uint32_t previous;
} T5AIMIPCList;

typedef struct {
  uint32_t ipc;
  uint32_t route;
  uint32_t name;
  uint32_t receive_callback;
  uint32_t context;
  uint32_t transmit_callback;
} T5AIMIPCChannelConfig;

typedef struct {
  uint32_t id;
  uint32_t config;
  T5AIMIPCList list;
  uint32_t ipc;
} T5AIMIPCHandle;

typedef struct {
  T5AIMIPCList list;
  uint32_t result;
  uint32_t handle;
  uint32_t semaphore;
  uint32_t data;
  uint32_t size;
  uint32_t flags;
} T5AIMIPCData;

typedef struct {
  uint32_t header;
  uint32_t object;
  uint32_t result;
  uint32_t reserved;
} T5AIMIPCFrame;

uint8_t t5ai_mipc_crc8(const void *data, size_t length, uint8_t initial);
uint8_t t5ai_mipc_data_crc(const T5AIMIPCData *data);
uint8_t t5ai_mipc_header(uint8_t source, uint8_t type);
uint32_t t5ai_mipc_frame_header(uint8_t command, uint8_t state, uint8_t control, uint8_t sequence,
                                uint8_t logical_channel);
bool t5ai_mipc_frame_valid(const T5AIMIPCFrame *frame, const T5AIMIPCData *data);

_Static_assert(sizeof(T5AIMIPCList) == 8u, "T5AI MIPC list ABI changed");
_Static_assert(sizeof(T5AIMIPCChannelConfig) == 24u, "T5AI MIPC channel ABI changed");
_Static_assert(sizeof(T5AIMIPCHandle) == 20u, "T5AI MIPC handle ABI changed");
_Static_assert(sizeof(T5AIMIPCData) == 32u, "T5AI MIPC data ABI changed");
_Static_assert(sizeof(T5AIMIPCFrame) == 16u, "T5AI MIPC frame ABI changed");
