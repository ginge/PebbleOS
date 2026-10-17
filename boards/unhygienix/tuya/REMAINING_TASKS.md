# Unhygienix validation and remaining work

The [operator handoff](../../../docs/boards/unhygienix/index.md) records current hardware
support and implementation gaps. The [tooling guide](../../../docs/boards/unhygienix/tooling.md)
records dependency, build, packaging and flash procedures.

## Next priorities — 2026-10-14

1. **Top priority: upstream divergence cleanup and readiness to merge to main.**
   Use the [2026-10-14 source audit](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-14-pebbleos/audit.md)
   and its [complete file inventory](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-14-pebbleos/file-inventory.md)
   as the cleanup checklist before proposing the port for merge to `main` or
   sending its general fixes upstream. These are local audit artifacts; preserve
   or attach them with the merge review so reviewers can access the evidence.
   The audit compares `tuya` at `fbdf1220a` plus the recorded working-tree changes
   with fork point `a2120e2ae` and fetched upstream `main` at `fc89e16b2`.
   It accounts for 381 changed or untracked files; refresh the comparison as
   code and upstream advance. This TODO does not establish merge readiness.

   - Restore stock QEMU board metadata and remove unused revision-override
     plumbing; fix allowed diagnostic combinations with duplicate linker wrappers.
   - Repair regression tests that still assume the retired FreeRTOS port and
     archive or repair the obsolete standalone Tuya reference build path.
   - Consolidate duplicated stock logic, especially GATT discovery. Reassess
     the PRF Bluetooth persistence selection and separate development-only
     product behavior from hardware requirements. Follow each inventory verdict;
     retain documented exceptions until their behavior is safely preserved.
   - Remove unused alternate syscall ABI code and unjustified shared edits;
     share redundant assets where supported. Track removal of the temporary
     SDK compatibility cache under its existing TODO below.
   - Prepare general correctness fixes as separate upstream tasks/patches:
     PULSE framing, bounds and transport lifetime; logging progress; accelerometer
     enqueue-result propagation; watchdog runnable-waiter accounting; KernelBG
     recovery; serial teardown; and MPU correctness. Reconcile the watchdog
     grace-period fix already present upstream and the upstream MPU relocation
     rather than carrying obsolete copies or submitting duplicate fixes.
   - Close or explicitly resolve every audit finding, record justification for
     each retained divergence, and rerun relevant tests and stock-board/QEMU
     checks. Qualify the resulting watch firmware with install/launch,
     reconnect cycles, cold-boot bond restoration and connected-idle soak before
     marking the cleanup ready for merge review. Preserve first-failure evidence.

   Prefer existing stock interfaces and narrow board-/SoC-/driver-local adapters.
   CMake-selected copies still require justification; avoid new logical forks
   merely to keep common source text unchanged. Preserve CP/flash ownership,
   PSRAM placement, display startup constraints and unsupported update gates.

   Progress — 2026-10-17 (first cleanup batch, based on `75cdedff7`):
   restored `tools/boards.py` exactly to local `main`, including QEMU metadata
   loading, and removed unused revision-specific runner/defconfig overrides.
   Added board-local Kconfig exclusions for conflicting mutex and timer
   wrappers, including App tracing's implicit BG tracing selection. Reminder
   profiling and watchdog recovery tracing are also mutually exclusive in
   Kconfig; their existing CMake rejection remains as a defensive check.
   Nine focused board/parser/configuration tests pass. The broad tool suite
   ran 269 entries: 10 failures, 12 errors and one skip, with leak detection
   disabled for the sandbox. Remaining failures include retired FreeRTOS and
   old CMake/source-path expectations, obsolete reference staging, and an
   unresolved `pebble.loghashing` import. Full output is preserved at
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/tool-tests.log`.
   Regression-suite repair, reference-build disposition and the other audit
   findings remain open. No firmware build, QEMU boot, flash or physical
   qualification was performed for this batch; merge readiness is not established.

   Progress — 2026-10-17 (regression and reference-staging batch):
   the broad tool suite is now green: 274 entries, one ARM SDK/QEMU fault-entry
   test skipped because its tools were unavailable. This supersedes the test
   failures recorded above. Retired FreeRTOS/startup assumptions and oversized
   source-snippet checks were replaced with current native integration checks;
   heartbeat and mailbox behavioral harnesses were retained and updated.
   Added executable fault-priority, PSRAM corruption/bounds and panel reset/
   command-order/write-failure coverage. The trace import resolves with the
   repository package roots on `PYTHONPATH`, without production changes.
   Archived the unused reference mutex adapter under `tuya/history/`, removed
   its self-test/build glue, and repaired repeatable reference staging using
   only the required button/touch event type headers. The reference runtime
   harness compiles and passes against the staged sources and headers alone.
   Full logs and the exact validation command are in
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/validation-after-repair.txt`.
   Leak detection remains disabled for the sandbox. Full firmware/vendor SDK
   builds, stock QEMU boot and hardware qualification have not been performed.
   GATT consolidation, persistence reassessment, the remaining audit verdicts
   and merge readiness remain open.

