# Getafix DVT3 shared-code cleanup

## Goal

Reduce the DVT3/T5AI port to the smallest practical delta from stock PebbleOS.
Keep hardware and SoC behavior in board-, SoC-, or driver-local files. Changes
to shared code must be either a narrow integration seam or an independently
valid stock fix with tests.

The comparison baseline for this audit is local `main` at `2a2844774`. At the
start of cleanup, the `tuya` branch changed 280 files: 133 additions and 147
existing stock files. Eighty-nine modified files were production sources
outside obvious build, documentation, and test glue, and 69 modified stock
files contained T5AI-specific conditionals.

## Findings

### Shared startup contains a platform implementation

`src/fw/main.c` contains roughly 460 lines of T5AI-specific diagnostics,
mailbox commands, retained-failure reporting, clock and power policy,
Bluetooth startup, and an alternate service initialization sequence. This is
the largest source of secondary shared-code changes. T5AI policy should move
to `soc/t5ai` behind a small number of generic lifecycle hooks. Stock startup
ordering should remain unchanged unless a hardware requirement is documented
and tested.

### Scheduler and queue workarounds change core semantics

The branch replaces the stock KernelBG queue-set path with a T5AI semaphore,
raises KernelBG to maximum priority, changes app priority, changes event-queue
reset behavior, and changes parts of the launcher event loop. These were
bring-up workarounds for starvation and lost-wakeup symptoms. Restore stock
behavior and fix any remaining defect in the T5AI FreeRTOS port, interrupt
configuration, timer implementation, mailbox transport, or peripheral driver.

The event queue-set capacity correction may be a valid generic stock fix, but
it must be separated from the port and tested independently. The larger queue
drain/rebuild algorithm must not remain merely because it was developed during
T5AI bring-up.

### Diagnostics remain in shared event, touch, and UI code

The T5AI command probe in `main.c` is the only consumer of several diagnostic
APIs and counters added to shared event, touch, event-loop, and recognizer
code. Removing the probe should also remove those shared counters and APIs.
Persistent, generally useful diagnostics need a separate design and review.

### Some shared edits are not used by the T5AI build

T5AI selects `gatt_client_discovery_t5ai.c` and explicitly excludes the stock
`gatt_client_discovery.c`, yet the stock implementation was extensively
rewritten. Restore the stock file.

The common I2C layer contains T5AI-specific timeouts, retry limits, logging,
and bit-bang recovery. The recovery entry point has no active caller. Restore
the common layer and keep CST92xx error policy in the T5AI touch driver.

`bluetooth_persistent_storage_t5ai.c` exists but is not selected; the build
uses the PRF backend instead. Choose one backend, remove the dead path, and
ensure comments describe the selected persistent-storage behavior.

### Shared product behavior is disabled for bring-up

T5AI conditionals suppress launcher glances, animation behavior, onboarding,
settings migration, battery UI, service runlevels, watchdog behavior,
firmware update, and parts of factory reset. Restore stock product behavior.
Unsupported hardware capabilities should be expressed through Kconfig and
selected implementations or stubs, not scattered exclusions in shared code.

### Valid platform requirements are implemented in shared services

The following requirements appear valid but should be localized:

- PSRAM-backed compositor framebuffer allocation and validation;
- T5AI boot-splash rendering;
- direct-mapped system-resource access;
- mailbox-backed debug serial;
- retained boot-bit and reboot-reason storage;
- unsupported OTA and persistent-coredump policy;
- T5AI Bluetooth discovery and persistent storage.

Prefer build-time source selection. If a shared hook is unavoidable, make it
generic, small, and supplied by every platform through a stock default.

### Compatibility identity needs explicit ownership

DVT3 currently advertises the DVT2 public platform identity and fabricates a
recovery metadata record when no Pebble PRF is present. The public identity
mapping may be necessary for phone compatibility. Fake recovery state should
be replaced by a protocol capability or isolated compatibility adapter rather
than changing the meaning of shared version data.

### Generic fixes are mixed into the port

Several changes may be valid for stock but should be reviewed as independent
fixes rather than hidden in the DVT3 delta:

- PULSE packet validation and BulkIO bounds/lifetime hardening;
- NimBLE pairing-context lifetime handling;
- CMSIS/FreeRTOS interrupt-priority conversion;
- event queue-set sizing and reset cleanup;
- PRF pairing enumeration and missing interface functions.

Each should be a separate commit with non-T5AI tests and must not be a hidden
dependency of the board port without evidence.

### Documentation assertions need mechanical verification

Existing handoff documents describe the shared event loop, app manager, GATT
discovery, and common I2C layer as stock, but all differed from the audited
baseline. Final validation must compare these files mechanically rather than
relying on prose.

## Cleanup sequence

### 1. Remove instrumentation and obsolete experiments

- Remove the T5AI command/probe diagnostics from shared startup.
- Remove their event, touch, recognizer, and queue diagnostic APIs and counters.
- Restore stock common GATT discovery.
- Restore stock common I2C behavior and remove unused recovery plumbing.
- Remove the reset-stage UART marker.
- Restore stock scheduler, queue, and app-priority behavior, then retain only
  any T5AI priority ordering demonstrated necessary by hardware.
