# Unhygienix validation and remaining work

The [operator handoff](../../../docs/boards/unhygienix/index.md) records current hardware
support and implementation gaps. The [tooling guide](../../../docs/boards/unhygienix/tooling.md)
records dependency, build, packaging and flash procedures.

## Tuya private workers — 2026-10-18

Heartbeat and touch consolidation were explicitly resumed on 2026-10-18. Battery
subsystem review and software consolidation are complete; device qualification
remains open. Native object ownership does not establish that every dedicated worker or its
current stack capacity is necessary.

- **Touch — implemented and flashed; broader qualification pending:** CST92xx now follows stock CST816's
  coalesced raised-priority KernelBG callback and synchronized native input reports.
  The private thread, semaphore and 3,072-byte stack are removed, along with direct
  app/worker/kernel queue inspection and the driver-specific 33 ms motion throttle.
  A native mutex serializes controller reads/reset with enable/disable; input
  listeners run after that lock is released. Generation checks reject stale work.
  CST9217-specific report decoding, mirrored coordinates, reset timing and bounded
  recovery remain local: a one-shot timer schedules the 20 ms contact/asserted-INT
  fallback and 80/1,000 ms read/reset retries. A shared one-second regular callback
  rescues dropped work or a missed asserted edge; idle does no I2C. Timers only
  enqueue work. Read failures report liftoff through the stock input path.
  Candidate `69549c57d6ef588a96516b36640d53b18e705d20` builds/packages.
  The 287 tooling tests pass with two skips, including five full-driver cases;
  four rebuilt native touch/input suites pass (57 cases). Static RAM falls by 3,168 B
  to 101,728 B; linker heap becomes 25,248 B. Flashed AP-only with boot ID verified;
  encrypted bond/phone session restored and discovery completed in 3,759 ms.
  The operator confirms taps, drags/swipes, clean finger lifts and sleep/wake touch
  work normally, and reconfirms touch after a KernelBG watchdog warning/recovery.
  The new image logged a 5,952 ms BG feed delay and App throttling for FdF Time,
  then recovery; no later AP boot was decoded. A later channel-mask warning lacks
  matching feed-age/recovery records before capture ends. Cause remains unresolved and the
  combined run is not clean qualification. First failure and full trace preserved;
  capture stopped. A pre-flash recovery record is kept separate from new-image
  findings. Queue saturation, measured latency/stack/heap, injected I2C recovery,
  cold power-cycle and sustained watchdog checks remain open.
  Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/cst92xx-kernelbg-20261018/flash/report.txt`.
- **Battery — repair/consolidation flashed; ADC and controls pass, watchdog open:** the private
  worker and its 2,048-byte stack are removed. The existing voltage service drives
  sampling; getters submit a nonblocking request and report unavailable until it
  completes. The existing mailbox owner advances bounded asynchronous MB-IPC/
  SARADC phases with a 5-second whole-acquisition deadline (500 ms connect,
  existing 2-second CP conversion timeout). No ADC waits, sleeps or extra thread
  run on KernelBG or NewTimer. The raw-read error is preserved; successful raw
  replies always complete READ_RAW_DONE before cleanup, including rejected
  CRC/size data. Cleanup only acts on successfully acquired resources. Ambiguous
  transport failures and failed cleanup retain/poison CP-visible storage until
  reset. Synchronous flash waits for a shared physical slot instead of failing
  busy; asynchronous callers cannot overtake it. Wiring/calibration are board
  data, and BATTERY_T5AI selects the driver. A native both-edge charge IRQ plus
  shared regular timer provides debounced stock connection events, missed-edge
  reconciliation and dropped-callback recovery. CHRG remains only a charging
  proxy; full/plugged versus unplugged is not independently observable.
  The previous validity fix remains: no 3,700 mV default, failure/expiry returns
  unavailable, and the stock voltage service skips invalid readings. All 299
  tooling entries pass with two skips; all 15 native battery-monitor cases pass.
  Static RAM falls by 1,760 B to 99,968 B; linker heap becomes 27,008 B. These
  are linker capacities, not measured peak headroom. Final build
  `779f3e5a1bcaeed66d06043ead303ab3c91254ed` is flashed AP-only; boot ID verified.
  Four ADC samples succeeded in 6–7 ms at 4,011–4,025 mV; zero ADC errors and
  no additional AP boots in the completed 300.478-second capture. The operator
  confirms watchface, phone, controls and sleep/wake work normally. KernelBG
  watchdog stalls/recoveries persist and remain unresolved. Hardware testing
  exposed mailbox-owner synchronous logging and an inherited vendor CRC bit
  error; both are corrected with regression coverage. Failed candidates and the
  recovery flash are preserved. No cell is fitted; unloaded charger voltage does
  not qualify battery calibration or percentage accuracy. Cold-cycle, sustained
  watchdog, read-done timing under load, stack/peak heap and fitted-cell checks
  remain open. [Subsystem review](../../../docs/boards/unhygienix/battery-review.md).
  Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/battery-consolidation-20261018/vendor-crc/flash/report.txt`.
