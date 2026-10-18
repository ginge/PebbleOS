/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "tuya_cloud_types.h"
typedef OPERATE_RET (*TKL_HCI_FUNC_CB)(uint8_t *, uint16_t);
OPERATE_RET tkl_hci_callback_register(TKL_HCI_FUNC_CB, TKL_HCI_FUNC_CB);
OPERATE_RET tkl_hci_init(void);
OPERATE_RET tkl_hci_cmd_packet_send(const uint8_t *, uint16_t);
OPERATE_RET tkl_hci_acl_packet_send(const uint8_t *, uint16_t);