- Remove dead implementations, empty preprocessor blocks, and incidental
  formatting churn.

Build DVT3 after this slice. Treat failures as evidence of an undeclared
dependency; do not automatically restore the workaround.

### 2. Restore stock runtime and product policy

- Restore launcher glances and normal animation selection.
- Restore shell onboarding, settings migration, and battery UI.
- Remove the remaining T5AI exclusions from the common-service runlevel table
  once accel, light/vibes, and HRM each have a valid T5AI implementation or an
  explicit unavailable stub. The generic runlevel machinery is already stock;
  do not rewrite it as part of this slice.
- Replace firmware-update, PRF-update, core-dump, and factory-reset conditionals
  with selected T5AI implementations or explicit unavailable stubs.

### 3. Extract T5AI startup and transports

- Move mailbox readiness, power/clock votes, retained-failure handling, and
  Bluetooth ordering from `main.c` into a T5AI platform module.
- Select T5AI boot splash, debug serial, resource access, Bluetooth discovery,
  persistent storage, and retained-state implementations at build time.
- Move compositor PSRAM ownership and validation behind a narrow framebuffer
  provider or platform hook.

### 4. Split generic fixes

Reapply each justified generic change independently with focused unit tests and
representative stock-platform builds. Drop fixes that cannot be justified
without the T5AI port.

### 5. Deferred: localize the T5AI priority exception

The hardware-proven priority ordering is required for now: T5AI KernelBG runs
at the blocking maximum priority and its app/render task runs at priority 1.
Do not remove or weaken that ordering during the current cleanup.

As non-urgent follow-up work, determine why stock priorities allow the CP flash
write used by AppFetch to fail. Instrument task readiness and the T5AI mailbox
route result narrowly enough to distinguish AP scheduling starvation from a CP
busy/rejection condition. Then move the policy into the T5AI FreeRTOS port,
board configuration, or another platform-local priority provider so
`services/system_task/service.c` and `process_management/app_manager.c` can
return to stock. Accept that cleanup only after repeated stock-window AppFetch,
launch, reconnect, and idle-soak tests pass without a flash IPC fault or AP
reboot.

### 6. Validation and acceptance

After every slice:

- build DVT3;
- build representative NRF52, SF32LB52, QEMU, and PRF configurations;
- run focused Tuya tests and relevant generic unit tests;
- run `git diff --check`;
- verify boot, display, touch, pairing, reconnect, persistent bond, stock-window
  watchface transfer, PFS access, and idle soak on hardware.

The cleanup is complete when generic app/UI, scheduler, event-loop, stock GATT,
common I2C, and console protocol code contain no T5AI policy. Every remaining
modified stock file must be a small registration/interface seam or an
independently justified generic fix with tests.

## Progress

The first cleanup slice is complete in the working tree:

- shared GATT discovery, I2C, event queues, KernelBG queue handling,
  touch service, touch navigation, and event-loop code are restored to stock;
- reset UART marking, mailbox command probes, clock and retained-flash probes,
  touch/queue diagnostic counters, display register dumps, the display
  bit-bang probe, and the destructive diagnostic pattern are removed;
- T5AI keeps only the local runtime behavior required for DVT3 operation,
  including its no-op debug-input backend, mailbox processing, 120 MHz vote,
  display transport, and touch-driver failure recovery;
- focused board/port tests pass (99 tests), `git diff --check` passes, and a
  clean `getafix@dvt3` configure/build succeeds.

The next slice is **Restore stock runtime and product policy** above. It has
not yet been started.

The common-service runlevel subtask is complete: DVT3 uses the native QMI8658
accelerometer and T5AI backlight, the existing no-hardware vibration driver,
and compile-time-disabled HRM. The shared runlevel table is stock again and
contains no T5AI policy.

The first flashed runlevel candidate exposed a T5AI-local display race. Once
the stock light entry enabled its timeout/fade state machine, each fade step
sent a synchronous AMOLED brightness command while framebuffer traffic could
be active. Two boots HardFaulted shortly after runlevel 4; the second interval
was about eight seconds, matching the light timeout and fade. T5AI still uses
fixed maximum brightness, so its backlight driver now treats software
brightness changes as state-only and leaves the `0x51 = 0xff` panel setting to
display initialization. The shared light and runlevel services remain stock.

The corrected image flashed successfully to the protected AP-only range
`0x132000..0x213bb4`. Its raw SHA-256 is
`7f8bd6446fd92044dac4bb14a61bdbfe94e885ebc23127951edd1ac527a91173`
and its protected SHA-256 is
`72cddb8b007ca36c87647982f4ac28a8f61e9f36899a52bc980f4f0b1f93c481`.
A 120-second hardware capture passed runlevel 4, encrypted Bluetooth and
reversed PPoG setup, multiple light timeout intervals, BlobDB work, and an
AppFetch completion with result 0. It contained no HardFault, assertion,
NimBLE host reset, flash IPC fault, or AP reboot. The accepted trace is
`/tmp/pebbleos-runlevel-backlight-fix-dvt3.log`.

