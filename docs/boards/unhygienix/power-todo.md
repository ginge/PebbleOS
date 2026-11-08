# Unhygienix power TODO

Work on CP-awake AP sleep and local peripheral lifetimes first. Keep CP deep
sleep deferred, the normal 240 MHz active vote, dim display policy, Bluetooth
identity/calibration, heartbeat, timer accounting, flash/PFS integrity and shared
ownership intact. Production code and hardware release qualification are separate.
Use [current status](current-status.md) for installed identities and evidence.
Historical findings remain in [the investigation history](power-investigation-history.md).

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

## Correctness and measurement gates

- [ ] Diagnose Bluetooth discovery/reconnect and connection-parameter failures using aligned AP/CP/phone evidence.
- [ ] Confirm actual AP sleep entries and blockers with dim Nadir; the current revision's activity milestone was not received.
- [ ] Measure each local gate and combined policy against matched controls with fixed display/supply.
- [ ] Compare energy per completed reconnect/transfer/UI workload, elapsed time and idle power.
- [ ] Exercise durable SD/PFS writes, physical bus/DMA faults and recovery without unsafe partial-write retry.
- [ ] Verify early/pending/stale wakes, deadline/AON wraps, tick accounting and independent guard recovery on hardware.
- [ ] Qualify concurrent microphone, SD, display and CP operation, including cancel/abort paths.
- [ ] Complete 25 consecutive reconnects on this AP, then fully captured cold bond/face restoration.
- [ ] Complete deferred overnight connected idle with UI interaction; preserve the first failure.

## Remaining peripheral implementations

1. **Sensors:** profile QMI IRQ/watchdog/I2C cost; implement and qualify fitted-silicon
   wake-on-motion or bounded batching without losing requested samples, timestamps,
   software-shake sensitivity or gesture latency. Preserve low-power ODR and inactive
   shutdown. Fitted register identity 05/7c is recorded; WOM/INT2 is not qualified.
2. **Touch:** define active/idle policy. Reset-held disable already exists; integrate
   it when buttons suffice or qualify a controller gesture-wake mode. Exercise stuck
   INT, contact release, retry and shared-bus recovery.
3. **CP console:** establish balanced clock/owner lifetime and RX/fault-output needs.
   AP UART gating does not release the CP logging clock. Keep heartbeat/supervision.
4. **Shared DMA/domain:** coordinate last-owner clock release across AP channels and
   CP users. Never gate the global controller/domain from one MIC/SD/display client.
5. **Memory:** identify parts and CP owners, then add supported balanced retention/
   self-refresh votes. No AP-only shared-flash deep-power-down or live-PSRAM removal.
6. **Standby:** qualify bootloader flag and board power latch separately from sleep.

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