2. **Replace display completion polling with interrupt-driven completion where
   supported.** Audit the T5AI CO5300 QSPI/DMA path against other Pebble display
   drivers and the pinned Tuya implementation, then adopt a driver-/SoC-local
   completion interrupt and blocking wait if the hardware supports it. Current
   `fw/drivers/display/t5ai/display_co5300.c` busy-polls DMA enable in
   `prv_pixel_stream_finish_dma()` and polls QSPI command completion; the QSPI0
   system interrupt route is enabled, but its NVIC line remains disabled.
   Establish which DMA/QSPI event proves the full transfer has finished before
   releasing buffers or starting the next transaction. Preserve cache coherency,
   dirty-row streaming, transfer ordering, bounded timeout/error recovery and
   shared Pebble display semantics; qualify IRQ ownership against the pinned CP.
   Measure CPU time, draw latency and BG progress before/after on the same face
   and configuration, and validate repeated partial/full redraws without hangs,
   corruption or lost completions. Keep this distinct from the SDK compatibility
   cache TODO below: eliminating transfer waits may not fix software render cost
   or KernelBG starvation.
3. **Investigate further accelerometer CPU-cost reduction — exploratory (2026-10-14).**
   Measure how much CPU the accelerometer pipeline costs while the watch is idle
   and while an accelerometer-using watchface is active, then determine whether
   a worthwhile further reduction is practical. A reduction is not guaranteed;
   reporting the measured cost and an unsuccessful optimisation is a valid result.
   Establish matched baselines with the same firmware, clock, tracing and sensor
   settings; distinguish idle with shake/wrist-to-wake enabled, a live subscriber,
   and true sensor standby. Record actual sample rate and shake/batching state.
   Use worker CPU ms/s and ms/sample as the primary metrics, and account for
   I2C ISR and shared service/shake processing separately without double-counting.
   Preserve input latency, sample delivery, shake/wake and touch behavior. Keep
   changes driver-/SoC-local and preserve stock shared code and task priorities.
   Historical active FdF captures measured 9.21–9.74% worker CPU with legacy
   polling and 2.17–2.42% with interrupt-driven direct acquisition. The later
   controller-FIFO comparison measured 2.16–2.29% in byte controls, 2.42% with
   twelve-byte bursts and 2.57% with four-byte bursts. These are instrumented
   active measurements, not idle baselines or exclusive whole-pipeline CPU.
   A qualified matched idle baseline was not found in the reviewed historical
   tracing; obtain it before claiming an idle/active saving.

   The previous acquisition implementation iteration remains complete for now,
   closed at the operator’s request on 2026-10-14. Implemented driver-local interrupt-driven
   acquisition, bounded sensor FIFO batching and combined register reads, with
   the previous periodic implementation behind a legacy compile-time flag.
   The I2C controller receive FIFO defaults on with a twelve-byte maximum;
   the four-byte cap and acquisition profiling default off. Shared Pebble code,
   task priorities and the 100 kHz bus are preserved.
   Focused tests and matched FdF Time watch captures are recorded in the
   [acquisition results](../../../docs/boards/unhygienix/qmi8658_acquisition.md).
   Accelerometer-thread CPU is the primary metric: controller FIFO reduced IRQ
   entries by about 69%, but measured worker CPU was 2.42% versus 2.29%/2.16%
   in byte-mode controls, so no CPU reduction is claimed for that experiment.
   Animation, shake/wrist-to-wake and touch were operator-confirmed on the last
   restored normal image. The later default change has not been flashed.
   Sensor FIFO throughput/timestamp limitations, DMA support not established,
   unqualified low-power transitions and the earlier unresolved freeze/reboot
   remain documented limitations; completion of the previous iteration does not qualify them.

