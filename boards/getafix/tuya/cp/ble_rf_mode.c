// SPDX-License-Identifier: Apache-2.0
// DVT3 board override for the pinned vendor CP Bluetooth RF-mode callback.
// Mode 0 selects the dedicated Bluetooth PLL. The vendor callback derives
// this from the previous global Wi-Fi RF configuration, selecting mode 1
// (shared Wi-Fi PLL) on this watch. RF reconfiguration to ble_btpll recovered
// a stuck controller without reset; this makes BLE select that path at every
// event rather than relying on a startup initializer that PHY overwrites.
unsigned get_rf_mode(void) {
  return 0;
}
