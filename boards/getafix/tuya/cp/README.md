# DVT3 Bluetooth RF PLL initialization candidate

`ble_rf_init.c` calls the vendor physical RF configurator with `0x0202`
(`rfconfig ble_btpll`) immediately after a successful Bluetooth initialization.
This runs in the existing CP HCI-reset thread context and reports the effective
configuration on UART. The live command recovered a stuck radio and restored
bonded PPoG without a watch reset. Startup data-only and RF-mode callback-only
candidates were tested and rejected: PHY overwrites the initializer, and the
callback alone still failed at seven minutes. They are not stability fixes.

`tools/patch_t5ai_cp_rf.py` builds this source hook and integrates it into the
exact pinned vendor CP image. It verifies the complete input SHA-256 and
original reset function, appends the hook in unused space inside the CP
partition, and redirects only the existing Bluetooth-init BL instruction.
All existing code/data addresses and the AP ABI remain stock. The hook calls
the original initializer before configuring RF; initialization failure returns
without changing RF. Unknown CP images and oversized hooks are rejected.
This is a source-backed binary integration, not a full vendor CP rebuild.

Build with the ARM compiler on PATH:

```
python3 tools/patch_t5ai_cp_rf.py --stock /path/to/pinned/app.bin --out /tmp/cp-ble-pll/app.bin
python3 tools/flash_t5ai_cp_firmware.py --raw /tmp/cp-ble-pll/app.bin --protected /tmp/cp-ble-pll/app.crc.bin --tyutool /path/to/tyutool_cli --port /dev/serial/by-id/programming-uart
```

The second command only validates until `--flash` is added. It restricts writes
to CP, preserving AP, resources, PFS and bonds. Rollback uses the same CP-only
flasher with the stock image and regenerated stock CRC payload. Do not use
this DVT3 BLE policy in a Wi-Fi product without coexistence validation.

Hardware validation on 2026-10-03: UART confirmed effective configuration
`0x0202`; bonded encrypted PPoG remained connected for 15 minutes without
observed disconnect, assertion or uncommanded AP reset. A commanded hardware
reset reapplied the configuration and restored the bonded session. The operator
subsequently reported roughly 10 hours of stable connected operation with no
disconnects after this candidate was installed. This was not a verified
power-off cold boot. Final-image scrolling, 25 disconnect/reconnect cycles and
true cold boot remain pending. Preserve the first failed trace; the long
connected-idle observation strengthens the fix candidate but does not by itself
complete release qualification.

See the [Bluetooth, power and scrolling checkpoint](../../../../docs/boards/getafix_dvt3_bt_power_scroll_checkpoint.md)
for the failed/control RF evidence, installed image hashes, power policy,
scrolling fixes and outstanding validation.
