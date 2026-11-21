# Unhygienix sensor power review — through 13 November 2026

The initial 31 October review was source-only; no sensor register, sampling,
shake, display or installed firmware change was made during that Wi-Fi
qualification. Later hardware increments and their limits are recorded below.
Current normal AP `4545e34ad182f3f80411b93f46c2dbdf364a9e5f` includes default
quiet-period WOM and the installed readability/avoidable-work cleanup. Bounded
fitted 05/7c WOM entry/exit/INT2 and operator first-gesture checks pass, including
final wake/touch/buttons on the normal cleanup image. UART verifies 32 AP sleep
returns and encrypted phone recovery. Electrical savings, long-term sensitivity/
latency and client/concurrent-load qualification remain open. See the November
sections below and [current status](current-status.md) for exact identities.
The source-only conclusions and required-work list immediately below describe
the original 31 October review; they are superseded where the later evidence applies.

## Reference WOM helper is incomplete for the documented C procedure

Pinned TuyaOpen board `qmi8658.c` has `qmi8658_enable_wake_on_motion()` and a
matching disable helper. Enable disables sensors, selects 2 g / low-power 21 Hz,
writes threshold to `0x0b`, writes zero to `0x0c`, then enables acceleration.
Neither helper issues a CTRL9 command or performs its completion handshake.
The helper name alone does not establish working WOM on this watch.

[QST QMI8658C Rev A](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf),
sections 12.2–12.6 (pp. 67–68), requires applying CAL1 settings through CTRL9
command `0x08` for entry and exit. CAL1_H selects the interrupt, initial polarity
and blanking interval; zero selects INT1. Reading STATUS1 acknowledges a WOM
event. This is a C-documentation comparison, not identification of fitted silicon.

Native `drivers/imu/qmi8658/qmi8658_t5ai.c` uses INT2 and an existing bounded
CTRL9 handshake for FIFO operations. Initialization reads WHO_AM_I and revision,
but a successful revision is logged only at DEBUG. The frozen power AP has no
new revision/interrupt evidence. Its current software shake needs every sample:
watermark one and at most 32 ms sampling, with existing sensitivity/holdoff.
Do not silently replace that gesture contract with a low-power WOM threshold.

## Original required work — 31 October (see November disposition below)

1. Record fitted WHO_AM_I/revision and verify the corresponding command/status
   behavior in a separate qualification image or read-only diagnostic window.
   The older accel-reset artifact explicitly lacked independent live-sensor
   confirmation; a reset without an init error does not fill this gap.
2. Measure motion-backlight on/off IRQ, watchdog, I2C time and completed gestures
   with identical display settings. Existing opt-in QMI/I2C profiling can count
   these, but its logging/build changes need identical control/candidate setup.
3. Implement WOM entry/exit locally in the native driver, using the qualified
   command handshake, INT2 route and real acknowledgment behavior. Balance
   subscriber/rate changes, cancellation, failure recovery and return to normal
   FIFO acquisition. Preserve requested high-rate clients and gyro-off policy.
4. Qualify thresholds against shake sensitivity/latency and false positives,
   static/no-motion, repeated wake/rearm, concurrent touch and I2C errors. Compare
   energy per completed gesture as well as idle. Keep 100 kHz and current FIFO
   bounds until shared-bus timing and overflow behavior pass separately.

WOM interrupt behavior must then join the complete suspend/wake coordinator;
working sensor interrupts alone do not permit removing the application sleep veto.

## Sensor baseline and dispatch candidate — 12 November 2026

The operator confirmed dim Nadir, motion backlight enabled and the cell disconnected.
An AP-only profiling baseline (`277a810709f23ecdbd9e46a5c9d45ce6da24d5b8`)
received live QMI identity **05/7c**, shake-only configuration (no sample batch),
125 Hz requested ODR, watermark one and a 100 ms watchdog. Encrypted bonding and
protocol-session recovery were received. Identity does not qualify WOM commands
or interrupt acknowledgement on this revision.

A bounded FNB58 baseline received 8,972 samples at mean 4.99750 V,
28.8642 mA / 144.249 mW USB input. This is a single instrumented control,
not a battery-current measurement or a savings result. The acquisition trace
contains conversion-counter gaps; configured 125 Hz must not be reported as
lossless sample delivery. Full raw data, dictionaries, hashes, configuration,
source snapshots and the complete-window analysis are in the reference workspace
under `artifacts/sensor-power-20261112/`.

The driver-local candidate processes shake-only samples on the existing QMI
worker under the accelerometer device lock, after releasing the I/O lock.
Data clients retain NewTimer delivery. This removes the per-sample offload for
shake-only use without changing ODR, sensor registers, shake thresholds/holdoff,
watchdog period, CP clocks or closed-source settings. Configuration generation
checks and pending-slot completion are retained. Seven sensor/I2C host tests
pass with address/undefined-behaviour sanitizers, including queue saturation,
shake/holdoff and subscription/disable changes between acquisition and delivery.
These tests do not qualify worker stack headroom or hardware latency.

