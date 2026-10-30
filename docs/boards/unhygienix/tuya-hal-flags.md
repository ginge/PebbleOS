# Unhygienix Tuya HAL / CP flag audit — 2026-10-30

## Finding

The native PebbleOS AP needs **none of the Tuya application/HAL feature flags**.
It uses native board/SoC/drivers and NimBLE. The current CP is a separately
supplied, hash-checked vendor executable; changing a Tuya config file does not
change that executable. See [Wi-Fi investigation](wifi-power.md).

For a future source-built CP, preserve BLE controller/HCI, CP RTOS/startup,
mailbox/heartbeat, power/clock/RF/calibration, flash coordination and SARADC.
There is no Pebble requirement for CP Wi-Fi/VNET/IP networking, HTTP OTA,
vendor test commands, vendor UI or a second Bluetooth host.

The effective stock CP config has **526 entries: 198 enabled booleans, 88
nonboolean assignments and 240 explicitly disabled booleans**. This audit
identifies **94 enabled booleans as removal candidates** (59 networking,
26 diagnostics, four optional features, five reference UI), **18 directly
required service/build selectors**, and **86 enabled booleans to preserve or
resolve**. This is a dependency audit, **not a proven 18-flag minimal firmware**.
The 18 selectors need the hardware/ABI/support baseline below. Some candidates
are inert in this image, so these counts are not code-size or energy estimates.

No firmware/configuration changes, rebuild, flashing or hardware test were
performed. Removing flags is a subsequent CP implementation task.

## Scope and evidence

Production HEAD: `4d9976caa8b8cbc8f3cbfbc14c79c92b8649f86f`.
Pinned T5AI: `13379b63e07e78770fb4d0bffe36db2754658132`.
The optional TuyaOpen pin remains
`e82840dc9b629cb77fa922fb04e3f820dc2e78b4`.

The effective config is the Wi-Fi audit's archived
`artifacts/wifi-optional-20261030/vendor-evidence/sdkconfig` in the reference
workspace, SHA-256
`86145c0fcfd0b9b753556b789820a026cbf394934c2f941974fc5f104050e45a`.
That audit verified the accompanying ELF reproduces stock CP hash
`0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856`.
This audit uses that provenance, traces the pinned source and independently
checks retained symbols in that ELF. It does not re-establish the installed
watch identity or prove a modified CP links.

Full per-symbol inventory, values, classification, Kconfig definitions and up
to five lexical source references per symbol:
[/home/baz/Documents/ChatGPT/pebble/artifacts/hal-flags-audit-20261030/cp-flags.csv](/home/baz/Documents/ChatGPT/pebble/artifacts/hal-flags-audit-20261030/cp-flags.csv).
The adjacent `generate_inventory.py`, `summary.json`, `retained-symbols.txt`
and `project-fragment-comparison.csv` preserve method and supporting evidence.
Lexical references include comments, conditional branches and library config
headers; they are navigation aids, not proof of live execution. Classification
rules are explicit in the generator; unresolved symbols are not silently
classified as necessary or removable.

Paths beginning `cp/` below are relative to
`third_party/hal_tuya/TuyaOpen-T5AI/t5_os/`; `adapter/` means
`third_party/hal_tuya/TuyaOpen-T5AI/tuyaos/tuyaos_adapter/`.

## Separate the three configuration domains

| Domain | Actual consumer | Audit decision |
| --- | --- | --- |
| Native AP | `soc/t5ai/CMakeLists.txt`, `drivers/t5ai/CMakeLists.txt`, native board and `third_party/nimble/transport/t5ai_hci_ipc.c` | Tuya flags are not inputs. Preserve Pebble Kconfig, Gabbro app ABI and companion identity; these are outside this removal list. |
| Stock CP | `boards/unhygienix/firmware.cmake`, `tools/unhygienix_cp.py`, exact supplied binary | No config-only cleanup can alter it. Preserve packaging/hash guards and the current rollback input. |
| Optional Tuya reference app/vendor AP | `boards/unhygienix/tuya/app_default.config`, optional reference CMake, vendor `projects/tuya_app/ap/config/bk7258_ap/config` | Not production PebbleOS. Board-choice/display brightness flags describe that reference build. Vendor AP media/LVGL/audio/network flags do not establish a CP requirement. |

