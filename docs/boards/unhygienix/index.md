# Unhygienix: The village fishmonger

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](current-status.md). Dated results below retain their original evidence limits.

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

## Hardware add-on and case design

Canonical hardware docs, PDFs, generators, references and future CAD live in
[`boards/unhygienix/hardware/`](../../../boards/unhygienix/hardware/README.md).

See the [hardware and enclosure brief](../../../boards/unhygienix/hardware/docs/hardware-addon-design.md) for the proposed
BOM, battery choices, provisional clearances and unresolved fit requirements.
Use the [case-design handoff prompt](../../../boards/unhygienix/hardware/docs/case-design-handoff.md) to continue enclosure
work. These documents describe a preliminary design, not verified hardware.

## Latest implementation status — 2026-10-22

The latest recorded AP is `c0e283f0b861e0d0fbcb70fb9c5fae6e86b84065`, with
32 MiB four-bit DMA SD PFS and its 8 KiB metadata cache in dedicated PSRAM.
Notifications display normally; final observed free kernel heap is 11,992 B.
The resumed 25.56-minute capture contained no reboot, fatal OOM, SD error or
watchdog report, but seven recovered BLE supervision timeouts and one nonfatal
oversized-image drop remain open. Capture stopped. Display optimisation and CP
latency-transition work are implemented. **Language-pack work is closed by
operator direction on 2026-10-22.** See [current status](current-status.md) for
measurements, evidence and remaining work.

## Historical implementation status — 2026-10-18

Source baseline: `08a8212e8`, with the battery validity/ADC repair and worker
consolidation in the working tree. The port merged newer upstream
`main` in `9b6537971`; `a53522389` adapts GPIO, I2C, RTC, battery, touch and
QMI8658 integration to the current driver APIs. It also corrects the Armv8-M
MPU restore selection and bounds native unregistered-task logging state.
The unused alternate syscall header and duplicate multiwatch images are removed;
`resource_storage_flash.c` matches the compared upstream `b3a2dcb88`.

GATT discovery is consolidated into the shared engine in `ab3616d5e`. The full
source sanitizer harness passes 20 scenarios at each of two lease settings,
four native GATT suites pass, and the broad tools run passes 280 entries with
one ARM SDK/QEMU fault-entry test skipped. The Unhygienix firmware built and
linked, with existing LTO warnings. These are recorded checks for that candidate,
not a fresh broad run against the later persistence experiment.

The latest AP image recorded in this October 18 section was battery build
`779f3e5a1bcaeed66d06043ead303ab3c91254ed`, flashed AP-only on 2026-10-18.
ADC and operator control checks pass; KernelBG watchdog stalls remain open.
See the battery section below for the final capture and limitations.

The preceding hardware-tested touch AP image is the CST92xx KernelBG/native-input candidate,
build ID `69549c57d6ef588a96516b36640d53b18e705d20`, flashed AP-only on 2026-10-18.
The booted ID matches the frozen ELF; encrypted bonding and the phone session
restored, with discovery completing in 3,759 ms. The operator confirms taps,
drags/swipes, clean liftoff and sleep/wake touch work normally, and reconfirms
normal touch after a KernelBG watchdog warning/recovery. The image logged a
5,952 ms BG feed delay with App throttling for FdF Time, then recovery. No later
AP boot was decoded. A later watchdog channel warning lacks matching feed-age or
recovery records before capture ends. The cause remains open; this is not clean qualification.
A separate recovery record received before flashing belongs to the preceding run.
First-failure prefix and full trace retained; capture stopped. Filesystem,
pindb-mutex, dropped-event, battery and app-animation findings remain recorded.
No numerical touch latency, stack high-water or peak heap was measured. Evidence:
`/home/baz/Documents/ChatGPT/pebble/artifacts/cst92xx-kernelbg-20261018/flash/report.txt`.

