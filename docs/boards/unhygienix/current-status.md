# Unhygienix current status — 13 November 2026

## Touch source cleanup — 13 November (unflashed)

Separate cleanup build `9780fbc065bfc133635e8c0bc58ec87f9b871f88` extracts decoding,
recovery and polling helpers, simplifies read/ACK handling and removes one
redundant generation-check critical section from ordinary raw delivery. Palm
listener revalidation and all touch policies are preserved. Forty-seven targeted
ASan/UBSan tests and 10,000 modeled equivalence steps pass, as do normal/ARM
IO-profile builds, image/map/CRC/SRAM and style checks. Text/data/BSS, normal
configuration and resources are unchanged versus the installed image. No measured
speed/power improvement is claimed. The cleanup is not flashed; installed AP
remains `96b2e1aabc0e5f1748c6bbdaa765efe168f1a01b`. See the [touch review](touch-power-review.md)
and reference-workspace `artifacts/touch-cleanup-20261113/` for the separate diff
and exact evidence.

## Touch fault-backoff installation — 13 November

Normal AP build `96b2e1aabc0e5f1748c6bbdaa765efe168f1a01b` is installed AP-only.
The [touch review](touch-power-review.md) retains subscriber-driven raw touch and
reset-held global-off/no-subscriber disable. No speculative controller sleep or
gesture-mode command was added. Empty stuck-low INT polling now backs off to
1 s; IRQs/queued work respect existing 80 ms / post-reset 1 s transfer retries.
Known held/palm contacts retain 20 ms fallback and real falling edges remain
immediate. During an INT/bus fault a new touch can be delayed or a short tap
missed; this is not a normal-idle current saving claim.

The operator authorized flashing and granted standing authorization without
re-prompting. Programming succeeded in 32.6 s on the first attempt. Initial
post-programming boot received AP/CP reset sources 0x4f/0 and closed sleep
admission; that first failure is preserved. One commanded same-image hardware
restart then received the exact build ID, reset sources 0/0, 240 MHz AP clock,
contract 0x78f, SD startup and 32 AP returns with CP transition/clock/mask/resource/
fault deltas all zero. The operator confirms the companion connected normally.
Encrypted phone-session
messages were not captured on UART; that recovery is operator-observed.
The operator confirms taps, swipe/release, held touch, first touch after ten
seconds idle, motion wake and both buttons all normal. Forced physical INT/I2C
faults, shared-bus concurrency/RTC, measured latency and longer qualification
remain open. No overnight soak or electrical measurement was run.

All 46 targeted ASan/UBSan tests pass (leak detection disabled for sandbox
tracing), as do normal image/map/CRC, SRAM-gate, ARM IO-profile and style checks.
Text grows 112 B; data/BSS, configuration and resources are unchanged. CP firmware,
frequency/closed settings, identity/calibration, PFS and bonds were outside the
AP write; CP readback was not repeated this session. Raw SHA256:
`aee7bd2d0f712a8ebc2848a9b3044c69f4367092462cabcf85fa6b57e0d9736f`;
protected: `b19bafc8cca1b87ce77862ec8a31d04f619a164669a396f1d20f1aaf3ad111b6`.
Known-good `4545e34a` rollback, exact candidate/config/dictionary, first-failure
and restart UART, tests and results: reference-workspace
`artifacts/touch-power-20261113`. Captures are stopped. Final restart UART
received 4,442 bytes; exact results are
in `flash-result.json`.

## Earlier installation: QMI cleanup — 13 November

