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

The unselected `bluetooth_persistent_storage_t5ai.c` backend was removed. T5AI
uses the PRF backend selected in the Bluetooth service build, whose comments
describe the current protected-flash write constraint.

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

### 6. Deferred: resource and update capability cleanup

Do not remove the T5AI guard in `resource_unwatch()` mechanically. The stock
function assumes every callback belongs to `g_file_impl`, while T5AI omits that
system-file resource implementation and uses direct-mapped system resources.
The deeper cleanup should make the callback handle retain its owning resource
implementation, or provide a selected platform implementation, so unwatching
does not depend on a hard-coded backend.

Firmware update, PRF update, core dump, and factory reset also need capability
cleanup rather than simple guard removal. T5AI currently has no validated OTA
container, install slots, rollback path, recovery image, or persistent core-dump
region. Express those unavailable capabilities through build-time source
selection or explicit stubs, then restore the shared control flow to stock.
Validate update rejection, factory reset, and interrupted-reset recovery before
accepting that work.

### 7. Validation and acceptance

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

The next candidate restored the stock shell first-use block while leaving the
settings-DB compaction migration and battery UI excluded. It passed all 95
focused tests and built successfully, but it is not hardware-accepted. Earlier
T5AI firmware never set the getting-started-complete preference, so the first
upgrade boot looked like a factory reset, marked Bluetooth storage unfaithful,
and triggered a full phone resync. During the resulting AppDB flush,
`pfs_remove_files()` asserted because `@00000033/res` was still in use. A
non-flashing second boot was stable, proving that the preference persisted,
but every existing device would still encounter the unsafe transition once.
The candidate was therefore reverted. Keep the T5AI first-use exclusion until
full resync can safely retire resources that are held by the launcher or a
running app. The failing and second-boot traces are
`/tmp/pebbleos-onboarding-first-boot-dvt3.log` and
`/tmp/pebbleos-onboarding-second-boot-dvt3.log`.

The rolled-back source again passes all 95 focused tests and builds with FLASH
878,227 bytes. It was restored to the watch over the AP-only range
`0x132000..0x215d0a`; the raw SHA-256 is
`34b20f6580d53e6ea3afc34eb1aeecfb87d865fd74716a63d9cbb90e41acc3e0`
and the protected SHA-256 is
`0896397f19d8dda86aae317a6ddcd346909ce870b5653b4d50b3856382e1b017`.
A final 60-second capture completed AppDB and BlobDB flushes without an
assertion or reboot. The rollback trace is
`/tmp/pebbleos-onboarding-rollback-dvt3.log`.

The unused volatile T5AI Bluetooth persistence backend was then deleted. It
was never selected by the build; T5AI continues to use the bounded shared-PRF
backend. A focused test now requires both that the build selects the PRF source
and that the dead alternative file remains absent. This cleanup is intended to
produce no firmware behavior or image change beyond build-version metadata.

The semantically redundant nested condition in `app_cache_entry_exists()` was
restored exactly to stock. The T5AI-specific 300 KiB cache reserve remains: it
is a storage-capacity policy and is not part of this behavior-neutral cleanup.
Resource-unwatch and update/reset conditionals are explicitly deferred to the
capability work above rather than being removed mechanically.

The remaining behavior-neutral shared-file drift was also reduced: launcher
menu spacing and the built-in resource store's equivalent
`CONFIG_RECOVERY_FW` spelling now match stock. Existing final-newline-only
differences are retained because recreating stock's trailing blank lines or
missing final newline fails `git diff --check` and has no runtime value. The
`CONFIG_MAG` orientation guard and `CONFIG_MIC` reminders guard remain because
they express real T5AI hardware capabilities rather than bring-up policy.

The redundant temporary around `pfs_active()` in `pfs_init()` was removed, so
the filesystem activity check again matches stock exactly. The neighboring
FreeRTOS interrupt-priority conversions, lazy debug-serial batching, and
invalid-flash-log record consumption remain: those are functional Cortex-M33
or T5AI transport changes and require separate validation or localization.

Boot-splash ownership is now selected at build time. The shared
`boot_splash/service.c` is stock again, while T5AI's bounded row renderer lives
in `service_t5ai.c`. Both implementations preserve the service contract that
`boot_splash_start()` initializes the display, so the T5AI startup block no
longer calls `display_init()` directly. The splash asset and display interface
remain shared; framebuffer allocation and transfer strategy stay platform-local.

