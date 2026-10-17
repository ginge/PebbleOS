# Unhygienix: The village fishmonger

`unhygienix` is the PebbleOS target for the Waveshare
[T5-E1-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75),
using the Tuya T5AI/BK7258. Run commands from the PebbleOS repository root.
Board configuration and Tuya reference/CP material live in `boards/unhygienix/`;
vendor sources are pinned submodules under `third_party/hal_tuya/`.

The board has independent configuration and retains `CONFIG_PLATFORM_GABBRO`,
the Gabbro app ABI and the Round 2 companion identity
`FirmwareMetadataPlatformPebbleGetafixDVT2`. It shares display and splash assets
with Getafix. Getafix DVT and DVT2 remain independent Round 2 hardware targets.
Unhygienix is excluded from release matrices. Recovery firmware, OTA, PBZ
bundles, bulk erase and unsupported raw-flash operations remain disabled.

## Top priority cleanup for main and upstream — 2026-10-14

Complete the [upstream divergence cleanup TODO](../../../boards/unhygienix/tuya/REMAINING_TASKS.md#next-priorities--2026-10-14)
before proposing the port for merge to `main`. It references the complete
2026-10-14 source audit and file inventory, separates general fixes for upstream,
and defines the required regression and hardware validation. Each retained
stock deviation or CMake-selected replacement needs an explicit justification;
merge readiness remains unqualified until the checklist is resolved.

## Current controller baseline and paused bypass — 2026-10-11

Flash bypass development is paused. Active work returns to `tuya` with the
controller path; the complete experiment is preserved on
`codex/unhygienix-flash-bypass-paused`. Read the
[pause decision, exact installed images and tradeoffs](flash_spi_bypass_paused.md).
The operator confirmed the restored watch is usable. BG stalls remain unresolved;
there is no qualified bypass speed result and no live capture is running.

## Watchdog investigation — 2026-10-11

Two phone full-sync watchdog resets were captured with FdF animating. App
throttling works, but the settings rewrite remains active beyond the recovery
grace while BG runs and completes many small flash writes. Investigation is
open. Read the [evidence and debugging plan](settings_sync_watchdog.md).
Tracing has finished; the diagnostic firmware remains flashed. The
[flash optimisation and robustness audit](flash_optimisation.md) ranks driver,
transport and settings rewrite changes, with the required measurements.

## Latest refactor handoff — 2026-10-09

Read the [detailed refactor handoff](refactor_handoff.md) before continuing.
It records completed extractions, the five remaining refactor slices, deferred
KernelBG diagnosis, and the exact last-flashed candidate and hardware evidence.
The watch was boot looping before the latest erase-delay fix. Its short
post-flash capture recovered one KernelBG stall and observed no further reboot;
stability and app persistence are not qualified. SARADC timeouts remain.
Older hardware successes below are dated evidence, not proof these issues have
been resolved. No live capture is running at this handoff.

## Current status

The native target configures, builds, packages, boots, logs over USB, renders
through the native CO5300/QSPI path, reads the board's input and sensors, and
runs Pebble's NimBLE host against the CP-owned Bluetooth controller.

| Area | Hardware status |
| --- | --- |
| CPU1 startup and CP handshake | Proven |
| Dual USB UART roles and runtime logging | Proven |
| Native Pebble kernel and SysTick | Builds; hardware revalidation required |
| AON and external RTC | Previously proven on FreeRTOS; revalidate with native kernel |
| GPIO and Back/Select buttons | Proven |
| I2C0 and PCF85063 RTC | Proven |
| CO5300 AMOLED and native QSPI transport | Proven with colour bands and display off/on tests |
| CST9217 touch | Queue-pressure and release-ordering fix validated by repeated Settings scrolling |
| QMI8658 accelerometer | Sample delivery proven; software shake/display wake validated on 2026-10-04, sensitivity tuning installed |
| SARADC and charger-status input | Transactions proven; battery behavior blocked because no cell is fitted |
| Bluetooth HCI command/event and ACL transport | Proven |
| Connectable advertising and BlueZ connection | Proven |
| Pebble GATT service discovery | Proven |
| Pairing, bonding, reconnect, and Pebble protocol session setup | Physical RF PLL fix installed; 15-minute connected run, bonded hardware-reset reconnection and 10-hour connected-idle observation passed; broader reconnect and cold-boot qualification pending |
| Device identity | Phone recognizes the watch as Round 2; further compatibility remains under test |
| Sustained phone sync and watchface transfer | Six transfers completed and all faces loaded; clean-image startup verified |
| Sleep/wake and low-power operation | Shake wakes the timed-out display; deeper sleep, wake interrupts and low-power operation remain unvalidated |
| Writable PFS | NOR cache corruption fixed; 2.75 MiB PFS migrated and readback verified |
| OTA, recovery, and general writable flash | Deliberately disabled pending a safe storage/recovery contract |

The fitted CO5300/panel assembly presents inverted polarity on all three RGB222
channels: black/white and every two-bit channel level are complementary. The
Unhygienix display driver's RGB222-to-RGB565 table applies `pixel ^ 0x3f` before
conversion. This is panel compensation, not a theme, PFS setting, or a change
to Pebble's shared `GColor` values.

## Implementation gaps

| Area | Current implementation and remaining work |
| --- | --- |
| Bluetooth stability/recovery | Physical BLE/BT PLL configuration is applied after CP Bluetooth initialization. Live RF reconfiguration recovered the failed state; the installed fix passed 15 minutes connected, bonded reconnection after hardware reset and a 10-hour connected-idle observation. Exact internal failure mechanism remains unproven; broader reconnect and cold-boot validation are still required. |
| App/watchface stall | The captured whole-chip reset followed the process-manager assertion for an App task stuck in privileged code. The initiating blocked operation is unknown; the temporary CP SRAM and pre-reset fault-context experiment was removed on 2026-10-04. |
| Low power and wake alarms | `soc/t5ai/idle.c` uses the native Pebble kernel idle hook and WFI but ignores the requested idle duration; `pbl_soc_tick_enable()` returns false. `rtc_alarm_set()` ignores its deadline and `rtc_alarm_is_initialized()` returns false. Tick suppression, hardware alarm/wake routing and deeper sleep remain unfinished. |
| Motion and step tracking | QMI8658 software shake detection is implemented and display wake is operator-validated. Double-tap detection remains unimplemented. Pebble pauses activity tracking when external power is reported; step counting remains unvalidated on the USB-only board with no battery. |
| Serial console input | `fw/console/dbgserial_input_t5ai.c` contains no-op initialization, callback registration and input/DMA enable functions. UART logging and CP-shell AP commands work; a general bidirectional console is not established. |
| Persistent flash logging | The build selects `fw/debug/flash_logging_unavailable.c`: writes fail and dump requests report no saved logs. Serial logging remains available. |
| Persistent coredumps | Disabled pending a validated partition/recovery contract. Current fault evidence relies on UART diagnostics. A writable flash region alone does not enable this service. |
| Bootloader version | `soc/t5ai/bootbits.c` returns zero from `boot_version_read()`; version reporting is a placeholder. |
| Manufacturing OTP | Unprovisioned service rejects writes and provides no stored identity; manufacturing uses its dummy identity. The separate MCU factory eFuse serial reader is implemented. |
| Audio and optional peripherals | Native microphone DMA capture and phone dictation are implemented; see [integration details](microphone.md). Speaker playback remains disabled. Haptics, ambient light and temperature have absent interfaces; magnetometer, pressure and PMIC features are disabled. Absent hardware does not itself require implementation. |
| Firmware update/recovery | OTA, firmware/recovery object updates and arbitrary flash writes remain gated. Define packaging, metadata, slots, rollback, interruption recovery and factory restoration before enabling them. AP-only lab programming is implemented and must preserve PFS during routine updates. |

Display, touch, buttons, accelerometer, RTC timekeeping, RNG, MCU serial,
watchdog, bounded flash/PFS and Bluetooth transport have native implementations;
they are not stubbed merely because validation is incomplete. Device identity
now reports Round 2; older factory/unknown-platform notes are superseded.

Remaining validation includes cold-boot bond and selected-face persistence,
broader reconnect coverage, wake interrupts, RTC drift, isolation/privilege
transitions, and storage corruption/interrupted-write
recovery. Battery calibration, charge/unplug transitions and power measurements
require a fitted cell. Glyph/resource-check and missing app-entry warnings remain
unresolved despite successful face loading. Any uncommanded AP reset remains a
release blocker; retain the complete first-failure trace and matching dictionary.

## Device and USB ports

The test board has a CH342F dual serial bridge (`1a86:55d2`, serial
`5C75025844`). Its two USB interfaces have different jobs:

- `if00`, currently `/dev/ttyACM0`: Tuya programming and reset.
- `if02`, currently `/dev/ttyACM1`: CP shell and native runtime log at 460800
  baud.

ACM numbers can change. Prefer the stable by-id paths containing
`usb-1a86_USB_Dual_Serial_5C75025844-if00` and
`usb-1a86_USB_Dual_Serial_5C75025844-if02`.

If the CP becomes wedged after a failed mailbox or Bluetooth sequence, an AP
reset or reflash may not recover it. Disconnect the shared USB-C cable for five
seconds and reconnect it to power-cycle both cores and the dual-UART bridge.
The mailbox corrections are now implemented; power-cycle only if CP remains
wedged or the programming handshake cannot recover normally.

## Build

Run all commands from the real PebbleOS checkout:

```sh
. .venv/bin/activate
pbl configure --board unhygienix
pbl build
```

The build emits `build/pebbleos.t5ai-crc.bin`, an AP-only protected-block
payload. It is not a complete QIO, UA, UG, factory, recovery, or OTA image.

Routine updates use the native runner and preserve system resources, PFS and
bonds. Preview the exact write range before programming:

```sh
pbl flash --dry-run --tyutool tyutool_cli --tyutool-tty /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00
pbl flash --tyutool tyutool_cli --tyutool-tty /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00
```

`--resources` additionally updates system resources; it preserves PFS.
`--t5ai-provision --resources` explicitly writes an empty PFS and destroys its
stored apps/data. `--t5ai-cp` additionally writes the validated CP artifacts
produced by `pbl configure --board unhygienix -DT5AI_CP_STOCK="$PWD/build/inputs/stock-cp.bin"
pbl build t5ai_cp`.
The legacy AP/CP guard scripts remain available for rollback and validation.
See [dependency and tooling alignment](tooling.md) for setup and CP inputs.
Never pass logical `build/pebbleos.bin` directly to tyutool.

## Flashing and tracing

The pinned partition places the AP at physical flash offset `0x132000`, mapped
to logical XIP address `0x02120000`. Pebble links into a `0x190000`-byte
protected firmware region followed by a 1 MiB protected system-resource bank.
Together they occupy AP physical flash `0x132000..0x3eb000`. The CP flash
service accesses the PFS as raw physical data, so the 2.75 MiB PFS is a separate
image at physical `0x500000..0x7c0000` (704 pages), inside the otherwise-unused factory OTA
data partition. This preserves the shared-PRF area at `0x4ea000..0x4fa000` and
the factory bootloader, CP, configuration, and RF-calibration partitions.

Use `tools/flash_unhygienix_firmware.py --flash` for routine updates to preserve
existing resources and PFS.

For deliberate empty-filesystem provisioning on an already-backed-up lab board,
`pbl flash --resources --t5ai-provision` replaces existing PFS contents and builds two exact-range images:
the protected AP image contains firmware and system resources (including stored
base apps), while a second raw image contains a preformatted empty PFS. The PFS
image writes the same active-low page headers as `pfs_format(true)`, so
filesystem provisioning is deterministic before normal resource and launcher
startup. The factory bootloader and CP partitions remain untouched:

```sh
pbl flash --resources --t5ai-provision \
  --tyutool tyutool_cli \
  --tyutool-tty /dev/ttyACM0
```

The runner derives each `--end` from `build/pebbleos.t5ai-flash.bin` and
`build/pebbleos.t5ai-pfs.bin`; it writes the AP image at `0x132000` and the raw
PFS image at `0x500000`. Do not use either image with a factory or OTA
programming mode. Reset, bulk erase, recovery imaging, and firmware/resource
update paths remain disabled on T5AI.

For a persistent, decoded trace suitable for pairing, BlobDB, AppFetch, and
PutBytes diagnosis, run from the PebbleOS checkout:

```sh
.venv/bin/python tools/unhygienix_trace.py --wait 0 --command "app list"
```

The utility waits for the stable `if02` by-id device, overrides PULSE's generic
1 Mbaud default with Unhygienix's 460800-baud runtime setting, dehashes messages using
the current build dictionary, prints them live, and stores a timestamped copy
under `build/traces/`. `--wait 0` waits through a USB reconnect; omit
`--command` for a passive trace. Use `--duration SECONDS`, `--output PATH`, or
`--tty PATH` when a bounded capture, fixed artifact name, or explicit serial
device is required. A quiet PULSE session is reopened after 10 seconds by
default, which recovers tracing when the AP resets but the dual-UART bridge
never disconnects; use `--reconnect-idle 0` to disable this. The trace remains
active if the optional bidirectional console command is unavailable. Run the
command with host-level device access when a sandbox does not expose
`/dev/serial`.

For an undecoded raw UART view:

```sh
picocom --baud 460800 /dev/ttyACM1
```

## Bluetooth controller

Pebble's NimBLE host owns the AP Bluetooth stack. The CP supplies the controller
through the pinned mailbox/HCI ABI. Do not start the vendor AP Bluetooth host.
The RF initialization hook applies BLE/BT PLL configuration `0x0202` after
successful CP Bluetooth initialization. CP packaging verifies the stock image
hash, preserves existing code/data addresses and emits a validated manifest.
See [tooling](tooling.md) and the
[CP hook](../../../boards/unhygienix/tuya/cp/README.md) for packaging and rollback.

Diagnostic CP BLE sampling requires an ELF matching the exact pinned stock
image; its SRAM ABI is not portable across CP builds. Disable the diagnostic
option unless that match has been verified.

## Bluetooth persistence

The T5AI build uses Pebble's PRF Bluetooth persistence interface over the
bounded shared-PRF partition. Writes are enabled, and the backend enumerates the
saved BLE pairing so Bluetooth Settings shows the phone's name after reboot.
A firmware-only AP flash does not overwrite the partition; the tested phone
reconnected without a new pairing.

This does not validate every storage failure mode. Cold power-cycle retention,
compaction, corruption recovery, endurance, and full-board recovery still need
explicit tests before expanding writable-flash scope. Firmware, recovery,
persistent-coredump, and unsafe general write paths remain disabled.

The reconnect-time ATT failure was not mbuf exhaustion. The CP reads HCI packets
through a pointer nested in the mailbox request, so the T5AI IPC layer must clean
the pointed-to payload from the AP data cache before publishing the descriptor.
GATT discovery now uses the shared NimBLE engine on every board. Once a
service's descriptors are complete, the engine emits its compact service blob
and frees the temporary service/characteristic/descriptor nodes. It caches only
the Service Changed subscription handles before releasing those nodes. Empty
services are emitted without skipping descriptor discovery for later services.
Connection-validity checks and the existing queued-operation/error/cancellation
paths remain in the shared engine; an immediate subscription-write failure
also releases its callback context.

`CONFIG_BT_GATT_DISCOVERY_LEASE_SECONDS` retains the 120-second T5AI response-time
lease override when a range is queued. Other SoCs default to zero, retaining the
firmware's existing lease policy. Successful discovery still applies the common
10-second tail. This is a connection-responsiveness policy, not a longer ATT
procedure timeout. No alternate T5AI discovery implementation remains.

The full-source host harness exercises stock and T5AI lease settings with
allocation accounting and AddressSanitizer/UndefinedBehaviorSanitizer. The
four existing native GATT discovery/accessor/subscription/Service Changed
suites also pass. These host checks do not replace watch discovery/reconnect
and cold-boot qualification.

## Accelerometer and motion wake progress (2026-10-04)

The QMI8658 driver previously delivered samples but left shake detection and
sensitivity functions as no-ops. The driver now detects motion in software and
calls Pebble's existing `accel_cb_shake_detected()` path. All production changes
are local to `fw/drivers/imu/qmi8658/qmi8658_t5ai.c`; shared Pebble services
were preserved.

Shake detection keeps the sensor and polling timer active even without a data
subscriber, including while activity tracking is paused on external power.
Polling runs at least approximately 31.25 Hz (32 ms) while shake detection is
enabled. A low-pass gravity baseline, 100 ms startup settling period and 500 ms
event holdoff suppress startup transients and repeated events. The detector
supports the existing sensitivity percentage and high-sensitivity override;
rotation or a timestamp discontinuity resets the motion baseline. Peeking no
longer powers down a sensor being used solely for shake detection.

The initial firmware was built, AP-only flashed and traced through the host's
CH342 runtime UART. The operator confirmed that flick/shake wakes the display.
Two subsequent threshold increases were built and flashed in response to
operator feedback. The first adjustment was reported as much better but still
slightly too sensitive. The second adjustment used a 200–1300 mg range (695 mg at the default 55%
sensitivity), but the operator subsequently reported that it required too much
motion. The current setting is halfway between the two adjustments: 175–1200 mg,
or 637 mg at 55%, using integer arithmetic. The initial threshold was 415 mg;
the first adjustment was 578 mg. High-sensitivity mode now uses 175 mg.
The second adjustment's boot was captured with build ID
`adc9d3bb27299f9b2ed052ab3869cc6022ba70b4`. The midpoint setting is the latest
requested tuning; physical sensitivity confirmation remains pending.

Native firmware builds and `git diff --check` passed. Focused C harnesses under
`/home/baz/Documents/ChatGPT/pebble/tests/` passed for sample readiness, I2C
failure/recovery, scaling, peek behavior, gravity/noise rejection, deliberate
shake events, axis/direction, sensitivity, holdoff, power/subscriber transitions
and failed-enable rollback. These tests exercise the production driver with
simulated sensor/timer responses; physical wake validation comes from the
operator's observation.

Evidence is retained under `/home/baz/Documents/ChatGPT/pebble/logs/`:

- `shake-flash-20261004.log` and `shake-trace-20261004.log`: initial flash,
  boot and phone reconnection; operator confirmed display wake.
- `shake-tuned-flash-20261004.log` and `shake-tuned-trace-20261004.log`:
  first sensitivity adjustment and boot/reconnection.
- `shake-tuned2-flash-20261004.log` and `shake-tuned2-trace-20261004.log`:
  final sensitivity adjustment and verified boot output.

The initial trace had no QMI8658 errors or additional resets during the capture.
It still contained battery ADC, timeline and weather errors. Read-only console
queries timed out because a general bidirectional console is not established;
those queries did not verify live acceleration or charger state. Captures are
stopped. Shake display wake does not establish deep-sleep wake support or step
counting. The board has no battery and must remain USB-powered; charging-state
activity suppression was left unchanged.

## Other hardware notes

- The AMOLED has no conventional backlight. Brightness is controlled by the
  CO5300 panel command. Native display bring-up produced colour stripes and
  confirmed display off/on behavior.
- The board under test has no battery fitted. A USB-powered ADC result around
  4.0 V is the unloaded charger node, not a cell measurement or battery
  validation.
- The active-high P19 power latch is asserted early. Active-low amplifier P28
  remains disabled because speaker playback is not implemented.
- Missing vibration, ambient light, temperature, magnetometer,
  PMIC, and OTP hardware are represented by explicit unavailable or safe
  fallback implementations rather than inherited DVT2 drivers.
- Touch and accelerometer delivery are proven. The board-specific startup now
  calls the complete normal-service initializer after hardware bring-up, so
  protocol endpoints do not reach uninitialized music, AppDB, or timer state.

## Validation and remaining work

Run the focused tools tests and native firmware build from the repository root:

```sh
python -m unittest discover -s tools/tests -p 'test_unhygienix*.py'
pbl build
git diff --check
```

Hardware qualification requires watchface install/launch, 25 disconnect/reconnect
cycles, a true cold boot with bond and selected-face restoration, then an
overnight connected-idle soak with periodic UI interaction. Preserve complete
first-failure traces under `build/traces/`, with matching firmware and log hashes.
Verify device visibility and received UART bytes before claiming live monitoring.
Any uncommanded AP reset or externally triggered AP reboot is a release blocker.

Investigate the App privileged-code stall, glyph/resource and missing app-entry
warnings. Complete wake/alarm routing, low-power entry, battery measurements with
a fitted cell, storage interruption/corruption recovery, RTC drift and privilege
isolation validation. Define recovery and rollback before enabling updates.

## Source map

- `boards/unhygienix/`: board configuration and Tuya reference/CP sources.
- `fw/board/boards/board_unhygienix.c` and `.h`: board implementation.
- `fw/board/board_t5ai.h` and `soc/t5ai/`: generic SoC types and startup.
- `fw/drivers/t5ai/cp_mailbox.c`: mailbox ownership and ACK serialization.
- `third_party/nimble/transport/t5ai_hci_ipc.c`: HCI transport.
- `tools/unhygienix_image.py`: partition geometry and packaging.
- `tools/t5ai_image.py`: protected-block encoding and verification.
- `tools/unhygienix_cp.py`: CP packaging and manifest validation.
- `tools/unhygienix_trace.py`: runtime capture and log decoding.

```{toctree}
:maxdepth: 1

tooling
settings_sync_watchdog
flash_optimisation
refactor_handoff
history/index
```
