/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "hci_port.h"
#include "tal_api.h"
#include "tkl_hci.h"
#include <string.h>

static QUEUE_HANDLE s_rx;
static MUTEX_HANDLE s_tx;
static bool s_started;
static int s_ready, s_failed;

bool tuya_hci_packet_valid(uint8_t type, const uint8_t *data, size_t size) {
  if (!data) {
    return false;
  }
  switch (type) {
    case TUYA_HCI_COMMAND:
      return size >= 3 && size <= 258 && size == (size_t)data[2] + 3;
    case TUYA_HCI_EVENT:
      return size >= 2 && size <= 257 && size == (size_t)data[1] + 2;
    case TUYA_HCI_ACL:
      return size >= 4 && size <= TUYA_HCI_ACL_MAX && size == 4u + data[2] + ((size_t)data[3] << 8);
    default:
      return false;
  }
}

static OPERATE_RET prv_receive(uint8_t type, uint8_t *data, uint16_t size) {
  if (!__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE)) {
    return OPRT_COM_ERROR;
  }
  if (!tuya_hci_packet_valid(type, data, size)) {
    __atomic_store_n(&s_failed, 1, __ATOMIC_RELEASE);
    return OPRT_INVALID_PARM;
  }
  TuyaHciPacket packet = {.type = type, .size = size};
  memcpy(packet.data, data, size);
  /* The IPC callback buffer is borrowed; queue an owned copy, never block IPC. */
  OPERATE_RET rt = tal_queue_post(s_rx, &packet, 0);
  if (rt != OPRT_OK) {
    __atomic_store_n(&s_failed, 1, __ATOMIC_RELEASE);
  }
  return rt;
}
static OPERATE_RET prv_event(uint8_t *data, uint16_t size) {
  return prv_receive(TUYA_HCI_EVENT, data, size);
}
static OPERATE_RET prv_acl(uint8_t *data, uint16_t size) {
  return prv_receive(TUYA_HCI_ACL, data, size);
}

OPERATE_RET tuya_hci_start(void) {
  if (s_started) {
    return OPRT_COM_ERROR;
  }
  s_started = true;
  OPERATE_RET rt = tal_queue_create_init(&s_rx, sizeof(TuyaHciPacket), 8);
  if (rt != OPRT_OK) {
    return rt;
  }
  rt = tal_mutex_create_init(&s_tx);
  if (rt != OPRT_OK) {
    return rt;
  }
  rt = tkl_hci_callback_register(prv_event, prv_acl);
  if (rt != OPRT_OK) {
    return rt;
  }
  __atomic_store_n(&s_ready, 1, __ATOMIC_RELEASE);
  rt = tkl_hci_init();
  if (rt != OPRT_OK) {
    __atomic_store_n(&s_ready, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s_failed, 1, __ATOMIC_RELEASE);
  }
  /* Keep queues alive after failures: callbacks can still be in flight. */
  return rt;
}

OPERATE_RET tuya_hci_send(const TuyaHciPacket *packet) {
  if (!packet || !__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&s_failed, __ATOMIC_ACQUIRE)) {
    return OPRT_COM_ERROR;
  }
  if ((packet->type != TUYA_HCI_COMMAND && packet->type != TUYA_HCI_ACL) ||
      !tuya_hci_packet_valid(packet->type, packet->data, packet->size)) {
    return OPRT_INVALID_PARM;
  }
  OPERATE_RET rt = tal_mutex_lock(s_tx);
  if (rt != OPRT_OK) {
    return rt;
  }
  if (__atomic_load_n(&s_failed, __ATOMIC_ACQUIRE)) {
    tal_mutex_unlock(s_tx);
    return OPRT_COM_ERROR;
  }
  rt = packet->type == TUYA_HCI_COMMAND ? tkl_hci_cmd_packet_send(packet->data, packet->size)
                                        : tkl_hci_acl_packet_send(packet->data, packet->size);
  tal_mutex_unlock(s_tx);
  if (rt != OPRT_OK) {
    __atomic_store_n(&s_failed, 1, __ATOMIC_RELEASE);
  }
  return rt;
}

OPERATE_RET tuya_hci_receive(TuyaHciPacket *packet, uint32_t timeout_ms) {
  if (!packet || !__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&s_failed, __ATOMIC_ACQUIRE)) {
    return OPRT_COM_ERROR;
  }
  OPERATE_RET rt = tal_queue_fetch(s_rx, packet, timeout_ms);
  /* Do not silently continue a host session after any dropped packet. */
  return __atomic_load_n(&s_failed, __ATOMIC_ACQUIRE) ? OPRT_COM_ERROR : rt;
}
