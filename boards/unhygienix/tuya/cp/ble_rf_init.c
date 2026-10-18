/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>

// Packed RF configuration used by the vendor `rfconfig ble_btpll` command.
#define UNHYGIENIX_BLE_BT_PLL_RF_CONFIG 0x0202u

extern int bk_bluetooth_init(void);
extern unsigned rwnx_cal_set_rfconfig(unsigned config);
extern uint16_t rwnx_rfconfig;
extern void bk_printf(const char *format, ...);

// Unhygienix CP adapter: apply the vendor's physical BLE/BT-PLL configuration
// after Bluetooth initialization. PHY startup overwrites a data initializer,
// and changing only get_rf_mode did not prevent supervision loss on hardware.
// This runs in the same thread context as tkl_hci_reset, not a radio ISR.
int pebble_ble_init_bt_pll(void) {
  const int result = bk_bluetooth_init();
  if (result != 0) {
    return result;
  }
  // Identical to the vendor `rfconfig ble_btpll` command that recovered the
  // stuck radio without resetting the watch. Its return is the RF mode, not
  // a bk_err_t; report the effective configuration instead.
  (void)rwnx_cal_set_rfconfig(UNHYGIENIX_BLE_BT_PLL_RF_CONFIG);
  bk_printf("Pebble BLE RF PLL: config=0x%04x\r\n", (unsigned)rwnx_rfconfig);
  return result;
}