The first candidate (`d365ef76e722fbe046793e7b79515aa808010b07`)
booted, but its initial capture stopped receiving sensor progress. The operator
reported the watch responsive. Host programmer DTR/RTS subsequently failed with
USB protocol error -71; two cleanup rollback attempts stopped before a write.
After the operator reconnected the UART adapter, sustained acquisition resumed
without bus errors/recoveries in the accepted windows. The operator confirmed
wake, touch and buttons worked normally. The interrupted trace is inconclusive
and preserved separately; it does not establish a firmware stall. A matched
90-second window measured 142.993 mW against 144.249 mW control, only 0.87%
lower, with essentially unchanged energy per acquired sample. This alone does
not establish an electrical saving.

### Initial shake-only rate correction

Initialization also treated the default 8 ms interval as an actual data-client
request. On first shake enable, that unnecessarily retained a 125 Hz request
with no data client. The initial requested interval now uses the existing 32 ms
software-shake limit. Explicit rate requests still take precedence, including
8 ms requests while shake is enabled. The active-data acquisition/FIFO policy,
filter thresholds, sensitivity controls, holdoff, 100 ms watchdog and inactive
shutdown are unchanged. Sampling at 32 ms can differ from an 8 ms default in
response and filtering; register equality or host tests do not prove gesture
equivalence. This change therefore requires the operator check and further
sensitivity/latency qualification, rather than an assertion of identical timing.

The corrected profiling image is `256127d26ad5f3735ec03c635e347867832274a3`.
Received records confirm 05/7c, 32 ms shake-only operation, encrypted bonding and
protocol recovery. The 90-second USB window measured 140.548 mW at 4.99793 V.
Complete sensor windows inside that recording show 30.21 IRQ/s and 151.04 I2C
IRQ/s versus 61.79 and 309.05 in the original control. Watchdog callbacks remain
about 10/s. No conversion-counter gaps, bus errors, discards or recoveries were
reported in those candidate windows; the original control has counter gaps.
Acquired sample throughput differs by design, so energy per acquired sample
is not a comparison of completed gestures. Maximum profiled processing age
rose from 21 to 46 ms; this metric is not physical gesture-to-display latency.
The operator confirmed normal wake and controls on this rate. However, this
first rate-candidate boot recorded AP reset reason 0x4f and `T5SLEEP failed
reason=4 gen=0`: retained sleep was closed for that entire recording. Its power
comparison is therefore rejected as unmatched, even though the sensor IRQ counts
are received evidence. A repeat control has clean AP sleep entries. The rerun
uses the existing explicit-off AP control between programming transitions, then
requires a clean sleep contract and received 32-entry milestone with zero CP
transition/fault deltas before measuring. No guard is cleared or weakened.

The corrected profiling-off image is `196be9b720be760e0aadab48131714acf2da9949`.
It passes image/map/protected-CRC and linked SRAM wake-gate checks. Relative to
the frozen cleanup ELF, text grows by 48 bytes while data/BSS totals are
unchanged. It has not been flashed at this checkpoint. The seven targeted
sanitized host tests pass after the rate correction too. High-rate clients,
worker stack headroom, concurrent load and long-term false-positive behavior
still require hardware qualification.

WOM remains deferred pending fitted-silicon command/INT2 qualification and gesture
sensitivity/latency evidence. USB input measurements do not establish battery
life or fitted-cell sleep current.

### Matched rerun and disposition

The clean rate-candidate rerun received AP/CP reset sources 0/0, contract 0x78f,
and 32 AP returns with all CP transition/fault deltas zero before measurement.
The operator power-cycled the watch after two download-mode handshake failures;
that recovered programming from the temporary explicit-off control. This does
not establish that the intermediate-control procedure prevents programming faults.

| Instrumented window | USB input | Sensor IRQ/s | I2C IRQ/s |
| --- | ---: | ---: | ---: |
| Original 8 ms-request control | 144.249 mW | 61.79 | 309.05 |
| Repeated original control | 143.233 mW | 47.31 | 236.53 |
| Clean 32 ms-request candidate | 142.999 mW | 28.87 | 144.36 |

Each power window received about 8,980 CRC-checked meter samples over 90 seconds.
The sensor columns use complete interior profile windows. All three boots had
clean reset sources and received the 32-return AP-only milestone. The clean
candidate has no bus errors, host discards or recoveries in its matched windows,
but reports 101 conversion-counter gaps; it is not lossless sample qualification.
Maximum processing age is 45 ms. The original controls report 4,140/5,532 gaps.
Different settled throughput, serial recovery, cold/warm boot histories and
intervening trials limit a simple attribution to this patch.

Candidate USB power is only 0.516% below the mean controls, while the repeated
control drifts by 0.704%. **No reliable electrical saving is established.**
The demonstrated reduction is sensor/I2C IRQ frequency and elimination of the
per-sample NewTimer dispatch in shake-only use. Watchdog callbacks remain about
10/s. Acquired-sample energy is not a completed-gesture comparison, and worker
CPU alone omits work moved from NewTimer. Battery-life claims remain unsupported.

Normal build `196be9b720be760e0aadab48131714acf2da9949` was flashed AP-only.
Its first post-programming boot again received AP=0x4f and closed retained sleep.
A subsequent commanded warm restart of that same installed image received clean
0/0 reset sources and the 0x78f contract. The initial boot and programming traces
are preserved; no sleep guard or reset reason was changed in source. Final AP
return/protocol/UI confirmation is recorded in the session result and current
status. Warm-programming reset behavior remains a qualification issue, not a
sensor-power success claim. Profiling is disabled in the normal image.


## 13 November: retain direct interrupt pulses while the raw slot is busy

