/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/bluetooth/bonding_sync.h>
#include <pbl/bluetooth/gap_le_connect.h>
#include <pbl/services/analytics/analytics.h>
#include <pbl/services/bluetooth/bluetooth_persistent_storage.h>
#include <pbl/services/bluetooth/bluetooth_persistent_storage_unittest_impl.h>
#include <pbl/services/event_service.h>
#include <pbl/services/filesystem/pfs.h>
#include <pbl/services/settings/settings_file.h>

#include <clar.h>
#include <flash_region/flash_region.h>

// Stubs
////////////////////////////////////

typedef struct GAPLEConnection GAPLEConnection;

#include <pbl/drivers/flash.h>
#include <pbl/services/shared_prf_storage/shared_prf_storage.h>

#include <fake_bonding_sync.h>
#include <fake_events.h>
#include <fake_new_timer.h>
#include <fake_pbl_malloc.h>
#include <fake_rtc.h>
#include <fake_spi_flash.h>
#include <fake_system_task.h>

static int s_ble_bonding_change_add_count;
static int s_ble_bonding_change_update_count;
static int s_ble_bonding_change_delete_count;

typedef bool (*BondingSyncFilterCb)(const struct pbl_bt_bonding *bonding, void *ctx);
const struct pbl_bt_bonding *bonding_sync_find(BondingSyncFilterCb cb, void *ctx) {
  return nullptr;
}

void pbl_bt_pps_handle_status_change(const GAPLEConnection *connection) {
}

bool bt_ctl_is_bluetooth_running(void) {
  return true;
}

void pbl_bt_handle_le_conn_params_update_event(
    const struct pbl_bt_conn_update_complete_event *event) {
}

struct pbl_bt_pairing_confirm_ctx;

void pbl_bt_cb_pairing_confirm_handle_request(const struct pbl_bt_pairing_confirm_ctx *ctx,
                                              const char *device_name,
                                              const char *confirmation_token) {
}

void pbl_bt_cb_pairing_confirm_handle_completed(const struct pbl_bt_pairing_confirm_ctx *ctx,
                                                bool success) {
}

void gap_le_connect_handle_bonding_change(pbl_bt_bonding_id_t bonding_id, BtPersistBondingOp op) {
}

void gap_le_connection_handle_bonding_change(pbl_bt_bonding_id_t bonding, BtPersistBondingOp op) {
}

void gap_le_device_name_request(uintptr_t stack_id, GAPLEConnection *connection) {
}

void bt_pairability_update_due_to_bonding_change(void) {
}

void bt_local_addr_handle_bonding_change(pbl_bt_bonding_id_t bonding, BtPersistBondingOp op) {
}

void kernel_le_client_handle_bonding_change(pbl_bt_bonding_id_t bonding, BtPersistBondingOp op) {
  if (op == BtPersistBondingOpDidAdd) {
    s_ble_bonding_change_add_count++;
  } else if (op == BtPersistBondingOpDidChange) {
    s_ble_bonding_change_update_count++;
  } else if (op == BtPersistBondingOpWillDelete) {
    s_ble_bonding_change_delete_count++;
  }
  return;
}

uint16_t gaps_get_starting_att_handle(void) {
  return 4;
}

void gatt_service_changed_server_cleanup_by_connection(GAPLEConnection *connection) {
}

void launcher_task_add_callback(void (*callback)(void *data), void *data) {
  callback(data);
}

bool launcher_task_is_current_task(void) {
  return true;
}

static struct pbl_bt_cccd s_registered_cccd;
static unsigned s_cccd_count;

void pbl_bt_handle_host_added_cccd(const struct pbl_bt_cccd *cccd) {
  s_registered_cccd = *cccd;
  ++s_cccd_count;
}

void pbl_bt_handle_host_removed_cccd(const struct pbl_bt_cccd *cccd) {
}

void sys_pbl_analytics_set_unsigned(enum pbl_analytics_key key, uint32_t unsigned_value) {
}

// Migration uses the real shared-PRF, settings and PFS code over fake flash.
static struct pbl_bt_sm_pairing_info s_pairing;
static struct pbl_bt_sm_key s_root_keys[PBL_BT_SM_ROOT_KEY_TYPE_NUM];
static struct pbl_bt_addr s_pinned = {.octets = {1, 2, 3, 4, 5, 6}};