Installed AP-only build `4545e34ad182f3f80411b93f46c2dbdf364a9e5f` consolidates configuration
and rollback, separates direct/FIFO/WOM acquisition, and isolates the optional
on-watch FIFO experiment and its state. Shake/rate changes apply once; rotation
outside WOM does no register I/O. FIFO delivery decodes only its newest settled
sample for the cache when software shake is off; all raw samples are delivered.
WOM policy, interrupt handling, watchdog timing, CP/closed settings and shared
code are unchanged. Normal configuration matches the preceding WOM-default image.
Eleven targeted ASan/UBSan tests pass, as do ARM diagnostic-variant compilation,
image/map/CRC/SRAM checks and style checks. Linked text is 48 bytes smaller;
data/BSS are unchanged. Electrical savings remain unmeasured. Frozen
images, source snapshots and results: reference-workspace
`artifacts/qmi-cleanup-20261113`. See [sensor review](sensor-power-review.md).
The first flash attempt failed before erase/write with CH342 USB protocol error.
After the operator reconnected the adapter, AP-only programming succeeded in
32.6 s. Received build ID, AP/CP reset sources 0/0, 240 MHz active AP clock,
contract 0x78f, SD startup, 32 retained-sleep returns with all CP transition,
clock/mask/resource/fault deltas zero, and encrypted phone-session recovery.
No extra warm reset was needed. The operator reports final first-gesture wake,
touch and buttons all normal.
The capture received 6,970 bytes and is stopped; exact results are in
`artifacts/qmi-cleanup-20261113/flash-result.json`.
Raw SHA256: `954a9820690b617f2c0da9b8234054dde111af1afa395fdcba0329d12b79aa97`;
protected SHA256: `c99c9b10e503b676303409a4a7fbe32ee7ffacc2327ef7363b599e948c992171`.

## Earlier installation: default quiet-period WOM — 13 November (superseded)

Normal AP build **`329155c5a2073e1289f4b4efa767723502a49114`** was installed at this checkpoint,
AP-only, with profiling disabled. Raw SHA256:
`69d9621b1f2951b62f8680a7f3b68f987c6318c97757bb509f56b7931c8fb89a`;
protected SHA256:
`da940261f74c7161c9e38b49b4a7c5c677cbe345c59216bcfaafb78ea9d9a937`.
The sole configuration difference from the preceding normal image is
`CONFIG_T5AI_QMI8658_WOM=y`, now the board default at the operator's request.
CP97, CP frequency, calibration/closed settings, resources, PFS and bonds are
unchanged. Active CPU stays 240 MHz; no CP readback was repeated this session.

With shake enabled and no sample clients, five quiet seconds enters 21 Hz
low-power WOM (32 mg, INT2 initially low, one blanking sample). Motion restores
continuous sampling and the existing software shake detector, including its
threshold/holdoff and startup settling. WOM alone does not generate a shake.
Data clients bypass WOM; a fresh peek exits it. Failed WOM setup/health checks
fall back to continuous mode for that boot. A failed WOM exit can reset the
sensor only; no watch/CP reset policy was added.

In quiet WOM, a five-second health pass replaces the 100 ms minimum stream
watchdog. Hardware motion interrupts are still serviced immediately; a lost
interrupt or sensor fault may wait up to five seconds for backup detection.
Ordinary streaming watchdog timing is unchanged. Selected quiet diagnostic
windows total 90.001 s: zero sensor IRQs/sample reads, 18 health callbacks and
54 register reads, with no bus errors or sensor recoveries. This demonstrates
less QMI activity, not a measured electrical or battery-life saving.

Ten targeted sanitized tests pass, covering both feature-off and WOM paths,
client/FIFO/peek/disable transitions, first settled motion using the old baseline,
partial commands, corrupted configuration, stuck-high handling and timer wrap.
Image/map/protected CRC and linked SRAM wake-gate checks pass. Normal text grows
by 952 bytes; data/BSS are unchanged, with the kernel heap minimum preserved.

The operator reports three first-gesture tests after ten seconds stillness
passed on the diagnostic image, with touch/buttons normal; two corresponding
WOM resume records were decoded. Final normal motion wake/touch/buttons also
feel normal. Final normal boot received AP/CP reset sources 0/0, contract 0x78f,
and SD startup. Final normal UART stopped producing records after startup;
its AP return milestone and encrypted phone recovery were not received. Both
were verified on the WOM diagnostic image, so they are not asserted for this
normal image. The known immediate post-programming AP=0x4f failure and a
subsequent same-image reset are retained. No sleep guard was weakened.

CH342 re-enumeration changed its UART node during capture; reopening the by-ID
path restored live records. One final-flash attempt failed with protocol error
before erase/write; after operator reconnect, AP-only programming succeeded.
All traces, frozen images, hashes, tests and final checks are in
`artifacts/wom-default-20261113` in the reference workspace; see `result.json`,
`candidate-qualification.json` and [sensor review](sensor-power-review.md).
Captures are stopped. Long-term first-motion/latency, sample-client/concurrent
load, electrical measurements and release soak remain open.

## Earlier 13 November pulse-fix installation (superseded)