The extracted splash image passed all 95 focused tests and built with FLASH
878,227 bytes. The guarded tool flashed only `0x132000..0x215d0a`; the raw
SHA-256 is
`f5c2dfcc040d05e67ab7661739c189007dd71c2ca699d1358dd2ae560c2afb02`
and the protected SHA-256 is
`57549598efae2eca8fe0c238e40a9a2d8c369eeeb5dc58155c73ef9d02f70010`.
A non-flashing reset then reached the launcher and runlevel 4, restored the
encrypted phone session, flushed BlobDB, and completed AppFetch with result 0
without an assertion, fault, or AP reboot. The accepted runtime trace is
`/tmp/pebbleos-boot-splash-extraction-dvt3.log`; physical splash appearance
still requires visual confirmation because it is not observable over UART.

Debug-console ownership is now also selected at build time. The shared
`dbgserial.c` and `dbgserial_input.c` implementations are stock again, while
T5AI's buffered CP-mailbox output and intentionally disabled input live in
`dbgserial_t5ai.c` and `dbgserial_input_t5ai.c`. This is an ownership-only
extraction: the 159-byte mailbox batch, interrupt-context log dropping, and
single flush per completed message are preserved.

The shared `serial_console_write_log_message()` lazy-write batching remains a
deliberate deviation from stock. Reverting it would turn each character into a
blocking mailbox transaction on T5AI. Moving that requirement behind a bulk
debug-output API may be worthwhile in a deeper console cleanup, but expanding
the common interface is not justified for this extraction.

### Debug-console extraction hardware result — 2026-09-19

All 167 tool tests passed and the firmware built with FLASH 878,227 bytes and
KERNEL_RAM 75,232 bytes. The guarded flasher programmed and reprotected only
`0x132000..0x215d0a`; the raw image SHA-256 is
`afb4837e3e3623eddf14fde0ab00713a879a083ae205ecae85ae41a45882b188`
and the protected image SHA-256 is
`f4c0d219fbe1ff6300337af409dbd2d0492374e71aa73009d7e9761506c78311`.

A controlled reset continued to emit complete mailbox-backed logs, reached
the launcher and runlevel 4, restored the encrypted phone connection, opened
a reversed PPoG session, and completed GATT discovery. The final HCI snapshot
reported `fault=0`; there was no assertion, hard fault, or uncommanded AP
reboot. The accepted trace is
`/tmp/pebbleos-debug-console-extraction-dvt3.log`. AppFetch was not requested
during this capture, so the previously accepted priority/AppFetch validation
continues to cover that path.

Compositor framebuffer ownership is now platform-selected. The common
compositor retains only a narrow storage-preparation seam and otherwise
returns to stock formatting and control flow. T5AI's PSRAM power vote,
destructive handoff validation, aligned framebuffer object, and linker-section
placement live in the board-local `boards/compositor_framebuffer_t5ai.c`;
other boards select the ordinary internal-RAM provider from the same board
layer. The Getafix full-frame update workaround is
now controlled by `DISPLAY_FORCE_FULL_FRAME_UPDATES`, so the CO5300 driver's
partial-update capability no longer appears as T5AI policy in the compositor.

All 167 tool tests passed. The image built with FLASH 878,227 bytes and
KERNEL_RAM 75,232 bytes, and the map places only the 67,614-byte compositor
object at `0x60000000..0x6001081e`. The guarded flasher programmed only
`0x132000..0x215d0a`; the raw SHA-256 is
`be726d43251557d43742a2e473a36f827717055a0f3ffd612415a62e9b23dbf3`
and the protected SHA-256 is
`10c4740ee0ecce5fbc027646539322eda9ffebb604e6715d82af315c5f96818b`.

The first reset capture observed one HardFault immediately after an unusually
long Timeline/BlobDB flush. It is retained as
`/tmp/pebbleos-compositor-provider-dvt3.log`. A second no-reset soak did not
reboot, and the decisive 150-second controlled-reset capture repeated the full
settings, AppDB, and BlobDB flush path, completed AppFetch with result 0, and
then remained stable without an assertion, flash fault, or AP reboot. That
accepted reproduction capture is
`/tmp/pebbleos-compositor-provider-dvt3-reset2.log`. Visual launcher-transition
appearance still requires confirmation on the physical display.

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