QST Rev A sections 6.2–6.3 describe bypass-mode, non-SyncSample INT2 DRDY as a
short pulse at ODR that clears automatically. The driver previously masked INT2
for the entire acquisition/delivery lifetime, then re-enabled through a HAL that
clears pending state. The FIFO level catchup cannot recover a vanished direct
pulse. Registers and the qualified non-SyncSample mode are unchanged.

The driver now keeps direct INT2 armed and records a coalesced edge token while
the raw slot is occupied. At read start it consumes preceding edges under the
IRQ lock. A pulse during I/O or delivery requests one follow-up after that slot
is released. It never reads or overwrites raw data on the ISR/NewTimer thread,
and a high pad alone cannot create a loop. Reconfiguration clears the token;
transfer recovery masks interrupts. FIFO masking and bounded level catchup stay
as before. Optional diagnostics add busy/empty/duplicate/gap-event/max-gap counts.

The regression first failed against the masking implementation, then passed
with pulse coalescing. Eight ASan/UBSan host tests cover sensor startup, I2C and
QMI transitions, including pulses during reads/deferred delivery, coalescing,
duplicate suppression, stuck-high behavior, disable/reconfiguration and transfer
recovery. The normal build adds 96 bytes text and no data/BSS against the prior
normal build. Its configuration is byte-identical; all profiling is off.

Artifacts: `artifacts/qmi-gap-20261113` in the reference workspace. Control build
`59c83e8bffa337701905ac15b345ac816a5c73c8`, candidate
`868dcf512f542707f0454f4513a682c68aea7b1b`, normal
`d47e7b01977acc786d357f31617417fd3252f26d`. Frozen images, source diffs, hashes,
configs/maps, flash logs, received UART bytes, CRC decoding and selection scripts
are preserved. All flashes are AP-only. CP frequency and closed settings remain
untouched. The operator confirmed battery disconnected and stationary dim Nadir
with motion backlight enabled.

The first control capture lost UART progress while the watch remained responsive.
A later reset returned CH342 protocol error; adapter reconnect restored reception.
The immediate control/candidate post-flash boots recorded AP=0x4f and failed
retained-sleep admission. Same-image resets restored contract 0x78f and actual
32-return milestones with zero CP transition/fault deltas. The candidate's exact
reset-source frame was damaged; MCU mask zero and the live sleep milestones were
received. Do not infer its AP/CP reset pair from that damaged frame.

| Connected observation | Control | Candidate |
| --- | ---: | ---: |
| Selected duration | 55.168 s | 55.202 s |
| Acquired samples | 1,210 | 1,317 |
| Conversion gaps | 458 | 352 |
| Maximum conversion gap | 3 | 2 |
| Busy pulses / catchups | 0 / 0 | 28 / 26 |
| Empty reads / duplicate counter reads | 0 / 0 | 8 / 3 |
| Bus errors / host drops / recoveries | 0 / 0 / 0 | 0 / 0 / 0 |
| Maximum processing age | 46 ms | 44 ms |
| Worker CPU ms/s | 9.136 | 9.855 |

Selection is eleven complete profile windows starting at least ten device
seconds after an encrypted protocol session opens. Both runs received AP return
milestones. Boot uptime/reconnect history differ; the early candidate window
without a phone session was worse (624 gaps over 70.445 s, versus 492 over
70.246 s in the original control selection). Connected observation supports
retaining the bounded pulse fix, but does not isolate every remaining gap or
establish a reliable numerical improvement. No electrical power was measured,
and increased sample coverage entails additional I2C work. No battery-life or
lossless-stream claim follows.

The normal build was flashed with the same dim/AP-only policy and profiling off.
Final installed identity, return/protocol and operator checks are in `result.json`.
WOM was not enabled: the manufacturer says WOM supplies no sensor data, so the
remaining motion after a WOM wake must still satisfy the existing software-shake
contract. The next bounded probe must verify CTRL9 `0x08` entry and zero-threshold
exit on fitted 05/7c silicon, INT2 routing/polarity, STATUS1 bit-2 acknowledgement,
quiet stationary behavior and recovery. First-motion sensitivity/latency and
matched electrical/gesture-workload measurements remain prerequisites to any
automatic transition into WOM. The Tuya helper's missing CTRL9 handshake is not
used as evidence of working WOM.


## 13 November: WOM enabled by default after explicit operator request

The later operator request enables `CONFIG_T5AI_QMI8658_WOM` by default for this
board, superseding the earlier deferral above. This is a driver-local policy;
CP frequency, CP firmware/calibration/closed settings and shared Pebble code are
unchanged. The previous pulse fix and explicit sample-client rates remain intact.

With shake enabled and no sample clients, five seconds within 32 mg of the
software gravity baseline enters low-power 21 Hz / 4 g WOM. CAL1_L=32 mg and
CAL1_H=0x41 select INT2 initially low and one blanking sample. The driver applies
CTRL9 command 0x08 and completes its acknowledgement handshake before enabling
acceleration. Each WOM handshake phase has a bounded 200-attempt polling limit,
with a one-millisecond sleep plus bus completion per attempt. No bus operations
run on the ISR or timer callback.