Normal AP build **`d47e7b01977acc786d357f31617417fd3252f26d`** was installed at this checkpoint
with profiling disabled. Raw SHA256:
`cee9312585de88178d4b3bd00a7207be55a1bedce1f14b620dd7de88edc53e2c`;
protected SHA256:
`9217f72fad129524d9c93307c28f548ab7ec472b481c84640341f003b7d10da3`.
Its `.config` is byte-identical to the 12 November normal build. Every write
selected AP only; CP97, its frequency/calibration/closed settings, resources,
PFS and bonds were preserved. This session did not repeat CP readback.

Direct-mode INT2 now stays armed while acquisition or deferred delivery owns
the raw slot. A real pulse coalesces into one pending follow-up; acquisition
consumes the token before reading. Reconfiguration clears it, recovery masks
interrupts, and FIFO retains its existing bounded watermark handling. No sample
rate, shake threshold, holdoff, watchdog interval or WOM mode changed. Normal
text grows by 96 bytes against the 12 November normal ELF; data/BSS are unchanged.
Eight targeted sanitized sensor/startup/I2C tests pass, as do image/map/protected
CRC checks, Python lint and linked SRAM wake-entry audit.

Stationary dim Nadir, motion backlight enabled, battery disconnected: connected
55-second profile windows report control/candidate conversion gaps **458/352**,
with no bus errors, host discards or sensor recoveries. The candidate received
busy pulses and bounded catchups, but gaps remain. Boot uptime/reconnect timing
differ; its earlier unconnected window was worse. The candidate reset-source
UART frame was damaged, although MCU mask zero, contract 0x78f and 32 clean AP
returns were received. This is bounded functional evidence, not proof of an
energy saving or lossless acquisition. No electrical measurement was made.

Final normal boot received AP/CP reset sources 0/0 and contract 0x78f directly
after programming, then 32 AP returns with zero CP transition/fault deltas and
encrypted phone recovery. The operator confirms motion wake, touch and buttons
feel normal. Only the expected normal boot was received during this bounded
capture; all captures are stopped. Details are in
`artifacts/qmi-gap-20261113/result.json` in the artifact workspace. The diagnostic
runs preserve immediate post-flash AP=0x4f failures and successful same-image
resets. A recurring CH342 protocol error required one adapter reconnect; the
watch remained responsive while UART reception was stalled.

At this pulse-fix checkpoint WOM remained disabled. The later default-WOM
installation above supersedes this deferral. Its next qualification needed documented CTRL9 entry/exit,
INT2 assertion/STATUS1 acknowledgement and stationary/motion tests, followed by
first-gesture sensitivity/latency tests before replacing continuous software
shake. See [sensor review](sensor-power-review.md) and `comparison.json` in the
same artifact directory for exact windows and limitations.

## 12 November installation (superseded)

AP build **`196be9b720be760e0aadab48131714acf2da9949`** is installed, AP-only,
with profiling disabled. The received boot identity matches the frozen normal
ELF. Raw SHA256 is
`1a1fcc8b494845dea0a9dad0924249a76ae7bb82c05c49e48f2d5e1773ac8a8f`;
protected SHA256 is
`3aa2b85bedeeec4135fd9f4fed68ef5da1b5952fb338c2fe0d5a77ba5cb1be54`.
Its phone-facing version is `v4.41.2-911-gb41d61994-dirty`: source base
`b41d61994` plus the uncommitted driver-local sensor increment. Existing unrelated
battery-qualification edits were preserved. The resource bank, PFS and bonds
were not selected by these writes.

CP97, calibration/controller settings and CP frequency policy are unchanged.
CP readback was verified before the session's cleanup AP deployment, and every
subsequent write selected only AP firmware. Resources retain SHA256
`62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46`.
The active CPU remains 240 MHz, with CP awake and the original dim display.
The operator confirms the cell is disconnected.

The initial shake-only request now uses the existing 32 ms shake limit instead
of treating the default 8 ms interval as a client request. Explicit sample-client
requests retain precedence. Shake-only processing uses the existing QMI worker,
removing per-sample NewTimer offload; data clients keep their existing delivery
path. No sensor WOM mode, threshold, holdoff, watchdog period or shared Pebble
source was changed. Seven targeted sanitized sensor/I2C tests pass; normal image,
map/protected-CRC and linked SRAM wake-gate checks pass. Text grows by 48 bytes
against the cleanup ELF; data/BSS totals are unchanged.