Previous notes: [QMI8658 polling isolation and accelerometer-free comparison](../../../docs/boards/unhygienix/tooling.md),
[shake/display wake behaviour](../../../docs/boards/unhygienix/index.md), and
[input alignment, App render tracing and accelerometer-disabled/restored runs](../../../docs/boards/unhygienix/refactor_handoff.md).
The 2026-10-10 FdF comparison reduced median render/upkeep wall time from 448 ms
with the sensor enabled to 362 ms disabled, but KernelBG still starved; sensor
load is measurable and is not required for that starvation. The sensor was
subsequently restored. Preserve stock shared code and standard task priorities;
keep these optimisations board-, SoC- or driver-local wherever possible.

## Existing validation priorities

1. Verify watchface install/launch and selection persistence.
2. Run 25 disconnect/reconnect cycles, retaining the first complete failure trace.
3. Cold boot and verify bond restoration, protocol reconnection and selected face.
4. Run overnight connected-idle with periodic UI interaction.
5. Resolve App privileged-code stalls and resource/app-entry warnings.
6. Complete hardware wake/alarm routing and low-power operation.
7. Validate battery/charging with a fitted cell, RTC drift, privilege isolation,
   storage interruption and corruption recovery.
8. Define rollback and recovery before enabling firmware updates or OTA.

Keep captures and matching firmware/log dictionaries under `build/traces/`.
Host builds do not establish physical qualification. Confirm actual received
bytes before reporting a capture or soak as active.

## Deferred KernelBG stall investigation — 2026-10-09

Deferred at the operator's request. MU-TH-UR T loading still triggers a
KernelBG watchdog stall after the synchronous-erase delay fix. In the latest
flashed candidate, the task was unfed for 5435/6442 ms, then recovered after app
throttling without a reboot during the bounded observation. App-driven CPU
starvation is a hypothesis, not an established root cause; communications or
lock waits remain possible.

When resumed, correlate KernelBG/App CPU-time deltas and ready/blocked state
with the active communications endpoint, callback elapsed time, settings
filename/progress and send-buffer wait duration. Use the optional
`CONFIG_T5AI_STATE_TRACE` recorder if useful; it remains disabled by default.
Do not add stack/register tracing or change shared scheduling policy merely
from sampled PCs. Preserve exact firmware, dictionary and first-failure logs.
Evidence: `build/traces/erase-delay-fix-20261009/flash-results.json` and
`build/traces/erase-delay-fix-flash-20261009.log`.

## TODO: investigate and remove the SDK compatibility cache — 2026-10-10

Treat `CONFIG_T5AI_SDK_COMPAT_CACHE` / `soc/t5ai/sdk_compat.c` as a temporary
workaround and code smell, not the final draw-performance solution. The operator
notes that other SoCs do not need this cache. Although it reduced observed FdF
render/upkeep spans from a 448 ms median to 185 ms, it adds eight bytes of cached
App/Worker flags in read-only-to-app RAM and does not resolve KernelBG starvation.

- Establish why T5 pays such a large cost for the stock SDK compatibility queries
  and drawing path compared with other SoCs. Audit syscall/context-switch costs,
  memory/cache configuration, compiler settings and render handoff; do not assume
  a SoC-specific cache is inherently necessary.
- Fix the underlying cause and remeasure with the SDK cache disabled. Preserve
  stock SDK semantics, privilege isolation and process lifecycle behavior.
- Remove the cache and its linker/build glue once the underlying fix preserves
  the performance gain. Avoid replacing it with further RAM-backed workarounds,
  draw sleeps or unexplained priority changes.

Measured evidence and limitations:
`build/traces/sdk-compat-draw-20261010/analysis.txt`.
The currently installed lighter firmware retains the cache; this TODO does not
change firmware or disable it.

## Resolved for now: KernelBG watchdog starvation recovery — 2026-10-11

