/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "tuya_cloud_types.h"

#define TUYA_HCI_ACL_MAX 1028u
#define TUYA_HCI_COMMAND 1u
#define TUYA_HCI_ACL 2u
#define TUYA_HCI_EVENT 4u

typedef struct {
  uint16_t size;
  uint8_t type;
  uint8_t data[TUYA_HCI_ACL_MAX];
} TuyaHciPacket;

/* Exclusive host ownership is required. Never start alongside Tuya's host stack.
   No advertising/pairing is implemented here. Start once, from task context. */
OPERATE_RET tuya_hci_start(void);
OPERATE_RET tuya_hci_send(const TuyaHciPacket *packet);
OPERATE_RET tuya_hci_receive(TuyaHciPacket *packet, uint32_t timeout_ms);
bool tuya_hci_packet_valid(uint8_t type, const uint8_t *data, size_t size);
