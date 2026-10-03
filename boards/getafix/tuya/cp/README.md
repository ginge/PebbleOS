# DVT3 Bluetooth RF PLL candidate

The source policy is `ble_rf_mode.c`: the CP Bluetooth adapter selects mode 0,
its dedicated Bluetooth PLL, for every controller event. Stock mode selection
reads the previous global RF configuration and selects the shared Wi-Fi PLL
when it is 0x101. A stuck-radio trace showed no watch advertisements although
CP event counters advanced; the vendor `rfconfig ble_btpll` command restored
phone encryption and PPoG without resetting either CPU. This supports testing
the Bluetooth PLL path; it does not yet prove the precise PLL failure or
long-term stability.

The vendor CP is pinned and its RF library is supplied as an archive. The
production integration is `tools/patch_t5ai_cp_rf.py`, which verifies the whole
stock image SHA256 and the complete original callback signature, then installs
the compiled four-byte leaf function at the same address. All other code and
data, CP SRAM layout and AP interfaces remain identical. Unknown CP images
are rejected. This is an explicit binary integration of the source policy,
not a full CP source rebuild. Do not enable this override for a Wi-Fi product
without separate coexistence validation.

Build from the unmodified pinned CP image:

```
python3 tools/patch_t5ai_cp_rf.py --stock /path/to/pinned/app.bin --out /tmp/cp-ble-pll/app.bin
python3 tools/flash_t5ai_cp_firmware.py --raw /tmp/cp-ble-pll/app.bin --protected /tmp/cp-ble-pll/app.crc.bin --tyutool /path/to/tyutool_cli --port /dev/serial/by-id/programming-uart
```

The second command only validates until `--flash` is added. It restricts writes
to the CP partition, preserving AP, resources, PFS and bonds. Rollback uses
the same CP-only flasher with the stock CP image and regenerated stock CRC
payload. A data-initializer-only candidate was rejected because PHY startup
overwrites that initializer; do not substitute it for the callback override.

Required hardware validation: bonded reconnect after installation and cold
boot; connected idle longer than prior 6–8 minute failures with UI interaction;
25 disconnect/reconnect cycles; then overnight soak. Preserve the first failed
trace. Neither reconnect success nor a short soak establishes complete stability.
