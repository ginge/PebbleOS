/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pbl/bluetooth/gatt_service_types.h>
#include <pbl/util/list.h>
#include <pbl/util/uuid.h>

enum pbl_bt_errno {
  PBL_BT_ERRNO_OK,
  PBL_BT_ERRNO_INVALID_STATE,
  PBL_BT_ERRNO_SERVICE_DISCOVERY_TIMEOUT,
  PBL_BT_ERRNO_SERVICE_DISCOVERY_DISCONNECTED,
  PBL_BT_ERRNO_INTERNAL_ERROR_BEGIN = 100
};
struct pbl_bt_device_internal {
  unsigned id;
};
typedef struct GAPLEConnection {
  struct pbl_bt_device_internal device;
} GAPLEConnection;
typedef struct {
  uint16_t value;
} ble_uuid_t;
typedef union {
  ble_uuid_t u;
} ble_uuid_any_t;
#define BLE_UUID16_DECLARE(v)                   (&(ble_uuid_t){(v)})
#define BLE_UUID_STR_LEN                        37
#define BLE_HS_EDONE                            14
#define BLE_HS_ETIMEOUT                         13
#define BLE_HS_ENOTCONN                         7
#define BLE_GATT_SVC_UUID16                     0x1801
#define BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16 0x2a05
#define BLE_GATT_DSC_CLT_CFG_UUID16             0x2902
#define BLE_GATT_CCCD_INDICATE                  2
struct ble_gatt_error {
  int status;
};
struct ble_gatt_svc {
  uint16_t start_handle, end_handle;
  ble_uuid_any_t uuid;
};
struct ble_gatt_chr {
  uint16_t def_handle, val_handle;
  uint8_t properties;
  ble_uuid_any_t uuid;
};
struct ble_gatt_dsc {
  uint16_t handle;
  ble_uuid_any_t uuid;
};
struct ble_gatt_attr {
  uint16_t handle;
};
typedef int (*SvcFn)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_svc *, void *);
typedef int (*ChrFn)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_chr *, void *);
typedef int (*DscFn)(uint16_t, const struct ble_gatt_error *, uint16_t, const struct ble_gatt_dsc *,
                     void *);
typedef int (*WriteFn)(uint16_t, const struct ble_gatt_error *, struct ble_gatt_attr *, void *);
int ble_gattc_disc_all_svcs(uint16_t, SvcFn, void *);
int ble_gattc_disc_all_chrs(uint16_t, uint16_t, uint16_t, ChrFn, void *);
int ble_gattc_disc_all_dscs(uint16_t, uint16_t, uint16_t, DscFn, void *);
int ble_gattc_write_flat(uint16_t, uint16_t, const void *, uint16_t, WriteFn, void *);
int ble_uuid_cmp(const ble_uuid_t *, const ble_uuid_t *);
void ble_uuid_to_str(const ble_uuid_t *, char *);
void nimble_uuid_to_pebble(const ble_uuid_any_t *, Uuid *);
void *kernel_zalloc_check(size_t);
void kernel_free(void *);
void bt_lock(void);
void bt_unlock(void);
bool gap_le_connection_is_valid(const GAPLEConnection *);
bool pebble_device_to_nimble_conn_handle(const struct pbl_bt_device_internal *, uint16_t *);
void pbl_bt_cb_gatt_client_discovery_handle_indication(GAPLEConnection *,
                                                       struct pbl_bt_gatt_service *,
                                                       enum pbl_bt_errno);
bool pbl_bt_cb_gatt_client_discovery_complete(GAPLEConnection *, enum pbl_bt_errno);
void pbl_bt_cb_gatt_client_discovery_handle_service_changed(GAPLEConnection *, uint16_t);
void nimble_gattc_op_queue_push(int (*)(void *), void *);
void nimble_gattc_op_queue_complete(void);
struct pbl_sem {
  unsigned count;
};
#define PBL_SEM_DEFINE(n, a, b) struct pbl_sem n = {(a)}
#define PBL_FOREVER             -1
#define PBL_TICK_FOREVER        -1
#define PBL_TICKS(n)            (n)
int pbl_sem_give(struct pbl_sem *);
int pbl_sem_take(struct pbl_sem *, int);
#define PBL_BT_CONSUMER_LE_SERVICE_DISCOVERY 1
#define PBL_BT_RESPONSE_TIME_MIN             2
void conn_mgr_set_ble_conn_response_time(GAPLEConnection *, unsigned, unsigned, unsigned);
