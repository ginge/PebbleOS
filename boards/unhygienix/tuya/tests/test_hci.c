/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "hci_port.h"
#include "tal_api.h"
#include "tkl_hci.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static TuyaHciPacket s_queue[8];
static unsigned s_count, s_head, s_sent;
static bool s_locked;
static TKL_HCI_FUNC_CB s_event, s_acl;
static int s_failure;
OPERATE_RET tal_queue_create_init(QUEUE_HANDLE *queue, uint32_t size, uint32_t count) {
  assert(size == sizeof(TuyaHciPacket) && count == 8);
  *queue = s_queue;
  return OPRT_OK;
}
OPERATE_RET tal_queue_post(QUEUE_HANDLE queue, const void *data, uint32_t timeout) {
  assert(queue == s_queue && timeout == 0);
  if (s_count == 8) {
    return OPRT_COM_ERROR;
  }
  s_queue[(s_head + s_count++) % 8] = *(const TuyaHciPacket *)data;
  return OPRT_OK;
}
OPERATE_RET tal_queue_fetch(QUEUE_HANDLE queue, void *data, uint32_t timeout) {
  (void)timeout;
  assert(queue == s_queue);
  if (!s_count) {
    return OPRT_COM_ERROR;
  }
  *(TuyaHciPacket *)data = s_queue[s_head++ % 8];
  --s_count;
  return OPRT_OK;
}
OPERATE_RET tal_mutex_create_init(MUTEX_HANDLE *mutex) {
  *mutex = &s_locked;
  return OPRT_OK;
}
OPERATE_RET tal_mutex_lock(MUTEX_HANDLE mutex) {
  assert(mutex == &s_locked && !s_locked);
  s_locked = true;
  return OPRT_OK;
}
OPERATE_RET tal_mutex_unlock(MUTEX_HANDLE mutex) {
  assert(mutex == &s_locked && s_locked);
  s_locked = false;
  return OPRT_OK;
}
OPERATE_RET tkl_hci_callback_register(TKL_HCI_FUNC_CB event, TKL_HCI_FUNC_CB acl) {
  s_event = event;
  s_acl = acl;
  return OPRT_OK;
}
OPERATE_RET tkl_hci_init(void) {
  return s_failure == 1 ? OPRT_COM_ERROR : OPRT_OK;
}
OPERATE_RET tkl_hci_cmd_packet_send(const uint8_t *data, uint16_t size) {
  assert(s_locked && tuya_hci_packet_valid(TUYA_HCI_COMMAND, data, size));
  ++s_sent;
  return s_failure == 2 ? OPRT_COM_ERROR : OPRT_OK;
}
OPERATE_RET tkl_hci_acl_packet_send(const uint8_t *data, uint16_t size) {
  assert(s_locked && tuya_hci_packet_valid(TUYA_HCI_ACL, data, size));
  ++s_sent;
  return OPRT_OK;
}
int main(int argc, char **argv) {
  (void)argc;
  if (argv[1] && !strcmp(argv[1], "init_failure")) {
    s_failure = 1;
  }
  if (argv[1] && !strcmp(argv[1], "send_failure")) {
    s_failure = 2;
  }
  TuyaHciPacket reset = {.type = TUYA_HCI_COMMAND, .size = 3, .data = {3, 12, 0}}, rx;
  assert(tuya_hci_send(&reset) != OPRT_OK);
  assert(!tuya_hci_packet_valid(TUYA_HCI_EVENT, NULL, 0));
  uint8_t event[] = {0x0e, 4, 1, 3, 12, 0};
  for (unsigned size = 0; size < sizeof(event); ++size) {
    assert(!tuya_hci_packet_valid(TUYA_HCI_EVENT, event, size));
  }
  if (s_failure == 1) {
    assert(tuya_hci_start() != OPRT_OK);
    assert(s_event(event, sizeof(event)) != OPRT_OK);
    assert(tuya_hci_send(&reset) != OPRT_OK);
    assert(tuya_hci_receive(&rx, 0) != OPRT_OK);
    return 0;
  }
  assert(tuya_hci_start() == OPRT_OK);
  assert(tuya_hci_start() != OPRT_OK);
  if (s_failure == 2) {
    assert(tuya_hci_send(&reset) != OPRT_OK);
    assert(tuya_hci_send(&reset) != OPRT_OK && s_sent == 1);
    assert(!s_locked);
    return 0;
  }
  assert(tuya_hci_send(&reset) == OPRT_OK && s_sent == 1);
  assert(s_event(event, sizeof(event)) == OPRT_OK);
  event[5] = 99; /* Callback memory belongs to IPC and can change immediately. */
  assert(tuya_hci_receive(&rx, 0) == OPRT_OK && rx.data[5] == 0);
  assert(rx.type == TUYA_HCI_EVENT && rx.size == 6);
  uint8_t acl[] = {1, 0, 1, 0, 42};
  assert(s_acl(acl, sizeof(acl)) == OPRT_OK);
  assert(tuya_hci_receive(&rx, 0) == OPRT_OK && rx.data[4] == 42);
  assert(tuya_hci_send(&rx) == OPRT_OK && s_sent == 2);
  if (argv[1] && !strcmp(argv[1], "malformed")) {
    assert(s_event(event, 1) != OPRT_OK);
  } else {
    for (unsigned i = 0; i < 8; ++i) {
      assert(s_event(event, sizeof(event)) == OPRT_OK);
    }
    assert(s_event(event, sizeof(event)) != OPRT_OK);
  }
  assert(tuya_hci_receive(&rx, 0) != OPRT_OK);
  assert(tuya_hci_send(&reset) != OPRT_OK && s_sent == 2);
  puts("HCI ownership, framing and sticky fault tests passed");
  return 0;
}