The lightweight selector trace reproduced FdF KernelBG READY starvation while
PendSV continued choosing higher-priority work. The existing starvation recovery
is bypassed by a generic watchdog regression: commit `fb1f13e12` marks KernelBG
waiting before its poll wait, and `864cd70aa` exempts that flag unconditionally.
After wakeup, a starved READY thread cannot run to clear the flag, so its expired
channel never invokes the existing recovery callback. An actual-source host
reproduction confirms that READY and RUNNING channels remain exempt.

The generic watchdog now exempts genuinely blocked idle waits and starts a fresh
timeout at the first observed runnable state, without requiring the starved
thread to clear its waiting flag. No T5 sleep or priority workaround was added.
The upstream grace-period fix `a8772bc52` is also imported. Four additional native
scheduler tests cover READY starvation, recovery, long-idle wakeup and a busy
thread retaining its waiting flag; the 17-test watchdog suite passes.

The corrected image is flashed. The bounded hardware run recorded four KernelBG
expiries, each followed by recovery about one second later, with KernelBG
progress and no reset/OOM. The former idle-wait exemption is no longer preventing
recovery. This restores recovery, not the underlying expensive draws.

Peak observed READY age still reached 64.699 seconds during a communication
callback with a 65189 ms pindb mutex hold. Settings compaction can globally suspend
the watchdog for 60 seconds; this is a candidate explanation for the long gap,
not established for that episode. The 2026-10-11 SoC-only caller trace captured
two settings-compaction 60-second suspension requests, each explicitly resumed
after about 0.62 seconds, before FdF. No suspension overlapped the captured FdF
intervals; seven recovery reports and a 5.564-second peak READY age were recorded.
The operator accepts this as fixed enough for now: no recurrence of the prolonged
stall occurred during the latest trace. Further suspension investigation is
deferred unless the symptom returns; the earlier episode remains unattributed.
The operator attributed the intervening face exit to syncing another watchface.
Evidence: `build/traces/wdt-cause-20261011/analysis.md`. Continue draw/syscall
optimization independently. The bounded run is not long-term
stability qualification. Both lightweight diagnostic flags are now disabled in
the built and flashed firmware (fresh boot reached communication readiness); their opt-in, default-off implementations remain available.
The SDK compatibility cache TODO and draw optimization work remain separate.
Evidence:
`build/traces/wdt-recovery-effectiveness-20261010/analysis.txt`;
baseline: `build/traces/bg-pick-light-20261010/`.

## Candidate fix: end BG recovery on progress, not a fixed timer — 2026-10-11

The operator reported a reboot during the diagnostic-free validation. A
raw/decoded UART capture started before a requested hardware reset,
using the same flashed image with diagnostic flags off. Treat the reset boot as
commanded. Reproduction showed that the earlier accepted recovery was not reliable. Evidence: `build/traces/reboot-repro-trace-off-20261011/`.

## Remaining stock-code refactor work

See the [2026-10-09 detailed handoff](../../../docs/boards/unhygienix/refactor_handoff.md)
for scope, file ownership, known failed approaches, acceptance checks and the
last-flashed candidate.

The historical [shared-code audit](../../../docs/boards/unhygienix/history/shared_code_cleanup.md)
contains superseded startup/kernel work. The remaining candidates are:

- Extract the single-bank system-resource policy and bounded metadata validator
  from shared `resource_storage_flash.c` into a selected T5 backend. Preserve
  fast boot validation, driver-local mapped reads and resource backend ordering.
- Resolve resource-watch callback ownership before restoring stock
  `resource_unwatch()` behavior; do not simply remove its T5 guard.
- Select unavailable update, recovery, coredump and reset implementations or
  explicit capabilities at build time, reducing shared T5 conditionals without
  enabling unsupported operations.
- Finish compositor framebuffer-placement ownership. PSRAM validation and power
  setup have moved to the board; the shared framebuffer section annotation
  remains. Earlier provider extraction faulted and needs a sound new design.
- Audit remaining shared identity/profiler exceptions and CMake selection glue;
  refresh stale board tests after the startup/service extractions. Separate
  independently justified generic fixes from platform policy.

Stock `main.c`, native kernel wrappers/startup, SoC-local delay selection and
most common service restoration are already completed; do not repeat those
extractions. Hardware qualification and remaining peripheral implementations
are separate work from this refactor list.

