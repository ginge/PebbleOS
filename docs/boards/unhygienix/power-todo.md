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

## 1 Establish the baseline

- [ ] Investigate making Wi-Fi an optional feature, disabled by default for
  Unhygienix because this device does not use Wi-Fi. Trace AP/CP build options,
  startup initialization, background tasks and clock/power votes; determine
  whether Wi-Fi can be excluded or fully shut down while preserving Bluetooth,
  shared RF resources and CP supervision. Identify any pinned-CP rebuild or
  protocol requirements, and measure idle current before/after before choosing
  an implementation.
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

## 2 Balance non-display peripheral clocks

- [ ] Add balanced I2C clock acquisition/release and validate local automatic
  gating. Wait for physical STOP/bus-free before gating; retain recovery and
  timeout behaviour on the shared touch/RTC/IMU bus.
- [ ] Add balanced UART local gating and CP clock-vote release where supported.
  Drain TX first and define the required RX/wake behaviour.
- [ ] Reduce routine production serial traffic while retaining bounded fault
  diagnostics; compare current with and without the debug connection.
- [ ] Define shared backup-domain/GDMA ownership for microphone, optional SD
  and display. Preserve all active owners; do not gate the whole domain from
  an individual driver. Defer display-specific changes to phase 9.
- [ ] Verify error, cancellation and repeated initialization paths release each
  acquired resource exactly once.

## 3 Implement the wake timer and tickless idle

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
qualified two-row batch size; reassess energy as well as frame time.

- [ ] Define active, dim always-on and asleep AMOLED states. Separate the
  current nonzero brightness floor from true screen sleep.
- [ ] Qualify dim brightness, refresh policy and startup brightness on hardware.
- [ ] Implement the panel-qualified Display Off/Sleep In and wake sequence,
  including delays, redraw and serialized transitions with in-flight DMA.
- [ ] Gate display QSPI locally between transfers; balance CP power/clock votes
  at suitable idle boundaries without disturbing other domain owners.
- [ ] Fix shared GDMA IRQ ownership before reconsidering display completion
  interrupts: microphone shutdown must not disable an active display route.
- [ ] Compare polling and IRQ completion energy, latency and frame time while
  preserving batching and overlap. Keep the measured winner.
- [ ] Measure dim-watchface and panel-asleep states separately; qualify wake,
  brightness changes and concurrent microphone/SD activity.
- [ ] Repeat the combined regression and overnight soak after display changes.
