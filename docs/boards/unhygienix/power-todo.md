# Unhygienix power TODO

Power optimization is the highest-priority workstream as of 30 October 2026.
Complete the non-display phases first. Leave all display power work until the
final phase, including AMOLED states, display QSPI gating and display DMA waits.

Source: [code power audit](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-30-power/audit.md).
Main checklist: [validation and remaining work](../../../boards/unhygienix/tuya/REMAINING_TASKS.md).

Make production changes in `/home/baz/coredevices/pebbleos`, scoped to the board,
T5AI SoC or individual drivers. Preserve stock shared code and existing companion
compatibility. Check off implementation and device qualification separately;
source inspection and host tests do not establish electrical savings.

Local correctness history is tracked in [current status](current-status.md).
A5 was committed in `add66a5e5`; the subsequent native-driver migration is in
`2eacc9eaa`; I2C qualification was subsequently committed as `198385cb0`. The 31 October entry AP was verified as native PSRAM build
`6b314eb5…`; historical A5 installed/default claims are superseded. Current DMA
ownership is centralized in `drivers/dma/t5ai.c`. Physical correctness/stability
qualification remains open. See the [31 October continuation](non-display-power.md)
for exact images, measurement limitations and ordered implementation dependencies.
No new power policy is enabled by default. Bounded USB measurements below are
separate from calibrated or fully hardware-qualified savings.

## 1 Establish the baseline

- [x] Investigate optional, default-off Wi-Fi: [findings and 31 October revalidation](wifi-power.md).
  Native AP has no Wi-Fi stack; verified stock/recorded-installed CP initializes
  Wi-Fi and acquires MAC/PHY/clock/RF votes. Per-block Wi-Fi electrical state is
  unmeasured; bounded system input-power comparisons are recorded below. Current builds and last recorded installed AP/CP are frozen with
  hashes under `artifacts/wifi-optional-20261030/baseline/` in the reference workspace.