The clean final boot, after one commanded warm restart, received reset sources
0/0, contract 0x78f, 32 AP returns with zero CP transition/fault deltas, SD startup
and encrypted protocol recovery. The immediate post-programming boot had AP=0x4f
and closed retained sleep; that trace is preserved. Warm-programming sleep-resume
reset behavior remains a qualification issue. No sleep guard was changed.

Instrumented connected-idle windows measured sensor IRQs at 28.87/s versus
61.79/47.31 in controls, and I2C IRQs at 144.36/s versus 309.05/236.53.
USB input was 142.999 mW versus 144.249/143.233 mW controls. The 0.516% reduction
from mean controls is smaller than their 0.704% drift: **no reliable electrical
saving is established**. Conversion-counter gaps and physical gesture latency,
high-rate clients, worker stack headroom, concurrent load and long-term false
positives remain qualification work. The final retained-sleep UI check is recorded
in the session result. WOM, battery-side measurements and release soak remain open.

Source, frozen images/configuration/dictionaries, raw UART/meter data, rejected
runs, programmer/USB failures and the final comparison/result are preserved under
`/home/baz/Documents/ChatGPT/pebble/artifacts/sensor-power-20261112/`.
See [sensor review](sensor-power-review.md) and [remaining power work](power-todo.md).

## Preserved 8 November cleanup checkpoint

The sections below preserve the earlier cleanup record. Installed-image and
unflashed-build statements in this checkpoint describe **8 November**; the latest
13 November installation and sensor evidence above supersede those statements.


The active direction is **CP-awake AP sleep plus local peripheral clock
lifetimes**, with the original dim display behavior. CP deep sleep is deferred.
The board enables the existing savings by default; hardware qualification remains incomplete.
Historical checkpoints are preserved in [the power history](power-investigation-history.md)
and [the CP sleep history](coordinated-sleep-history.md).

## Historical installed firmware — 8 November

| Item | Verified state |
| --- | --- |
| AP | `f1f862e83489b050a27b1e24ef29e2e9ce5938f2`, flashed 8 November at 10:06 UTC |
| AP raw SHA256 | `c6e4d18d9b3f6191bbee16ac60944f4de076ebce33942f8ef5f23d2fd45ee750` |
| CP | Calibrated Wi-Fi-off alarm server CP97; raw SHA256 `c02c2236f210aa2a2c69b303aa59a4471990656b0e7be31d008d1d83fd6a00fe` |
| CP protected SHA256 | `f114a5abbbc18e8f2a6ae4aa8f25653fe11f5425e418054b6d955ca8daf238c4` |
| Resources | SHA256 `62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46`; unchanged |
| Display | True-zero brightness and explicit panel Sleep In disabled; original dim floor |
| Active CPU policy | Request 240 MHz; no fixed 120 MHz experiment |

The last AP write selected no CP, resource or PFS partition. Full actual CP
readback and protected CRCs matched before that write. Bonded encryption and
protocol service were received at 10:08:19/10:08:21 UTC after one companion restart.
The bounded captures are stopped; no overnight soak or ongoing monitor is running.
The later source cleanup has not been flashed over this image.

## Implemented policy

- AP retained idle uses an owned, generation-aware CP alarm. CP keeps its APP
  sleep veto; shared core/flash/PSRAM clocks and voltage remain active.
- The AP rechecks kernel idle, deadlines, peripheral/IPC ownership and pending
  interrupts, suspends SysTick, executes the wake gate from SRAM, restores routes,
  verifies retention and accounts elapsed AON time exactly once.
- Every return rejects any increase in CP low-voltage, resource, clock or native
  mask-barrier counters. Unknown controller state or restore failure closes sleep.
- I2C, SD and AP UART local bypass lifetimes are enabled in the installed profile.
  Uncertain drains/faults retain clocks. Shared GDMA and CP UART source votes are
  preserved. Battery-success logging is suppressed; conversion and error behavior
  are unchanged. Existing I2C receive FIFO remains enabled.
- The board-default `T5AI_POWER_SAVINGS` configuration selects this non-display
  profile. Production retained idle requires CP-awake mode. Bounded CP experiments
  are separated in `boards/unhygienix/Kconfig.power-debug`.

See [AP-only implementation and build instructions](ap-only-sleep.md).

## Evidence and its limits

