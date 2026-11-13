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