void test_bluetooth_persistent_storage_migration__initialize(void) {
  bonding_sync_init();
  fake_spi_flash_init(0, 0x1000000);
  pfs_init(false);
  shared_prf_storage_init();
  s_cccd_count = 0;
  s_ble_bonding_change_add_count = 0;
  s_ble_bonding_change_update_count = 0;
  s_ble_bonding_change_delete_count = 0;
  s_pairing = (struct pbl_bt_sm_pairing_info){
    .identity = {.address = {.octets = {6, 5, 4, 3, 2, 1}}, .is_random_address = true},
    .irk = {.data = {0x12, 0x34}},
    .local_encryption_info = {.ediv = 123, .rand = 456, .ltk = {.data = {0x56, 0x78}}},
    .remote_encryption_info = {.ediv = 789, .rand = 321, .ltk = {.data = {0x9a, 0xbc}}},
    .is_remote_identity_info_valid = true,
    .is_local_encryption_info_valid = true,
    .is_remote_encryption_info_valid = true,
  };
  memset(s_root_keys, 0x35, sizeof(s_root_keys));
  shared_prf_storage_set_root_keys(s_root_keys);
  shared_prf_storage_set_local_device_name("Migration watch");
  shared_prf_storage_set_ble_pinned_address(&s_pinned);
  shared_prf_storage_store_ble_pairing_data(&s_pairing, "Migration phone", true, 3);
}

void test_bluetooth_persistent_storage_migration__cleanup(void) {
  bonding_sync_deinit();
  fake_spi_flash_cleanup();
}

static pbl_bt_bonding_id_t prv_assert_migrated(void) {
  char name[PBL_BT_DEVICE_NAME_BUFFER_SIZE];
  struct pbl_bt_sm_key irk;
  cl_assert(bt_persistent_storage_get_ble_pairing_by_addr(&s_pairing.identity, &irk, name));
  cl_assert_equal_m(&irk, &s_pairing.irk, sizeof(irk));
  cl_assert_equal_s(name, "Migration phone");
  cl_assert(bt_persistent_storage_get_local_device_name(name, sizeof(name)));
  cl_assert_equal_s(name, "Migration watch");
  struct pbl_bt_addr pinned;
  cl_assert(bt_persistent_storage_get_ble_pinned_address(&pinned));
  cl_assert_equal_m(&pinned, &s_pinned, sizeof(pinned));
  cl_assert(bt_persistent_storage_has_pinned_ble_pairings());
  for (unsigned i = 0; i < PBL_BT_SM_ROOT_KEY_TYPE_NUM; ++i) {
    struct pbl_bt_sm_key key;
    cl_assert(bt_persistent_storage_get_root_key(i, &key));
    cl_assert_equal_m(&key, &s_root_keys[i], sizeof(key));
  }
  bt_persistent_storage_register_existing_ble_bondings();
  cl_assert(s_ble_bonding_head);
  cl_assert(!s_ble_bonding_head->node.next);
  const struct pbl_bt_bonding *bond = &s_ble_bonding_head->bonding;
  cl_assert(bond->is_gateway);
  cl_assert_equal_i(bond->flags, 3);
  cl_assert(sm_is_pairing_info_equal_identity(&s_pairing, &bond->pairing_info));
  cl_assert_equal_m(&bond->pairing_info.local_encryption_info, &s_pairing.local_encryption_info,
                    sizeof(s_pairing.local_encryption_info));
  cl_assert_equal_m(&bond->pairing_info.remote_encryption_info, &s_pairing.remote_encryption_info,
                    sizeof(s_pairing.remote_encryption_info));
  cl_assert(bond->pairing_info.is_local_encryption_info_valid);
  cl_assert(bond->pairing_info.is_remote_encryption_info_valid);
  cl_assert(bond->pairing_info.is_remote_identity_info_valid);
  pbl_bt_bonding_id_t id = bt_persistent_storage_get_ble_ancs_bonding();
  cl_assert(id != PBL_BT_BONDING_ID_INVALID);
  return id;
}

static void prv_reload(void) {
  bonding_sync_deinit();
  bonding_sync_init();
  pfs_init(false);
  shared_prf_storage_init();
  bt_persistent_storage_init();
}

void test_bluetooth_persistent_storage_migration__import_and_repeat(void) {
  bt_persistent_storage_init();
  pbl_bt_bonding_id_t first = prv_assert_migrated();
  prv_reload();
  cl_assert_equal_i(first, prv_assert_migrated());
  cl_assert_equal_i(s_ble_bonding_change_add_count, 1);
  cl_assert_equal_i(s_ble_bonding_change_delete_count, 0);
}

