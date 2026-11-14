# Unhygienix power TODO

Work on CP-awake AP sleep and local peripheral lifetimes first. Keep CP deep
sleep deferred, the normal 240 MHz active vote, dim display policy, Bluetooth
identity/calibration, heartbeat, timer accounting, flash/PFS integrity and shared
ownership intact. Keep CP frequency and closed-source settings unchanged.
Production code and hardware release qualification are separate.
Use [current status](current-status.md) for installed identities and evidence.
Historical findings remain in [the investigation history](power-investigation-history.md).

## DMA default promotion — 14 November

- [x] Enable native Unhygienix DMA inheritance by default at operator request,
  independently of optional profiling; leave both test workloads default-off.
- [x] Keep explicit opt-out, reset-held failure and no shared-global writes.
- [ ] Normal connected retained-idle electrical qualification remains open;
  source promotion does not resolve the captured AP=0x4f boot guard or qualify
  shared-controller shutdown. Startup/BT diagnosis stays deferred.

Installed DMA-enabled 4865f074 is unchanged by this source-default request.
See [current status](current-status.md) for build/test evidence and limits.

## Cleanup and requested normal-use DMA trial — 14 November

- [x] Clean up private snapshot/workload constants and redundant bounds without
  changing event/layout/transfer behavior; correct stale index installed-state text.
- [x] Pass 72 actual-source tests and normal/candidate release/image/resource/
  memory/SRAM checks. Preserve exact faae34b9 rollback and all earlier failures.
- [x] Flash AP 4865f074 with DMA inheritance/profile enabled, both workloads off;
  receive exact build and actual inherited/ready global 1/1.
- [x] Preserve AP=0x4f / CP=0 / sleep rejection 4 after programming. Leave the
  requested DMA image installed; no automatic reset or guard changes.
- [ ] Receive operator UI/input acceptance and qualify retained idle on a valid
  boot before attributing normal-use savings. Retained entry is blocked on this
  captured boot; startup/BT diagnosis remains explicitly deferred.

Source defaults remain off. No additional shutdown, soak or electrical claim.
See [latest installed checkpoint](current-status.md).

## Matched fixed-workload energy — 14 November

- [x] Add default-off pre-splash dim display measurement with fixed pixels,
  bounded phase/frame deadlines, synchronous completion and brightness restore.
- [x] Preserve the initial timing rejection; correct boundary flushing locally,
  then pass 72 firmware tests and five analysis tests.
- [x] Complete control/trial/control with 48 redraws each, phone Bluetooth off,
  zero retained entries and actual DMA globals 3/1/3. Validate marker/sample
  coverage and timing sensitivity; preserve frozen images and normal rollback.
- [x] Observe small USB input reduction in this workload: approximately 1.005 mW
  idle, 0.869 mW during redraws and 0.231 mJ per scheduled completed redraw.
- [ ] Repeat trial/control boots and qualify normal connected retained use before
  default enablement. Do not infer panel-only savings or isolated transfer energy;
  nominal 100 Hz sampling, absolute timing and meter accuracy remain limitations.

See [method/results](display-energy.md). Keep startup/BT diagnosis deferred as
requested; no new shutdown, overnight or default display policy is included.

## DMA inheritance trial — 14 November

- [x] Add default-off, diagnostic-only DMA policy inheritance with reset-held
  rejection and no shared-global writes. Preserve normal bypass behavior.
- [x] Pass 69 actual-source tests and three release builds; validate CRC, image
  layout, resources, heap and actual linked SRAM retained entry.
- [x] Trial inherited global 1 through mixed-I/O qualification; operator confirms
  display/dim, motion and inputs normal. Preserve exact CP function-byte evidence.
- [x] Record two bounded USB windows, explicitly reject savings attribution due
  to different AP sleep admission and radio/input activity.
- [ ] Measure energy per fixed completed workload with matched AP/radio state;
  repeat idle comparisons and assess effect size before default enablement.
- [ ] Complete hardware SD-TX and live display cancellation qualification before
  claiming coverage of those paths; inherited policy is not shared-gate ownership.

Operator explicitly defers startup and Bluetooth diagnosis for this increment.
The older recovery prerequisites below remain historical release limitations,
not permission to reopen that investigation now. See [trial](dma-clock-policy.md).

## Final comparison failure and recovery — 14 November

- [x] Preserve AP=0x4f admission failures on qualification, old control and final
  normal before their respective recovery actions; do not weaken the boot guard.
- [x] Record two short whole-watch USB windows (normal 140.91 mW, control
  138.03 mW), then abort A/B/A when final normal cannot reconnect after reset.
- [x] Preserve `final-install/first-reconnect-failure/`; recover unchanged latest
  normal `faae34b9` via operator USB cycle, with encrypted bonded session and
  32 clean AP returns. Operator confirms reconnection; captures stopped.