WOM itself emits neither synthetic samples nor a synthetic shake. STATUS1 bit 2
acknowledges motion; the worker exits WOM with zero threshold and CTRL9 0x08,
then restores the requested continuous mode. It preserves the pre-WOM software
shake baseline and holdoff, resets the conversion-counter anchor, and retains the
100 ms startup settling gate. Thus a first gesture still depends on sufficient
motion remaining after restart. Requested data streams bypass WOM; disabling
shake exits and shuts down when otherwise unused. A peek exits WOM and obtains
a settled fresh sample rather than presenting a cached pre-WOM sample as fresh.

Quiet WOM replaces the ordinary 100 ms minimum sampling watchdog with a 5 s
health pass. The ISR still schedules immediate service for actual motion. The
health pass acknowledges a missed latched motion event and otherwise verifies
CTRL2/CTRL7; quiet is not interpreted as stalled sampling. A stuck-high pad gets
at most one immediate catchup per real edge/health pass. The normal streaming
watchdog is unchanged. A missed interrupt or sensor fault can take up to one
health interval to be noticed, after which the ordinary fast recovery path runs.

Failed entry/health checks latch continuous fallback for the rest of that boot.
Configuration exits even a partially applied WOM command. If exit fails, a
sensor-only reset restores the known interface and requested mode; no watch or
CP reset is introduced. Failed restoration remains an error/recovery state.

Ten sanitized sensor/startup/I2C host tests pass. WOM cases include entry/exit,
no fabricated shakes on WOM status alone, first settled motion using the old
baseline, subscriber/FIFO transitions, fresh peek, disable, partial commands,
sensor-only reset fallback, corrupt configuration, stuck-high handling and quiet
timer wrap. WOM bookkeeping fits existing state padding: normal data/BSS totals
are unchanged. Normal text grows by 952 bytes against the preceding pulse-fix
normal ELF. The heap minimum and SRAM wake-entry guards remain intact.

Artifacts are in `artifacts/wom-default-20261113` in the reference workspace.
Diagnostic build `69050508f2b9a1f67d7a4e5999088ea9cddc99da` demonstrates fitted
05/7c command entry, quiet health and exit/re-entry. Selected complete quiet
windows total 90.001 s: zero sensor IRQs or sample reads, 18 health callbacks,
54 register reads, and zero bus errors or sensor recovery events. This is about
0.2 callbacks/s and 0.6 register reads/s. Zero samples is intentional WOM
behavior, not lossless-stream evidence. No electrical saving was measured.

The operator reports all three first-gesture tests after at least ten seconds
stillness passed, with touch/buttons normal. Two resume transitions and three
entries were decoded on the clean retained-sleep run; do not claim three
independently decoded transitions. Contract 0x78f, 32 AP returns with zero CP
transition/fault deltas and encrypted protocol recovery were received. The exact
reset-source frame on that diagnostic boot was damaged and is not asserted.

USB evidence matters: the hub disabled/re-enumerated the CH342 during the first
capture, moving IF02 from ttyACM1 to ttyACM2 while the old reader remained open.
Reopening the stable by-ID path restored live records. A later normal-flash
attempt failed with serial protocol error before erase/write. All such traces
are retained; they are not attributed to a sensor fault. The initial post-flash
AP=0x4f admission failure is retained separately from the subsequent run with
actual AP returns.

Normal build `329155c5a2073e1289f4b4efa767723502a49114` has profiling disabled;
its sole configuration difference from the prior normal build is default WOM.
Final installation, boot/return/protocol and operator checks are recorded in
`result.json`. Every selected write is AP-only; resources/PFS/bonds are preserved.
Broader first-motion sensitivity/latency, sample-client/concurrent-load, fault
injection, electrical workload measurement and long-term soak remain open.


## 13 November: QMI readability and avoidable-work cleanup

The driver now separates direct acquisition, FIFO acquisition, quiet WOM service
and partial-WOM teardown. Acquisition order, command/ACK/resume cleanup, bounded
catchup, timestamp anchoring and recovery sequencing are retained. State remains
compact: WOM-active, partial command configuration and fallback flags retain
distinct meanings, including a partially applied WOM command. No new persistent production state is added.

Rate selection is shared, and public rate/count/shake changes use one apply and
rollback path. Shake changes derive their final clamped/restored ODR before
configuration instead of configuring once and then configuring again for the
ODR. Failed transactions restore rate, count and shake together; failed rollback
still leaves recovery required. Requested intervals publish only after success.

Rotation drains pending data once, advances the sample generation and resets
cached samples, baseline, quiet tracking and startup settling in software.
Outside WOM it performs no register I/O or timer restart. If a WOM command is
configured, rotation keeps the prior exit/restart path to rebuild the baseline.
Fresh standby/WOM peek reads share the same settled-read helper. With shake off,
FIFO processing decodes only the newest settled sample for the peek cache; raw
batch delivery, original timestamps and partial startup trimming are unchanged.
Quiet classification and software-shake baseline updates retain their existing
order; the separate quiet-path decode is intentionally retained.

The optional automatic FIFO experiment is isolated in `qmi8658_self_test.h`;
its state is private and allocated only when that experiment is compiled, rather
than being present in every profiled device state. Profile instrumentation stays
in its existing helper header. No automatic experiment is enabled in the normal
candidate. The worker stack size and normal static state are unchanged.