The vendor CP project fragment is not the effective configuration. It inherits
many defaults, and some assignments never appear in the archived config. The
comparison CSV explicitly records these differences; absent means absent,
not `n`. For example, fragment `LWIP_PPP_SUPPORT`, `LWIP_HOOK_IP4_ROUTE_SRC`,
`BK_MODEM` and `TRNG_PM_CB_SUPPORT` do not appear in the archived effective
config. Do not count their removal as a saving in the existing CP.

## Directly required selectors for the current CP contract

All names in this and subsequent tables have the `CONFIG_` prefix.
These are required for this implementation, not claims about every possible
replacement CP. The CSV lists each symbol separately.

| Keep enabled | Concrete dependency |
| --- | --- |
| `BLUETOOTH`, `BLE`, `BTDM_5_2`, `BTDM_CONTROLLER_ONLY`, `BLUETOOTH_SUPPORT_IPC` | `cp/components/bk_bluetooth/CMakeLists.txt` selects the BLE controller-only library, HCI core and IPC implementation; `api/bt_main.c` registers IPC. Native NimBLE supplies the host. `adapter/src/misc/tuya_tkl_ipc.c` dispatches HCI to `tkl_hci_ipc_func`; preserve `tuya_ipc_init` and `adapter/src/driver/tkl_hci_0.c`. |
| `FREERTOS`, `FREERTOS_V10`, `CMSIS` | CP task/queue/timer and architecture implementation: `cp/components/bk_rtos/CMakeLists.txt`, `cp/components/cmsis/CMakeLists.txt`, `cp/components/bk_startup/system_main.c`. The native AP using PebbleOS does not remove the CP's RTOS requirement. |
| `MAILBOX`, `MAILBOX_V2_0`, `MAILBOX_IPC`, `SLAVE_HEART_BEAT` | `cp/middleware/driver/common/driver.c` starts IPC/MB IPC; mailbox CMake selects the matching HAL; `system_main.c` starts heartbeat. Native `drivers/t5ai/cp_mailbox.c`, `mb_ipc.c`, `mipc.c` and HCI transport depend on these channels/ABI. |
| `SYSTEM_CTRL`, `AON_PMU` | Vendor power/clock and always-on PMU support used by CP initialization and Bluetooth power/sleep callbacks. `cp/components/bk_pm/pm.c` and `bk_bluetooth/soc/bk7236/bluetooth.c` implement those callbacks. Keep PM code even though the separate `PM_ENABLE` flag is not its build gate. |
| `FLASH`, `FLASH_MB` | Flash driver and inter-core flash notification/coordination: `driver.c`, `cp/middleware/driver/flash/flash_notify.c`. Native CP mailbox handles flash notifications even with SD PFS. Calibration storage also uses flash. |
| `SARADC`, `SARADC_MB` | `driver.c`, `cp/middleware/driver/saradc/saradc_notify.c` and `saradc_server.c`; native `drivers/t5ai/saradc_ipc.c` uses the service. Preserve the server implementation; its separate `SARADC_SERVER` flag is not visibly a source gate. |

Retained stock ELF symbols include `bk_bluetooth_init`, `tuya_ipc_init`,
`tkl_hci_ipc_func`, `mb_ipc_init`, `bk_pm_mailbox_init`,
`bk_flash_driver_init` and `bk_adc_driver_init`.

## Hardware, ABI and support baseline: retain, not a minimality claim

| Preserve | Why it stays in the first reduced profile |
| --- | --- |
| `SOC_BK7258=y`, `SOC_BK7236XX=y`, `ARCH_CM33=y`, `CPU_INDEX=0`, `CPU_CNT=3`, `TZ=y`, `SPE=1`, ITCM/DTCM sizes | Actual vendor CP is CPU0; native AP runs the other application core. BK7236XX is the selected compatibility family, not an accidental wrong-chip flag. Changing core count/security changes startup, libraries and layout. |
| `BT=n`, `BLUETOOTH_RELEASE_CODESIZE=n`, `BLUETOOTH_BLE_SLAVE_ONLY=n`, `BT_REUSE_MEDIA_MEMORY=n` | No Classic Bluetooth or second host. Retain the known controller library/role contract and media-memory separation. Reduced/slave-only libraries need separate feature qualification; Kconfig explicitly warns the reduced variant lacks SMP/central support. |
| PSRAM allocation/cache/calibration settings, SRAM/shared-memory ownership, DMA channel allocation | CP linker and native `soc/t5ai/psram-memory.ld` divide ownership. Do not reclaim whole vendor heaps/slabs based on feature flags. `PSRAM_AS_SYS_MEMORY` is not merely a network buffer switch. |
| `VND_CAL`, `VND_CAL_VERSION`, temperature/voltage support, analog/flash calibration inputs | Bluetooth adapter calls calibration functions; PHY adapter table contains temperature callbacks. `bk_phy` depends on `temp_detect`. Disabling the Pebble temperature feature does not establish these CP functions are expendable. |
| AON RTC/64-bit RTC, timer, ROSC calibration/compensation, tickless/tick settings, LPO/XTAL, supply/domain settings, clock monitor | Preserve BLE timing, CP scheduler and PM behavior. Exact minimal settings require source/closed-library and hardware qualification. Native AP's 240 MHz setting does not determine CP default frequency. |
| GPIO defaults/retention/wake settings, DMA, MPU/cache/FPU/spinlock, flash mode/OTP access | Platform support and ABI. Keep the original pin, cache/coherency and flash behavior in a feature-removal build. |
| Watchdogs/assert/reset policy, UART1/460800 logging and async log backend | Retain supervision and diagnostics needed to qualify the new CP. Logging transport is separate from interactive CLI. Disabling CLI must not silently remove CP log handling. |