- [ ] Explain post-programming AP=0x4f and the post-reset advertising/reconnect
  stall before release qualification (investigation currently deferred by the operator).
  Successful USB recovery is not a fix.
- [ ] Repeat genuinely matched power and completed wake/redraw measurements.
  Current partial readings have radio-traffic confounding and prove no saving.


## Mixed DMA test and normal restoration — 14 November

- [x] Add separate, default-off one-shot pre-splash qualification with static
  callback lifetime, bounded SD join and explicit failure/restore results.
- [x] Pass 63 focused tests, release/config/resource/map/CRC/heap/SRAM checks.
- [x] Observe 16 concurrent display/microphone/SD-RX owner starts, 40 redraws,
  four disable/enable/rotation cycles, successful cleanup and operator confirmation.
- [x] Preserve qualification AP=0x4f / sleep reason 4 before normal restoration.
- [x] Restore normal `faae34b9`: clean reset 0/0, 32 AP-only returns, encrypted
  phone recovery and operator-confirmed normal UI/input/wake/phone.
- [x] Reconfirm battery disconnected, FNB58-only watch USB input and no UART power.
- [ ] Complete matched normal/control/normal input-power analysis; do not attribute
  whole-watch readings to panel alone or claim wake/redraw energy from idle data.
- [ ] Qualify SD-TX concurrency and display cancellation on hardware if needed;
  retain first-startup AP=0x4f and CP ownership/restore as unresolved release gates.

See [exact method and evidence](qspi-dma-diagnostics.md).

## Diagnostic startup correction — installed

- [x] Fix premature PSRAM writes with explicit board activation and bounded SRAM
  bootstrap evidence; preserve the PSRAM-only trial's subsequent failure trace.
- [x] Fix register access before DMA reset/AP grants; eliminate CP channel reads.
- [x] Pass 59 focused regressions, both release builds and image/memory checks.
- [x] Install corrected diagnostic `e3b04f32`, receive one boot/reset 0/0, existing
  SD PFS, 32 clean AP returns, both reports and encrypted bonded phone recovery;
  operator confirms all display/input/wake/reconnect checks normal. Capture stopped.
- [x] Observe QSPI bypass already clear (global 0→1); do not add a redundant gate.
- [ ] Complete mixed DMA and repeated explicit disable/orientation hardware checks;
  CP linked ownership and matched electrical measurements remain open.

## QSPI/DMA first package — built, not installed

- [x] Fail QSPI initialization on CP power/clock acquisition failure before
  controller/source writes; preserve uncertain shared votes and bounded PM behavior.
- [x] Add default-off bounded ownership/timing/register diagnostics in AP-only
  PSRAM, with first-failure retention and no polling timer or clock-policy change.
- [x] Pass 54 focused actual-source tests and normal/diagnostic release builds,
  CRC/layout/map/config/resource/SRAM checks; retain 28,960-byte kernel heap.
- [x] Diagnose/correct unsafe startup accesses; rejected trials and rollbacks are
  preserved. Final corrected image is installed as recorded above.
- [ ] Finish bounded mixed-owner diagnostic qualification; boot/input/sleep/phone
  checks alone do not qualify physical microphone/SD/display concurrency.
- [ ] Locate the hash-matching installed CP97 ELF and complete linked-use inventory;
  shared DMA gating stays blocked pending that proof and all-channel semantics.
- [ ] Measure matched idle input power and energy per completed redraw/wake after
  excluding UART reports and accounting for back-power. No saving is measured.

See [implementation and exact limitations](qspi-dma-diagnostics.md) and
[the ranked gating scope](qspi-dma-power-scope.md). No new gate is enabled.

## 14 November standby/display increment (now installed)

- [x] [Audit](standby-display-review.md) source, boot-bit/reset flow and pinned
  power circuit; confirm current USB/FNB58 supply and operator power-cycle recovery.
- [x] Replace unsupported reset-as-standby with board-local early `-ENOTSUP`;
  no latch operation or storage teardown. This does not implement battery off.
- [x] Cache applied CO5300 brightness/on/off commands, retry faults and invalidate
  on prepare/sleep; propagate reset GPIO failures. Preserve dim policy and clocks.
- [x] Build normal candidate `b40a6cb1`, preserve prior images and independent
  patches; validate 65 checks plus layout/CRC/SRAM/config/resources/style. Record
  three pre-existing obsolete DMA test errors separately.
- [x] AP-only install `b40a6cb1` after actual CP97 readback/CRC and rollback checks;
  receive exact build, 32 clean AP returns and encrypted bonded protocol recovery.
  First-install operator checks were normal after an unresolved splash/apparent
  restart. Final reinstallation needed one commanded reset after AP=0x4f; UART
  then received 32 clean returns. No new phone/physical confirmation on that pass.