Launcher cleanup restores the stock Gabbro 48 ms render cadence and removes
the T5AI branches that bypassed launcher glance initialization, drawing, and
selection playback. This intentionally lowers DVT3's animation render rate
before re-enabling glance work; no shell or storage policy changes are included
in this slice.

The launcher slice builds successfully with FLASH 878,227 bytes, KERNEL_RAM
75,232 bytes, and resources at 97.90%. All 95 focused cleanup tests pass and
`git diff --check` is clean. The guarded tool flashed only the protected AP
range `0x132000..0x215d0a`; the raw SHA-256 is
`9b20499b3dc180800f535994a5bd59a097fa88084816e3bc58a1b9582326349c`
and the protected SHA-256 is
`aa97d4e59406f9a6c0fdcaf2be803c47d054713af58ef295764a364ec0ba3f2d`.

A 120-second runtime capture reached the launcher and runlevel 4, exercised
launcher selections, and handled an encrypted Bluetooth reconnect plus reversed
PPoG session without a HardFault, assertion, watchdog, panic, or AP reboot.
Missing optional glance resources 613, 616, and 618 were reported as handled
resource-check warnings and did not destabilize the launcher. The accepted
trace is `/tmp/pebbleos-launcher-cleanup-dvt3.log`.

### Hardware validation result — 2026-09-19

The first cleanup slice is **not yet hardware-accepted**. The clean build was
flashed AP-only, preserving resources and PFS:

- raw firmware SHA-256:
  `c144262655cc4d2486fb71ef0c40243ad33add1e6d5f879f46a1a91f6bc9dba9`;
- protected AP SHA-256:
  `bdb29a4a9c0c46b857bba369b056a7e569c3b8601b51b3340f071379a5efb965`;
- exact physical range: `0x132000..0x213b0a`.

Boot and the initial phone path succeeded: encrypted bonded connection, MTU
256, reversed PPoG subscription, protocol session open, and settings sync. An
automatic app fetch then exposed a blocking failure:

- flash command 5 (`FLASH_WRITE`) returned `E_INVALID_OPERATION` (`-10`) for
  address `0x02427059`, length 167;
- `flash_write_bytes()` asserted at `flash_api.c:148` and rebooted;
- the next boot reset the NimBLE host three times with reason `0x0013`, then
  `bt_driver_start()` asserted at `init.c:164` because host start timed out;
- a third boot reached the launcher and reopened the encrypted phone session.

A later 20-second capture observed another startup sequence (`Checking bank 0
for system resources` and AppDB enumeration), so the board cannot yet be
considered settled even though it can reconnect between failures.

The complete UART capture is temporarily available at
`/tmp/pebbleos-cleanup-dvt3-soak.log`. Do not begin the second cleanup slice or
claim hardware acceptance until this app-fetch flash failure is reproduced and
attributed. The evidence does not yet establish which restored stock behavior,
if any, is causal.

### First corrective candidate

The failing boot retained the previously proven read-reconnect recovery, and
command 5 was the first flash IPC fault, so removing retained diagnostics did
not remove the functional flash recovery. The app-fetch callback also reached
`flash_impl_write_page_begin()`, which rules out the reverted KernelBG wake
semaphore as the immediate cause.

The only reverted behavior on the active path was T5AI task ordering. The
first corrective candidate therefore restores only the T5AI priorities:
KernelBG runs at the blocking maximum priority and the app/render task at
priority 1. The stock queue-set implementation, watchdog policy, event queues,
and all removed probes remain untouched. This is a deliberately narrow A/B
candidate and is not accepted until an AP-only flash completes a watchface
transfer and launch without a flash fault or reboot.

The candidate builds successfully with FLASH 870,067 bytes and KERNEL_RAM
75,232 bytes. Its raw image SHA-256 is
`b509efb1dcb103b3b72197c78e561a1e870fdd16df53e0e2a2a664ee73d8ae59`;
the validated protected image SHA-256 is
`ac33cfe722fc0b1803a755e2d4f327c30f916620848ca90765346edf0b465d23`
and covers only `0x132000..0x213b2c`. Hardware validation is pending because
both CH342F USB interfaces disappeared before flashing; do not substitute an
unresolved tty path or widen the programmed range.

### Priority-candidate hardware result

After the CH342F returned, the guarded flash tool wrote and reprotected exactly
`0x132000..0x213b2c`, then rebooted successfully. The phone restored its
session and retried the pending application transfer. At `13:03:13`, AppFetch
reported `App fetch cleanup with result 0`; the installed app then ran without
a flash fault, assertion, NimBLE host reset, or AP reboot. A later Bluetooth
disconnect snapshot also reported `fault=0`. The 150-second decisive capture
is `/tmp/pebbleos-priority-fix-dvt3-appfetch.log`.

This accepts the priority restoration as the fix for the cleanup regression.
It does not justify restoring the T5AI semaphore/queue implementation,
watchdog exception, event-queue changes, or removed instrumentation. The
subsequent `Moddable XS not supported` and missing AppInbox messages describe
unsupported application features in this build and did not reset the AP.