- [x] Build isolated board-owned Wi-Fi off/on CP profiles with pinned inputs,
  conditional VNET/lwIP/PHY dependencies, source RF correction, exact image/config
  guards, relocation-verified latency correction and native ABI contract.
  Preserve the readback-verified installed controller flash ACK/deadline contract
  in ITCM; 25 focused host tests pass. See [the latest implementation](wifi-power.md#source-built-offon-profiles--31-october-2026).
- [ ] Integrate a qualified production default-off selection. Experimental
  packaging defaults to off, but normal CP packaging/runner and installed control
  selection remain separate. Final corrected raw/config hashes reproduce across clean source builds;
  device/PFS/service behavior and energy acceptance remain gates. Runtime deinit
  remains unsupported. No fixed-hash/address guard was weakened.
- [ ] **Wi-Fi qualification — bounded measurements and 25 reconnects complete:**
  initial off `e48465c3…` faulted through a NULL calibration callback; its complete
  first failure and exact rollback are retained. Pinned callbacks are now kept
  independently of Wi-Fi, covered by the actual ARM path in 25 host tests.
  Corrected off `d4d65559…` is installed with AP `c656055a…`; actual on/off CP
  readbacks match the guarded raw/protected packages and CRCs. Existing SD PFS
  reopens and bonded/encrypted Bluetooth reconnects.
  Quiet idle off/on is 148.091 / 150.332 mW: 2.240 mW (1.490%) lower in a single
  pair. Earlier notification-contaminated controls are secondary; no fully quiet
  idle ABA is established. Fixed-dim-Nadir on/off/on workloads completed five
  encrypted/protocol recoveries per batch: 9.117362 / 8.999244 / 9.196576 J per
  completion. Off is 1.7225% below the mean flanking controls; on drift is 0.8688%.
  The separate 25-consecutive-cycle run completed exact disconnect/encryption/
  protocol counts and full tail, with post-run dim Nadir/BT confirmation and
  delayed review. Known ATT errors are retained; its energy is excluded because
  notifications occurred during settling. Failed readback handshake, successful
  retry and all excluded measurement attempts remain preserved.
  Pebblecraft `0x6e69616c/0x1b0d` and watchface `0/0xc89` faults reproduce on exact
  original CP `1713dee…`; root cause and application/PFS integrity remain open.
  True all-power-removed cold boot is pending; overnight observation is deferred.
  Production default-off integration remains gated. No calibrated battery-life
  or fully hardware-qualified saving is claimed. See [current status](current-status.md).
- [x] Record firmware/AP and CP identities, effective build configuration,
  fitted cell, supply voltage and measurement setup. Frozen AP `c656055a…`,
  actual on/off CP readbacks and guarded effective configs are retained. Setup:
  USB/no cell, dim Nadir, bonded Bluetooth, authorized USB Android phone with
  scan policy 1; workload mean supply 4.988343–4.997812 V. Numeric panel luminance
  and battery-side current are unmeasured; FNB58 records USB input (0.15 W is
  150 mW, not mV). This records the baseline, not hardware qualification.
- [ ] Measure connected idle, disconnected advertising, scripted interaction,
  app transfer, motion wake, and microphone recording followed by idle. Hold
  display brightness and content fixed throughout the non-display phases.
- [ ] Record sparse counters for IRQ wakeups, actual CPU clock, PM votes,
  peripheral clock state, sensor ODR/watermark, touch state and negotiated BLE
  interval/latency. Avoid continuous logs that distort the measurement.
- [ ] Preserve current controls: one-second CP heartbeat, ADC cleanup, microphone
  audio shutdown, disabled amplifier/gyro, unused-accelerometer shutdown and
  touch reset on disable.

31 October baseline progress is recorded in [the continuation](non-display-power.md):
AP identity and no-cell USB setup verified, exact off/on images frozen and matched
idle windows recorded with display settings fixed. The later CP readbacks and
counted reconnect workloads are in [current status](current-status.md). Sparse
complete runtime state and the remaining workload matrix are still open.

## 2 Balance non-display peripheral clocks

- [x] Implement default-off local I2C bypass release/acquisition with sanitized
  STOP/fault/cancel/recovery tests and normal image validation.
- [ ] Qualify local automatic gating and a repeatable electrical benefit; system
  source-clock/CP vote release is still separate. The option remains default off.
  Wait for physical STOP/bus-free before gating; retain recovery and
  timeout behaviour on the shared touch/RTC/IMU bus.
- [ ] Add balanced UART local gating and CP clock-vote release where supported.
  Drain TX first and define the required RX/wake behaviour.
- [ ] Reduce routine production serial traffic while retaining bounded fault
  diagnostics; compare current with and without the debug connection.
- [x] Centralize AP GDMA interrupt/channel ownership in the native DMA controller
  (existing migration); preserve CP channels. Host isolation tests pass.
- [x] Implement default-off SD controller-local bypass release after card-ready,
  command completion and drained DMA; reacquire before MMIO/IRQ delivery. Native
  failure/ownership tests and matched image checks pass. See [SD clock experiment](sd-idle-clock.md).
- [ ] Qualify SD local gating, electrical benefit, durable PFS and physical fault
  recovery. Bounded existing-PFS/bonded startup and fixed-display idle/five-reconnect
  off/on/off windows are complete, without a causal/repeatable saving established.
  Cold-start captures are incomplete; durability, physical faults, 25 cycles and
  deferred overnight qualification remain open. Source/domain/CP votes stay held;
  default off.
- [ ] Define shared backup-domain/GDMA power ownership for microphone, optional SD
  and display. Preserve all active owners; do not gate the whole domain from
  an individual driver. Defer display-specific changes to phase 9.

Native SD already clears its FIFO `SD_CLOCK_ALWAYS_ON` override after CMD12 and
status-ready polling. The default retains controller bypass and the CP-managed
source/domain vote. The opt-in experiment releases only local bypass; continue
qualifying it separately from domain release, including final card busy/drain,
fault recovery and CP ownership; clearing a FIFO override does not establish
safe source-clock/domain release. Preserve storage geometry and the existing PFS.

- [ ] Verify error, cancellation and repeated initialization paths release each
  acquired resource exactly once.

See [current ownership/dependency findings](non-display-power.md#non-display-dependencies-and-concrete-next-actions)
for CP-owned runtime UART, drain/RX semantics, shared-domain arbitration, the
single CP-managed AON RTC and native wake-notification prerequisites. These are
open gates, not implemented suspend behavior.

## 3 Implement the wake timer and tickless idle

- [x] Implement an opt-in ordinary-WFI timer contract using the AP-owned SysTick,
  acknowledged CP veto/vote guards and a conservative 160 MHz transient floor.
  Native sanitizer tests and matched release image checks pass. This is separate
  from CP AON wake routing and retained sleep. See [timer experiment](tickless-wfi.md).
- [ ] Qualify that opt-in contract on hardware: steady AON/tick accounting,
  deadlines and heartbeat, connected idle/workload energy, external wake, transfers,
  faults, reconnect/cold-power and overnight soak. Keep default off.

- [ ] Establish ownership of an AON compare or another suitable low-power wake
  timer and qualify its AP/CP interrupt and wake routing.
- [ ] Replace the T5AI RTC alarm stubs with a real deadline implementation.
- [ ] Use `expected_idle_ticks` to arm the next deadline and suppress SysTick
  during idle only after the wake mechanism works.
- [ ] Account elapsed AON time exactly once, preserving fractional ticks,
  counter wrap, early external wake, pending interrupts and clock changes.
- [ ] Verify scheduler deadlines, RTC progression, buttons, mailbox reception,
  CP heartbeat and reconnect behaviour before proceeding to deeper sleep.

## 4 Implement coordinated retained sleep

- [ ] Add T5AI sleep blockers for mailbox transactions, flash/PFS work, I2C,
  UART TX, microphone/SD DMA and the existing display transfer lifetime.
- [ ] Implement the native-kernel equivalent of the pinned BK7258 CP/AP sleep
  handshake, including the final idle check, wake routing, sibling AP state,
  required peripheral retention and clock restoration.
- [ ] Replace the permanent application sleep veto only after suspend/resume
  and wake-source validation pass. Do not simply flip the current vote.
- [ ] Distinguish retained sleep from reset-on-wake deep sleep. Preserve all
  live state and the CP heartbeat contract.
- [ ] Verify standby separately: confirm the pinned bootloader honours the
  standby boot flag and establish power-latch behaviour before claiming off.

## 5 Reduce sensor wakeups

- [ ] Measure QMI8658 wakeups and I2C cost with motion backlight on/off and
  with representative sampling subscribers.
- [ ] Qualify hardware motion/wake-on-motion on the fitted silicon revision,
  or evaluate bounded FIFO batching for software shake detection.
- [ ] Preserve requested sample rates, timestamps, sensitivity and gesture
  latency; retain the existing low-power ODR and inactive shutdown paths.
- [ ] Requalify bus timing/overflow before increasing FIFO limits. Evaluate
  400 kHz only with every device on the shared bus and compare total energy.
- [ ] Define touch active/sleep states for the intended wake policy. Use
  reset-held sleep when buttons suffice, or qualify low-power gesture wake.
- [ ] Confirm subscriptions/session filtering result in the intended hardware
  state; test stuck INT, contact release, read failures and retry recovery.

## 6 Qualify shared memory power states

- [ ] Identify fitted PSRAM/flash parts, their retention modes and CP ownership.
- [ ] Add balanced CP-managed memory votes and retention transitions only when
  supported by all clients. Preserve live framebuffer/cache allocations.
- [ ] Qualify flash sleep through CP coordination; never issue AP-only deep
  power-down to memory still used by CP.
- [ ] Verify cache coherency, PFS integrity, SD concurrency and wake latency.

## 7 Evaluate CPU and radio energy

- [ ] Compare fixed 120 and 240 MHz votes using energy per completed workload
  and idle current. Record the actual elected clock, not just the request.
- [ ] Add coarse activity-based frequency votes with hysteresis only if measured
  savings justify them. Preserve tick calibration and CP voltage/PLL sequencing;
  avoid a mailbox frequency vote on every idle entry.
- [ ] Verify accepted BLE interval/latency and time in fast/idle states before
  changing the existing idle-latency policy. Check CP radio/module votes rather
  than inferring Wi-Fi or RF power from AP inactivity.

## 8 Qualify non-display savings

The operator deferred the overnight soak to a later overnight window. It remains
an open qualification gate; no soak is running or recorded as passed.

- [ ] Compare each change against the baseline, with display configuration held
  fixed. Record average current, energy per completed task and wake latency.
- [ ] Repeat sustained phone/app transfers, 25 reconnect cycles, cold-boot
  bond/watchface restoration and overnight connected idle with interaction.
- [ ] Check timer deadlines, watchdogs, heap/stack, flash/PFS integrity and
  microphone/SD/display concurrency. Preserve the first failure trace.

## 9 Optimize display power last

Start this phase after completing or explicitly recording blockers for the
non-display phases above. Preserve the earlier measured polling baseline:
[the current status](current-status.md) records IRQ completion as slower at the
historical two-row batch size; the current native transport has different
batching/completion. Reassess energy as well as frame time at its actual size.

- [ ] Define active, dim always-on and asleep AMOLED states. Separate the
  current nonzero brightness floor from true screen sleep.
- [ ] Qualify dim brightness, refresh policy and startup brightness on hardware.
- [ ] Implement the panel-qualified Display Off/Sleep In and wake sequence,
  including delays, redraw and serialized transitions with in-flight DMA.
- [ ] Gate display QSPI locally between transfers; balance CP power/clock votes
  at suitable idle boundaries without disturbing other domain owners.
- [x] Centralize shared GDMA IRQ/channel ownership (native migration); the old
  facade/teardown path is retired. This alone is not physical qualification.
- [ ] Qualify mixed microphone/display/SD operation and CP coexistence before
  reconsidering any display completion experiment.
- [ ] Compare polling and IRQ completion energy, latency and frame time while
  preserving batching and overlap. Keep the measured winner.
- [ ] Measure dim-watchface and panel-asleep states separately; qualify wake,
  brightness changes and concurrent microphone/SD activity.
- [ ] Repeat the combined regression and overnight soak after display changes.