- **CP heartbeat — implemented and flashed; broader qualification pending:**
  This consolidation was not required for correctness, but removes one dedicated
  thread and its 1,024-byte stack, saving 1,152 B of static RAM overall. The existing
  mailbox worker now services the one-second heartbeat deadline. Runtime readiness
  explicitly wakes it; idle waits are bounded by the next heartbeat or physical
  fault check, and busy-loop iterations also check the deadline. Submission and
  physical/logical completion share the mailbox context. The private thread and
  1,024-byte stack are removed; no timer-service dependency or blocking heartbeat
  operation was added. Late execution skips missed intervals without catch-up
  bursts. Firmware builds/packages; 284 tooling entries pass with two skips.
  Static RAM falls by 1,152 B, restoring linker heap capacity to 22,080 B. This is
  not measured peak heap headroom. Flashed AP-only; boot ID
  `c42b19caafdddfeef102c7d1eec3182da1fc0fcb` verified. Phone protocol
  reconnection/discovery and operator face/control checks pass. Capture is stopped;
  warnings/errors remain recorded. Heartbeat timing, mailbox stack high-water,
  cold power-cycle and sustained watchdog behavior still need device checks.
  Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/heartbeat-mailbox-20261018/flash/report.txt`.

Before heartbeat consolidation these workers reserved **6,144 B of internal stack
storage**. Heartbeat, touch and battery are consolidated and flashed. Battery
removes the remaining **2,048 B**. None of those three private stacks remain
in the final image. Together the consolidations recover 6,080 B of static
RAM after accounting for replacement state, bringing linker heap from 20,928 B
at the native-object baseline to 27,008 B. Startup/peak heap and mailbox stack
high-water still need measurement. The original native-object conversion itself
moved allocations from heap to static storage; its +8,768 B static increase
replaced 9,044 B of former heap requests and was not an equivalent steady-state
live-RAM increase. HCI and mailbox owner threads remain necessary to the current
transport design. Broader combined device qualification remains open.
See the [stock comparison and accounting](/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/stock-comparison.md).

## Current disposition — 2026-10-18

The [2026-10-18 stock convergence review](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-18-stock-convergence/review.md)
is the active cleanup checklist and supersedes earlier audit dispositions.
Reassess retained divergences against current source and stock behavior; historical
justifications do not establish a continuing requirement. Completed work must not
be repeated merely because it appears in older progress records.
Current committed source reviewed is `ecf82bce8`, compared with the already merged
local `origin/main` at `b3a2dcb88`; the review did not fetch upstream.

- Completed: QEMU metadata/unused override cleanup, diagnostic flag exclusions,
  tool regression repair/reference staging, alternate syscall-header removal,
  shared resource-bank condition restoration, duplicate image removal, and
  shared GATT discovery consolidation. Recorded GATT validation: 280 tool entries
  passed with one skip, four native suites passed and firmware ELF linked.
- Hardware smoke-tested: the consolidated GATT image restored an encrypted bond,
  opened the protocol session and discovered services in 6499 ms. The operator
  confirmed responsiveness; the 141-second post-flash interval contained no
  later boot or watchdog expiry. This does not close install, cold-boot,
  reconnect-cycle or soak qualification. Exact evidence is linked below.
- Completed and committed: normal T5AI firmware selects the existing normal
  Bluetooth backend; recovery keeps PRF. The SoC selection exception and temporary
  evaluation switch are removed. Three native persistence suites pass (17 cases).
  The normal-backend evaluation image built/packaged successfully; its initial
  Python/protoc mismatch was corrected. The operator accepts re-pairing if needed;
  old-bond preservation and the reproduced PRF-deletion re-export behavior do not
  block this cleanup. A bounded flash/cold-cycle check now passes with a capture
  gap recorded below; broader hardware qualification remains open. Evidence:
  `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/persistence/validation.txt`.
- Open: remaining audit verdicts and justified exceptions, separate upstream
  correctness patches, SDK compatibility-cache removal, display completion IRQ
  investigation, exploratory accelerometer CPU measurement, and full hardware
  qualification. Watchdog/full-sync failures, resource/app-entry warnings and
  transient I2C/touch errors remain unresolved. Flash bypass remains paused.

Latest recorded installed image and configuration are summarized in the
[operator handoff](../../../docs/boards/unhygienix/index.md#latest-implementation-status--2026-10-17).
No capture remains from the latest GATT test. Merge readiness remains unqualified.

<a id="next-priorities--2026-10-14"></a>

## Next priorities — 2026-10-18

1. **Top priority: stock convergence and readiness to merge to main.**
   Follow the [2026-10-18 stock convergence review](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-18-stock-convergence/review.md)
   for current decisions, implementation order and validation requirements. It
   reviews `ecf82bce8` against local `origin/main` at `b3a2dcb88`. Preserve or attach
   this local report with the merge review so reviewers can access its evidence.
   The [October 14 audit](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-14-pebbleos/audit.md)
   and its file inventory are historical evidence only, not the active checklist
   or authority for retaining exceptions.

   - Pixel fast-path removal is deferred by the operator. The Getafix full-frame
     compositor override and unrelated utility/cosmetic changes are removed.
     Separate independently useful improvements from the port.
   - SDK compatibility-cache benchmarking is deferred by the operator as of
     2026-10-18. The T5AI GATT discovery lease override is removed; stock zero
     is inherited without a build override. Keep its broader device qualification
     and later real connection-parameter negotiation checks separate.
   - Remeasure stock full-pack validation before accepting reduced integrity
     checks. PFS system-file routing and watch/unwatch are restored as recorded
     below; qualify language/font changes on the device. Preserve the physical
     single-bank layout and reject unsupported resource updates.
   - Test stock reconnect advertising transitions, then stock connection-parameter
     negotiation, independently on the RF-corrected CP. Record actual parameters
     and update completions. Retain only reproduced controller limitations at a
     narrow boundary; replace the upper layer's unconditional local grants.
   - Share ordinary boot-bit and reboot-record logic through small retained-storage
     and reset hooks. Convert local driver allocations to native kernel objects,
     make allocation/capability contracts explicit, and revalidate current RAM
     budgets, compiler tuning and watchdog timing.
   - Review general correctness changes independently and prepare separate
     upstream patches for GATT lifetimes, PULSE framing/bounds/transport lifetime,
     logging progress, pairing context lifetime, watchdog runnable-waiter
     accounting, KernelBG recovery, serial teardown and MPU correctness. Do not
     resubmit fixes already present in stock or assume each current fix is complete.
   - Resolve every remaining finding with current evidence, then run relevant
     tests and stock-board/QEMU checks. Qualify the combined watch firmware with
     install/launch, 25 reconnect cycles, cold-boot bond/face restoration and
     connected-idle soak before marking it ready for merge review. Preserve the
     first complete failure trace.

   Prefer stock interfaces and narrow board-/SoC-/driver-local implementations.
   CP ownership, actual hardware interfaces, necessary PSRAM placement and
   unsupported update rejection remain constraints; historical failures do not
   justify keeping whole replacements when a smaller stock integration works.
   The dated progress records below are history, not outstanding instructions;
   the current review identifies work already completed. Merge readiness remains
   unqualified.

   Progress — 2026-10-18 (retained boot/reboot consolidation, audit items 14/15):
   implemented and AP-only boot smoke-tested. The common boot-bit API and common
   four-word record/first-reason/restarted-safe/last-app operations now serve T5AI
   through a small retained-storage binding. The local boot-bit API copy is gone;
   reset snapshots, AP marker mirroring/clear and optional trace/freeze remain
   local hooks. Store format/checksum and NOLOAD address/size are unchanged:
   `0x280100a8`, 136 B. Thirty-five focused tests pass, including actual-store
   corruption/warm-state cases, fault-context first-reason preservation and eight
   stock-backend generated-code comparisons. Normal firmware builds/packages;
   static RAM remains 99,968 B. Flashed build
   `256a67bee969fb0415e52acf82b36f0757f7c25c` boots with matching identity,
   reports AP/CP boot sources and restores encrypted bonding, the phone session
   and GATT discovery (3,058 ms). Physical fault/cold-cycle/soak qualification,
   and unrelated boot warnings remain open. Operator confirms normal watchface,
   controls and sleep/wake; the 180.361-second capture received 23,729 bytes and
   is stopped, with no additional boot/watchdog/assertion decoded. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/retained-consolidation-20261018/report.md`.

   Progress — 2026-10-18 (connection negotiation, audit items 8/9): restored
   the shared upper state machine and stock NimBLE update adapter; removed both
   T5AI replacement files and build selectors. Both shared C files and both
   selection CMake files match local `origin/main`. Eight new native policy cases,
   four connection-manager cases and 29 board checks pass. Diagnostic firmware
   applied 15 ms/latency 0 and 30 ms/latency 30 with 6-second timeout via actual
   controller completions. Both requested reconnects restored sessions/discovery
   and fast updates, followed by idle-update events; repeated parameter-detail
   records are incomplete. No port-specific forced L2CAP, timeout inflation or
   nonzero-latency rejection remains. NimBLE retains its ordinary transport
   choice/fallback. Final normal-logging image
   `6974866918986203ee3d1796be87690aa9b012bd` is flashed AP-only and boot identity
   verified, encrypted phone session restored and discovery completed in 2,691 ms.
   Final normal-build actual-parameter records are absent; diagnostic completions
   establish the applied tuples. Both captures are stopped. Operator responsiveness
   confirmation, broad qualification and unrelated warnings remain open. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/stock-conn-params-20261018/report.md`.

   Progress — 2026-10-18 (stock reconnect advertising, audit item 10): implemented
   and device smoke-tested. `gap_le_slave_reconnect.c` matches local `origin/main`
   byte for byte: stock 30-second short/indefinite long terms and churn handling
   are restored. Upper/lower parameter overrides remain unchanged. Normal/debug
   builds package; 13 rebuilt advertising scheduler cases and 29 board tests pass.
   Three captured cycles request 32 then 1635 HCI units and restore encrypted
   bonding, protocol sessions and discovery (5,451/3,880/4,449 ms). Operator confirms
   all three reconnects and responsiveness. No additional boot/watchdog expiry
   is decoded in the diagnostic capture; unrelated warnings remain preserved.
   The debug build override is removed. Final normal-logging firmware
   `7c5d20f0aebae01dee706eb5640340c5d3381a30` is flashed AP-only; boot identity,
   phone session and discovery (3,914 ms) verified. Both captures are stopped.
   Churn-threshold, HRM, 25-cycle, cold-cycle and soak qualification remain open.
   Items 8/9 were subsequently restored as recorded above.
   Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/stock-advertising-20261018/report.md`.

   Progress — 2026-10-18 (Getafix compositor override, audit item 3): removed
   the full-frame dirty-region override, its Kconfig option and obsolete board-test
   expectations. The shared compositor now matches local `origin/main` and the
   pinned stock baseline exactly. All 29 Getafix/Unhygienix board checks pass.
   No hardware flash or Getafix display qualification was performed.

   Progress — 2026-10-18 (GATT discovery lease, audit item 11): removed the
   four-line T5AI 120-second default; stock zero now applies naturally. No
   shared discovery implementation changed. SDK compatibility caching remains
   enabled; display profiling is off and pixel-path removal is deferred.
   The actual-source discovery harness passes 21 scenarios under each lease
   setting (42 runs), including a new 24-service/96-characteristic/288-descriptor
   case with incremental release and final allocation accounting. QEMU Gabbro,
   Getafix DVT2 and Unhygienix configuration checks all select stock zero.
   Firmware builds/packages with the default inherited, without an explicit
   lease override. The final AP-only image's boot ID
   `a7dde5d4e97d85395949fe84d93f7d09c493f85c` is verified; initial discovery
   took 4,144 ms and two operator-confirmed Bluetooth off/on reconnects completed
   discovery in 4,992/4,908 ms, restoring encrypted bonds and protocol sessions.
   The earlier explicit-zero trial took 4,001 ms and reproduced existing
   KernelBG/FdF Time watchdog stalls/recovery. The final image logged a watchface
   app fault/relaunch during the operator's switch/upload test; its cause remains
   unresolved. Neither is clean stability qualification. Evidence and limits:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/gatt-lease-zero-20261018/report.md`.
   Large-database hardware, cold power-cycle, sustained soak and a repeat after
   restoring actual controller negotiation remain open.

   Progress — 2026-10-18 (native kernel objects, audit item 7): removed the
   allocating T5AI kernel adapter. Mailbox, heartbeat, HCI, battery and touch
   threads now own native thread objects; stacks retain their byte sizes and
   priorities. The HCI stack stays in board-cache PSRAM; the other four stacks
   are explicitly aligned internal SRAM. Nine semaphores and eight mutexes are
   embedded/static, including the microphone's device-owned mutex. This removes
   26 boot-lifetime heap allocations (9,044 requested bytes before allocator
   overhead), rather than introducing a replacement OS abstraction. Retry tests
   cover mailbox/heartbeat/battery creation, CP/MB-IPC registration and HCI worker
   reuse with quarantined descriptors retained. The broad tooling suite passes
   283 entries with two skips; the existing native kernel suite passes.
   Firmware packaging succeeds with existing LTO warnings. Static RAM rises
   8,768 B to 106,048 B; the linker heap spans 20,928 B instead of 29,696 B,
   while the removed allocations no longer consume that heap. Peak heap and
   cold-boot/reconnect/touch/battery/microphone behavior still need hardware
   qualification. Hardware update — 2026-10-18: flashed AP-only; the booted
   build ID `e570b7959ef3ff01ffe5b51e68c35460478146fc` matches the saved candidate.
   The encrypted bond and protocol session restored; discovery completed in
   4,275 ms. The operator confirms connection, selected face and working controls.
   One KernelBG feed-delay warning (5,984 ms) triggered app throttling; a watchdog
   recovery record followed. This is an unresolved finding, not clean watchdog
   qualification or evidence of causation by this change. Capture is stopped.
   Flash report:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/flash/report.md`.
   Candidate, exact build ID and validation:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/validation.txt`.
   Static-RAM follow-up: stock HCI also uses static native objects/stacks, while
   stock touch and battery reuse system-task callbacks. Native ownership does
   not justify the existing T5AI worker count. Reassess the touch/battery/
   heartbeat workers (6,144 B of internal stacks) and the extra CP HCI worker;
   preserve IPC ownership/deadlines and measure boot/peak heap when comparing.
   Detailed comparison:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/stock-comparison.md`.

   Progress — 2026-10-18 (unrelated utility cleanup, audit item 6): restored
   the activity attachment download directory, BSS reporting script and
   orientation service's final-newline state to `origin/main` at `b3a2dcb88`.
   All three files now match that baseline byte for byte. Existing upstream
   UTC aliases, include cleanup and native accel/magnetometer APIs are retained;
   local `main` at `a2120e2ae` predates those merged changes. Python syntax checks
   pass. No firmware behavior changes, rebuild or flash; the BSS tool again
   expects `pebbleos.elf` in the current directory and uses stock shell/temp-file
   behavior. The removed rewrite remains in Git and the saved patch at
   `/home/baz/Documents/ChatGPT/pebble/artifacts/utility-cleanup-20261018/before.patch`.

   Progress — 2026-10-18 (PFS system-resource restoration): restored stock
   `resource_impl.def` ordering and `resource_storage.c` unwatch behavior; both
   files now match `origin/main` at `b3a2dcb88` exactly. The existing file backend,
   PFS and file-transfer path are reused without a migration or seeded language
   pack. Current resource IDs place optional translation/extension resources
   outside the bank; English strings and base fonts already have stock fallback
   behavior. This supersedes the audit's concern that registering absent files
   would displace working bank fonts/strings. Eight new integration cases link
   real PFS, the T5AI bank and shared language/font code; six failures were
   reproduced before restoration, and all eight cases now pass. Together with
   existing language/font suites, 42 native cases pass; 23 resource/image tooling
   tests pass. Normal firmware builds/packages with existing LTO warnings:
   flash 1,596,832 B (+608 B), static RAM 97,280 B and display RAM 118,272 B
   (unchanged). The system resource pack is byte-identical to the prior candidate.
   Build ID: `50d14c63d1336e77edb840d723bfefc230982883`; source is uncommitted.
   Flashed AP-only on 2026-10-18; the booted build ID matches. The trace records
   valid system resources, restored bond registration and an open reversed
   PPoGATT session. CP, resource bank and persistent storage were not selected
   for programming. Capture is stopped; existing PFS/app warnings remain.
   Device language/font install, replacement, removal, heap use and UI checks
   remain open. Flash evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-system-resources-20261018/flash/report.md`.
   Build evidence and candidate details:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-system-resources-20261018/validation.txt`.
   Full-pack integrity validation remains a separate open item.

   Follow-up — 2026-10-18 (marker warning): the startup layout probe temporarily
   restores a zero-page PFS, triggering the stock marker warning without reading
   any page. A native preserved-flash restart finds marker page 6 before/after,
   warns only at zero pages, and passes the language/font checks. Deliberately
   retiring that marker in fake NOR produces additional nonempty-scan warnings.
   Exact candidate disassembly confirms the zero-page path; the watch warning
   alone is not evidence of marker loss. No production fix, hardware read/reset
   or storage repair was performed. A generic zero-page scan guard is the narrow
   potential correction; keep nonempty-filesystem warnings. Evidence and limits:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-marker-investigation-20261018/report.md`.

   Progress — 2026-10-18 (legacy accelerometer retirement): removed the periodic
   QMI8658 driver, its Kconfig selector, conditional driver-state layout and
   legacy-only tests. The interrupt/FIFO driver is now selected directly;
   acquisition logic and normal sensor settings are unchanged. Shared test stubs
   and disabled-mode coverage are retained in the interrupt/FIFO suite. Git
   history and saved images retain historical rollback material. All 63 focused
   driver/I2C/memory/board/kernel checks pass. Normal firmware builds/packages
   successfully: flash 1,596,224 B, static RAM 97,280 B and display RAM 118,272 B,
   unchanged from the prior shell-init candidate. Existing LTO warnings remain.
   Candidate build ID: `6dc9f1c7e63c3eeb3fa5d926f35782f8f1126442`; source is
   uncommitted and no image has been flashed. Evidence and matching images:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/retire-legacy-accel-20261018/`.
   On-device animation, shake/wrist-to-wake and touch smoke checks remain open.

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

   Progress — 2026-10-17 (unused code and duplicate assets, based on
   `a53522389` after upstream merge): removed the unused alternate syscall ABI
   header and restored the stock resource-bank condition. The shared
   `fw/resource/resource_storage_flash.c` now matches `origin/main` at `b3a2dcb88`
   exactly. The Unhygienix map references the eight existing Getafix multiwatch
   PNGs directly; byte-identical local copies were removed. The real resource
   generator produced identical affected PNG payloads and an identical declared
   resource ID header before/after; all 607 resource definitions and 31
   declarations retain their metadata/order apart from the eight source paths.
   Fifty-five focused resource/pack/font/board tests pass. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/leftovers/validation.txt`.
   This is not a full resource-pack/firmware rebuild or hardware qualification.
   GATT discovery consolidation is the next substantive cleanup; persistence
   reassessment and the remaining audit dispositions stay open.

   Progress — 2026-10-17 (GATT discovery consolidation, based on `441534a95`):
   removed the alternate T5AI engine and its CMake selection. The shared
   NimBLE engine now emits/frees completed services incrementally, retains
   Service Changed handles before freeing nodes, and frees subscription
   contexts on immediate write failure. It preserves the newer stock
   connection-validity, queue-completion and immediate-procedure-error paths;
   failed-start completion now uses the same guarded notification helper.
   The descriptor walk handles an empty first service without skipping later
   nonempty services. A narrow generic response-time lease override defaults
   to zero on stock boards and 120 seconds on T5AI, retaining its prior policy.
   The common 10-second success tail is unchanged.
   The new full-source ASan/UBSan harness passes 20 scripted scenarios under
   both lease settings; four old-stock failure cases were reproduced first.
   All four rebuilt native GATT suites pass; the broad tool suite passes 280
   entries with one skipped ARM SDK/QEMU fault-entry test. Host allocation
   accounting confirms early node release and no outstanding allocations at
   terminal paths. The normal Unhygienix firmware ELF also built and linked
   successfully; its log retains LTO warnings outside discovery. No firmware
   was flashed, and physical qualification remains outstanding. Validation
   artifacts are under
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/gatt/`.
   Persistence reassessment and the remaining audit dispositions stay open.

   Hardware update — 2026-10-17: the consolidated GATT candidate was flashed
   AP-only after regenerating and validating the binary/protected image and
   matching dictionary. The booted build ID matches the saved ELF. In a bounded
   141-second post-flash capture, the phone restored its encrypted bond,
   opened a protocol session and completed GATT discovery in 6499 ms. The
   operator confirmed reconnect and responsiveness. One commanded flash boot
   was seen; no later reset/watchdog expiry was observed. Battery ADC, transient
   I2C/touch, resource-check and app-entry errors were retained and remain open.
   This is a smoke test, not 25-cycle/cold-boot/soak or install qualification.
   Capture is stopped; raw bytes, dictionary, exact images and report are at
   `/home/baz/coredevices/pebbleos/build/traces/gatt-consolidation-20261017-145916/analysis.md`.

   Progress — 2026-10-17 (Bluetooth persistence cleanup, based on `ab3616d5e`):
   restored stock normal/recovery backend selection; the Bluetooth service CMake
   file matches `origin/main` exactly. Normal T5AI firmware uses the existing
   normal backend and its PRF import/synchronization path. Removed the temporary
   evaluation switch and T5AI-specific airplane-mode stubs from PRF. The normal
   backend, shared PRF implementation, flash ownership and geometry are unchanged.
   PFS/settings writes already fall within the T5AI flash allowlist.
   Three native suites pass (17 cases), including five migration cases linking
   real shared PRF, settings and PFS over fake flash. They cover key import and
   repeated initialization, CCCD/gateway/settings/capability restoration, unchanged
   PRF bytes, stale-bond replacement and PRF-deletion re-export. The final 56
   focused tooling checks pass. Default firmware builds/packages successfully;
   existing LTO warnings remain in the saved log.
   Forgetting a phone in PRF can leave an older normal bond that normal boot
   exports back into PRF. The operator explicitly accepts re-pairing as recovery
   (2026-10-17), so this behavior does not block cleanup. No candidate was flashed;
   physical CCCD/gateway/capability behavior, CP flash latency, heap low water,
   cold-boot reconnect, installation and soak remain unqualified. The installed
   watch still runs the earlier GATT candidate with PRF storage.
   Default candidate: `build/traces/persistence-default-20261017/`; evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/persistence/validation.txt`.
   Source selection is resolved; physical qualification, development-only product
   policy and the remaining audit findings remain open.

   Hardware update — 2026-10-17: default normal-persistence candidate flashed
   AP-only, matching build ID `28416c29102c08546e1b83d6c23826ea3c476133`.
   Trace shows restored encrypted bonding, reversed PPoGATT session and discovery
   in 6890 ms; operator confirms connected/responsive before and after a requested
   USB cold power cycle, with the same face selected. Two expected matching boot
   banners were captured. A host port-busy error caused a 56.218-second gap after
   the cold-boot banner; that reconnection is operator-confirmed, not independently
   traced. No fatal/watchdog or gap-bonding update error was decoded in captured
   portions. PFS startup/resource/app/animation/weather warnings remain. No
   battery ADC or I2C error was decoded in this run; earlier issues stay open.
   Capture stopped; installed image now uses normal persistence. Direct CCCD/
   gateway/capability/airplane-mode qualification, latency/heap measurements,
   repeated reconnects, controlled installs and soak remain open. Evidence:
   `build/traces/persistence-test-20261017-152214/analysis.md`.

   Progress — 2026-10-17 (stock battery UI restoration, based on `c06ee2a46`):
   at the operator's request, removed the T5AI battery-event UI bypass entirely.
   Charging/full-charge and low/critical battery events now use the stock handler;
   no development flag or replacement UI was added. Battery driver/calibration
   and charge controls are unchanged. Both existing native battery UI suites
   pass (14 cases), and the firmware builds/packages with existing LTO warnings.
   Candidate saved at `build/traces/stock-battery-ui-candidate-20261017/`;
   not flashed. The installed persistence-test image still has the bypass.
   First-use/onboarding and settings-migration exceptions are the next remaining
   product-policy review. Battery behavior with a fitted cell stays unqualified.
   Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/stock-battery-ui/validation.txt`.

   Flash update — 2026-10-17: the stock battery-UI candidate is now installed,
   AP-only, build ID `36ce226a40bbc90127f02ba1505313be86977367` confirmed at boot.
   Capture stopped. Normal Bluetooth persistence is retained. No operator visual
   charging-modal check was collected in this flash-only run; fitted-cell battery
   validation and broader hardware qualification stay open. Exact flash/boot
   evidence: `build/traces/stock-battery-ui-flash-20261017-153945/analysis.md`.

   Progress — 2026-10-17 (stock shell initialization, based on `30ff66be8`):
   removed all remaining T5AI guards from `normal/shell_event_loop.c`; the file
   now matches `origin/main` exactly. Existing first-use handling records setup
   complete and requests phone sync once when needed; it does not launch an
   onboarding app or require recovery graphics. The existing one-time KernelBG
   callback compacts eligible DBs and records its preference marker. No migration
   fork, special board flag, flash-layout or permission change was added.
   Four new T5AI-enabled native init cases pass alongside the settings-file,
   shared-PRF, prefs and app-state suites. Firmware builds/packages with existing
   LTO warnings; flash +128 B, static RAM unchanged. Candidate not flashed:
   `build/traces/stock-shell-init-candidate-20261017/`.
   On first hardware boot, trace all per-DB compaction results and completion,
   first-use sync, app/face retention and repeat boot. Stock marks the attempt
   done even if a DB compaction returns an error, so check results explicitly.
   Source exceptions are removed; physical migration qualification remains open.
   Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/shell-init/validation.txt`.

   Hardware update — 2026-10-17: stock shell-init candidate flashed AP-only,
   build ID `6e39f48175667f6dce5d6e419b8581a805ebed62` confirmed on both boots.
   Compaction completion and seven DB results (all zero) were captured; start,
   reminder and health results are absent. Operator confirms connected/responsive
   with the selected face working. A commanded non-writing reset reconnects;
   no repeat compaction was observed. Startup logged a delayed force-quit/back
   event after compaction; PFS/resource/battery/app warnings remain. No fatal or
   watchdog event was decoded, but missing log records limit absence claims.
   Fresh first-use state was not forced, and the two missing DB results remain
   unverified. Capture stopped; installed image now has the complete stock shell
   path. Evidence: `build/traces/stock-shell-init-flash-20261017-155019/analysis.md`.

2. **Replace display completion polling with interrupt-driven completion where
   supported.** Audit the T5AI CO5300 QSPI/DMA path against other Pebble display
   drivers and the pinned Tuya implementation, then adopt a driver-/SoC-local
   completion interrupt and blocking wait if the hardware supports it. Current
   `drivers/display/t5ai/display_co5300.c` busy-polls DMA enable in
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
   acquisition, bounded sensor FIFO batching and combined register reads.
   The periodic comparison implementation and its legacy compile-time flag were
   retired on 2026-10-18; historical measurements remain below.
   The I2C controller receive FIFO defaults on with a twelve-byte maximum;
   the four-byte cap and acquisition profiling default off. Shared Pebble code,
   task priorities and the 100 kHz bus are preserved.
   Focused tests and matched FdF Time watch captures are recorded in the
   [acquisition results](../../../docs/boards/unhygienix/qmi8658_acquisition.md).
   Accelerometer-thread CPU is the primary metric: controller FIFO reduced IRQ
   entries by about 69%, but measured worker CPU was 2.42% versus 2.29%/2.16%
   in byte-mode controls, so no CPU reduction is claimed for that experiment.
   Animation, shake/wrist-to-wake and touch were operator-confirmed on the last
   restored normal image. The October 17 GATT image includes the FIFO default
   change, but its bounded smoke test did not repeat matched sensor measurements.
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

- Reassess resource-policy findings against the current source: as of
  `441534a95`, shared `resource_storage_flash.c` matches the compared upstream
  `b3a2dcb88`. The earlier extraction proposal is superseded; preserve the stock
  implementation and review only deviations still present.
- Stock file-store routing and resource-watch ownership were restored on
  2026-10-18 and verified with real PFS integration cases. Complete the device
  checks listed above; full-pack validation remains open.
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


Historical October 11 capture finished at operator request (175,988 raw bytes flushed). See the
[settings full-sync watchdog investigation](../../../docs/boards/unhygienix/settings_sync_watchdog.md) for the two
reproductions, verified CP binary findings, limitations and staged debugging
plan. That capture has stopped. The October 17 GATT image supersedes the
installed diagnostic firmware; the investigation findings remain open.


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
