/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/bluetooth/bluetooth_persistent_storage.h"

#include <bluetooth/bonding_sync.h>
#include <bluetooth/id.h>
#include <pbl/btutil/bt_device.h>
#include <pbl/drivers/rng.h>
#include <system/passert.h>
#include <string.h>

#define T5AI_VOLATILE_BONDING_ID ((BTBondingID)0)
#define T5AI_VOLATILE_CCCD_ID ((BTCCCDID)0x80u)

typedef struct {
  bool valid;
  bool is_gateway;
  bool requires_address_pinning;
  uint8_t flags;
  SMPairingInfo pairing;
  char name[BT_DEVICE_NAME_BUFFER_SIZE];
} T5AIVolatileBonding;

static T5AIVolatileBonding s_bonding;
static bool s_pinned_address_valid;
static BTDeviceAddress s_pinned_address;
static bool s_root_keys_valid;
static SM128BitKey s_root_keys[SMRootKeyTypeNum];
static char s_local_device_name[BT_DEVICE_NAME_BUFFER_SIZE];
static bool s_airplane_mode_enabled;
static bool s_unfaithful = true;
static PebbleProtocolCapabilities s_capabilities;

bool bt_persistent_storage_has_pinned_ble_pairings(void) {
  return s_bonding.valid && s_bonding.requires_address_pinning;
}

bool bt_persistent_storage_set_ble_pinned_address(const BTDeviceAddress *address) {
  s_pinned_address_valid = address != NULL;
  if (address != NULL) {
    s_pinned_address = *address;
  }
  return true;
}

bool bt_persistent_storage_get_ble_pinned_address(BTDeviceAddress *address_out) {
  if (!s_pinned_address_valid) {
    return false;
  }
  if (address_out != NULL) {
    *address_out = s_pinned_address;
  }
  return true;
}

BTBondingID bt_persistent_storage_store_ble_pairing(const SMPairingInfo *pairing_info,
                                                    bool is_gateway, const char *device_name,
                                                    bool requires_address_pinning, uint8_t flags) {
  if (pairing_info == NULL) {
    return BT_BONDING_ID_INVALID;
  }
  s_bonding = (T5AIVolatileBonding){
      .valid = true,
      .is_gateway = is_gateway,
      .requires_address_pinning = requires_address_pinning,
      .flags = flags,
      .pairing = *pairing_info,
  };
  if (device_name != NULL) {
    strncpy(s_bonding.name, device_name, sizeof(s_bonding.name) - 1u);
  }
  s_unfaithful = true;
  return T5AI_VOLATILE_BONDING_ID;
}

bool bt_persistent_storage_update_ble_device_name(BTBondingID bonding, const char *device_name) {
  if (!s_bonding.valid || bonding != T5AI_VOLATILE_BONDING_ID || device_name == NULL) {
    return false;
  }
  strncpy(s_bonding.name, device_name, sizeof(s_bonding.name) - 1u);
  s_bonding.name[sizeof(s_bonding.name) - 1u] = '\0';
  return true;
}

void bt_persistent_storage_delete_ble_pairing_by_id(BTBondingID bonding) {
  if (bonding == T5AI_VOLATILE_BONDING_ID) {
    memset(&s_bonding, 0, sizeof(s_bonding));
  }
}

void bt_persistent_storage_delete_ble_pairing_by_addr(const BTDeviceInternal *device) {
  if (device != NULL && s_bonding.valid &&
      bt_device_equal(&device->opaque, &s_bonding.pairing.identity.opaque)) {
    memset(&s_bonding, 0, sizeof(s_bonding));
  }
}

bool bt_persistent_storage_get_ble_pairing_by_id(BTBondingID bonding,
                                                 SMIdentityResolvingKey *irk_out,
                                                 BTDeviceInternal *device_out, char *name_out) {
  if (!s_bonding.valid || bonding != T5AI_VOLATILE_BONDING_ID) {
    return false;
  }
  if (irk_out != NULL) {
    *irk_out = s_bonding.pairing.irk;
  }
  if (device_out != NULL) {
    *device_out = s_bonding.pairing.identity;
  }
  if (name_out != NULL) {
    strncpy(name_out, s_bonding.name, BT_DEVICE_NAME_BUFFER_SIZE);
    name_out[BT_DEVICE_NAME_BUFFER_SIZE - 1u] = '\0';
  }
  return true;
}

bool bt_persistent_storage_get_ble_pairing_by_addr(const BTDeviceInternal *device,
                                                   SMIdentityResolvingKey *irk_out,
                                                   char name_out[BT_DEVICE_NAME_BUFFER_SIZE]) {
  return device != NULL && s_bonding.valid &&
         bt_device_equal(&device->opaque, &s_bonding.pairing.identity.opaque) &&
         bt_persistent_storage_get_ble_pairing_by_id(T5AI_VOLATILE_BONDING_ID, irk_out, NULL,
                                                     name_out);
}

BTBondingID bt_persistent_storage_get_ble_ancs_bonding(void) {
  return s_bonding.valid && s_bonding.is_gateway ? T5AI_VOLATILE_BONDING_ID : BT_BONDING_ID_INVALID;
}

bool bt_persistent_storage_is_ble_ancs_bonding(BTBondingID bonding) {
  return bonding == bt_persistent_storage_get_ble_ancs_bonding();
}

