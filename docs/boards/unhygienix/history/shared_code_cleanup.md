# Unhygienix shared-code cleanup

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](../current-status.md). Dated results below retain their original evidence limits.

This retained report records dated observations, not current operating instructions.
Use the [operator handoff](../index.md) and [tooling guide](../tooling.md)
for current procedures. Obsolete commands and machine-specific paths have been
omitted. Artifact labels are descriptive; external captures are not distributed
with this repository. Recorded dates, measurements and SHA-256 values are retained.


## Latest checkpoint — 2026-10-03

The [CP and storage checkpoint](cp_storage_checkpoint.md) records
implemented mailbox/CRC and NOR-cache fixes, 2.75 MiB PFS migration, six loaded
faces and five successful encrypted reconnects. Later diagnostic runs reproduced
supervision loss and an App privileged-code stall/reset. Root causes and reliable
Bluetooth recovery remain unresolved; a subsequent stable interval is not a fix.
The latest recorded image is diagnostic. Capture state is a dated observation,
not proof of current monitoring; verify device visibility and received bytes.
Cold-boot persistence, overnight soak and power behavior remain open.

See the [implementation-gap inventory](../index.md#implementation-gaps)
for explicit low-power/RTC-alarm, console-input, flash-logging, boot-version and
OTP placeholders, disabled coredumps/audio/optional hardware, and gated updates.
Native watchdog and primary peripheral paths are implemented; device identity
reports Round 2. Older image hashes and implementation claims below are historical.
Shared-code cleanup remains paused unless explicitly resumed.


## Goal

Reduce the Unhygienix/T5AI port to the smallest practical delta from stock PebbleOS.
Keep hardware and SoC behavior in board-, SoC-, or driver-local files. Changes
to shared code must be either a narrow integration seam or an independently
valid stock fix with tests.

The comparison baseline for this audit is local `main` at `2a2844774`. At the
start of cleanup, the `tuya` branch changed 280 files: 133 additions and 147
existing stock files. Eighty-nine modified files were production sources
outside obvious build, documentation, and test glue, and 69 modified stock
files contained T5AI-specific conditionals.

## Historical shared-code cleanup status — paused 2026-09-19

Shared-code cleanup is paused at the user's request. Do not start another
cleanup slice until it is explicitly resumed.

The accepted source state is based on `e68bf6ba1`. At that checkpoint the watch ran
the restored fast resource-validator build: raw SHA-256
`b98f7def2600b11a25582346e2de7d108a62299ca5704f5a36a64cd64ef421f9`,
protected SHA-256
`56f924a353426fc28417a89a1a802ec4efeaf7a6155042a4010a3b277619d37c`,
flashed only over `0x132000..0x215d0a`. The latest reset trace confirmed the
bounded resource check completed in the same log timestamp, followed by
runlevel 4, encrypted Bluetooth, reversed PPoG, and BlobDB activity without a
fault or unexpected reboot. That trace is
`external artifact: pebbleos-resource-validator-restored-unhygienix.log`. The complete tool suite
contains 168 tests and passes.

Accepted cleanup includes removal of obsolete instrumentation and diagnostic observation,
stock event/GATT/I2C/launcher/runlevel behavior where validated, board-local
backlight policy, build-selected boot splash and debug console, the narrow
display update capability, and SoC-local retained boot-bit/reboot-reason
implementations. The T5AI priority exception remains intentionally deferred.

Rejected or deferred work must not be repeated without addressing its recorded
failure mode:

- compositor framebuffer-provider extraction caused repeatable startup
  HardFaults and remains in shared compositor code;
- restoring first-use onboarding caused an unsafe AppDB/PFS resource-retirement
  assertion on upgraded devices;
- replacing the T5AI resource metadata check with the generic full-pack CRC was
  stable but added 4.754 seconds to every boot;
- `resource_unwatch()`, OTA/update/reset capability cleanup, and priority-policy
  localization require the deeper designs described below.

If cleanup resumes, the next resource-storage step is to design a T5AI-local or
capability-selected system-bank backend. It must preserve the approximately
1 ms bounded metadata check, direct XIP reads/checksums, single-bank semantics,
and the existing app-resource backend ordering. Do not include the deferred
`resource_unwatch()` change in that slice.

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

Unhygienix currently advertises the DVT2 public platform identity and fabricates a
recovery metadata record when no Pebble PRF is present. The public identity
mapping may be necessary for phone compatibility. Fake recovery state should
be replaced by a protocol capability or isolated compatibility adapter rather
than changing the meaning of shared version data.

### Generic fixes are mixed into the port

Several changes may be valid for stock but should be reviewed as independent
fixes rather than hidden in the Unhygienix delta:

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

### 1. Remove instrumentation and obsolete diagnostic observation

- Remove the T5AI command/probe diagnostics from shared startup.
- Remove their event, touch, recognizer, and queue diagnostic APIs and counters.
- Restore stock common GATT discovery.
- Restore stock common I2C behavior and remove unused recovery plumbing.
- Remove the reset-stage UART marker.
- Restore stock scheduler, queue, and app-priority behavior, then retain only
  any T5AI priority ordering demonstrated necessary by hardware.
- Remove dead implementations, empty preprocessor blocks, and incidental
  formatting churn.

Build Unhygienix after this slice. Treat failures as evidence of an undeclared
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

### 5. Priority exception follow-up — superseded diagnosis

The October CP/flash work keeps changes local to T5AI transport and the flash
driver. Flash reads retain the priority boost through copy/CRC and READ_DONE;
ordinary physical commands are serialized across logical channels. Shared
app/SystemTask priorities and timer behavior remain stock. The earlier proposed
broad readiness instrumentation is not the next required task and session
measurement probes have been removed. Future scheduling localization must
preserve these ownership and lease guarantees and undergo transfer/reconnect
validation. Longer soak coverage remains outstanding.

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

- build Unhygienix;
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

The initial cleanup slice is complete in the current branch:

- shared GATT discovery, I2C, event queues, KernelBG queue handling,
  touch service, touch navigation, and event-loop code are restored to stock;
- reset UART marking, mailbox command probes, clock and retained-flash probes,
  touch/queue diagnostic counters, display register dumps, the display
  bit-bang probe, and the destructive diagnostic pattern are removed;
- T5AI keeps only the local runtime behavior required for Unhygienix operation,
  including its no-op debug-input backend, mailbox processing, 120 MHz vote,
  display transport, and touch-driver failure recovery;
- focused board/port tests pass (99 tests), `git diff --check` passes, and a
  clean `unhygienix` configure/build succeeds.

Runtime/product-policy cleanup was subsequently handled in the focused slices
recorded below. Remaining work is summarized in the paused status above.

The common-service runlevel subtask is complete: Unhygienix uses the native QMI8658
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
`external artifact: pebbleos-runlevel-backlight-fix-unhygienix.log`.

Launcher cleanup restores the stock Gabbro 48 ms render cadence and removes
the T5AI branches that bypassed launcher glance initialization, drawing, and
selection playback. This intentionally lowers Unhygienix's animation render rate
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
trace is `external artifact: pebbleos-launcher-cleanup-unhygienix.log`.

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
`external artifact: pebbleos-onboarding-first-boot-unhygienix.log` and
`external artifact: pebbleos-onboarding-second-boot-unhygienix.log`.

The rolled-back source again passes all 95 focused tests and builds with FLASH
878,227 bytes. It was restored to the watch over the AP-only range
`0x132000..0x215d0a`; the raw SHA-256 is
`34b20f6580d53e6ea3afc34eb1aeecfb87d865fd74716a63d9cbb90e41acc3e0`
and the protected SHA-256 is
`0896397f19d8dda86aae317a6ddcd346909ce870b5653b4d50b3856382e1b017`.
A final 60-second capture completed AppDB and BlobDB flushes without an
assertion or reboot. The rollback trace is
`external artifact: pebbleos-onboarding-rollback-unhygienix.log`.

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
`external artifact: pebbleos-boot-splash-extraction-unhygienix.log`; physical splash appearance
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
`external artifact: pebbleos-debug-console-extraction-unhygienix.log`. AppFetch was not requested
during this capture, so the previously accepted priority/AppFetch validation
continues to cover that path.

The attempted compositor framebuffer-provider extraction was rejected after
an A/B hardware test. The board-local provider preserved the framebuffer's
exact address and size (`0x60000000..0x6001081e`), but repeatedly introduced
startup HardFaults. Restoring the pre-extraction image at `1fa722916` survived
the equivalent reset, full-sync, BlobDB flush, and AppFetch workload without a
HardFault or NimBLE host reset; its trace is
`external artifact: pebbleos-compositor-baseline-ab.log`. A second candidate removed all
unrelated `compositor.c` formatting churn and retained only the narrow provider
seam. It still HardFaulted twice immediately after reset; its trace is
`external artifact: pebbleos-compositor-minimal-unhygienix.log`. This isolates the regression to
the ownership seam rather than framebuffer placement or source formatting.

The provider files, board build wiring, and provider-specific tests are
therefore removed, and the accepted T5AI framebuffer allocation, PSRAM vote,
and destructive handoff validation remain together in the common compositor
for now. Moving them requires deeper investigation into why relocating the
framebuffer object and preparation routine across object boundaries destabilizes
startup despite an identical linker address. The independent display-policy
cleanup remains: `DISPLAY_FORCE_FULL_FRAME_UPDATES` replaces the explicit
board/SoC conditional, so partial-update capability is expressed through the
display configuration without changing Unhygienix behavior.

The final stock-ownership image passed all 167 tool tests and built with FLASH
878,227 bytes and KERNEL_RAM 75,232 bytes. Its raw SHA-256 is
`1f21d1145f68e63b68bd8e9b540282d305660047971e0ee0e45b8974c66ec009`
and its protected SHA-256 is
`059521d94733498fe627ea82b9bdfac222371b557af7143dae260838423a0438`.
The guarded flasher programmed only `0x132000..0x215d0a`. The final controlled
150-second trace reached runlevel 4, completed the settings and BlobDB flushes,
and completed AppFetch with result 0. It contained no HardFault, assertion,
NimBLE host reset, or unexpected AP reboot and is retained as
`external artifact: pebbleos-compositor-final-unhygienix.log`.

T5AI retained boot-bit and reboot-reason ownership is now selected at build
time. The shared `system/bootbits.c` and `system/reboot_reason.c` are stock
again. Unhygienix excludes them from the system object and supplies behaviorally
equivalent implementations from `soc/t5ai/bootbits.c` and
`soc/t5ai/reboot_reason.c`, using the existing checksummed `.t5ai_retained`
store. This keeps T5AI persistence policy out of shared system code without
changing the public interfaces or retained register layout.

All 168 tool tests passed. The image built with FLASH 878,227 bytes and
KERNEL_RAM 75,232 bytes; its raw SHA-256 is
`62da8f2654ef7fcae4f1110ff90ad63803841fd7c8d41f24c4c62086e9f0e425`
and its protected SHA-256 is
`16d3fa5eb3a4bc640992f8d3485adf65695c1ce0dc7ce82b804e69bdc6a7ea6c`.
The guarded flasher programmed only `0x132000..0x215d0a`. A controlled reset
reported and cleared the retained reboot state, reached runlevel 4, restored
the encrypted phone and reversed PPoG session, and completed the AppDB and
BlobDB flush sequence without a HardFault, assertion, NimBLE host reset, or
unexpected AP reboot. The accepted trace is
`external artifact: pebbleos-retained-state-extraction-unhygienix.log`; AppFetch was not requested
during this capture, so the previous accepted AppFetch traces continue to
cover that independent path.

The T5AI system-resource metadata validator was tested separately before
resource-backend extraction. Replacing it with the stock generic full-store
validator was functionally correct: the 150-second hardware run reached
runlevel 4, completed BlobDB work and AppFetch with result 0, and had no fault
or reboot. However, validating the 1,026,537-byte pack took 4.754 seconds on
each boot, compared with approximately 1 ms for the bounded manifest/table
check. The generic candidate is therefore rejected on startup-performance
grounds and the shortcut is restored. Its behavior should move with the
eventual T5AI-local resource backend rather than be deleted. The diagnostic
trace is `external artifact: pebbleos-resource-generic-validator-unhygienix.log`.

### Historical first-slice hardware result — 2026-09-19

The first cleanup candidate was not hardware-accepted at this point in the
sequence. This section is retained as historical failure evidence; the priority
correction and later accepted state are recorded below. The clean build was
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
`external artifact: pebbleos-cleanup-unhygienix-soak.log`. Do not begin the second cleanup slice or
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
is `external artifact: pebbleos-priority-fix-unhygienix-appfetch.log`.

This accepts the priority restoration as the fix for the cleanup regression.
It does not justify restoring the T5AI semaphore/queue implementation,
watchdog exception, event-queue changes, or removed instrumentation. The
subsequent `Moddable XS not supported` and missing AppInbox messages describe
unsupported application features in this build and did not reset the AP.