2026-10-11 reproduction update: three spontaneous reboots were captured after
one commanded reset with diagnostics disabled. Each follows unrecovered KernelBG
watchdog expiry and reports AP=112 (0x60 + Watchdog=16), CP=16. Recovery is not
reliable under this reproduction; the earlier accepted disposition is superseded
by this evidence. Root cause of failed recovery is still open. Frozen evidence:
`build/traces/reboot-repro-trace-off-20261011/three-reboots/analysis.txt`.

2026-10-11 candidate update: expiry-only priority/CPU probes reproduced reset
with App priority successfully lowered 2/2 → 0/0 and BG READY at 1/1. BG received
240 then 237 ms CPU between checks, but fixed 300 ms App unthrottling interrupted
recovery before the communication callback fed. Recovery now keeps the affected
App throttled until a successful BG feed; the watchdog's two-second grace remains
unchanged. Timer expiry checks and priority restoration are atomic, and an old
recovery cannot change a replacement App. A separate T5 flash priority scope race
now preserves concurrent throttle/priority changes instead of restoring a stale
base priority. Both regression cases fail before their fixes and pass afterwards.
Twelve focused host tests plus native kernel/watchdog suites pass. Initial combined
candidate bounded capture contains five recovery reports and seven expiry reports,
with no subsequent boot in that interval;
this is not long-term qualification or an isolated comparison of the two fixes.
Evidence: `build/traces/wdt-recovery-cpu-20261011/` and
`build/traces/wdt-progress-recovery-20261011/`. Draw cost remains open.


2026-10-11 final-candidate validation **failed**: build
`a6a8697cb47428676579b5930585b9fd24401a5d` rebooted with AP=112/CP=16.
App remained at priority 0 throughout the final grace checks. BG CPU advanced
356 → 1027 → 1688 ticks while executing the default communication receiver;
the sampled PC/LR includes PFS/FTL flash reads. This proves the remaining failure
can occur despite sustained BG scheduling. Do not mark the reboot issue fixed.
The progress-driven recovery and flash priority corrections are candidates,
not sufficient fixes. An expanded expiry-only SoC probe records existing flash
operation counts/timings and a settings full-sync marker to distinguish slow
transactions from long filesystem work. Evidence:
`build/traces/wdt-progress-recovery-final-20261011/analysis.md`.


2026-10-11 targeted probe captured two further watchdog resets after the
commanded reset. Both have settings mark-all-dirty active for over 7.5 seconds,
App at 0/0, advancing BG CPU and over 100 completed small writes in the final
roughly two seconds. Completed read maxima are 8/9 ms; writes 22/25 ms, with no
erases completed in those intervals. Investigate synchronous settings rewrite
and T5 per-write overhead/durability-safe batching, rather than assuming a
scheduler failure or one stuck flash request. CP reference code toggles flash
protection around each write; its contribution needs measurement. Frozen report:
`build/traces/wdt-flash-recovery-20261011/two-reproductions/analysis.md`.


Capture finished at operator request (175,988 raw bytes flushed). See the
[settings full-sync watchdog investigation](../../../docs/boards/unhygienix/settings_sync_watchdog.md) for the two
reproductions, verified CP binary findings, limitations and staged debugging
plan. No live UART capture remains; diagnostic firmware is still flashed.


## Flash optimisation and rewrite robustness — 2026-10-11

See the [audited avenues and implementation sequence](../../../docs/boards/unhygienix/flash_optimisation.md).
The reset remains open; documentation of a candidate is not qualification.

- Split request-send timing into socket lock, physical handshake and logical
  response; measure CP protection, notifications, programming and service wait.
- Add record/file progress only after verified success, using the existing
  current-thread watchdog feed without prematurely releasing App throttling.
- Correct dirty-all allocation/write/open error propagation and preserve old
  settings on abort; resolve READ_DONE cleanup on failed read validation.
- Evaluate sequential dirty-all copy using existing temporary-file semantics
  to remove setter searches and combine key/value I/O.
- Keep global deadlines, CP ownership and write durability intact. Do not add
  caches or blindly retry mutations. SDK compatibility cache removal stays
  tracked separately above.