`preserve-contract` and `preserve-support` in the CSV distinguish this baseline
from direct service selectors. Numeric values are recorded, not tuned. They
are not asserted to be the smallest possible heap, stack, timer or log setting.

## Features Pebble does not require

| Candidate removal | Dependency work and evidence |
| --- | --- |
| `WIFI_ENABLE`, `WIFI_VNET_CONTROLLER`, Wi-Fi 4/6/raw TX/country/fast-connect/scan/PM knobs, RWNX, supplicant, WPA3/PMF/SoftAP features | No native AP Wi-Fi client. `bk_init.c` initializes Wi-Fi at CP boot; this is real included functionality. Remove as one network profile, not scattered defines. Retain shared PHY/RF and calibration. `WIFI_PS_DISABLE` is already off; enabling it disables Wi-Fi power saving. |
| `LWIP`, `LWIP_V2_1`, `BK_NETIF`, `NETIF_LWIP`, DHCP, TCP/UDP buffer/statistics knobs, `OTA_HTTP`, iperf | No CP IP/HTTP client needed by Pebble; phone networking terminates on native Pebble Bluetooth services. `projects/tuya_app/cp/CMakeLists.txt`, `controller_if/CMakeLists.txt` and adapter CMake contain unconditional lwIP/network dependencies to remove or split first. |
| `CLI`, `BKREG`, `WIFI_CLI_ENABLE`, test/demo flags, `AT`, Wi-Fi/BT/BLE/misc AT commands | No production client. `bk_cli/CMakeLists.txt` pulls tests and commands. `bk_init.c` calls Wi-Fi AT setup; AT server startup calls are also partly compiled out by Tuya. BT/BLE AT sources are already excluded in controller-only mode despite their flags being enabled. Keep UART/log backend until replacement proves it works. |
| `MEM_DEBUG*`, `CM_BACKTRACE`, `HAL_DEBUG_SYS`, command forwarding | Optional observability. Stage after a working reduced network build so useful failure evidence remains. `ASSERT_REBOOT` and watchdog policy are deliberately retained. `MEMDUMP_ALL` with `DUMP_ENABLE=n` is not evidence of a live full-memory dump. |
| `TUYA_LCD`, `TUYA_ST7701S`, `TUYA_ST7701SN`, `LCD_SPI_GC9A01`, `TUYA_LCD_RGB565` | Vendor reference display options; adapter's `CPU_INDEX=0` branch excludes AP display drivers. Native QSPI AMOLED is board/driver code. These flags do not prove any CP display task exists. |
| `TOUCH`, `SUPPORT_IRDA`, `USE_CONV_UTF8`, `BLUETOOTH_BLE_DISCOVER_AUTO` | No native AP client identified. CP capacitive TOUCH is not native CST92xx touch. Discover-auto is documented as single-vendor-host behavior and does not supply native NimBLE discovery. Candidate only; link/map check needed. |

Disable parent features through Kconfig and inspect regenerated config rather
than blindly assigning every child to `n`. Many children are numeric/string
settings or derived choices. Keep network child settings in an optional Wi-Fi-on
profile if that variant is retained.

## Unresolved or misleading switches

The inventory leaves **31 assigned entries (21 enabled booleans)** unresolved:

- `ATE` and its GPIO/CPU2/UART parameters: `cp_main.c` calls `ate_is_enabled`
  unconditionally; driver initialization samples ATE state. `bk_ate` CMake
  includes `ate.c` unconditionally. Factory testing is not a Pebble feature,
  but setting `ATE=n` alone does not prove the code or its startup coupling is gone.
- `PSA_MBEDTLS`, `SW_CRYPTO`, SSL sizes/log level, `TRNG_SUPPORT`, `OTP_V1`,
  `AON_ENCP`, `EFUSE`, MAC policy/mask and `GET_UID_ENABLE`: shared identity,
  crypto/entropy/calibration/library dependencies. `bk_bluetooth` declares
  `psa_mbedtls` as a private dependency even without the supplicant. Do not
  remove all crypto with Wi-Fi. Native host bonding does not prove the CP
  controller has no cryptographic dependency.
- `PHY_SERVER`, `PHY_MB`: native AP has no identified PHY-service client,
  but PHY source and sensor notifications share these paths. Separate the
  unnecessary AP-facing service from Bluetooth PHY/calibration support first.
- `PM_LV_WDT_PROTECTION`, deep-sleep configuration/timeout: protection invokes
  `pm_wifi_event_init` in `bk_init.c`. Resolve Wi-Fi event coupling while
  preserving equivalent supervision; do not disable all PM or assume native
  retained sleep is qualified.
- `PM_ENABLE`: Kconfig flag with no functional CP source gate found; `bk_pm`
  CMake unconditionally builds PM sources. Keep PM implementation; this flag
  is not an effective on/off switch for it.
- `SYSTEM_CLOCK`: visible CP source use is a CLI register-list condition, not
  the gate for hardware clocks. `SARADC_SERVER`: source references found only
  in shipped library config headers, not a gate around the actual server.
  Neither spelling is proof of indispensability or safe library incompatibility.
- `MAC_PHY_BYPASS`, `ICU`, `SDMADC`, `CKMN`: investigate remaining platform,
  calibration and diagnostic coupling. Stock has `SDMADC_TEMP=n`, and its
  SDMADC initialization call in `driver.c` is commented out. That makes the
  enabled flag poor evidence of an active peripheral; it does not yet prove
  source/library removal is safe. `CKMN` is used in Bluetooth clock handling.

## Concrete reduction sequence and acceptance gate

1. Reproduce stock CP from the pinned source/toolchain with the archived
   effective config. Preserve partitions, shared addresses, AP boot, HCI IPC,
   heartbeat, flash and ADC ABIs. Use a staged board-owned project outside the
   pinned submodule. Do not start by changing native Pebble common code.
2. Introduce a BLE-controller service profile. Remove CP `cifd_cust_msg_init`
   and its VNET-only customer handler dependency, split unconditional
   `controller_if`/lwIP/adapter dependencies, and gate network AT/CLI together.
   Preserve AP boot vote and `tuya_ipc_init`; the HCI dispatch uses it.
3. Set Wi-Fi off and verify CMake selects `libcom_phy.a` (appropriate NSPE
   form if security profile changes) and `bk_cal_if_init` is reached. The
   stock ELF has no retained `bk_cal_if_init`; simply bypassing Wi-Fi startup
   does not reproduce this Bluetooth calibration path. Keep the current
   controller-only BLE library and shared RF arbitration.
4. Remove diagnostic tests and reference-only/peripheral candidates in
   separate steps; retain logs, asserts, reset reasons and watchdogs during
   qualification. Resolve each unresolved family with actual link/map and
   call-graph evidence. Report before/after ELF sections, heap reservations
   and runtime task/queue allocations; do not total configured lwIP buffer
   limits as guaranteed RAM savings.
5. Integrate the existing RF/interval corrections at source/link time or
   regenerate hooks bound to the new exact ELF. Existing fixed-address and
   full-hash patch guards reject a relinked CP correctly. Require a new
   variant-specific manifest/packaging contract, retaining stock rollback.
6. Check maps/symbols for absence of Wi-Fi/VNET/lwIP/supplicant/test startup,
   correct BLE/PHY library and preserved service ABI. Then verify boot,
   heartbeat, HCI reset, bond restore, advertising/reconnect, transfers,
   flash/PFS, ADC, microphone/SD clock service interactions, 25 reconnects,
   cold boot and overnight idle. Measure power only against a frozen baseline.

This audit closes feature ownership/inventory. It leaves the qualified minimal
CP build and electrical savings open, with the same reproducible-build and
packaging dependency recorded in the Wi-Fi investigation.
