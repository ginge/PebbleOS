# Unhygienix Wi-Fi optional-feature investigation — 2026-10-30

## Disposition

Investigation complete; **default-off implementation blocked, electrical and
device qualification pending**. Native PebbleOS AP already has no Wi-Fi stack.
The supplied CP does initialize Wi-Fi without an AP Wi-Fi client. Its pinned
runtime cannot fully deinitialize Wi-Fi. Vendor source offers a Bluetooth-only
compile path, but it requires a new CP build and a supported packaging contract;
the present hash-checked binary-hook pipeline cannot accept that build.

No firmware behavior, common Pebble source, vendor source, installed firmware,
build configuration or original build output was changed. No power saving is
claimed. A board Kconfig switch alone would misleadingly report Wi-Fi off while
the CP still initializes it, so none was added.

## Evidence and identities

Production source inspected at `4d9976caa8b8cbc8f3cbfbc14c79c92b8649f86f`;
worktree initially clean. Applicable repository, board and workspace AGENTS.md
instructions and the power TODO/current-status pages were read.

Pinned inputs remain `boards/unhygienix/tuya/sdk.lock.json`:

- T5AI: `13379b63e07e78770fb4d0bffe36db2754658132`.
- Optional TuyaOpen: `e82840dc9b629cb77fa922fb04e3f820dc2e78b4`.
- Stock CP SHA-256:
  `0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856`.

The reference `.vendor/tuyaopen/platform/T5AI/t5_os/build/bk7258/tuya_app/bk7258/`
under the artifacts workspace contains the stock CP ELF, map, generated config
and binary. **Objcopy of that ELF exactly reproduces the hash-checked stock CP**.
The generated config therefore has substantially stronger provenance than a
project config fragment alone. Selected startup function bytes also match the
last recorded installed, fix-only CP (`1713dee17dd98cd8aea8114a438d24ed87112585c16c45960221c4b96f344efb`).
This verifies archived artifacts, not the device's currently installed identity.

All new evidence is preserved at
[/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261030/](/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261030/).
It includes `cp-evidence.json`, `cp-startup-disassembly.txt`, matching vendor ELF,
map/config, original build snapshots and SHA-256 manifests. Do not confuse the
old diagnostic `interval-fix/cp` recorder with the installed `fix-only/cp` image.

## Ownership trace

Vendor paths below are relative to
`third_party/hal_tuya/TuyaOpen-T5AI/t5_os/` unless stated otherwise.

| Area | Confirmed ownership and behavior |
| --- | --- |
| Native AP | `soc/t5ai/CMakeLists.txt`, `soc/t5ai/init.c`, board configuration and `drivers/t5ai/CMakeLists.txt` build native Pebble code, not the vendor AP RTOS, network stack or Tuya application. No Wi-Fi initialization or station/AP requests were found in the native board/SoC/drivers. Bluetooth uses `third_party/nimble/transport/t5ai_hci_ipc.c`. |
| Vendor AP reference | `projects/tuya_app/ap/config/bk7258_ap/config` enables `WIFI_TRANSFER`, `WIFI_VNET_CONTROLLER`, `WIFI_SOFTAP` and `ENABLE_LWIP`. These do not control the native AP or the already-built CP. The optional `boards/unhygienix/tuya` reference application is also a separate build. |
| CP build | Stock generated config has `WIFI_ENABLE=y`, `WIFI_VNET_CONTROLLER=y`, `WIFI_CP_IPC` unset, `BLUETOOTH=y`, `BTDM_CONTROLLER_ONLY=y`, `BLUETOOTH_SUPPORT_IPC=y`, `LWIP=y`, Wi-Fi CLI/AT enabled and `WIFI_PS_DISABLE` unset. Controller-only Bluetooth does **not** mean Wi-Fi-free CP. |
| CP startup | `cp/components/bk_init/bk_init.c:bk_init()` initializes components and PM mailbox, calls `app_wifi_init()`, then Bluetooth. Stock ELF has a direct call to `bk_wifi_init` at `0x0206b516`; it is not waiting for an AP request. |
| Wi-Fi initialization | `cp/components/bk_wifi/src/wifi_init.c:wifi_init()` initializes workqueue, votes MAC/PHY power and clocks, opens the Wi-Fi RF vote, initializes MAC/PHY and calibration, then calls `rwnx_intf_init`, `fhost_rxbuf_push`, `wpas_thread_start` and `cif_init`. ELF `wifi_init` at `0x0202d7d2` confirms these calls and votes. `bk_wifi_init` registers deep-sleep and low-voltage callbacks. Its success/init flag is not reliable evidence of all initialization succeeding: the wrapper ignores the underlying initialization result. |
| Tasks | The source starts the Wi-Fi workqueue, core/event and message handlers, supplicant and VNET controller thread. Generated config reserves core-task stack 2048 B and kmsg-task stack 4096 B; `cif_init` creates a 4096 B task plus queue and oneshot timer. Creation failures remain possible; these are configured/attempted allocations, not live measured stack usage. |
| Memory | CP linker `cp/middleware/soc/bk7258/bk7258_bsp.ld` retains Wi-Fi/controller/lwIP text and CP RAM/heap/PSRAM ownership. Wi-Fi task state, queues, protocol tables and VNET event buffers occupy static/dynamic CP memory. `os_malloc_wifi_buffer` uses the CP allocator on BK7258. `projects/tuya_app/partitions/bk7258/ram_regions.csv` reserves CP PSRAM heap at `0x60700000`, size `0x40000`. Native AP explicitly owns the first 128 KiB PSRAM plus its 8 KiB SD metadata reservation (`soc/t5ai/psram-memory.ld`). Do not reclaim vendor/AP media slabs or CP memory merely by disabling Wi-Fi. No exact recoverable-RAM total or RAM power saving has been established. |
| Power and clocks | Wi-Fi startup votes `WIFIP_MAC`, parent `PHY`, `PHY_WIFI`, MAC and PHY clocks, and `RF_OPEN/RF_BY_WIFI_BIT`. `bk_wifi_adapter.c` exposes later sleep, power and CPU-frequency callbacks to the prebuilt Wi-Fi library. Firmware inspection confirms startup votes, not their continuously elected state or idle duty cycle. Native AP's 240 MHz vote is independent of Wi-Fi ownership. |
| Shared RF/PHY | `app_phy_init` initializes PHY/RF adapters when either Wi-Fi **or Bluetooth** is enabled. `bk_pm/pm.c` tracks PHY_BT/PHY_WIFI/PHY_RF owners; Bluetooth's `soc/bk7236/bluetooth.c` uses its own PHY_BT and RF_BY_BLE votes. Keep Bluetooth, calibration, RF arbitration and PM support. Never gate the shared PHY/RF/PLL wholesale from AP. |
| Supervision/services | General mailbox, AP startup announcement, one-second CP heartbeat, flash, logging, ADC and PM IPC are independent service contracts. Keep `drivers/t5ai/cp_mailbox.c`, `soc/t5ai/init.c`, vendor mailbox/heartbeat and Bluetooth IPC. VNET's `cif_ipc_init` opens Wi-Fi channels; it is not a substitute for all mailbox services and its removal still needs a service dependency audit. |