Eleven targeted ASan/UBSan tests pass. Added assertions cover one configuration
pass for combined shake/rate changes, full rollback after a failed final ODR,
rejected request publication, zero-I/O standby/stream rotation, pending batch
flush exactly once, distinct FIFO frames/newest cache, partial startup trimming,
and rotation from WOM. Existing missed-edge, busy-slot, FIFO recovery, first
settled shake, WOM health/fallback, client and peek tests remain. Profiling and
self-test variants compile with real ARM headers. Formatting, lint, image map,
protected CRC, resource hash and linked SRAM wake-gate checks pass.

Normal candidate `4545e34ad182f3f80411b93f46c2dbdf364a9e5f` has exactly the
installed WOM-default configuration. Against frozen normal build `329155c5`,
GNU size reports text 1,637,987 -> 1,637,939 bytes (-48), data 5,064 bytes and
BSS 241,776 bytes (both unchanged). This is a linked image-size comparison;
register-work reductions are host-model evidence. No electrical or watch runtime
comparison was performed during development. Artifacts and exact
source snapshots are in `artifacts/qmi-cleanup-20261113` in the reference
workspace. Watchdog backoff remains a separate future experiment; current WOM
and streaming timing, CP frequency/closed settings and shared code are unchanged.


### Cleanup installation and boot smoke check

After the first CH342 protocol-error attempt (before erase/write), the operator
reconnected the adapter and AP-only programming of cleanup normal `4545e34a`
succeeded in 32.6 s. Live UART received its full build ID, AP/CP reset sources
0/0, 240 MHz active AP clock, contract 0x78f, SD startup, 32 retained-sleep
returns with zero CP transition/clock/mask/resource/fault deltas and encrypted
phone-session recovery. No further warm reset was needed. Capture is stopped
at 6,970 bytes. The operator reports final first-gesture wake, touch and buttons
all normal. This is a
boot/sleep/protocol smoke check, not a matched workload or electrical comparison.
`flash-result.json` retains exact checks; failed-attempt evidence is preserved.

## 15 November: time-based filtering and opt-in hardware any-motion

Source implementation only; this change has not been flashed or electrically
qualified. Normal builds retain quiet-period WOM and the 31.25 Hz shake-only
rate. The software gravity baseline now retains 7/8 per elapsed 8 ms, using
integer exponentiation and interpolation between 8 ms periods. Previously,
7/8 retention per sample made the baseline four times slower after the change
from 125 Hz to 31.25 Hz. Threshold mapping, startup settling, long-gap reset
and 500 ms event holdoff remain unchanged. The synthetic 90-degree gravity-only
rotation regression rejects a false shake while retaining impulse detection.
This does not establish equivalence for all physical gestures. WOM resume still
preserves the pre-WOM baseline to retain the existing first-gesture behavior.

`CONFIG_T5AI_QMI8658_ANY_MOTION=y` selects a separate candidate for shake-only
operation. It defaults off until fitted 05/7c behavior and power are measured.
It takes precedence over WOM while no data clients are subscribed. The candidate:

- Disables acceleration while programming both motion parameter banks through
  CTRL9 `0x0e`, using CAL4_H `0x01` and `0x02` as specified in section 9.4's
  prose. Table 34 labels that field inconsistently as bits 7:4; the exact bank
  selection must be verified on the fitted revision.
- Enables only Any-Motion, using OR across all three axes, 4 g / low-power
  21 Hz, INT2 and disabled data-ready interrupts. The gyro stays off. No
  per-sample host acquisition or software gesture check runs in this mode.
- Maps sensitivity 0..100 to slope threshold codes 12..2 (375..62.5 mg per
  sample), with two consecutive qualifying samples. Stationary high sensitivity
  uses code 2 and one sample. These are initial candidate values, not a
  calibration equivalent to the software threshold.
- Checks STATUS1 bit 5 and CTRL2/7/8 on motion interrupts and five-second health
  passes. XYZ is read only for a qualifying event or an explicit peek. Events
  retain startup settling and the 500 ms holdoff; persistent status/pad levels
  do not cause repeated health-generated shakes or unbounded catchup work.
- Reports approximate axis/direction using the dominant sampled axis: Any-Motion
  does not supply the triggering axis. Applications needing accurate gesture
  axis information should be included in qualification.
- Restores requested stream rates and software shake when a data client
  subscribes. Generation checks discard motion captured before reconfiguration.
  Sensitivity changes reprogram the banks; rotation remains a software mapping.
- Resets only the sensor after partial motion setup failure and falls back to
  software plus the existing quiet-period WOM policy. Health/read errors use
  the ordinary bounded recovery path and disable hardware any-motion until
  reboot. A successful command/readback cannot detect a silently ineffective
  motion engine; fallback is not a substitute for interrupt qualification.

The API's sampling interval remains the saved continuous-mode interval while
hardware motion is active, as with existing WOM. Power snapshots report the
hardware motion rate (48 ms nominal / low-power 21 Hz); opt-in QMI profiling
includes `QMIANY` active/failed state. Shared Pebble services are unchanged.

Reference: [QST QMI8658C Rev A](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf),
sections 9.1–9.6 and CTRL7/CTRL8 descriptions. Low-pass smoothing alone would
not replace the slope detector's rejection of constant acceleration.

Before enabling by default, verify actual INT2 and status behavior with DRDY
disabled, first and repeated gestures, slow rotation, walking/vibration,
stationary sensitivity, data clients and concurrent touch/BLE. Compare matched
idle and gesture workloads against normal WOM with identical display and sleep
settings. No unchanged-power or battery-life claim follows from host tests or
a firmware build. No automatic flash or push is part of this implementation.

