# Unhygienix Bluetooth RF PLL initialization hook

`ble_rf_init.c` calls the vendor RF configurator with `0x0202` immediately after
successful Bluetooth initialization in the CP HCI-reset thread. The hook calls
the original initializer first and leaves RF unchanged if initialization fails.

`tools/patch_unhygienix_cp_rf.py` verifies the complete pinned stock image hash
and reset function, compiles the hook into unused CP partition space and redirects
only the existing Bluetooth-init call. Existing addresses and the AP mailbox/HCI
ABI stay unchanged. Unknown images and oversized hooks are rejected.
This is source-hook integration into a pinned binary, not a full CP rebuild.

Run from the repository root, with the stock image supplied under `build/inputs/`:

```sh
./pbl configure --board unhygienix
./pbl waf t5ai_cp --t5ai-cp-stock build/inputs/stock-cp.bin
./pbl flash --dry-run --t5ai-cp --tyutool tyutool_cli --tyutool-tty /dev/serial/by-id/programming-uart
```

The native runner updates AP and explicitly selected CP, preserving PFS.
Remove `--dry-run` to program. For CP-only rollback, regenerate a protected stock
payload and use `tools/flash_unhygienix_cp_firmware.py`; it validates by default
and writes only with `--flash`. AP, resources, PFS and bonds remain outside its
write range. Wi-Fi coexistence requires separate validation of this radio policy.

See the [tooling guide](../../../../docs/boards/unhygienix_tooling.md) for pinned
inputs and provisioning, and the [operator handoff](../../../../docs/boards/unhygienix.md)
for hardware qualification requirements.