- [ ] Investigate operator-reported long splash/apparent automatic restart. UART
  captured one candidate boot, without a second reset sequence. Preserve first
  evidence; startup/release qualification remains blocked and savings unmeasured.
- [ ] Explain recurring post-programming AP=0x4f sleep-admission failures without
  weakening guards; preserve `reinstall-latest/first-failure/` and distinguish
  commanded same-image recovery from clean initial startup.
- [ ] Establish exclusive bounded storage/peripheral quiescence, battery/no-back-power
  and button/supply recovery before implementing and testing P19 latch-off.
- [ ] Resolve disabled-panel sleep timing/GRAM/wake qualification separately;
  raw-zero and Sleep In remain default off. No shared-source clock gate is proven.

## Commit preparation

- [x] Preserve pre-cleanup source, index state, raw captures and failure findings.
- [x] Separate bounded CP diagnostic Kconfig from production power controls.
- [x] Require CP-awake operation in non-test retained builds.
- [x] Provide one board-default non-display power-profile selection with an explicit opt-out.
- [x] Complete formatting/lint, 155 affected regressions, board-default and explicit-off release builds; record the full shared-test build failure.
- [x] Verify formatted CP payload/package equivalence without changing calibration/controller.
- [x] Record dependency-ordered commit groups and retain qualification limitations.

## Existing implementation

- [x] Local I2C/SD/AP-UART clock lifetimes, conservative fault/drain fallback.
- [x] SD interrupt-mask setup race correction; preserve the first partial-write/PFS evidence.
- [x] Independent applied CP alarm replies, owner/generation, cancel/fire and cleanup lifetime.
- [x] AP SRAM wake/restore gate, sleep blockers, integrated SysTick accounting and guard reset.
- [x] CP-awake AP-only policy with per-return CP exclusion checks and startup settling.
- [x] Quiet successful battery logs; preserve conversion, retry and error handling.
- [x] Central AP GDMA channel ownership while preserving CP channels.
- [x] Remove failed fixed 120 MHz selection; retain normal active clock election.
- [x] Keep raw-zero brightness and explicit panel sleep disabled in the selected dim profile.
- [x] Use the existing 32 ms limit for the initial shake-only request; process shake-only samples on the QMI worker. Explicit client rates and data offload remain intact. See the 12 November sensor review for bounded evidence and open qualification.

- [x] Keep direct QMI INT2 pulses armed and coalesce busy-slot followups; preserve FIFO policy. Bounded 13 November host/hardware evidence still has conversion gaps and establishes no electrical saving.

- [x] Default to quiet-period QMI WOM after the operator request, with client bypass, fresh peek and continuous fallback. Bounded hardware wake checks pass; long-term sensitivity/latency and electrical qualification remain open.

- [x] Install the QMI cleanup: one configuration/rollback path, separate direct/FIFO/WOM acquisition, software-only rotation outside WOM, newest-sample FIFO cache decode and isolated diagnostics. Eleven targeted sanitized tests and ARM variants pass; normal text -48 B, data/BSS unchanged. Final wake/touch/buttons, 32 AP sleep returns and encrypted phone recovery pass on build `4545e34a`.

## Correctness and measurement gates

- [ ] Diagnose Bluetooth discovery/reconnect and connection-parameter failures using aligned AP/CP/phone evidence.
- [x] Receive the bounded 32-return AP-only milestone with dim Nadir (12/13 November).
- [ ] Qualify long-term AP sleep/blocker behavior, including warm-programming reset admission failures.
- [ ] Measure each local gate and combined policy against matched controls with fixed display/supply.
- [ ] Compare energy per completed reconnect/transfer/UI workload, elapsed time and idle power.
- [ ] Exercise durable SD/PFS writes, physical bus/DMA faults and recovery without unsafe partial-write retry.
- [ ] Verify early/pending/stale wakes, deadline/AON wraps, tick accounting and independent guard recovery on hardware.
- [ ] Qualify concurrent microphone, SD, display and CP operation, including cancel/abort paths.
- [ ] Complete 25 consecutive reconnects on this AP, then fully captured cold bond/face restoration.
- [ ] Complete deferred overnight connected idle with UI interaction; preserve the first failure.

## Remaining peripheral implementations

1. **Sensors:** the 12 November startup-rate/dispatch increment reduces IRQs; the
   13 November pulse-coalescing increment retains busy-slot edges. Conversion gaps
   persist in continuous mode and no reliable electrical savings are established.
   Default quiet-period WOM now has bounded fitted 05/7c entry/exit, INT2 and
   operator first-wake evidence, including final cleanup-image control checks.
   Streaming watchdog backoff beyond the current policy remains an unimplemented,
   separately qualified experiment. Extend gesture latency/sensitivity, client and
   concurrent-load, electrical and long-term qualification; preserve requested
   samples, timestamps, low-power ODR and inactive shutdown.