Validation: 17 targeted IMU/I2C test methods pass with ASan/UBSan
(`ASAN_OPTIONS=detect_leaks=0` is required by the sandbox's ptrace environment).
The filter regression was also run against a temporary mutation restoring
`delta / 8`; that version fails the rotation assertion. Normal and opt-in
hardware builds pass, including image-map and protected-block CRC/payload
checks. The opt-in candidate adds 384 bytes of linked text over normal, with
identical data/BSS totals. Hardware-motion and motion-plus-profiling driver
variants compile with the production ARM headers. The optional profiler's
obsolete `switched_in_at` reference was replaced with the scheduler's accounted
`run_time`; tick counts are converted to milliseconds using `PBL_TICK_HZ`.
Those counters retain scheduler tick granularity and are not electrical energy
measurements. Formatting, Python lint and diff whitespace checks pass.
Build artifacts are preserved locally under
`build-tuya-upstream/imu-motion-20261115/{normal,hardware}/`; the build is
returned to normal defaults, with any-motion disabled. The initial slope
thresholds, event timing and power remain unverified on hardware.

### Operator-requested installation — 15 November, 20:16 UTC

The operator subsequently requested flashing this candidate. Hardware motion was
explicitly enabled in the fresh build and AP-only programming succeeded. Live
UART confirms build `eb704a420f5463c05c143b0cf07709f1ef9b127e`, reset sources
0/0, contract 0x78f, launcher and encrypted phone-session recovery; no IMU setup
or fallback warning was received in the bounded boot window. The capture is
stopped. This supersedes the earlier no-flash checkpoint above and leaves the
build configured with any-motion enabled, while the board default stays off.
Gesture, fitted engine/interrupt behavior and electrical qualification remain
open. Exact artifacts and received evidence are in
`build-tuya-upstream/imu-motion-20261115/install/`; see
[current installed status](current-status.md).

### Reduced-sensitivity follow-up — 15 November, 20:22 UTC

The first installed hardware candidate was reported far too sensitive. The next
candidate doubles the threshold range to codes 4..24 (125..750 mg/sample), with
code 14 / 437.5 mg at 50% sensitivity, and requires two consecutive samples in
both normal and stationary-high modes. Stationary mode uses code 4 / 125 mg.
The low-power rate, interrupt policy and health cadence are unchanged. Nine
IMU test methods pass; fresh build, metadata, CRC/payload, map and matching
resources checks pass. AP-only flash and live boot/launcher/encrypted phone
recovery were verified for `9aa04fc13c2f67c29a82c8a141bac9f38c76207f`, with
reset sources 0/0 and contract 0x78f. No IMU failure/fallback was logged in the
bounded capture; on-wrist acceptance and power measurement remain pending.
Artifacts are in `build-tuya-upstream/imu-motion-20261115/install-less-sensitive/`.

### Asterix/Obelix sensitivity mapping — 15 November

The operator selected Asterix/Obelix as the reference. Their application board
fragments, `pebble/boards/{asterix,obelix}.conf`, set LSM6DSO to ±4 g, wake
threshold minimum 1, maximum 40, initial register default 16, and duration 1.
Both use 12.5 Hz in shake-only mode. Getafix instead sets LIS2DW12 to ±4 g,
thresholds 1..28, initial default 14 and duration 1; its high-pass wake path is
not the chosen reference.

The QMI mapping reproduces the ST percentage interpolation first:

```
st_code = 40 - floor(percent * (40 - 1) / 100)
qmi_code = ceil(st_code * 4 * 48000 / 80000)
```

Each ST code is 4 g / 64 = 62.5 mg of the selected slope-filter output.
LSM6DSO's slope is `(a[n] - a[n-1]) / 2`; QMI Any-Motion uses the full
`a[n] - a[n-1]` and 31.25 mg threshold units. The factor 4 converts threshold
units and filter gain. The 48/80 period ratio preserves the threshold for a
constant-rate acceleration ramp while retaining QMI low-power 21 Hz. Thresholds
round upward by less than one QMI LSB. This rate conversion is an engineering
approximation for ramps, not an exact mapping of arbitrary sampled gestures,
analog filtering, noise, phase or interrupt latency.

| Sensitivity | ST register | ST filtered threshold | QMI register | QMI sample-difference threshold |
| --- | ---: | ---: | ---: | ---: |
| 0% | 40 | 2500 mg | 96 | 3000 mg |
| 25% | 31 | 1937.5 mg | 75 | 2343.75 mg |
| 50% | 21 | 1312.5 mg | 51 | 1593.75 mg |
| 55% (default preference) | 19 | 1187.5 mg | 46 | 1437.5 mg |
| 75% | 11 | 687.5 mg | 27 | 843.75 mg |
| 100% / stationary high | 1 | 62.5 mg | 3 | 93.75 mg |

The ST duration register's 1/12.5 Hz interval is 80 ms; QMI uses two qualifying
samples (nominally 96 ms), rounding upward to its available granularity. The
initial ST register default 16 maps to QMI code 39 until a percentage preference
is applied. This is distinct from a 50% preference, just as in the ST driver.
Stationary high sensitivity uses the reference minimum without losing the
saved normal slider value. Gyro-off, 21 Hz, data-ready suppression, five-second
health checks, startup settling and event holdoff are unchanged. Data-client
and software/WOM fallback behavior retains the previous software contract;
the ST hardware mapping applies to the hardware Any-Motion path.

Ten ASan/UBSan IMU test methods pass, including every percentage compared with
an independently calculated rational conversion from the actual Asterix/Obelix
configuration files, monotonicity, rounding, initial default, explicit 50%
override, stationary override/restoration, bank programming and failure paths.

Sources: [ST AN5192, accelerometer slope filter and wake-up](https://www.st.com/resource/en/application_note/an5192-lsm6dso-alwayson-3d-accelerometer-and-3d-gyroscope-stmicroelectronics.pdf),
[ST LSM6DSO register definitions](https://www.st.com/resource/en/datasheet/lsm6dso.pdf),
and [QST Rev A, sections 9.1–9.4](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf).
The code/period translation cannot replace an on-wrist comparison or a matched
power measurement.

The ST-mapped image was subsequently AP-only installed as build
`c7a55db731cb738bca128c4cd896e4b6fddb5501`. The first programming boot received
AP=0x4f / CP=0 and retained-admission rejection 4. That trace is preserved. One
controlled restart of the same image received reset sources 0/0, contract 0x78f,
launcher and encrypted phone recovery, without a new IMU failure/fallback or
retained-admission failure in the final window. Capture is stopped. Artifacts
are in `build-tuya-upstream/imu-motion-20261115/install-st-map/`; first-boot
admission, electrical savings and on-wrist equivalence remain unqualified.

## Motion burst re-arming — 15 November

Following the operator's report of aggressive triggering once awake, review
found that the QMI hardware path treated every INT2 edge as a new motion event
once the 500 ms holdoff expired. QST Rev A section 9.6 says motion interrupts
are synchronized with DRDY, so continuing movement can produce repeated pulses.
The ST driver's latched-interrupt edge assumption does not transfer directly.

The QMI driver now groups continuing pulses into one burst. An observed clear
Any-Motion status or a gap of at least 240 ms between motion interrupts re-arms
it; 240 ms is an engineering debounce choice of five nominal 48 ms periods,
not a QMI hardware guarantee. IRQ timestamps use the always-on millisecond
counter, with unsigned subtraction across wrap. Pending re-arm state is consumed
with IRQs locked and reset when the sensor is reconfigured. The existing 500 ms
holdoff still applies. This does not add timers, health polls, sample streams or
sensor commands. ST thresholds, duration, 21 Hz sampling and gyro-off are unchanged.
It reduces repeated event delivery; it does not establish on-wrist equivalence
or explain every possible wake-time trigger. Data clients still use the existing
software detection path and remain a separate behavior to investigate if needed.

All ten ASan/UBSan IMU test methods pass. The hardware test now feeds 110
consecutive 48 ms motion pulses (>5 seconds), verifies one event and one XYZ
read, and verifies re-arming after a quiet gap without an intervening health
poll, explicit clear status, stuck-high handling and client transitions. A
scratch mutation restoring raw-edge eligibility fails the continuous-pulse
assertion. ARM motion and profiling variants compile successfully.

The re-arming candidate was AP-only installed at 21:01 UTC as build
`445232d6d7278ab633fe93cf37d34bbd26b3a0f3`. The first boot received reset sources
0/0 and contract 0x78f, launcher startup, encrypted/bonded phone recovery and an
open PPoGATT session. No extra hardware restart was needed. The bounded UART
capture is stopped; artifacts and result are under
`build-tuya-upstream/imu-motion-20261115/install-rearm/`. On-wrist behavior and
power remain unmeasured; earlier admission failures remain preserved and open.

## Wake-time fallback investigation — 15 November

The operator reports that stillness settles the display but small movements
keep it awake. A diagnostic image (driver DEBUG logs only, no profiler or sensor
rate changes) captured an actual transition at 21:10:13 UTC: hardware shake at
13.281, hardware disabled with `motion_failed=1` at 13.474, then software shake
at 13.998. High-sensitivity override was false, saved preference was 55%, and
there were no data clients. Software threshold was 637 mg, versus the hardware
ST-mapped setting of 1437.5 mg/sample; filter responses also differ. Subsequent
triggers remained in software/WOM mode. This is evidence of a real failure
transition, not a data-client subscription or stationary override. It does not
yet distinguish health/register validation from a failed wake-time peek.

The first trace is preserved under `install-wake-diag/` (build
`dca8b133888803b4089709d05d8f46acc94955a5`). A second diagnostic image, build
`115718600512c16c6eec80bd0d323be1ccf0baac`, adds exact service/peek failure
reporting and is AP-only installed under `install-wake-cause/`. Its initial boot
received 0/0 reset sources and contract 0x78f. These diagnostic builds enable
driver DEBUG event logs; they are not power measurements or a sensitivity fix.


### Failed strict fallback trial and rollback — 15 November, 22:01 UTC

Additional diagnostics located configuration-health reads returning recently
read register values (for example CTRL7 reading CTRL2's 0x1d instead of 0x21).
Confirmation reads, short RX, controller reset, and STOP interrupt-mode trials
did not eliminate the problem. All experimental SoC I2C changes were reverted.
The motion-only STOP-separated register-read sequence matches the Tuya reference;
the operator rated that candidate “much better, marginally too sensitive,” but
later hardware fallback still occurred. This is not evidence of a transport fix.

A subsequent candidate attempted to retain the ST slope threshold and 80 ms
duration in software fallback. Build `754bc4e6a96e20f6dd4c71a00782c277196e70fc`
booted cleanly, but the operator reported that it stopped working after a couple
of shakes. Its trace includes another configuration mismatch at 21:55:26.850.
The lost-response cause is not established. The new software detector and state
were reverted, restoring the earlier gravity-baseline detector. The failed
image/source snapshot and full stopped trace remain in `install-st-fallback/`.

The exact previously usable frozen image, `bd3b37c4416ec994a11fe675659c9d12dd5bf1c8`,
was AP-only restored. UART confirms reset sources 0/0 and contract 0x78f at
22:01:02, launcher at 22:01:13, encrypted/bonded phone recovery and PPoGATT open
at 22:01:24. Rollback evidence is in `restore-stop-read/`; all paths are under
`build-tuya-upstream/imu-motion-20261115/`. Ten IMU regression test methods pass
on reverted production source. The restored image includes DEBUG diagnostics;
power and final sensitivity remain unqualified. No code was pushed.


### Fallback amplitude adjustment — 15 November, 22:20 UTC reproduction

A fresh UART capture on the restored image confirms software fallback is still
active (failed=1, high=0, preference 55%, no clients). During the operator's
reproduction, the reported software peaks repeatedly reached about -885 mg,
exceeding the legacy 637 mg threshold. The stopped trace is preserved under
`build-tuya-upstream/imu-motion-20261115/excess-trigger-followup/`; it also
contains an RTC write error. It does not establish the cause of bad I2C reads.

For the opt-in hardware-motion board configuration, software fallback now uses
62.5 mg per saved ST threshold code, rounded up and floored at the legacy
175 mg minimum. At 55%, code 19 gives 1188 mg instead of 637 mg; initial code 16
gives 1000 mg. The high-sensitivity override retains 175 mg. This uses the ST
register's amplitude scale as a tuning starting point, not proof that the
baseline and slope filters have equivalent physical responses. The elapsed-time
gravity baseline and 500 ms holdoff remain; there is no new sustained-slope
qualification or software assertion latch. The failed strict detector remains
reverted. Builds without hardware motion retain their prior threshold curve.

This is a sensitivity mitigation while hardware fallback remains unresolved.
It adds no bus reads, timers, sensor commands or sampling and leaves gyro-off,
I2C idle gating and retained sleep enabled. The candidate uses normal IMU log
level 100; DEBUG event records compile out. Power has not been measured.
Eleven ASan/UBSan regression methods pass, including repeated 885 mg movements,
a single deliberate impulse, holdoff and a second impulse after settling;
ARM motion/profile compilation, formatting, lint and the firmware build pass.
The frozen candidate is `install-fallback-amplitude/`, build
`d930079040e851864c89d743c7d5d9d8f70ac8b1`. Installation and operator outcome
are recorded in its result file and the latest current-status checkpoint.


### Small amplitude trim — 15 November

The operator found the 1188 mg fallback setting too hard to trigger and requested
a small reduction. The software-only amplitude scale is now 90% of the previous
ST-code scale: 56.25 mg/code, rounded up and floored at 175 mg. At the current
55% preference (code 19), the threshold is 1069 mg. Initial code 16 gives 900 mg.
Hardware thresholds, high override, elapsed-time baseline, 500 ms holdoff,
sampling, timers, I2C idle gating and retained sleep remain unchanged. This does
not resolve the hardware configuration-health fault or establish measured power.

All eleven ASan/UBSan IMU regression methods pass, including small-movement
rejection and deliberate impulses separated by settling. Formatting, lint,
firmware build and image checks pass. Frozen build
`6f792c680efb4041ec623f77af9bb88adcb9cfcc` and installation evidence are in
`build-tuya-upstream/imu-motion-20261115/install-amplitude-trim/`.
Nothing was pushed.


### Cleanup and investigation disposition — 15 November

The operator accepted the small amplitude trim (“much better”) and requested
code cleanup, motion enabled by default and diagnostics disabled. Any-motion
now defaults on for Unhygienix, with the existing software/quiet-period WOM
fallback. The accepted sensitivity and hardware settings remain unchanged.
The driver removes temporary event/mode/peek dumps and detailed register failure
dumps, names the fallback amplitude scale, separates health validation from event
qualification, and makes the vendor-style motion-mode read sequence explicit.
Concise failure warnings remain. Optional profiling remains available but off by
default; normal builds do not enable IMU debug logging.

**Investigation status: unresolved.** The captured register mismatches still
explain the software transition but do not establish their root cause. The
latched fallback policy remains intact, all first-failure evidence is retained,
and this cleanup introduces no further transport experiments. No power
qualification or hardware-fallback fix is claimed. This is a source/build cleanup;
the accepted amplitude-trim image remains installed. Nothing was pushed.


The cleanup image was subsequently AP-only flashed at the operator's request.
Its first boot at 22:47:30 received AP=0x4f/CP=0 and sleep-admission rejection
reason 4; that full trace is preserved separately in `cleanup/first-boot/`.
One controlled same-image hardware reset produced clean sources 0/0, contract
0x78f and launcher startup at 22:48:38. No guards were changed, no CP/resources/
apps/bonds were programmed, and nothing was pushed. The post-flash admission
issue remains a release blocker, separate from the unresolved IMU fallback.