bool bt_persistent_storage_has_ble_ancs_bonding(void) {
  return bt_persistent_storage_get_ble_ancs_bonding() != BT_BONDING_ID_INVALID;
}

bool bt_persistent_storage_has_active_ble_gateway_bonding(void) {
  return s_bonding.valid && s_bonding.is_gateway;
}

void bt_persistent_storage_for_each_ble_pairing(BtPersistBondingDBEachBLE callback, void *context) {
  if (callback != NULL && s_bonding.valid) {
    callback(&s_bonding.pairing.identity, &s_bonding.pairing.irk, s_bonding.name,
             &(BTBondingID){T5AI_VOLATILE_BONDING_ID}, context);
  }
}

void bt_persistent_storage_register_existing_ble_bondings(void) {
  if (!s_bonding.valid) {
    return;
  }
  BleBonding bonding = {
      .pairing_info = s_bonding.pairing,
      .is_gateway = s_bonding.is_gateway,
      .flags = s_bonding.flags,
  };
  bt_driver_handle_host_added_bonding(&bonding);
}

BTCCCDID bt_persistent_storage_store_cccd(const BleCCCD *cccd) {
  (void)cccd;
  return T5AI_VOLATILE_CCCD_ID;
}

bool bt_persistent_storage_delete_cccd(const BTDeviceInternal *peer, uint16_t chr_val_handle) {
  (void)peer;
  (void)chr_val_handle;
  return true;
}

void bt_persistent_storage_set_active_gateway(BTBondingID bonding) {
  (void)bonding;
}

bool bt_persistent_storage_get_active_gateway(BTBondingID *bonding_out,
                                              BtPersistBondingType *type_out) {
  if (!s_bonding.valid || !s_bonding.is_gateway) {
    return false;
  }
  if (bonding_out != NULL) {
    *bonding_out = T5AI_VOLATILE_BONDING_ID;
  }
  if (type_out != NULL) {
    *type_out = BtPersistBondingTypeBLE;
  }
  return true;
}

bool bt_persistent_storage_is_unfaithful(void) {
  return s_unfaithful;
}

void bt_persistent_storage_set_unfaithful(bool is_unfaithful) {
  s_unfaithful = is_unfaithful;
}

bool bt_persistent_storage_get_root_key(SMRootKeyType key_type, SM128BitKey *key_out) {
  if (!s_root_keys_valid || key_type >= SMRootKeyTypeNum) {
    return false;
  }
  if (key_out != NULL) {
    *key_out = s_root_keys[key_type];
  }
  return true;
}

void bt_persistent_storage_set_root_keys(SM128BitKey *keys_in) {
  if (keys_in != NULL) {
    memcpy(s_root_keys, keys_in, sizeof(s_root_keys));
    s_root_keys_valid = true;
  }
}

bool bt_persistent_storage_get_local_device_name(char *name_out, size_t max_size) {
  if (s_local_device_name[0] == '\0' || name_out == NULL || max_size == 0) {
    return false;
  }
  strncpy(name_out, s_local_device_name, max_size);
  name_out[max_size - 1u] = '\0';
  return true;
}

void bt_persistent_storage_set_local_device_name(char *name, size_t size) {
  if (name == NULL || size == 0) {
    return;
  }
  const size_t length =
      size < sizeof(s_local_device_name) ? size : sizeof(s_local_device_name) - 1u;
  memcpy(s_local_device_name, name, length);
  s_local_device_name[length] = '\0';
}

bool bt_persistent_storage_get_airplane_mode_enabled(void) {
  return s_airplane_mode_enabled;
}

void bt_persistent_storage_set_airplane_mode_enabled(bool enabled) {
  s_airplane_mode_enabled = enabled;
}

void bt_persistent_storage_get_cached_system_capabilities(
    PebbleProtocolCapabilities *capabilities_out) {
  if (capabilities_out != NULL) {
    *capabilities_out = s_capabilities;
  }
}

void bt_persistent_storage_set_cached_system_capabilities(
    const PebbleProtocolCapabilities *capabilities) {
  s_capabilities = capabilities == NULL ? (PebbleProtocolCapabilities){} : *capabilities;
}

void bt_persistent_storage_init(void) {
  memset(&s_bonding, 0, sizeof(s_bonding));
  s_pinned_address_valid = false;
  /* NimBLE requests the identity root key while synchronizing the host. If no
   * key exists, its default generator issues an HCI Random command from inside
   * that synchronization sequence. The T5AI CP transport cannot service that
   * re-entrant command, so seed both per-boot keys from the AP-local TRNG. */
  for (size_t offset = 0; offset < sizeof(s_root_keys); offset += sizeof(uint32_t)) {
    uint32_t random_word;
    PBL_ASSERTN(rng_rand(&random_word));
    memcpy((uint8_t *)s_root_keys + offset, &random_word, sizeof(random_word));
  }
  s_root_keys_valid = true;
  s_local_device_name[0] = '\0';
  s_airplane_mode_enabled = false;
  s_unfaithful = true;
  s_capabilities = (PebbleProtocolCapabilities){};
}

void bt_persistent_storage_delete_all_pairings(void) {
  memset(&s_bonding, 0, sizeof(s_bonding));
}

int bt_persistent_storage_get_raw_data(const void *key, size_t key_len, void *data_out,
                                       size_t buf_len) {
  (void)key;
  (void)key_len;
  (void)data_out;
  (void)buf_len;
  return 0;
}