2. **Touch:** [active/idle audit and fault-backoff candidate](touch-power-review.md)
   completed and AP-only installed as `96b2e1aa`. Preserve subscriber-driven raw
   touch and existing
   reset-held global-off/no-subscriber lifecycle; no verified gesture-mode command.
   Host fault/release/retry/lifecycle tests pass. The operator confirms normal
   first touch, held/released contacts, button/motion wake; UART verifies 32 clean
   retained returns after a commanded restart. Preserve the first AP=0x4f boot.
   The up-to-1 s stuck-INT detection/missed-short-tap tradeoff remains; physically
   qualify forced INT/I2C faults and QMI/RTC shared-bus recovery, then measure
   electrical effects.
3. **CP console:** establish balanced clock/owner lifetime and RX/fault-output needs.
   AP UART gating does not release the CP logging clock. Keep heartbeat/supervision.
4. **Shared DMA/domain:** coordinate last-owner clock release across AP channels and
   CP users. Never gate the global controller/domain from one MIC/SD/display client.
5. **Memory:** identify parts and CP owners, then add supported balanced retention/
   self-refresh votes. No AP-only shared-flash deep-power-down or live-PSRAM removal.
6. **Standby:** unsupported requests now fail early in the unflashed candidate.
   USB bypasses the battery latch; no supported Pebble boot-bit contract is known.
   Establish storage quiescence and battery-only latch/recovery qualification
   separately from AP retained sleep and panel sleep; see the 14 November review.

## Deferred CP deep sleep

The mask-only peripheral failure and real-WFI resets are preserved in
[the CP investigation](coordinated-sleep.md). Production cannot enter this path.
Any future restart needs a specific new hypothesis and independent wake/interrupt,
shared-clock/voltage, memory and Bluetooth evidence; do not remove admission guards.

## Final display work

Keep the current dim behavior. Revisit brightness/refresh policy, explicit panel
Sleep In/wake and source-clock lifetimes only after the preceding work or explicit
blockers. Recheck the inherited local QSPI bypass state; shared source release
needs ownership proof. See the [QSPI/DMA scope](qspi-dma-power-scope.md).
The historical DMA IRQ path was slower at its qualified two-row batch. The current
batching differs, so require mixed-owner qualification and matched energy/latency
before changing completion again. No display power saving is currently claimed.

Applied-command deduplication is implemented in the 14 November now-installed
candidate; see [standby/display review](standby-display-review.md). It does not
change refresh cadence, dim policy or clock lifetimes, and has no electrical
saving claim. Existing hardware release gates above remain open.

## Next on-but-dim investigation (not implemented)

- [ ] Profile actual current-watchface redraws, dirty rows, transferred bytes and
  AP wake activity at fixed dim brightness. The compositor already skips clean
  framebuffers and passes dirty-row ranges; establish unnecessary work first.
- [ ] Reduce repeated unchanged-content redraws and over-wide invalidations without
  changing displayed time/content, wake responsiveness or shared Pebble policy.
- [ ] Keep seconds/animation suppression, lower dim brightness and panel internal
  modes as explicit visible-behavior choices, separate from unchanged-work removal.
- [ ] Prove local QSPI ownership/drain/restoration before any additional gate;
  retain shared source/GDMA/flash/PSRAM votes until ownership is established.
- [ ] Measure matched idle power and energy per completed redraw workload. Less
  CPU/QSPI work does not quantify panel-side or whole-watch savings.

## QSPI/DMA scoped work — 14 November

The [QSPI/DMA scope](qspi-dma-power-scope.md) ranks diagnostics/acquisition
correctness, conditional local QSPI auto-gating, shared GDMA hardware auto-gating
and later QSPI source-clock lifetime. No policy was implemented or flashed by
this scoping work. DMA init explicitly forces global bypass; QSPI init preserves
its inherited bypass bit, so the historical "already clear" observation is not
a source guarantee. AP-only last-owner accounting does not prove CP ownership.

- [x] Bounded snapshots/counters and QSPI PM-acquisition failure handling with
  actual-source fault tests and both release builds; see package 1 above.
- [ ] Complete matching installed CP97 linked-use inventory; current evidence is
  insufficient for shared DMA gating.
- [ ] Establish actual QSPI bypass state; add no redundant gate if already clear.
- [ ] Prove whole-controller/CP safety before a separate default-off GDMA
  automatic-gating trial. No global off/reset/domain removal.
- [ ] Design thread-context, generation-aware QSPI source release only after
  exclusive/CP ownership proof and measured idle break-even. No PM exchange in IRQ.
- [ ] Qualify mixed DMA/peripheral completion and matched completed-workload
  energy on stable controls; retain startup/failure evidence and all existing gates.
