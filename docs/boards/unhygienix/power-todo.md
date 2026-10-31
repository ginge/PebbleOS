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
No new power policy is enabled by default and no savings are claimed.

## 1 Establish the baseline

- [x] Investigate optional, default-off Wi-Fi: [findings and 31 October revalidation](wifi-power.md).
  Native AP has no Wi-Fi stack; verified stock/recorded-installed CP initializes
  Wi-Fi and acquires MAC/PHY/clock/RF votes. Present idle electrical state is
  unmeasured. Current builds and last recorded installed AP/CP are frozen with
  hashes under `artifacts/wifi-optional-20261030/baseline/` in the reference workspace.
- [ ] **Wi-Fi implementation — blocked:** pinned runtime deinit is unsupported.
  Vendor Bluetooth-only build selects different PHY/calibration code; current
  fixed-hash/fixed-address CP hook packaging cannot accept a relinked CP. Stage
  a reproducible CP build, resolve VNET/CLI/PM dependencies and introduce verified
  off/on CP variants with default off. The 31 October staged CP-only build
  previously stopped before configuration. CMake and verified vendor GCC 10.3
  are now provisioned in an isolated environment; the continuation records the
  later CP build disposition: configuration/prebuild pass, then C compilation
  stopped at the missing matching TuyaOpen-generated `tuya_kconfig.h` header.
  The later timer continuation recovered matching archived hardware configuration
  and completes the isolated Wi-Fi-on CP ELF/bin build. Effective CONFIG matches
  pinned stock; binary hashes differ (linker veneers), so exact reproduction and
  guarded off/on variant integration remain open. The project unconditionally registers a CIF customer handler
  and requires VNET/lwIP; resolve those separately from HCI/SYS IPC.
  Preserve Bluetooth, shared RF/PHY,
  mailbox services and supervision; see the exact [integration steps](wifi-power.md#exact-implementation-dependency-and-next-step).
- [ ] **Wi-Fi qualification — pending:** build/test both CP variants, verify
  service/Bluetooth regressions and measure matched idle/workload current against
  a verified baseline. The FNB58 is now available: 1,976 samples on 31 October
  averaged 29.486 mA at 4.98388 V (USB, minimum dimming, Bluetooth connected).
  Charging state and installed identities remain unknown; this is a setup reading,
  with no off/on comparison or savings claim. No new firmware flashed. Investigation completion does not close implementation or
  device/electrical qualification.
- [ ] Record firmware/AP and CP identities, effective build configuration,
  fitted cell, supply voltage and measurement setup.
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
idle windows recorded with display settings fixed. CP readback, sparse complete
runtime state and the completed-workload matrix remain open.

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
- [ ] Define shared backup-domain/GDMA power ownership for microphone, optional SD
  and display. Preserve all active owners; do not gate the whole domain from
  an individual driver. Defer display-specific changes to phase 9.
Native SD already clears its FIFO `SD_CLOCK_ALWAYS_ON` override after CMD12 and
status-ready polling. It still retains controller bypass and the CP-managed
source/domain vote. Audit those separately, including final card busy/drain,
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