void test_bluetooth_persistent_storage_migration__settings_and_cccd_restore(void) {
  bt_persistent_storage_init();
  pbl_bt_bonding_id_t id = prv_assert_migrated();
  struct pbl_bt_cccd cccd = {
    .peer = s_pairing.identity,
    .chr_val_handle = 42,
    .flags = 2,
    .value_changed = true,
  };
  cl_assert(bt_persistent_storage_store_cccd(&cccd) != PBL_BT_CCCD_ID_INVALID);
  bt_persistent_storage_set_active_gateway(id);
  bt_persistent_storage_set_airplane_mode_enabled(true);
  bt_persistent_storage_set_unfaithful(false);
  PebbleProtocolCapabilities caps = {.flags = 0x1234};
  fake_event_reset_count();
  bt_persistent_storage_set_cached_system_capabilities(&caps);
  cl_assert_equal_i(fake_event_get_last().type, PEBBLE_CAPABILITIES_CHANGED_EVENT);
  cl_assert_equal_i(fake_event_get_count(), 1);
  bt_persistent_storage_set_cached_system_capabilities(&caps);
  cl_assert_equal_i(fake_event_get_count(), 1);
  prv_reload();
  cl_assert_equal_i(id, prv_assert_migrated());
  pbl_bt_bonding_id_t active;
  BtPersistBondingType type;
  cl_assert(bt_persistent_storage_get_active_gateway(&active, &type));
  cl_assert_equal_i(active, id);
  cl_assert_equal_i(type, BtPersistBondingTypeBLE);
  cl_assert_equal_i(s_cccd_count, 1);
  cl_assert_equal_m(&s_registered_cccd.peer, &cccd.peer, sizeof(cccd.peer));
  cl_assert_equal_i(s_registered_cccd.chr_val_handle, cccd.chr_val_handle);
  cl_assert_equal_i(s_registered_cccd.flags, cccd.flags);
  cl_assert_equal_i(s_registered_cccd.value_changed, cccd.value_changed);
  cl_assert(bt_persistent_storage_get_airplane_mode_enabled());
  cl_assert(!bt_persistent_storage_is_unfaithful());
  PebbleProtocolCapabilities restored;
  bt_persistent_storage_get_cached_system_capabilities(&restored);
  cl_assert_equal_m(&restored, &caps, sizeof(caps));
  // Reverting to PRF firmware can still load the synchronized bond and keys.
  struct pbl_bt_sm_pairing_info prf;
  cl_assert(shared_prf_storage_get_ble_pairing_data(&prf, nullptr, nullptr, nullptr));
  cl_assert(sm_is_pairing_info_equal_identity(&prf, &s_pairing));
}

void test_bluetooth_persistent_storage_migration__shared_record_survives_import(void) {
  const size_t size = FLASH_REGION_SHARED_PRF_STORAGE_END - FLASH_REGION_SHARED_PRF_STORAGE_BEGIN;
  uint8_t *before = kernel_malloc_check(size);
  uint8_t *after = kernel_malloc_check(size);
  flash_read_bytes(before, FLASH_REGION_SHARED_PRF_STORAGE_BEGIN, size);
  bt_persistent_storage_init();
  prv_assert_migrated();
  flash_read_bytes(after, FLASH_REGION_SHARED_PRF_STORAGE_BEGIN, size);
  cl_assert_equal_m(before, after, size);
  kernel_free(before);
  kernel_free(after);
}

void test_bluetooth_persistent_storage_migration__new_prf_bond_replaces_stale_normal_bond(void) {
  bt_persistent_storage_init();
  prv_assert_migrated();
  bonding_sync_deinit();
  bonding_sync_init();
  // A phone paired while PRF firmware was installed must win on the next normal boot.
  s_pairing.identity.address.octets[0] ^= 0xff;
  s_pairing.irk.data[0] ^= 0xff;
  shared_prf_storage_store_ble_pairing_data(&s_pairing, "Migration phone", true, 3);
  prv_reload();
  prv_assert_migrated();
  cl_assert_equal_i(s_ble_bonding_change_add_count, 2);
  cl_assert_equal_i(s_ble_bonding_change_delete_count, 1);
}

void test_bluetooth_persistent_storage_migration__normal_bond_repopulates_missing_prf_record(void) {
  bt_persistent_storage_init();
  pbl_bt_bonding_id_t id = prv_assert_migrated();
  // Model forgetting the phone after rolling back to the PRF backend.
  shared_prf_storage_erase_ble_pairing_data();
  cl_assert(!shared_prf_storage_get_ble_pairing_data(nullptr, nullptr, nullptr, nullptr));
  prv_reload();
  // Existing normal initialization treats an absent PRF bond as a reason to export its own.
  cl_assert_equal_i(id, prv_assert_migrated());
  struct pbl_bt_sm_pairing_info restored;
  cl_assert(shared_prf_storage_get_ble_pairing_data(&restored, nullptr, nullptr, nullptr));
  cl_assert(sm_is_pairing_info_equal_identity(&restored, &s_pairing));
}