## What is and is not established about power

Confirmed: the pinned executable contains an unconditional normal-boot Wi-Fi
initialization path and explicit power/clock/RF acquisition, even with no native
AP Wi-Fi user. The recorded installed CP retains that path. Wi-Fi being unused
does not imply it was never initialized or powered.

Not established: whether initialization completed on the current boot, present
MAC/PHY/RF vote values, automatic radio sleep, Wi-Fi task wake frequency or the
electrical cost of idle Wi-Fi. The library can subsequently change votes.
`wifi_deepsleep_enter_cb` releases Wi-Fi RF/PHY/MAC votes, but that callback is
part of the deep-sleep transition; it is not a supported live teardown API.
Native AP retained-sleep coordination is separate unfinished work. Do not invoke
that callback in isolation or infer steady Wi-Fi power from these source paths.

Host USB enumeration found CH342 UARTs `if00`/`if02` and no USB current-monitor
instrument. A bounded six-second read at 460800 baud received 0 B on ttyACM0 and
246 B on ttyACM1, saved with `hardware-read.json`. Both descriptors were closed
and original terminal attributes restored. No serial commands, resets, flashing
or running soak were involved. Received bytes establish a live UART read only;
they do not establish firmware identity or electrical state. No suitable current
measurement setup is available in this session; the operator is awaiting it.

## Runtime disable versus build exclusion

**Runtime full disable is unsupported by this pinned implementation.**
`wifi_deinit()` and `bk_wifi_deinit()` explicitly return `BK_ERR_NOT_SUPPORT`;
the latter even clears its init bit despite not freeing resources. Stopping a
station/AP or Wi-Fi RF vote alone does not delete tasks, free buffers, balance
every power/clock vote or unregister callbacks. No complete live shutdown and
restart protocol exists in the native AP mailbox interface. Adding one would
require CP ownership/state/error handling and qualification, not an AP-only flag.

**Build exclusion is a plausible vendor-supported configuration path, not yet
a qualified Unhygienix image.** `cp/components/bk_wifi/Kconfig` defines
`WIFI_ENABLE` (vendor default y), and its CMake omits Wi-Fi sources/libwifi when
disabled. Crucially, `cp/components/bk_phy/CMakeLists.txt` selects `libcom_phy.a`
(or its NSPE variant) for Bluetooth with Wi-Fi disabled. Both libraries exist in
the pinned BK7258 SDK. `bk_pm/pm.c` switches to
`phy_wakeup_for_bluetooth`; `bk_init.c` has a Bluetooth-only calibration branch
using `bk_cal_if_init`; stock generated config also sets `SOC_BK7236XX=y`,
so that branch applies to this BK7258 configuration. The stock ELF has no retained
`bk_cal_if_init` symbol. Simply bypassing its Wi-Fi call would not reproduce this
link/configuration/calibration change.