The preceding installed AP image was the heartbeat-consolidation candidate, build ID
`c42b19caafdddfeef102c7d1eec3182da1fc0fcb`, flashed AP-only on 2026-10-18.
The booted ID matches the frozen ELF. Stored bond keys registered, the phone
protocol session opened and discovery completed in 4,213 ms. The operator confirms
connection, the selected watchface and working touch/buttons. Filesystem,
pindb-mutex, dropped-event, battery ADC and AppInbox warnings/errors were retained.
No later boot or task-watchdog expiry was decoded in the bounded capture; this
does not close the earlier watchdog finding or qualify heartbeat timing/stack
headroom. Capture is stopped. Exact images, raw trace and limits:
`/home/baz/Documents/ChatGPT/pebble/artifacts/heartbeat-mailbox-20261018/flash/report.txt`.

The preceding installed AP image was the native T5AI kernel-object candidate, build
ID `e570b7959ef3ff01ffe5b51e68c35460478146fc`, flashed AP-only on 2026-10-18.
Its booted ID matches the frozen ELF. The watch restored encrypted bonding,
opened the phone protocol session and completed discovery in 4,275 ms. The
operator confirmed connection, the selected face and working controls. CP,
resource bank and persistent storage were not selected for programming.
A KernelBG watchdog feed delay of 5,984 ms triggered app throttling, followed
by a watchdog recovery record. The cause is unresolved; peak heap, repeated
reconnect and broader driver qualification remain open. Capture is stopped.
Exact images, raw trace, warnings, operator observation and limits:
`/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/flash/report.md`.

The preceding installed AP image was the PFS system-resource restoration candidate,
build ID `50d14c63d1336e77edb840d723bfefc230982883`, flashed AP-only on 2026-10-18.
The booted build ID matches the saved candidate. Startup accepted the system
resource bank, registered the stored bond and opened reversed PPoGATT. CP,
resources and persistent storage were not selected for programming. Capture is
stopped; PFS/app warnings were retained. Language-pack follow-ups are now closed
by operator direction; the original capture did not qualify peak heap/UI behavior.
Exact images, raw trace, matching dictionary and limits:
`/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-system-resources-20261018/flash/report.md`.

The preceding installed AP image was the stock shell-init candidate, build ID
`6e39f48175667f6dce5d6e419b8581a805ebed62`, flashed AP-only on 2026-10-17 at
15:51 London time. First boot logged DB compaction completion and seven successful
individual results; reminder/health results and the start record were not captured.
The operator confirmed connection, responsiveness and the selected face. A
separately commanded non-writing reset reconnected without observed repeat
compaction. Startup UI-delay and existing storage/resource/battery warnings remain.
Capture stopped. Exact images/raw trace and limits are preserved at
`build/traces/stock-shell-init-flash-20261017-155019/analysis.md`.

The preceding installed AP image was the stock battery-UI candidate, build ID
`36ce226a40bbc90127f02ba1505313be86977367`, flashed AP-only at 15:40 London time
on 2026-10-17. The saved ELF matches the booted build ID. Normal Bluetooth
persistence remains selected; battery events now use the stock UI handler.
Capture is stopped. This run confirms flashing/boot, not visual charging-modal
behavior or battery calibration. See
`build/traces/stock-battery-ui-flash-20261017-153945/analysis.md`.

The preceding normal-persistence candidate was flashed with build ID `28416c29102c08546e1b83d6c23826ea3c476133`, on 2026-10-17 at 15:23 London time.
The trace restored encrypted bonding, opened reversed PPoGATT and completed GATT
discovery in 6890 ms. The operator confirmed responsiveness and, after a requested
USB cold power cycle, the same face selected with phone connected and responsive.
A host port-busy failure caused a 56.218-second capture gap after the second boot
banner; the cold-cycle reconnection relies on operator observation. No fatal or
watchdog event was decoded in captured portions. PFS/resource/app/animation and
weather/timeline warnings remain. Capture is stopped. Exact images, matching
dictionary, raw trace and limitations are preserved at
`build/traces/persistence-test-20261017-152214/analysis.md`.

