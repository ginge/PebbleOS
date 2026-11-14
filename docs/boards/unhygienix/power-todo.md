# Unhygienix power TODO

Work on CP-awake AP sleep and local peripheral lifetimes first. Keep CP deep
sleep deferred, the normal 240 MHz active vote, dim display policy, Bluetooth
identity/calibration, heartbeat, timer accounting, flash/PFS integrity and shared
ownership intact. Keep CP frequency and closed-source settings unchanged.
Production code and hardware release qualification are separate.
Use [current status](current-status.md) for installed identities and evidence.
Historical findings remain in [the investigation history](power-investigation-history.md).

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
blockers. Local QSPI bypass is already clear; shared source release needs ownership.
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