Additional integration remains: `controller_if/CMakeLists.txt` has its Wi-Fi
guard commented out and unconditional Wi-Fi/lwIP dependencies. CLI/AT, Wi-Fi PM
event integration and BLE/Wi-Fi exchange settings must be resolved together;
turning off just one symbol is not proven to link. No claim is made that a full
Wi-Fi-off CP build was attempted or that the vendor path already passes.

## Exact implementation dependency and next step

The blocking dependency is a **reproducible, packaged BK7258 CP variant with
Bluetooth-only PHY/calibration and the existing AP service ABI**.
`boards/unhygienix/firmware.cmake:t5ai_cp` currently calls
`tools/unhygienix_cp.py` on a separately supplied stock binary.
`tools/patch_unhygienix_cp_rf.py` checks the entire stock hash and fixed reset
function; the latency patch also binds fixed vendor functions/call sites. A full
CP relink moves those functions and cannot be accepted by those guards or fixed
addresses. Do not relax the hash or relabel a manifest to make it pass.

The concrete next Wi-Fi implementation task is:

1. Stage a pristine copy of pinned T5AI outside its submodule. Preserve the
   current `tuya_app` partitions, shared-memory addresses and service settings.
   First reproduce the stock Wi-Fi-on CP using the recorded generated config
   and appropriate vendor toolchain; retain compiler identity, map and config.
2. Add a board-owned CP build profile with `WIFI_ENABLE=n`, Bluetooth controller
   and IPC retained, and dependent VNET/CLI/AT/lwIP/exchange features resolved.
   Check actual selected PHY library and calibration/wake linkage. Keep an
   explicit Wi-Fi-on profile; default the future Unhygienix CP selector to off.
   Do not claim that a native AP Kconfig option controls the existing stock CP.
3. Integrate the existing RF `0x0202` and interval-transition corrections at
   source/link time (or regenerate and verify hooks against each exact new ELF).
   Define variant-specific manifests binding compiler, effective config, input
   libraries, ABI, layout and output hashes. Extend packaging/runner rejection
   tests. Preserve the stock rollback path and never accept an arbitrary CP.
4. Build both variants, inspect symbols/maps and exclusions, then verify boot,
   heartbeat, HCI reset, advertising, bonded reconnect, sustained transfers,
   25 reconnects, cold boot and overnight idle. Exercise flash/PFS, ADC,
   microphone and SD services without changing their power policies. Confirm
   per-owner RF/PM state with bounded diagnostics. Wi-Fi-on coexistence also
   needs qualification; it is not qualified by the current Bluetooth RF hook.
5. Measure matched idle/workload energy against the frozen baseline before
   marking electrical qualification complete. Do not combine this with other
   power changes.

This is a build/ABI integration blocker, not evidence that BK7258 cannot run
Bluetooth without Wi-Fi or that a needed PHY library is missing.

## Baseline preservation and validation

`artifacts/wifi-optional-20261030/baseline/manifest.json` binds 117 copied files
(320,942,623 bytes). Original build directories remain available and untouched.
The snapshot separates existing local builds (some old) from the last recorded
installed AP/CP. In particular, `build-main-tuya` is dated October 19 and must
not be labelled the October 22 installed AP. The installed-record AP build ID is
`c0e283f0b861e0d0fbcb70fb9c5fae6e86b84065`, raw SHA-256
`3320c182e51a5e886c844d1fe7db59409dd3b5b0d4a29c55071dff9274c2cc4b`.
Its protected image, ELF/map, dictionary and October 22 validation record were
copied. Installed-record CP, supported stock-derived CP and pinned stock rollback
input are separate directories. These identities were not re-read from flash.

The unchanged production CP source hooks were compiled and packaged from the
verified stock input into `repackaged-cp/`; manifest validation passed. The
recorded-installed AP map passed `check_unhygienix_image_map.py`. This is a CP
hook build and AP artifact check, not a full Wi-Fi-off CP or new AP build.
All 70 focused host regressions passed (CP manifest/RF/latency, image/packaging,
runner, power and board configuration); see `regression-final.log`. These do
not qualify Wi-Fi-off operation or electrical savings.
The initial test invocation used an interpreter without the project imports;
the corrected invocation supplies Python 3.12 site-packages and `pbl-cli` paths.

## Handoff when the current monitor arrives

Before changing firmware, verify the frozen-file hashes, re-establish actual
installed AP/CP identities, record fitted cell/supply voltage, instrument,
connection and sampling setup, phone state and fixed display content/brightness.
Measure connected idle and disconnected advertising first; preserve raw traces
and sparse diagnostics. Use the same setup for Wi-Fi-off/on candidates and
re-test the baseline to catch drift. Do not overwrite the frozen baseline with
newly built firmware. A deliberate rollback may require the documented
`--allow-older-version` AP path and CP-only writer for the archived installed CP;
historical installed CP is not a current normal-runner manifest. Validate exact
write ranges and preserve PFS/bonds.

The next power-optimization task is baseline measurement and completion of this
CP build integration gate. If CP integration remains blocked, explicitly retain
that blocker before moving to phase 2's balanced I2C clock work in
[power-todo.md](power-todo.md). No other power work was started here; display
power remains last.