The installed configuration has investigation tracing/profiling and task CPU
metrics off, QMI acquisition enabled with its legacy path off, twelve-byte I2C
receive FIFO enabled, and the SDK compatibility cache enabled. Previous notes
that the October 11 diagnostic image remains installed are superseded. The
normal-storage candidate is now installed; the earlier GATT candidate used PRF.
Watchdog/full-sync failures and the paused flash bypass remain open as described
below. No merge or release qualification is established.

Source update — 2026-10-17: battery state events now use the stock battery UI
handler on T5AI; the SoC charging-UI bypass is removed at the operator's request.
Both existing UI suites pass (14 cases), and firmware packaging succeeds.
The candidate is now installed; the boot trace and limitations are linked above.
No battery driver/calibration or charger-control changes accompany this cleanup.

Source update — 2026-10-17: the remaining T5AI first-use and settings-compaction
exceptions are removed; `normal/shell_event_loop.c` now matches upstream exactly.
First-use handling records completion and requests sync once, without launching
an onboarding screen. The normal one-time KernelBG DB compaction is restored.
Four native init cases and the related storage/app-state suites pass; firmware
packaging succeeds. The candidate is now flashed. Completion and seven DB results were observed;
no repeat compaction appeared after the commanded reset. Missing results and
the startup UI-delay warning remain documented in the latest flash report above.

## Top priority cleanup for main and upstream — 2026-10-17