| Evidence | Result and limit |
| --- | --- |
| AP-only bounded control | 12 AP returns, zero CP transition deltas, aborts, deadline misses or contract faults; post-test ADC and operator-confirmed Nadir/BT recovery |
| Combined continuous AP `efca4e76…` | 32-return milestone received, all CP transition deltas/fault 0; display wake/update confirmed |
| Current dim AP `f1f862e8…` | Exact build/contract and protocol recovery received; only the two AMOLED flags differ. Its 32-return activity milestone was not received during the short capture |
| Wi-Fi off | Earlier nominal USB on/off/on reconnect workload showed 1.7225% less energy/completion than mean controls; bounded observation, not calibrated battery life or qualification of this AP |
| SD local gating | Bounded functional evidence; measured differences were smaller than control drift. No reliable saving demonstrated |
| Current combined policy | No accepted matched idle or completed-workload energy comparison |

The installed experimental profile was explicitly requested despite incomplete
qualification. Enabled configuration is not proof of clock gating or runtime
residency. Do not turn the earlier 12/32-return observations into qualification
of the current dim image, a CP deep-sleep claim or a measured battery saving.

## Historical power-profile cleanup validation — 8 November

The board-default release build is `c162b6d4…`, configured without a power-profile
command-line override. Its configuration exactly matches the previously tested
savings profile. The linked SRAM audit passes and resources remain unchanged.
`CONFIG_T5AI_POWER_SAVINGS=n` is the explicit control opt-out. The runtime policy
is committed in `1c3f54198`; no cleanup/default-change build has been flashed.

The pre-default-change savings release build is `53e61e12…`; the explicit-off control is `e1ba2fc9…`.
Those cleanup builds passed layout, protected-image and resource-size build checks, with the
resource hash above unchanged. The savings configuration matches the installed
dim revision apart from the new umbrella profile flag. Its linked AP WFI gate
and PC-relative literals are in SRAM; its only call targets the SRAM reset helper.
These cleanup builds have not been flashed.

Clang-format 22, Ruff and whitespace checks pass. All 155 affected native fault,
ownership, timer, alarm and packaging tests passed with zero skips, including
CP instruction emulation. Exact logs are in the cleanup artifact record. The broad `pbl test --no-images` build stopped
on a host nanopb-generator dependency problem and an untouched pdc2png C23
const-pointer compile error. That full shared suite is not recorded as passing.

An isolated rebuild of the formatted CP source produced byte-identical app.bin,
sdkconfig, calibrated raw package and protected package. CP provenance hashes
now match the formatted source; controller/calibration/ABI payloads are unchanged.
Every paragraph of the three previous status/TODO journals remains in the
investigation history; the exact originals are also in the cleanup backup.

## Release blockers and next actions

1. Resolve Bluetooth discovery failures, connection-parameter timeouts and
   intermittent drops. Preserve the existing first-failure captures; they also
   occurred in normal controls, so causation is not established.
2. Record actual AP sleep entries/blockers with dim Nadir and fixed supply.
   Measure matched idle power and energy per completed workload.
3. Qualify SD/PFS durability, shared I2C recovery, microphone/SD/display DMA
   coexistence, deadlines and watchdog behavior on the combined profile.
4. Repeat 25 consecutive reconnects and a fully captured all-power-removed cold
   boot with bond/face restoration. Earlier CP-only results do not qualify this AP.
5. Complete the explicitly deferred overnight connected soak with UI interaction.
   Externally triggered AP resets remain release blockers under board instructions.

Default sensor WOM and its cleanup are now installed with bounded checks above.
Broader sensor qualification, touch idle policy, CP console clock lifetime,
shared GDMA/domain votes and memory retention remain separate work. Display changes remain
last and the user's dim policy is preserved. See [the ordered TODO](power-todo.md).

## Historical reproduction and evidence — 8 November

That checkpoint's installed image/config/source and received data are retained in the
reference workspace under `artifacts/ap-savings-dim-display-20261108/` and
`artifacts/coordinated-sleep-radio-20261031/ap-savings-dim-display-20261108-built/`.
The preceding combined image and Bluetooth first failures are under
`artifacts/ap-all-savings-20261108/`. Cleanup backup, formatter/test/build results
and CP payload-equivalence proof are under `artifacts/power-production-cleanup-20261108/`.
Production source remains this repository; these artifact paths are evidence,
not prerequisites hidden inside the production implementation.