Complete the [upstream divergence cleanup TODO](../../../boards/unhygienix/tuya/REMAINING_TASKS.md#next-priorities--2026-10-14)
before proposing the port for merge to `main`. It references the active
2026-10-18 stock convergence review, separates general fixes for upstream,
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
Tracing has finished; the latest recorded installed image is the October 17
GATT candidate described above. The reset mechanism remains unresolved. The
[flash optimisation and robustness audit](flash_optimisation.md) ranks driver,
transport and settings rewrite changes, with the required measurements.

## Historical refactor handoff — 2026-10-09

Read the [detailed refactor handoff](refactor_handoff.md) before continuing.
It records completed extractions, earlier refactor slices and dated hardware
evidence. Use the current remaining-work list for scope and the October 17
status above for the latest recorded flash; its installed-state notes are historical.
The watch was boot looping before the October 9 erase-delay fix. Its short
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
| Pebble GATT service discovery | Shared engine installed; October 17 bounded reconnect/discovery smoke test passed (6499 ms); broader qualification pending |
| Pairing, bonding, reconnect, and Pebble protocol session setup | Physical RF PLL fix installed; 15-minute connected run, bonded hardware-reset reconnection and 10-hour connected-idle observation passed; broader reconnect and cold-boot qualification pending |
| Device identity | Phone recognizes the watch as Round 2; further compatibility remains under test |
| Sustained phone sync and watchface transfer | CP latency-transition fix passed two Pebblecraft transfers on October 21; seven recovered supervision timeouts remain in the October 22 capture. Sustained stability remains open. |
| Language packs and optional fonts | Complete; closed by operator direction on October 22. Historical watchdog evidence remains under general stability. |
| Sleep/wake and low-power operation | Shake wakes the timed-out display; deeper sleep, wake interrupts and low-power operation remain unvalidated |
| Writable PFS | Internal NOR PFS preserved; latest image uses optional 32 MiB four-bit DMA SD PFS. Metadata cache moved to PSRAM on October 22; performance/recovery qualification remains. |
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

Normal T5AI firmware now selects the existing normal Bluetooth backend, matching
stock boards. Recovery firmware continues to select PRF. The SoC exception and
temporary evaluation switch are removed, along with the T5AI-only airplane-mode
stubs in the PRF backend. No persistence implementation was copied or added.

The normal backend stores CCCDs, active-gateway state, cached capabilities and
airplane mode in `gap_bonding_db`. Its existing initialization imports the shared
PRF bond (encryption keys, flags and address-pinning requirement), local root
keys, device name and pinned address, and continues PRF synchronization. PFS
writes already fall within the existing T5AI flash allowlist; flash ownership
and geometry are unchanged. The installed candidate now uses normal storage.

The native `test_bluetooth_persistent_storage_migration` suite links the real
normal backend, shared PRF storage, settings and PFS over fake flash. Five cases
check import/repeated initialization, CCCD/gateway/settings/capability restoration,
unchanged PRF bytes, stale-bond replacement and PRF-deletion re-export. All three
persistence suites pass (17 cases). The normal-backend evaluation image built
and packaged successfully after correcting the Python/nanopb tool mismatch;
its preserved build log includes the initial missing-`protoc` failure.

A confirmed rollback limitation remains: forgetting a phone in PRF firmware
clears the shared record, but a later normal boot can export an older normal
bond back into PRF. On 2026-10-17 the operator explicitly accepted re-pairing as
recovery, so preserving old pairings is not a blocker to this cleanup. Re-pair
through normal firmware if necessary; clear matching phone-side pairing state
when needed. PRF firmware does not use normal-only CCCD/settings records.

The bounded AP flash and operator cold-cycle check above passed. Broader hardware
qualification remains open: independently verify CCCD restoration, gateway state,
capability changes and airplane-mode persistence across a true cold power cycle.
Record settings-write latency and heap low water. Repeat reconnects and watchface
install/launch/selection checks, preserving the first failure trace. Host
initialization tests do not establish real power-loss or CP flash behavior.

This does not validate every storage failure mode. Retention beyond the bounded check,
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

The T5AI 120-second `CONFIG_BT_GATT_DISCOVERY_LEASE_SECONDS` default override
was removed on 2026-10-18. T5AI now inherits stock zero, retaining the firmware's
normal response-time lease policy; successful discovery still applies the common
10-second tail. This setting controls connection responsiveness, independently
of the ATT procedure timeout. The SDK compatibility cache remains enabled and
display profiling remains disabled. No alternate T5AI discovery implementation
remains. The full-source host harness now also covers a database of 24 services,
96 characteristics and 288 descriptors under both zero and 120-second settings.
Device comparison and qualification limits are recorded in
[the lease comparison report](/home/baz/Documents/ChatGPT/pebble/artifacts/gatt-lease-zero-20261018/report.md).

The full-source host harness exercises zero and 120-second lease settings with
allocation accounting and AddressSanitizer/UndefinedBehaviorSanitizer. The
four existing native GATT discovery/accessor/subscription/Service Changed
suites also pass. The October 17 watch smoke test above additionally verifies
one encrypted reconnect and discovery sequence. Broader reconnect and cold-boot
qualification remain outstanding.

## Native T5AI kernel objects — 2026-10-18

The allocating T5AI kernel adapter is removed. At the initial conversion, five
worker threads acquired native thread objects and aligned stacks. Mailbox/heartbeat/battery/touch keep their
previous stack capacities in internal SRAM; HCI retains its PSRAM stack. Native
mutexes/semaphores are embedded in their owning mailbox/socket/microphone state
or defined statically. Common kernel code and scheduling policy are unchanged.

The conversion removes 26 persistent heap allocations (9,044 requested bytes,
excluding allocator overhead). Static RAM rises by 8,768 B to 106,048 B; available
linker heap capacity becomes 20,928 B. That is a storage-ownership change, not a
measured reduction in peak live RAM. Nine focused ownership/timing/touch cases,
the broader 283-entry tooling suite (two skips) and the existing native kernel
suite pass; firmware packaging succeeds with existing LTO warnings.

Stock HCI also uses fixed native stacks/objects. Stock touch and battery instead
reuse system-task callbacks. Subsequent heartbeat/touch changes removed their
private stacks; the battery candidate now removes the last of the original
6,144 B of private heartbeat/touch/battery stacks. Their individual sections
record the current implementation and qualification state. The static increase is largely a heap-to-static transfer, but does not
prove equal startup/peak headroom. See the comparison and accounting at
`/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/stock-comparison.md`.

The candidate is flashed AP-only; its exact boot ID, encrypted reconnection
and discovery were captured. The operator confirms the selected face and controls.
A KernelBG feed-delay warning and subsequent recovery remain documented in the
latest flash report above. Repeated startup/reconnect, battery/microphone behavior
and peak heap still need qualification. Exact candidate/build ID and test limits:
`/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/validation.txt`.

## CP heartbeat worker consolidation — 2026-10-18

The private CP heartbeat thread and its 1,024-byte internal stack are removed.
The existing mailbox worker services the heartbeat deadline before each wait,
including busy RX/ACK/reset/TX retry iterations. Runtime initialization explicitly
wakes the owner; its wait is bounded by the heartbeat deadline and the existing
physical-transaction timeout polling interval. The first heartbeat is immediately
eligible after runtime readiness. Late scheduling skips missed intervals rather
than issuing catch-up bursts.

Heartbeat submission and physical/logical completion now share the mailbox task.
The wire format, one-second interval, asynchronous channel-owned message copy,
pending-response parameter lifetime and fault handling are preserved. There is
no timer-service dependency or added blocking operation. Touch, battery, HCI and
the mailbox worker's stack size and priority are unchanged; common kernel code
is unchanged.

The packaged candidate is `c42b19caafdddfeef102c7d1eec3182da1fc0fcb`.
Compared with the saved native-object image, static RAM falls by 1,152 B to
104,896 B and linker heap capacity grows from 20,928 B to 22,080 B. Flash remains
1,596,880 B. This includes object/state/alignment changes as well as the stack;
boot/peak heap and the combined mailbox stack high-water remain unmeasured.

Host validation: 284 tooling entries pass with two skips, including a new harness
using the actual mailbox loop, heartbeat functions and async submission. It covers
idle activation, busy RX/ACK/reset/TX paths, physical polling, pending logical
responses, channel-busy rejection, fault suppression, late execution and 32-bit
tick-boundary crossing. These are host models, not physical scheduler or deadline
measurements. Firmware build and measured RAM accounting are recorded at
`/home/baz/Documents/ChatGPT/pebble/artifacts/heartbeat-mailbox-20261018/validation.txt`.

This candidate is flashed AP-only with its boot ID verified. The operator confirms
connection, the selected face and controls. Phone protocol reconnection/discovery
passes in the bounded startup trace; warnings/errors remain recorded in the latest
flash report above. Verify cold power-cycle behavior, encryption changes, heartbeat
gaps during traffic/UI load, mailbox stack high-water and sustained watchdog
behavior. The earlier KernelBG feed-delay finding remains open.

## Battery ADC repair and worker consolidation — 2026-10-18

The current candidate removes the private 2,048-byte battery stack/thread.
The stock voltage service drives sampling; the SoC-local SARADC client uses
bounded asynchronous MB-IPC phases on the existing mailbox owner. KernelBG and
NewTimer never wait for CP ADC conversion or IPC. The whole acquisition has a
5-second deadline; ambiguous failures retain static CP-visible storage until
reset. Successful raw replies complete READ_RAW_DONE even when their data fails
CRC/size checks, then clean up only acquired resources. Shared physical-slot
serialization preserves synchronous flash progress while ADC traffic is active.

Wiring/calibration are board data. A native charge IRQ and shared regular timer
provide debounced connection events, GPIO reconciliation and dropped-callback
recovery. The previous validity correction remains: no fabricated startup voltage,
failed/stale readings return unavailable, and the voltage service skips failures.
CHRG remains a charging proxy; it cannot distinguish full/plugged from unplugged.
No battery is fitted on the operator's board, and the inherited curve is unqualified.

All 299 tooling entries pass with two skips; all 15 native battery-monitor cases
pass. Static RAM is 99,968 B (1,760 B recovered), with 27,008 B linker heap.
These are linker budgets, not measured peak usage. See [subsystem review](battery-review.md).

Final build `779f3e5a1bcaeed66d06043ead303ab3c91254ed` was flashed AP-only
on 2026-10-18 and its boot ID verified. The 300.478-second UART capture received
23,438 bytes, with four successful ADC samples (4,011–4,025 mV, 6–7 ms), zero
ADC errors and one AP boot. The operator confirms watchface, phone connection,
taps/swipes, buttons and sleep/wake all work normally. Encrypted bonding and
phone sessions restored; two discovery completions took 5,059 and 5,265 ms.
The trace still contains 21 KernelBG feed delays, 23 watchdog recoveries and
25 channel warnings. These counts are separate record types, not paired stall
counts; this is not clean watchdog qualification. Capture is stopped.

The first candidate reset after synchronous logging from the mailbox owner
waited on that same owner. ADC diagnostics now log from KernelBG instead.
The next image exposed an inherited checksum error: vendor CRC tests bit 7
of an unmasked 32-bit accumulator, while the port tested bit 31. Independent
vendor vectors and CP-model checks now cover that correction. Failed images,
rollback to the previously tested touch build, and each trace remain preserved.

No cell is fitted: these voltages measure the unloaded charger node, not cell
calibration or battery percentage accuracy. Cold power-cycle, sustained watchdog,
read-done timing under load, stack high-water, peak heap and fitted-cell/meter
qualification remain open. Final evidence:
`/home/baz/Documents/ChatGPT/pebble/artifacts/battery-consolidation-20261018/vendor-crc/flash/report.txt`.

## CST92xx stock touch scheduling — 2026-10-18

The flashed CST92xx candidate follows CST816's coalesced,
`system_task_add_callback_from_isr_droppable_raised()` dispatch to KernelBG and
its synchronized `pbl_input_report_key/abs()` sample sequence. The driver no longer
calls the touch service directly, inspects application event queues or imposes a
separate 33 ms motion limit. The private thread, wake semaphore and 3,072-byte
internal stack are removed. Shared input/touch service and CST816 source are
unchanged. Native input delivery follows stock event-queue behavior; the old
private-worker backpressure policy is deliberately removed and needs load testing.

Controller-specific behavior stays local: the CST9217 report layout, scaling
of the 466-pixel coordinates, panel mirroring, IRQ/reset pins, 100 kHz bus and
5/50 ms reset timing are preserved. A native mutex serializes hardware operations;
input listeners run without it to avoid inversion with the touch-service lock.
Callbacks carry an enable-generation token so old queued/timer work cannot read
or reset a newly disabled/re-enabled controller. Read errors release contact via
native input, as CST816 does; the existing three-failure reset threshold and
80/1,000 ms retry intervals remain.

Unlike CST816's controller-specific idle-reset watchdog, CST9217 retains a
one-shot 20 ms contact/asserted-line fallback to recover lost liftoff edges. A
one-second shared regular timer retries a dropped callback or a missed asserted
IRQ only when no fallback is armed. It reads the IRQ pad but does no idle I2C or
periodic reset. Both timers only enqueue KernelBG work; no I2C, sleep or reset
runs on ISR/NewTimer. The retry timer uses one slot from the existing fixed timer
pool; the shared regular callback uses a static 24-byte registration.

Candidate build `69549c57d6ef588a96516b36640d53b18e705d20` builds/packages and
passes 287 tooling tests with two skips. Four rebuilt native touch/input suites
also pass (57 cases). Five full-driver host cases cover coalescing, input framing/coordinates/liftoff,
queue rejection and timer recovery, I2C failures/reset, interrupts during reads,
and stale callback/timer cancellation. These are modeled interleavings, not real
scheduler tests. Static RAM falls from 104,896 B to 101,728 B (3,168 B saved);
linker heap grows from 22,080 B to 25,248 B. Flash grows by 104 B to 1,596,984 B.
This does not measure peak heap, stack depth or latency. Exact candidate ID,
images, source, tests and limits:
`/home/baz/Documents/ChatGPT/pebble/artifacts/cst92xx-kernelbg-20261018/validation.txt`.

The candidate is flashed AP-only with its boot ID verified. The operator reports
normal taps, drags/swipes, clean liftoff and touch after sleep/wake, and confirms
normal touch again after a KernelBG watchdog warning/recovery. Encrypted bonding
and phone session restoration pass. A 5,952 ms KernelBG feed delay triggered App
throttling for FdF Time and then recovered; no later AP boot was decoded. The
first-failure prefix and complete capture are retained. A later watchdog channel
warning lacks matching feed-age/recovery records before capture ends. This does not identify
causation or close the existing watchdog finding; the combined run is not clean.
Capture is stopped. Exact flash scope, observations and warnings:
`/home/baz/Documents/ChatGPT/pebble/artifacts/cst92xx-kernelbg-20261018/flash/report.txt`.

Queue saturation, measured latency, forced I2C reset recovery, enable/disable under
load, cold power-cycle, peak heap and KernelBG stack high-water remain open. The
current image has neither shell nor the T5AI BulkIO memory endpoint available for
stack inspection; no bypass was attempted. Raised-priority callbacks still wait
behind existing KernelBG work. Broader hardware qualification remains incomplete.

## PFS system resources — 2026-10-18

Normal firmware now uses stock system-file routing and watch/unwatch behavior.
The existing `lang` file supplies translations and optional font extensions;
`pug` is an optional file-backed test resource. English strings come from the
firmware, and base fonts remain in the system bank. An empty PFS therefore needs
no seeded language pack or migration. The normal named-file PutBytes transfer
path installs `lang`; Settings can select English or the installed language.

File watches are registered even before a pack exists, so installation, replacement
and removal invalidate language/font state without rebooting. Unwatch now releases
those PFS callbacks through the stock backend. Corrupt or truncated language
resources are rejected by the existing validation and retain the English/base-font
fallback. The immutable bank layout, validation policy and update gates are
unchanged by this restoration.

Eight new native integration cases use real PFS and the T5AI bank backend with
host flash fixtures. Together with existing language/font suites, 42 cases pass;
23 resource/image tooling tests also pass. Firmware builds/packages successfully
with 608 additional flash bytes and unchanged static RAM; the system resource
pack is unchanged. Candidate build ID: `50d14c63d1336e77edb840d723bfefc230982883`.
The candidate was flashed AP-only on 2026-10-18; its booted build ID matches.
Startup accepted the system resource bank and opened a phone protocol session
with the stored bond registered. Capture is stopped; the flash report above
records remaining warnings and evidence limits. Language-pack work, including installation/removal and language/font UI follow-ups,
is closed by operator direction on 2026-10-22. This adds no new device measurements;
general peak-heap and stability qualification remain separate. Matching candidate and validation records are at
`/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-system-resources-20261018/`.

The startup "Couldn't resolve last written pg" warning can be emitted by a
zero-page layout probe even when the on-flash marker is valid. Native restart
probes preserve marker page 6 and warn only at zero pages; a controlled missing
marker also warns on nonempty scans. Exact installed-candidate disassembly
confirms this false-positive path. This does not independently verify the
watch's physical marker; no firmware or storage repair was applied. See
`/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-marker-investigation-20261018/report.md`.

## Accelerometer and motion wake progress (2026-10-04)

The QMI8658 driver previously delivered samples but left shake detection and
sensitivity functions as no-ops. The driver now detects motion in software and
calls Pebble's existing `accel_cb_shake_detected()` path. All production changes
are local to `drivers/imu/qmi8658/qmi8658_t5ai.c`; shared Pebble services
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
- `drivers/t5ai/cp_mailbox.c`: mailbox ownership and ACK serialization.
- `drivers/imu/qmi8658/`, `drivers/i2c/`, `drivers/gpio/` and
  `drivers/display/t5ai/`: current native driver integration.
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
