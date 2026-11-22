# Unhygienix current status — 22 November 2026

The latest installation checkpoint below is the current installed state. Dated entries preserve
the state and evidence at their recorded checkpoint.

## Finding 12 touch increment installed — 22 November, 16:43 UTC

Installed AP-only build **27131da0fc4e09587bb24de78e1b362f788c39fe**,
`v4.41.2-1660-gc645d1134-dirty`, protected SHA-256
`ab045507fadf2632afbb56e95aacd0f4b96aeb4f307592863422bdb6b2b665ae`.
Guarded flash passed in 52 seconds. UART confirms Launcher, existing resources,
PFS and 30 apps, all 15 graphics output checks, and 32 AP sleep returns with
zero recorded sleep faults. Operator confirms direction/corner/drag/hold/release,
palm/recovery and touch disable/re-enable checks pass. Finding 12's targeted
acceptance is complete; broader qualification remains outside this short bench.

Phone protocol checks pass three pongs, version/app queries and a complete
260×260 screenshot matching the earlier Ping image. An initial Bluetooth
connection dropped before the successful session. One early touch report-transfer
failure log is retained; no later failure log appears, but logging is rate-limited
and does not establish an exact final count. Two animation errors also occurred
in an earlier control. See the [TODO checkpoint](code-quality-todo.md) for full
commands, evidence and limits. Capture is stopped and forward removed. Source
is uncommitted; paused before finding 13. No CP/resource/storage programming or
bond clearing was performed.

## Finding 12 CST92xx native-instance migration in source — 22 November 2026

Panel work is committed as `c645d1134`. The uncommitted touch increment keeps the
normal `pbl_touch` class embedded in the native CST92xx device. Board `TOUCH`
points to `&T5AI_TOUCH.touch`; class reference counts, reporting and last sample
remain stock. Private state/work/retry objects and hardware descriptors now
belong to the supplied instance. Board scaling/inversion reproduces every prior
axis value and initial idle (0,0), with unchanged timing/recovery policy.

The broader regression pass has 57 passing methods; the final focused pass has
20 passing touch/startup methods, including real-class listener disable and
two-device isolation. ARM/LTO and all image gates pass. Growth is 48 bytes FLASH
and 32 bytes RAM; retained/reserved layouts are unchanged. The complete real
class/driver units use real types/configuration under host sanitizers, with
hardware/work execution modeled. On-watch touch verification remains open.

No new firmware has been flashed or committed for this increment. The installed
panel-only checkpoint below remains current. See the [TODO record](code-quality-todo.md).

## Finding 12 panel increment installed — 22 November, 16:15 UTC

At the operator's request, installed AP-only build
**46c239607873ce2c9f770bc26a81fc34e6a5586a**, version
`v4.41.2-1659-g8782bc225-dirty`, protected SHA-256
`9540ef6846fa459996d7fedbd71c84bfdd81fcf71da6448773febfb47ec0a6c6`.
The uncommitted native-panel increment is now on the watch. Flash completed in
52.0 seconds without programming CP/resource/PFS/calibration images or clearing
bonds. Graphics/draw-profile bench and retained CP-awake sleep configuration
match the validated source checkpoint; HCI event waits are on.

UART confirms the new build reached Launcher, retained existing PFS/resources
and 30 apps, completed all 15 graphics output checks, and recorded 32 AP-only
sleep returns with zero recorded sleep faults. Physical-panel visual and wider
UI qualification are not established by these records. The phone companion was
restarted but did not reconnect within the bounded readiness check; its logs
are preserved. Capture stopped cleanly and no new commit was made.
See the [TODO flash checkpoint](code-quality-todo.md) for evidence and limits.
CST92xx migration and full finding-12 acceptance remain open.

## Finding 12 CO5300 native-instance state in source — 22 November 2026

Finding 11's placement guard increment is committed as `8782bc225`; its supported
storage-hook replacement remains open. Finding 12 now has an uncommitted panel
increment: CO5300 callbacks use their supplied native instance, with brightness,
prepared/display state and transaction locking in board-owned instance state.
The local display/backlight consumers use the same per-panel mutex/argument.
Commands, timings, wiring and initialization dependencies are preserved.

A complete committed-unit probe reproduced the old ignored-device GPIO routing.
All 16 affected tests pass, including real-header two-panel isolation and exact
sequence/fault tests under sanitizers. ARM/LTO and image gates pass with unchanged
memory usage. Touch migration, full finding-12 acceptance and hardware verification
remain open. See the [TODO progress record](code-quality-todo.md).
No new flash was performed; the installed finding-10 checkpoint below applies.

## Finding 11 placement guards in source — 22 November 2026

Finding 10 is committed as `e0985d9a5`; its performance/reconnect limits remain
open. Finding 11 now has an uncommitted guard increment: linker assertions and
final-map validation reject missing/renamed, resized, duplicate or misplaced
NimBLE backing arrays. The actual ARM map retains both pool addresses/sizes,
framebuffer/cache/PFS/DMA regions, heap boundaries and configuration.

All 15 affected tests and the ARM build/image gates pass. Upstream/shared source
and runtime allocation remain unchanged. Replacing the private-name selectors
with supported backing-storage hooks remains open; current upstream does not
expose both hooks. See the [TODO progress record](code-quality-todo.md).
No new firmware has been flashed; the installed checkpoint below still applies.

## Finding 10 ARM bench installed — 22 November, 15:35 UTC

Installed AP-only diagnostic build **ada42d32395354471f04da81ad21c9a5f0df53b3**,
protected SHA-256 `a5cc76a850c003a966f027633dfa4b77b7ba4c92eedf95f998992e7e6b045270`.
Graphics copy/pixel/text cache, repaired draw profiling, retained CP-awake sleep
and HCI event waits are enabled; display/HCI profiling are off. The one-shot
benchmark defaults off in source. No automatic commit was made.

All 15 on-watch microbenchmark checks matched stock outputs. Median paired
elapsed-time changes were clear -86.6%, pixels -32.6%, cached text hits -91.2%,
copy +6.5%, text misses +2.8%. These are small warm PSRAM/real-font CPU
comparisons at 240 MHz, not FPS, current or full UI qualification. Copy benefit
and representative UI/cache traffic remain unresolved.

The initial profiling image stalled at the boot logo. Its complete failure
capture is preserved; a local scheduler wrapper had omitted the real third
interrupt-lock-key argument. The repaired image boots and benchmarks with draw
profiling enabled. The draw-profile-off control also completed protocol checks,
restored the existing Ping app and matched its preflight framebuffer screenshot.

The final image reached encrypted phone/PPoGATT connection, then disconnected
after a connection-parameter timeout (`0x0208`). Final protocol/screenshot
verification remains incomplete; the phone-only cause assumption is unproven.
No bond clearing, formatting, CP/resource flash or extra watch reset was used.
See the [TODO hardware checkpoint](code-quality-todo.md) for all samples,
first-failure evidence, validation and limitations. Finding 10 remains open.

## Finding 10 graphics cleanup in source — 22 November 2026

Uncommitted graphics work replaces compositor and text-cache stock-definition
renaming with named local caller entries, derives compositor dimensions from
bitmaps, and avoids redundant extent scans for regular rows. A full-unit pixel
comparison reproduced and fixed a null row-table dereference: display-backed
circular bitmaps now use the supported stock fallback. Shared graphics source
and option policies remain unchanged.

Real-header, complete-unit host comparisons and separate host benchmarks now
cover all three paths. One explicit expected failure preserves an unchanged
stock wide-legacy signed-shift defect. Host timing is not on-watch performance;
text fixtures use synthetic glyph metrics and do not render fonts or panels.
See the [finding 10 progress record](code-quality-todo.md) for measurements,
commands and remaining target/UI qualification. Nothing has been flashed;
the installation checkpoint below remains current.

## HCI event waits enabled by default in source — 22 November 2026

At the operator's explicit request, `CONFIG_T5AI_HCI_EVENT_WAIT` now defaults
to `y` on Unhygienix, with or without the power profile. Explicit `n` restores
the polling fallback; HCI profiling remains default off. This supersedes the
earlier default-change hold. The incomplete connected candidate A/B comparison
and phone-side reconnect assumption remain documented limitations.

No firmware was flashed: the installed image is still fallback Af described
below. The source, tests and qualification record are included in the
operator-requested finding-9 commit; finding 10 is next.

## Finding 9 qualified by operator acceptance — 22 November 2026

The operator accepts finding 9 as qualified, assuming auto-reconnect failures
are phone-side for now. This is an explicit acceptance decision; phone-only
causation and candidate latency/throughput non-regression were not established
by the tests. Original failures, the incomplete matched comparison and the
sealed evidence remain preserved in the [TODO record](code-quality-todo.md).

There is no new installation or default change: fallback Af remains installed,
event waits remain default off, and finding 9 remains uncommitted. The matched
device comparison is still required before changing the default. Broader power,
cold-boot, application and soak qualification remains open. Finding 10 is the
next backlog item; pause for review, with no automatic commit.

## HCI A/B qualification blocked; fallback restored — 22 November, 12:50 UTC

The installed AP-only image is fallback build
**`4762101d711f74b2f6db465cb9c07a520502c219`**, version
`v4.41.2-1656-g8371a1b2a-dirty`, protected SHA-256
`f7ca5cb25dffe198455097e3567e447213a51ad6502ed9804a94b67340ae136a`.
HCI event waits are **off**, HCI profiling is on and retained CP-awake AP sleep
remains on. Source defaults are unchanged. CP, resources, PFS, calibration and
identity/bonds were not programmed; the finding-8 CP source refactor remains
uninstalled. First fallback boot verified its ID and AP/CP reasons 0/0.

Baseline A completed ten minutes connected idle, 2,200 protocol echoes, three
67,600-byte transfers and 25 Bluetooth reconnects. It measured about ten upper
timeout wakes/s. Its reconnect trace also contains two transient GATT discovery
errors; successful protocol return does not erase those errors. Two candidate
starts failed the 90 s session-ready limit and a warm candidate attempt had
GATT/session failures. A2 fallback control completed the traffic/transfer test,
but final fallback Af also failed the startup limit. The failures therefore
cannot be attributed solely to event waits; no candidate latency or throughput
qualification is claimed. No uncommanded AP boot or HCI/sleep guard fault was
observed. First failures remain preserved before recovery attempts.

Restarting only the Pebble phone app, without clearing its data/bonds or
resetting the watch, restored an encrypted session. Three echoes returned in
138.600, 80.357 and 81.738 ms; version and launcher-state queries passed. Later
heartbeats remained active=1, queue=0, started=1, fault=0, with no new boot in
the continuation trace. This demonstrates a phone-client-state dependency,
not candidate qualification or a proven root cause. All UART captures and test
processes are stopped; the task-owned ADB forward was removed. Pause for review.
Finding 9 stays uncommitted and default-off; broader power, cold boot,
watchface installation, pool-pressure and overnight qualification remain open.
See the [TODO record](code-quality-todo.md) and evidence under
`build-quality-20261121/hci-work-finding9/ab-20261122/`.

## HCI event-wait candidate installed — 22 November, 11:28 UTC

Installed AP-only build **`37a2be6dcc819bbe6d55062419813135d31e577b`**, version
`v4.41.2-1656-g8371a1b2a-dirty`. Event waits and HCI profiling are enabled in this
candidate; qualified source defaults remain unchanged. Protected SHA-256:
`617c02879daa36ac263c6d5ac40e0a174ae07bbe62c038da6bdc584ab2c0724e`.
The operator requested this flash; it completed in 51.9 s without a programming
hold or recovery reset. CP, resources, PFS, calibration and identity/bonds were
not programmed. The CP policy source refactor is not installed on CP.

The first boot verified the exact ID, reasons 0/0, awake contract 0x78f, existing
SD PFS (`new=0`), resources, launcher and encrypted bonded PPoGATT. At firmware
time 11:29:10.245 it reported 32 AP-only sleep returns with zero CP/clock/mask/
resource transitions and no fault. Two healthy HCI heartbeats 30 s apart showed
active=1, queue=0, started=1, fault=0 and advancing receive counters. The stopped
167.221-second capture received 4053 bytes, one boot and no panic or subsequent
transport/sleep failure. Existing vendor boot warnings remain in the trace.

Evidence: `build-quality-20261121/hci-work-finding9/flash-20261122/` and the
[code-quality progress record](code-quality-todo.md). Finding 8 is committed as
`8371a1b2a`; finding 9 is an uncommitted candidate. Its normal/candidate ARM/LTO
builds and host tests passed. Device wakeup/packet-latency A/B, reconnect cycles,
power and soak qualification remain open. All captures are stopped; pause for
operator verification with this image installed.

## Evidence-preserving sleep retry installed — 22 November, 09:57 UTC

Installed AP-only build `31b50d165281fa78016a880573b910fefe8c69cf`, version
`v4.41.2-1653-g110701cc8-dirty`, protected SHA-256
`55c07280616340f63aed9e599594e3fb1a1a89da92ca639aa6bf4cdbfe647e72`.
The uncommitted programming-handoff alternative below has been removed.
Production CP-awake AP-only startup keeps/reports prior interruption evidence,
then uses existing owner cleanup and awake-contract validation before retrying.
Current runtime guards and diagnostic CP-sleep quarantine remain intact.

The raw AP-only flash took 51.8 s without a programming hold or recovery reset.
Its first boot at 09:57:43 UTC preserved AP/CP reasons `0x4f/0`, reported the
interruption and validated contract `0x78f`. At 09:58:18.607 it reported 32 clean
AP-only returns with zero CP/clock/mask/resource transitions and zero faults.
Existing SD PFS (new=0), resources, launcher and bonded/encrypted PPoGATT passed.
The stopped 158.298-second capture received 4161 bytes, one boot and no panic or
sleep-failure record. Temporary admission probes are absent from this image.

Final validation: 45 sleep/alarm/reset/watchdog methods under ASan/UBSan, 10
flasher/runner methods, normal ARM/LTO build and all image guards passed. The
previous boot trace gets a 16-byte ordinary-RAM snapshot; retained/PFS flag
layouts, CP ABI, timing, calibration and identity are unchanged. Kernel heap
32800 bytes; unchanged 28 KiB guard. No other partition was programmed/formatted.
Earlier runs without a 32-return report remain preserved and inconclusive;
the temporary probe established 20 clean returns in one of those runs.
No power, electrical, fault-injection or soak qualification is claimed.

Evidence: `build-quality-20261121/sleep-retry-final2-20261122/` and the
[code-quality progress record](code-quality-todo.md). The operator authorized
committing this verified increment; further backlog work remains paused. All
captures are stopped and the device is left running the recorded pre-commit
image. Finding 7 build integration is now complete at host/build scope without
flashing this source change; finding 8 is the next backlog section.

## Historical programming-handoff alternative — 22 November, 09:07 UTC

Sleep-admission repair is installed as AP-only candidate build
`1855111f5362c1deef582b8372760f286b4440b9`, phone version
`v4.41.2-1653-g110701cc8-dirty`, protected SHA-256
`b88f37b524e6897c9220cb513876d93596a518dd7eedb687e5034e158f52021b`.
This includes the committed touch and explicit SD/PFS backend changes plus the
uncommitted programming handoff. Source changes remain uncommitted for operator
verification; further backlog work is paused.

The old workflow reproduced `AP=0x4f / CP=0 / reason=4` by reflashing identical
bytes from verified retained idle. The new workflow uses the pinned CP's
existing `ap_cmd` bridge to request an AP programming hold, finishes retained
cleanup and requires a fresh nonce-matched READY before tyutool resets the watch.
It does not clear fault evidence or relax timeout/admission guards. Missing or
refused handoffs stop programming; old unsupported or quarantined firmware needs
an explicit, separately reviewed initial/recovery installation. The hold lasts
until reset if an attempt is abandoned. See the historical handoff entry in the [progress record](code-quality-todo.md).

Two handoff-controlled AP flashes passed their **first** boot with no recovery
reset: exact build, reasons `0/0`, contract `0x78f`, 32 clean AP-only returns,
zero CP/clock/mask/resource transitions and zero faults/panics. Both found existing
SD PFS (`new=0`), valid resources, launcher startup and bonded/encrypted PPoGATT.
The final flash started from verified retained idle, received READY in 140 ms,
programmed the AP in 52.1 s and booted at 09:07:09 UTC. Its stopped capture
received 3,783 bytes over 160.2 s including programming. The watch is left in
normal retained idle; no programming hold was requested after this final boot.

Host validation: 41 distinct targeted methods plus 39 image/configuration
methods, full normal ARM/LTO build and map/CRC/resources/syscall guards. Kernel
heap 32,768 bytes; retained and pinned PFS buffer addresses/sizes unchanged.
No CP/resources/PFS/calibration/identity/bond partition was programmed or
formatted. No power, electrical timing, hardware fault-injection, CP replacement
or soak qualification is claimed. Manual normal-use verification remains for
the operator before commit.

Evidence: `build-quality-20261121/sleep-handoff-2-20261122/` and the separate
baseline/failure/bootstrap/first-handoff directories described in the
[code-quality progress record](code-quality-todo.md). Next backlog item after
verification/commit: finding 7.

## Timer completion-read fix installed — 21 November, 23:47 UTC

The operator-requested item 3 repair is AP-only installed, build ID
`a967c9ad05ee7835cc8b8fe9d1ec7934a0cc97d4`, version
`v4.41.2-1650-ga253674e7-dirty`. Hardware diagnosis proved that the polling loop
observed completion, then a separate status read observed busy and falsely
panicked before either bound expired. The repair preserves the first readiness
observation and all timeout/fatal/timing/setup guards. Four host sanitizer suites,
a control reproducing the duplicate-read fault, the full ARM/LTO build and image
checks pass. No common source changed; the prior touch changes are included.

The stopped 150.3-second capture spans flashing and approximately 94 seconds
after the first boot bytes. It received 3,788 bytes: **one AP
boot, zero panics**, exact build ID, existing SD PFS (`new=0`), valid resources
and launcher startup. Bonded encrypted PPoGATT recovery observed:
**yes**. The known post-flash reset sources `0x4f/0` and
sleep-admission reason 4 remain an unresolved release blocker. No separate
reset or rollback was issued. CP/resources/PFS/calibration/identity/bonds were
not programmed. No wider hardware qualification is claimed.

Evidence is frozen in `build-quality-20261121/timer-fix-1/`; the diagnostic
and original failed traces remain separate. See the
[code-quality progress record](code-quality-todo.md) for the proof, commands,
hashes and remaining limits. Next backlog item is finding 6.

## Code-quality candidate installed — 21 November, 23:36 UTC

At the operator's request, AP-only flashed the current dirty tree at `a253674e7`,
including the owned touch sample transport. Build ID
`12a02047368b739cd94cfbbfb0d5b4fa90790909`, phone version
`v4.41.2-1650-ga253674e7-dirty`. CP/resources/PFS/calibration/identity/bonds
were not programmed. Fresh normal build and image/layout guards pass; kernel
heap is 32,800 bytes and the resource pack exactly matches the previous build.

**Not qualified:** the stopped 120-second UART capture records 4 early
panics in the bounded timer-latch read (`soc/t5ai/delay.c:47`) before an eventual
successful startup. Final boot reports AP/CP reset sources `0x71/0x10`, contract
`0x78f`, existing SD PFS (`new=0`), valid resources and launcher startup; the
bonded, encrypted phone PPoGATT session reopens. No additional commanded reset
or flash was issued. The early panics remain a release blocker and reopen
finding 3 before further backlog work. Frozen artifacts, exact dictionary and
first-failure trace are in `build-quality-20261121/flash-20261121/`.
See the [code-quality progress record](code-quality-todo.md) for commands,
hashes, limitations and the next investigation. Touch/latency/soak hardware
qualification is still open.

## Cleanup installed — 15 November, 22:47 UTC

Hardware any-motion now defaults on for Unhygienix; quiet-period WOM remains
enabled as the fallback policy. Profiling defaults off and normal IMU logging
is retained. Temporary per-event software/hardware, mode and peek dumps were
removed; failures retain concise warnings. Register selection and health
validation were simplified without changing transaction order, fallback policy,
accepted 1069 mg sensitivity, sampling or sleep settings. Shared Pebble and SoC
code were not changed by this cleanup.

The hardware configuration-health/I2C fault remains **unresolved**, as requested.
Prior failure images and traces remain preserved. This cleanup does not claim
a root-cause fix or measured power qualification. The cleanup build `e7dc9a197b37f13a62ea820d6b444e3f4930108e` is now AP-only
installed. The first post-flash boot received AP=0x4f/CP=0 and sleep-admission
rejection reason 4. Its full stopped trace is preserved in `cleanup/first-boot/`;
the known post-flash reset/admission issue remains a release blocker. One
commanded same-image hardware reset then booted with sources 0/0, contract
0x78f and launcher startup. BLE advertising succeeded; phone reconnection was
not observed in the bounded capture. No guard was cleared or weakened. CP/resources/apps/
bonds were not programmed. Nothing was pushed. Local verification evidence
is in `build-tuya-upstream/imu-motion-20261115/cleanup/`. All 23 configuration,
diagnostic-default and IMU tests pass; final ARM normal/profile compilation,
formatting, lint, firmware build and image checks pass. The built dictionary
contains no temporary motion diagnostic strings.

## Small fallback amplitude trim installed — 15 November, 22:30 UTC

The operator found the 1188 mg candidate too hard to trigger and requested a
small adjustment. The software fallback threshold is reduced by 10%, to 1069 mg
at the current 55% preference. The ST-code amplitude scale is now 56.25 mg/code,
rounded up and floored at 175 mg. Hardware detection, high override, filtering,
holdoff, sampling, timers and sleep settings remain unchanged. The hardware
configuration-health/fallback fault remains unresolved; power is not measured.

All eleven ASan/UBSan IMU regression methods, formatting, lint, firmware build
and image checks pass. AP-only installed build:
`6f792c680efb4041ec623f77af9bb88adcb9cfcc`. Actual UART confirms the first boot
at 22:30:46 with reset sources 0/0, contract 0x78f and launcher startup.
No additional hardware restart was issued. Frozen source/image/configuration,
build/test/flash logs and capture are under
`build-tuya-upstream/imu-motion-20261115/install-amplitude-trim/`.
The operator reports “much better” sensitivity. CP/resources/apps/bonds were
not programmed, and nothing was pushed.

## Fallback amplitude candidate installed — 15 November, 22:24 UTC

The operator still reported excessive triggering after rollback. A fresh stopped
UART capture confirms software fallback, high override off, saved preference
55%, no clients, and repeated peaks around -885 mg against the old 637 mg
threshold. The opt-in motion configuration now uses the saved ST threshold code's
62.5 mg amplitude scale in software, floored at 175 mg: code 19 at 55% gives
1188 mg. This is an amplitude tuning choice, not equivalent ST slope filtering.
The earlier gravity baseline and 500 ms holdoff remain; the failed sustained-
slope detector remains reverted. No sampling, timer, bus or sleep changes were
added. High override and builds without hardware motion retain their old curve.

All eleven ASan/UBSan IMU regression methods, ARM motion/profile compilation,
formatting, lint and the firmware build pass. The added functional test rejects
repeated 885 mg movements, accepts an impulse and permits another after settling.
Normal IMU logging (level 100) is enabled; routine DEBUG event records compile
out. I2C idle gating and retained sleep remain enabled. No power measurement or
on-wrist equivalence is claimed; the hardware health/fallback fault remains open.

AP-only installation restored normal logging and installed build
`d930079040e851864c89d743c7d5d9d8f70ac8b1`. Actual UART confirms the first boot
at 22:24:38 with reset sources 0/0 and contract 0x78f, then launcher startup
and encrypted/bonded phone recovery. At 22:25:06.996, CTRL7 again read 0x1d
instead of 0x21 and forced software fallback; preserve this first failure.
No extra hardware restart was issued. Frozen image/configuration/source snapshot,
test/build/flash logs and UART evidence are under
`build-tuya-upstream/imu-motion-20261115/install-fallback-amplitude/`.
The operator reported this threshold was too hard to trigger and requested a
small reduction. CP/resources/apps/bonds were not programmed.
Nothing was pushed. See [fallback amplitude rationale](sensor-power-review.md#fallback-amplitude-adjustment--15-november-2220-utc-reproduction).

## Previous usable IMU image restored — 15 November, 22:01 UTC

The operator reported that the stricter ST software-fallback candidate stopped
responding after a couple of shakes. That candidate (`754bc4e6a96e20f6dd4c71a00782c277196e70fc`)
and its complete stopped UART trace are preserved under
`build-tuya-upstream/imu-motion-20261115/install-st-fallback/`. Its trace also
records another configuration-health mismatch at 21:55:26.850 (CTRL2/7/8 read
1d/1d/82), which forces software fallback. The precise cause of the operator's
lost response is not established; the failed detector trial has been reverted.

AP-only rollback restored the exact frozen `install-stop-read/` image,
`bd3b37c4416ec994a11fe675659c9d12dd5bf1c8`, which the operator described as
“much better, marginally too sensitive.” Actual UART confirms its first boot at
22:01:02 with reset sources 0/0, retained-sleep contract 0x78f, launcher startup,
and encrypted/bonded phone recovery with an open PPoGATT session. No additional
hardware restart was issued. Rollback evidence is under
`build-tuya-upstream/imu-motion-20261115/restore-stop-read/`.

Production source again uses the earlier gravity-baseline software detector and
vendor-style STOP-separated register reads only while hardware motion mode is
active. ST hardware mapping and burst re-arming remain. Ten ASan/UBSan IMU tests,
formatting and lint pass after the rollback. All experimental SoC I2C changes
are reverted. The installed image includes driver DEBUG diagnostics; it is not
a power-qualified final image. I2C idle gating and retained sleep remain enabled.
CP/resources/apps/bonds were not programmed, and nothing was pushed.

Wake-time fallback remains unresolved. Bounded confirmation reads, short-byte
RX, controller reset-on-enable, STOP-separated reads and STOP interrupt-mode
trials have not eliminated configuration-health mismatches. The STOP-separated
candidate improved reported behavior but later also fell back. Preserve those
failures; do not describe transport reliability, ST on-wrist equivalence or
power savings as qualified.

## Motion re-arming fix installed — 15 November, 21:01 UTC

The operator reported aggressive triggering once awake. Driver review and a
regression test identified repeated eligibility from QMI's continuing motion
interrupt pulses after each 500 ms holdoff. The driver now groups those pulses
into one burst, re-arming after an observed clear status or a 240 ms inter-IRQ
gap. This debounce choice is five nominal sample periods; it adds no timer,
polling, sensor commands or sampling. ST-mapped thresholds, two-sample duration,
21 Hz low-power mode, gyro-off and the five-second health check are unchanged.
Shared Pebble code is unchanged. See [re-arming evidence and limits](sensor-power-review.md#motion-burst-re-arming--15-november).

Ten ASan/UBSan test methods pass, including >5 seconds of continuous 48 ms
pulses delivering one event and one XYZ read, quiet/clear re-arming and existing
client/recovery transitions. Restoring old edge eligibility in a scratch
mutation fails the regression. Formatting, lint, ARM motion/profile compilation,
firmware build, CRC/payload, version, image map and resource checks pass.

AP-only installation took 51.7 seconds. Installed build ID:
`445232d6d7278ab633fe93cf37d34bbd26b3a0f3`. Actual UART data confirms the first
boot received reset sources 0/0 and retained-sleep contract 0x78f, then launcher,
encrypted/bonded phone recovery and an open PPoGATT session. No extra hardware
restart was issued. No new IMU fallback/failure, hard fault/assertion or retained
admission rejection was observed in this bounded capture. Routine boot warnings
remain in the preserved trace. Prior first-boot admission failures remain open;
this clean boot does not qualify power savings or on-wrist sensitivity.

Frozen image/configuration, source hashes, build/test/flash logs, dictionary,
stopped UART capture and `result.json` are in
`build-tuya-upstream/imu-motion-20261115/install-rearm/`. CP/resources/apps/bonds
were not programmed. No code was pushed.

## Asterix/Obelix IMU mapping installed — 15 November, 20:42 UTC

The operator selected Asterix/Obelix as the sensitivity reference. The driver now
uses their ±4 g / threshold-code 1..40 percentage curve and initial default 16.
It accounts for LSM6DSO's half-difference slope filter, QMI's full-difference
filter, and 80 ms versus nominal 48 ms sampling. QMI code is
`ceil(st_code * 4 * 48000 / 80000)`, rounding toward a less-sensitive threshold.
At the default 55% preference ST code 19 maps to QMI code 46 (1437.5 mg/sample).
The 80 ms ST duration rounds to two QMI samples (nominally 96 ms). This matches
constant-rate acceleration ramps approximately, not arbitrary gestures or
analog filter response. Gyro-off, low-power 21 Hz, suppressed DRDY and the
five-second health check remain. ST shared drivers/services are unchanged.
See [mapping derivation and comparison table](sensor-power-review.md#asterixobelix-sensitivity-mapping--15-november).

Ten ASan/UBSan IMU test methods pass, including all 101 sensitivity values
compared with the actual Asterix/Obelix configuration files, initial/default
versus explicit percentage behavior, stationary override/restoration and hardware
programming/fallback transitions. Formatting, Python lint, firmware build,
metadata, protected CRC/payload, image-map and matching resource checks pass.
AP-only flashing at 460800 baud succeeded in 51.7 seconds. New build ID:
`c7a55db731cb738bca128c4cd896e4b6fddb5501`.

The first post-programming boot received AP=0x4f / CP=0 and retained-sleep
admission rejection reason 4. This trace is preserved in `first-boot/`; it is
not a successful power-saving boot. One explicitly commanded host hardware
restart of the same image then received reset sources 0/0, contract 0x78f,
launcher startup and encrypted/bonded PPoGATT recovery, with no new IMU
failure/fallback, hard-fault/assert or admission rejection in the final bounded
window. No guard was cleared. The first-boot reset/admission issue remains a
qualification concern; the restarted smoke check does not erase that evidence.
Actual sleep-current savings and on-wrist sensitivity equivalence remain open.

Frozen artifacts, source/configuration hashes, dictionary, flash/reset logs,
first and final boot traces, stopped UART capture and `result.json` are under
`build-tuya-upstream/imu-motion-20261115/install-st-map/`. Prior candidates remain
preserved. CP/resources/apps/bonds were not programmed; no code was pushed.

## Reduced IMU sensitivity installed — 15 November, 20:22 UTC

The operator reported the first hardware-motion candidate was still far too
sensitive. The follow-up raises slope threshold codes from 2..12 to 4..24
(125..750 mg per sample); at 50% the threshold rises from 218.75 to 437.5 mg.
Stationary high sensitivity uses code 4 and now requires two consecutive samples
instead of one. Normal motion also retains two-sample qualification. Low-power
21 Hz sampling, DRDY suppression, the five-second health check and existing
software/WOM fallback policy are unchanged. No shared Pebble code was changed.

Nine targeted IMU test methods, formatting, lint, firmware build, metadata,
CRC/payload, map and resource-compatibility checks pass. The new build ID is
`9aa04fc13c2f67c29a82c8a141bac9f38c76207f`; AP-only programming completed at
460800 baud in 51.8 seconds. Live UART confirms that exact build, reset sources
0/0, contract 0x78f, launcher startup and encrypted/bonded PPoGATT recovery.
No IMU setup/fallback, hard-fault/assert or retained-admission failure was
received in the bounded capture. Capture is stopped. This verifies installation
and startup; the operator's on-wrist sensitivity assessment and electrical
comparison remain pending. No code was pushed; CP/resources/apps/bonds were
preserved.

Artifacts, hashes, exact configuration/dictionary, flash log, stopped UART
capture, decoded text and `result.json` are in
`build-tuya-upstream/imu-motion-20261115/install-less-sensitive/`. The original
sensitive candidate and its trace remain in the adjacent `install/` directory.

## Hardware IMU motion candidate installed — 15 November, 20:16 UTC

The operator requested installation of the new IMU candidate. Freshly built
`v4.41.2-1643-g66488f004-dirty`, build ID
`eb704a420f5463c05c143b0cf07709f1ef9b127e`, was AP-only flashed at 460800 baud.
`CONFIG_T5AI_QMI8658_ANY_MOTION=y` enables the low-power 21 Hz hardware slope
candidate; data clients and error fallback use the corrected time-based software
filter. The board default remains off; this build has an explicit override.

Programming completed successfully in 51.8 seconds, with the protected write
bounded to physical `0x132000..0x2e080e` (end exclusive). Resource bytes match the
installed bank; CP, resources, apps/data and bonds were not programmed. The
initial invocation stopped at a Python module import error before device access;
its log is retained separately. No code was pushed.

Live host UART received the exact build ID, AP/CP reset sources 0/0, retained
sleep contract 0x78f, launcher startup and encrypted/bonded phone recovery with
an opened PPoGATT session. The bounded capture reports no QMI initialization,
any-motion fallback, hard-fault/assert or retained-admission failure records.
This is a boot/protocol smoke check, not proof of hardware gesture qualification,
actual sleep-current savings or sustained UI behavior. On-wrist first/repeated
wake, false triggers, touch/buttons and matched power checks remain pending.

The frozen ELF/images, configuration, log dictionary, hashes, flash log, stopped
UART capture, decoded text and `result.json` are preserved under
`build-tuya-upstream/imu-motion-20261115/install/`. The previous normal and
hardware builds remain under the adjacent `normal/` and `hardware/` directories.
See [sensor implementation and qualification notes](sensor-power-review.md#15-november-time-based-filtering-and-opt-in-hardware-any-motion).

## Native I2C interrupt lifetime repair — 15 November

The screen also froze before physical input. Removing the synthetic scroll,
using the native cycle-counter delay, disabling AP retained sleep, and matching
the vendor GPIO bank acknowledgement did not individually resolve the freeze.
Those experimental source changes are removed from the normal image.

An independent priority-zero Timer0 probe captured a stalled trace with SysTick
and PendSV pending and I2C0 as the next pending exception (`ICSR=0x14416813`).
The interrupted thread was rendering text; BASEPRI and PRIMASK were both zero.
The exact probe ELF, first stall record and subsequent CP reset reason 10 are
preserved under `build-tuya-upstream/screen-freeze-20261115/probe1/`.

The T5AI I2C driver enabled NVIC delivery before publishing an active transfer.
Its inactive ISR returned without masking that source, while native bus completion
only wakes the caller and leaves controller shutdown to that thread. A stale
interrupt could therefore prevent setup or cleanup and starve lower-priority
SysTick/PendSV. Commit `340d1c03a` keeps the IRQ masked until START and transfer
state are published, masks it before completion wakes the caller, and masks late
inactive interrupts. STOP draining, clock ownership and deferred recovery remain
in the driver; the common native I2C implementation is unchanged.

Eight I2C tests pass, including stale-IRQ interleavings with idle gating both
on and off, FIFO transfers, STOP draining and retained-owner recovery. The full
tooling suite completes 468 tests with no failures and 16 optional skips. The
fixed diagnostic processed thousands of physical touch reports and advanced for
over five minutes with one boot banner, no stall record and no reset. That
instrumented run does not replace normal-image UI or sleep qualification.

Normal `v4.41.2-1642-g340d1c03a` was successfully AP-only flashed at 460800 baud.
Its build ID is `0b878d2516573a3e3501a4b1fc0d0830c6837fa8`; matching ELF, images,
configuration, hashes and UART capture are in
`build-tuya-upstream/screen-freeze-20261115/final-i2c-normal/`. CP, resources,
PFS and bonds are preserved. Original retained sleep and peripheral policy are
restored; live tracing, the independent probe and synthetic input are absent.
Startup reached the launcher with AP/CP reset sources 0/0 and contract 0x78f.
Operator confirmation of sustained button/touch responsiveness is pending.

The native SDK syscall fix from the following checkpoint is installed too.
A 921600-baud attempt stopped partway through AP programming; its failure log is
preserved in `gpio-ack2/`, and complete AP programming subsequently succeeded at
460800 baud after adapter reconnection. Do not treat a partial write as installed.

## Native watchface SDK fault — 15 November

The operator reported a frozen screen after the short startup check. A retained
first-state capture received 32 AP-only sleep returns with no reported sleep
fault, followed by one CST92xx report-transfer error. That trace alone does not
establish the freeze mechanism.

A later diagnostic captured Nadir faulting twice at
`__wrap_process_manager_compiled_with_legacy2_sdk`: the unprivileged watchface
read protected `pbl_cur` at `0x2802009c` (PC `0x02134314`, diagnostic ELF in
`build-tuya-upstream/screen-freeze-20261115/live3/`). The former SDK draw-cache
wrapper bypassed the native privileged syscall entry. The cache, option,
lifecycle wrappers and obsolete host-only fixture are retired. Compatibility
queries now use the unchanged upstream syscalls. A SoC-local linker retention
rule preserves the native syscall-exit hook called from ARM assembly under LTO.

Temporary live tracing and the one-time same-watchface relaunch are preserved
as diagnostic artifacts, not production source changes. All 467 tooling tests
complete successfully (15 optional tests skip); seven native process, syscall,
metadata and graphics tests pass. Installation of the SDK fix is pending CH342
recovery: two attempts failed with USB serial protocol errors before writing.
The previous diagnostic remains installed at this checkpoint.

## Native startup repair — 15 November

Hardware startup exposed three missing native contracts: the round-platform
compositor requires a display mask, and the light and vibration services require
valid device roles. Unhygienix now supplies a zero-inset mask for its rectangular
AMOLED, a board-local native ALS stub returning a fixed dark reading, and an inert
native vibration device. Shared Pebble service sources remain unchanged.

The CP console now buffers IRQ-locked UART callbacks and sends from native work
context at the supported 460800 baud. Optional T5AI fault tracing wraps the native
CPU fault handlers; diagnostic traces localized the missing ALS and vibration
roles before the board stubs were added.

The diagnostic image reached the launcher, restored the existing bond, enabled
advertising and opened an encrypted reversed PPoGATT phone session. AP-only
programming preserved CP, resources, filesystem and bond data. The CH342 adapter
required a physical reconnect after repeated serial protocol errors before the
successful write. Evidence, matching ELF files and dictionaries are retained in
`build-tuya-upstream/native-bootfix-20261115/`; attempt 9 contains the stub image
startup. Earlier failures are retained separately.

The final normal image from `2fce502f6` is now installed, version
`v4.41.2-1639-g2fce502f6`, GNU build ID
`cf0058fc5e5fb7ce87c5764dc092d29f6609c071`. Fault tracing is disabled.
The AP-only write took 32.4 seconds over physical range
`0x132000..0x2e05aa` (end exclusive). A 210-second capture, approximately
175 seconds after reboot, contains one boot, launcher startup, restored bonding
and an encrypted phone protocol session, with no new fault or reboot observed.
Matching images, ELF, config, dictionary, hashes, raw UART and `result.json` are
frozen in `build-tuya-upstream/native-bootfix-20261115/final-normal/`.
UI/watchface regression, disconnect-cycle testing and overnight soak remain open.

Validation: all 468 tooling tests complete successfully (15 optional tests skip),
including native class tests for the fake peripherals, compositor startup, and
console work scheduling. The ARM image links with partition and resource checks.

## Native driver completion — 15 November

Flash and microphone no longer adapt the removed `flash_impl_*` and `mic_*`
interfaces. The NOR driver implements `pbl_flash_ops` directly, uses partition
offsets throughout, splits complete writes into pages and returns synchronous
erase completion. It retains CP transaction quarantine, partition policy and
cache invalidation/verification. The upstream flash class owns serialization
and range protection. Coredump initialization rejects CP-backed access because
that transport requires kernel services.

The ADC implements the native audio DAI directly. It converts completed DMA
halves into the client's period ring and reports completion from the interrupt,
with no legacy microphone callbacks, KernelBG dispatch or intermediate mono
ring. Settling, DMA cache maintenance and power votes remain; lost blocks report
an audio xrun. Native start/stop paths release allocations on failures and reject
a faulted restart. The RTC class reads/writes the retained/external clock directly;
legacy RTC entry points and the old board/configuration headers are removed.

The `soc/t5ai/compat` include tree is retired. Firmware layout and resource storage
use native partition metadata. Optional deadline observation uses native one-shot
timers; IMU self-test scheduling uses delayed work. Watchdog storage diagnostics
intercept the native filesystem, settings and flash interfaces. Drawing and
storage profilers that own the same mutex wrapper cannot be selected together.
The unregistered pre-upstream C resource fixture was retired; executable native
resource regression tests remain in `tools/tests/test_t5ai_resources.py`.

Validation: normal firmware and the tick/state/reboot/heap/reminder diagnostic
configuration link and package with partition-map checks. Eleven optional sources,
including the IMU self-test and watchdog/deadline diagnostics, compile with ARM
firmware flags. All 432 upstream host tests pass. The tooling suite discovers
465 tests: 450 pass and 15 optional CP qualification tests skip. Formatting and
commit lint checks pass. Logs use the `native-` prefix under `build-tuya-upstream/`.

Gabbro application ABI support, the pinned CP wire protocol, SD PFS integration
and board-specific safety hooks remain. Those implement hardware/application
contracts rather than emulating removed upstream driver APIs. No hardware has
been flashed or exercised for this change.

## Upstream main migration — 15 November

The Tuya branch incorporates Core Devices main through `f4926c952` (717 upstream
commits). The production application now lives under `pebble/`; Unhygienix board
implementation and device roles live under `boards/unhygienix/`. Scoped legacy
hardware declarations initially remained under `soc/t5ai/compat/`; the native
driver completion above removes that tree.

The T5AI port uses upstream device classes for RTC, NOR flash, microphone audio
and CP console UART. AON clock-source and one-shot SysTick clock-event drivers
replace periodic tick compensation; retained sleep preserves the programmed
wake deadline. Board-local MPU regions preserve non-shareable cache behavior.
The context switch selects a T5AI-local MPU restore sequence because the stock
sequence permits transient overlapping ARMv8-M descriptors. Other targets retain
the upstream context-switch implementation.

Normal Unhygienix firmware builds and generates the protected T5AI image with
its existing partition-map checks. All 432 upstream host tests pass, including
graphics fixtures. Migrated driver regressions exercise storage, RTC, timer,
touch, battery, IMU, codec allocation and console behavior; ARM QEMU checks
interrupt injection at MPU descriptor-write boundaries. Optional tick, heap,
reboot, state, draw and reminder diagnostic sources compile against the new APIs.
Diagnostic combinations have not all been linked or exercised on hardware.

The `tools/tests` fixtures have been migrated or retired for the upstream APIs.
The full suite passes: 463 tests discovered, with 15 optional CP qualification
cases skipped because they require Unicorn and/or reviewed external artifacts.
This is host validation; optional diagnostic modules still need hardware exercise.
The [tooling test notes](tooling.md#tooling-tests-after-the-upstream-migration--15-november)
describe retained coverage and retired fixtures. Build and test logs are retained
locally under the ignored `build-tuya-upstream/` directory.

This firmware has not been flashed. The installed-watch checkpoints below remain
the hardware evidence; boot, reconnection and connected-idle validation are still
required for the migrated build. CP inputs, HCI wire ABI and SD/NOR partition
geometry remain pinned. Unsupported update and recovery paths remain disabled.

## Native QSPI/DMA readability — 14 November

The native SPI driver now names register offsets, bit fields, clock/divider
settings, framing/FIFO limits and timeouts against the pinned BK7258 AP register
reference. Transfer state and DMA callbacks have descriptive names; native DMA
channel state, direction, widths and indices are also expanded. Register access
order, deadlines, DMA inheritance default and visible display behavior are unchanged.

All 57 actual-source ASan/UBSan regressions pass. Before/after optimized host
assembly is identical in the existing endpoint models for SPI and both DMA
policy paths. Normal release `b6a1ca163edd307b0bc11b7a0480a540e72c971a` passes
CRC/map/config/resources, 28,960-byte heap and linked SRAM-WFI checks. The linked
DMA initializer retains its reset guard with no shared-global store. Evidence:
reference `artifacts/driver-readability-20261114/`.

This build is not flashed; installed AP remains `4865f074` with the previously
recorded boot limitations. No new hardware observation or electrical savings
claim follows from this refactor. The diagnostic recovery guide now pins its
historical patch baseline before this refactor.

## QSPI/DMA diagnostic code retired — 14 November

The recorder, mixed-I/O and fixed-display-energy harnesses are removed from
production: nine diagnostic-only files, their options/build entries and all
DMA/SPI/board/sleep hooks. Functional regressions and safety handling remain;
PSRAM acquisition acknowledgement and both range checks precede framebuffer use
without a diagnostic conditional. DMA inheritance remains default-on.

An exact 18-file source archive and unapplied recovery patch are preserved under
reference `artifacts/diagnostics-retirement-20261114/`. The patch passed checks
against the cleaned tree and restored all archived bytes in a temporary tree.
See [diagnostic archive/reuse](diagnostics-archive.md). Existing electrical data,
failed captures and rollback images remain untouched.

This is source cleanup, not a flash. Installed AP remains 4865f074 with its older
profiling and DMA inheritance enabled, and its last AP=0x4f / retained rejection 4
capture remains the latest observed boot state. No new hardware, power, shutdown
or overnight claim is made. Startup/BT diagnosis remains deferred.

Validation: the 54-test retained suite passed; three additional functional SPI
regressions were then retained from the retired module and passed in the ordinary
four-test SPI suite (57 unique tests total). Clean normal release
0d3f28ddb4efba67367c1d848b2313ace4eb22ef passes CRC/map/config/resources,
28,960-byte heap and actual SRAM-WFI checks. Source/config and linked-ELF audits
confirm diagnostic files/options/hooks/symbols are absent and DMA inheritance is
on. No firmware was flashed for this source cleanup.


## DMA inheritance enabled by default — 14 November

At the operator's explicit request, Unhygienix native DMA now defaults
`T5AI_DMA_INHERIT_CLOCK_POLICY=y`. The dependency on QSPI/DMA profiling is removed;
profiling, mixed-I/O qualification and the display energy workload stay default-off.
Explicit `=n` restores the old AP-forced bypass path. The reset-release guard,
no shared-global writes, inherited CP bypass requests, channel lifetimes and
retained blockers are unchanged. No CP or shared Pebble code was changed.

This is a source/build default change. Installed AP remains 4865f074 with DMA
inheritance already enabled and profiling enabled; no flash/reset accompanies
this request. Its preserved AP=0x4f / retained rejection 4 remains the last
observed boot state. Startup/BT diagnosis remains deferred and normal connected
power savings remain unqualified. Earlier default-off statements below refer to
the policy at those historical checkpoints. See [policy](dma-clock-policy.md)
and reference `artifacts/dma-default-20261114/` for unprofiled tests and build checks.

44 focused actual-source ASan/UBSan tests pass, including unprofiled read-only
global transfer/error/cancel paths and reset-held rejection before channel/access/
IRQ setup. New normal release 61661de0fa759d81def71403144f09f219a03d05 passes
CRC/map/config/resources and actual SRAM-WFI audit. Its effective config enables
inheritance with no cached inheritance override; profiling/workloads remain off.
Kernel heap is 28,960 bytes. Linked device-init disassembly retains the reset
guard and has no shared-global store. This new unprofiled image is built, not
flashed; installed 4865f074 and its observed guarded boot state remain unchanged.


## Cleanup and DMA-enabled normal-use trial — latest checkpoint, 14 November

**Installed AP: `4865f0740c250426618095f1e3fd6cf6e6262bd9`. DMA inheritance is
ENABLED at the operator's explicit request.** QSPI/DMA profiling is enabled for
its bounded boot/32-return reports; both pre-splash workloads are disabled.
Panel Sleep In/raw-zero remain off. Source option defaults remain off; this is
an explicitly configured normal-use trial, not default release qualification.

The cleanup names private snapshot slots and compile-checks their separation,
derives early-clock capacity from that layout, names display workload timing/
row bounds, removes an already-satisfied AP-channel condition and tidies Kconfig
spacing. Event numbering, snapshot layout/size (844 bytes), bounded reporting,
channel ownership/completion and existing power/display policy are preserved.
The index no longer labels earlier diagnostic/standby images as current.

72 actual-source ASan/UBSan tests and style/diff checks pass. Normal and candidate
release builds pass CRC/map/config/resources/heap checks. Candidate has a
28,832-byte kernel heap, 127,328 / 131,072 display/cache bytes and 14,976 / 16,384
display-DMA bytes. The actual SRAM WFI entry audit passes. The default-off normal
image remains runtime-identical to known-good faae34b9 apart from GNU build ID.
Known-good faae34b9 rollback images/hashes remain frozen.

AP-only flash took 32.6 seconds, physical range 0x132000..0x2dcc4a end-exclusive.
Fresh UART bytes received the exact build, 240 MHz AP and inherited/ready DMA
global 1/1; this confirms AP no longer forces bypass. It does not prove physical
clock-stop residency or permission to gate a shared controller/domain. QSPI
ready global remains 1. CP firmware/frequency/settings/identity/calibration,
resources, PFS and bonds were not programmed or erased.

This boot reported AP=0x4f / CP=0 and the existing retained-sleep rejection
reason 4 before normal startup. Preserve the first capture; no automatic reset,
guard weakening or startup/BT investigation followed. **Retained sleep is blocked
on this boot; no clean 32-return milestone is claimed.** The DMA-enabled image
is intentionally left installed for the operator's display/input test. Physical
UI/input acceptance is separate from UART confirmation and remains pending until
reported. No new power measurement, shutdown or overnight run accompanied this
cleanup. Earlier approximately 1 mW savings apply only to the bounded measured
workload, not this guarded normal-use boot.

Evidence: reference `artifacts/power-cleanup-20261114/` includes source/cleanup
patch, frozen images/config/dictionary, tests, layout/resource/SRAM audits,
rollback hashes, AP-only flash and first-startup UART capture. See
[DMA policy](dma-clock-policy.md), [measurement limits](display-energy.md),
[tooling](tooling.md) and [power TODO](power-todo.md).

## Fixed-workload DMA energy result — previous checkpoint, 14 November

A corrected control/trial/control comparison completed all 48 redraws per run,
with zero boundary owners, faults or retained entries and the same source word
0x83188491. Phone Bluetooth was off by operator confirmation; battery absent,
FNB58-only watch USB and no UART power lead remained unchanged. Measurement was
before UI/BT startup and retained registration, deliberately matching shallow
AP WFI rather than normal connected retained idle. See [method](display-energy.md).

| Run | DMA global | Idle USB mW | Redraw USB mW | Gross mJ / redraw |
| --- | --- | --- | --- | --- |
| Control A, af972d11 | 3 | 128.096 | 129.223 | 32.333 |
| Inheritance B, 2800bff0 | 1 | 127.135 | 128.341 | 32.112 |
| Exact control repeat C | 3 | 128.182 | 129.197 | 32.353 |

Relative to the controls' mean, trial was 1.005 mW lower idle (0.78%),
0.869 mW lower during the fixed redraw workload (0.67%), and 0.231 mJ lower
per completed scheduled redraw (0.71%). Control drift was +0.086 mW idle and
-0.026 mW redraw. This is a small observed whole-watch USB input reduction in
this bounded workload, not isolated panel/DMA power or qualified everyday savings.
Gross energy includes scheduled idle time. Idle-subtracted estimates were
0.289 / 0.308 / 0.287 mJ per redraw and do not show an incremental redraw-energy
improvement. UART marker spread was 0.57–0.61 ms; +/-100 ms alignment sensitivity
was evaluated. Absolute transport delay, meter accuracy and burst aliasing at
nominal 100 Hz remain uncalibrated; one trial boot does not establish a confidence
bound or release qualification.

The first control c4ae51f1 passed its workload but was rejected electrically
because lazy serial buffering delayed marker tails by up to six seconds. Its
capture remains in `control-a/`. The corrected default-off harness flushes
existing serial output only at phase boundaries. Shared logging code is intact.
72 actual-source firmware tests and five analysis tests pass. All frozen builds
pass resource/CRC/layout/heap/SRAM checks; normal runtime is unchanged apart
from its build ID. Artifact: `artifacts/dma-energy-20261114/`.

Exact normal faae34b9 was restored and its exact banner, reset sources 0/0
and 32 clean AP returns (all CP deltas zero) received. See `restore/` and the
final result. Benchmark, qualification and DMA inheritance remain
off in normal; normal dim/QMI WOM/storage/resources/bonds are preserved. No CP
partition or settings changes. Startup/BT diagnosis remains explicitly deferred.
Keep inheritance default-off pending repeated matched trials and normal-use
qualification. No shutdown or overnight test was performed.

## DMA clock-policy trial — latest checkpoint, 14 November

The separate default-off DMA inheritance trial `2921571f` passed: DMA global
was inherited as 1 and remained 1 through the recorded phases, whereas control
`99a8a768` forced it to 3. The experiment never writes that shared word; it
rejects reset-held hardware before channel/IRQ setup. This permits inherited
automatic gating but does not prove physical clock residency or authorize shared
controller/domain shutdown. Normal builds retain their existing bypass policy.
See [implementation and evidence](dma-clock-policy.md).

Both diagnostic arms completed 40 redraws, 16 SD-RX reads, four off/on/rotation
cycles, 168 microphone frames / 53,760 samples and 16 concurrent owner starts,
with no owners left, restore errors or microphone faults. The trial recorded
32 clean AP retained returns (all CP deltas zero), reset sources 0/0 and encrypted
bonded phone recovery. The operator confirmed the requested display/dim, motion,
touch/button checks: "done. fine". These are bounded observations; SD-TX and
in-flight display cancellation remain host-tested. 69 actual-source tests pass.

The exact prior normal AP `faae34b9e9037bec3cce3bb118f898beceae0bc3` was restored
and its exact build banner, reset sources 0/0 and 32 clean AP returns
(all CP deltas zero) received; the capture under
`artifacts/dma-auto-gating-20261114/restore/` records the final installed state.
No CP, resource, PFS or bond partition is programmed. Panel sleep and raw-zero
brightness remain off; normal dim policy is unchanged.

FNB58 whole-watch USB windows were 137.609 mW / 27.546 mA (control, 5,976
samples) and 137.369 mW / 27.497 mA (trial, 5,980 samples), near 4.996 V.
Control had retained-sleep admission closed (AP=0x4f, reason 4); trial admitted
retained sleep. Radio/input activity also differed. The approximately 0.240 mW
difference is **not demonstrated savings**. No panel-only current or energy per
completed redraw workload was measured. Battery remains absent, USB supplied
through FNB58, no UART power lead; signal back-power is not electrically excluded.

Startup and Bluetooth diagnosis are explicitly deferred by the operator for
this work; prior failures remain preserved and guards unchanged. Next power
work is a timed fixed-workload comparison with matched AP and radio states,
including completed-workload energy, before proposing default enablement.
No overnight or shutdown test was performed.

## Final installed state and incomplete power comparison — 14 November

**Installed: normal AP `faae34b9e9037bec3cce3bb118f898beceae0bc3`; phone connectivity
recovered by an operator USB supply cycle.** Qualification/QSPI diagnostics,
raw-zero brightness and panel Sleep In remain disabled. Normal dim remains.
No CP/resource/PFS/bond partition was programmed, and no pairing was deleted.

The short power sequence recorded normal then the original pre-panel-cache
normal `96b2e1aa`, with identical effective configuration/resources, battery
removed, fixed FNB58 watch USB input and no UART power lead. Both accepted
windows followed 32 clean AP returns and encrypted phone recovery. Each has
5,980 CRC-enabled samples (nominal 59.8 s): normal 140.91 mW / 28.209 mA,
control 138.03 mW / 27.632 mA, both approximately 4.995 V. These are whole-watch
USB input readings, not panel power or demonstrated savings. Phone AppMessage
traffic changed connection intervals during the windows, and normal's first
window still contained connection-parameter changes. Do not infer a regression
or saving from the 2.88 mW difference. The planned final normal repeat was
**not recorded**, so the A/B/A comparison is incomplete; wake/redraw workload
energy remains unmeasured.

The control and final normal restoration both initially reported AP=0x4f / CP=0
and sleep rejection reason 4. Their first traces were frozen before one
commanded same-image reset each. Both then reached clean reset sources and 32
AP-only returns. On the final normal reset, however, phone reconnection failed:
UART ended at advertising disable/slow-parameter setup without a later enable
or encrypted session; the operator confirmed "It will not reconnect". This
is a failure observation, not a proven controller/driver root cause. Preserve
`final-install/first-reconnect-failure/`; it supersedes the earlier successful
normal-install checkpoint as the latest recovery requirement.

The operator performed the agreed watch-USB recovery and reported "everything
reconnected fine". A new capture received contract 0x78f, 32 AP returns with all
CP deltas zero, encrypted bonded recovery and an open phone session. Its capture
began after the early startup banner/reset-source rows, so those rows were not
re-verified on the supply cycle. No further flash followed the last verified
normal AP. Actual complete supply removal/back-power and screen-dark behavior
were not separately measured; this is not standby/shutdown qualification.
No further power recording followed the failure. All captures are bounded and
stopped; no overnight run is active.

Next: investigate the repeatable post-programming sleep-resume marker and the
post-reset advertising/reconnect stall before further controller clock changes.
Do not clear the boot guard merely to admit sleep, or change CP firmware,
identity/calibration or bonds to recover this test. Then repeat a fully matched
power sequence and a completed wake/redraw workload. Shared DMA gating and
panel-sleep qualification remain open. Exact images, 63 passing focused tests,
source increment, failed attempts, readings and recovery evidence: reference
`artifacts/qspi-dma-qualification-20261114/RESULTS.md`.


## Mixed DMA qualification and normal restoration — 14 November

A default-off, board-local `CONFIG_T5AI_IO_QUALIFICATION` now runs once before
splash/compositor ownership. It borrows the validated framebuffer, discards
microphone input, reads SD sector zero without writes, and performs 16 full and
16 visibly changed partial redraws plus four display off/on/rotation cycles
with full redraw before each disable and a last-row update after enable.
No Sleep In, raw-zero policy, clock gate, shared-source change or periodic task
is introduced. Native synchronous completion is asserted before framebuffer
handoff; its existing bounded SPI/DMA error paths remain in force. Missing
completion invokes the standard assertion rather than reusing busy pixels.
Accepted SD callbacks are joined with a one-second deadline, including display
failure. Static callback storage survives timeout and one-shot reentry is
blocked. Cleanup stops only the microphone it started, clears its sample buffer,
restores orientation and enables the display. Fault/cleanup/remaining-owner or
missing-frame/overlap results cannot be reported as success.

Qualification AP `c682e88d2963ea418a2f506aafae143d4102fc5d` was programmed AP-only
in 32.7 seconds. Received result 0: 168 audio frames / 53,760 samples, 40 redraws,
16 successful read-only sectors, four disable/enable cycles and 16 simultaneous
AP DMA-owner starts involving display (0), microphone (1) and SD RX (3).
All owners were released, restore results were 0/0 and microphone fault was zero.
These are driver ownership intervals, not coherent hardware clock-residency or
energy measurements. SD TX concurrency and cancellation during a display batch
remain host-tested rather than physically qualified. Operator: "yes, all ok"
for the requested patterns/rotation, normal UI, motion wake, touch/buttons and
absence of unexpected restarts/corruption.

That qualification boot reported AP reset source 0x4f / CP 0 and sleep rejection
reason 4. Preserve `install/first-failure/`; no qualification-image reset or guard
relaxation followed. Mixed I/O passing does not make that first startup a clean
retained-sleep qualification. This repeats the known post-programming issue.

Normal AP `faae34b9e9037bec3cce3bb118f898beceae0bc3` was then restored AP-only in
32.6 seconds, with both qualification and QSPI/DMA profiling excluded. UART
received its exact build, AP/CP reset sources 0/0, 240 MHz, existing SD PFS
(`new=0`), contract 0x78f, 32 AP returns with all CP deltas zero and bonded
encrypted phone-session recovery. No extra commanded reset was needed.
Operator confirms normal display/dim, motion wake, touch/buttons, phone and the
usual watchface. This normal raw image equals the previously prepared `513b648c`
runtime except its GNU build ID. Prior rollback and failure artifacts remain.

Validation: all 63 focused actual-source tests passed, including the board
PSRAM call-site and final cleanup regressions. Normal/qualification release builds,
protected CRC, AP/map layout, identical resources, effective policy config,
SRAM retained-entry audit and style checks passed. Heap is 28,960 / 28,832 bytes;
qualification display/cache aperture usage is 128,512 of 131,072 bytes. The normal
image links neither the qualifier state nor the QSPI/DMA profile. Exact source,
separate qualification increment, images and captures are in reference
`artifacts/qspi-dma-qualification-20261114/`.

Electrical setup was reconfirmed: **battery disconnected**, watch USB supplied
only through FNB58, UART no power/5 V lead. Signal/ground wiring remains fixed.
Fresh meter readiness received 968 samples; it used diagnostics and is not a
savings comparison. Matched normal/control/normal idle recordings are tracked
with final installed state and limitations in that artifact's RESULTS.md.
They measure whole-watch USB input, including conversion losses; no separate
panel current measurement or completed wake/redraw energy result is established.
Shared DMA gating remains blocked by CP ownership and restore proof. No overnight
soak or shutdown test was run.


## QSPI/DMA startup correction installed — 14 November

Corrected diagnostic AP `e3b04f32d4f0c8705eb1d9ee3e457c59dc1e3f57` is now installed.
AP-only programming took 32.6 seconds. The final bounded UART capture received
10,380 bytes and is stopped: one bootloader entry, exact build, reset sources
AP/CP 0/0, 240 MHz AP, existing 32 MiB SD PFS (`new=0`), contract 0x78f,
32 clean AP retained returns with all CP deltas zero, diagnostic phases 0/1,
and bonded encrypted phone protocol recovery. No extra commanded reset or
rollback was needed on this final attempt. Operator: "all good" for display/dim,
motion wake, touch/buttons and companion reconnection.

Two unsafe diagnostic paths were corrected locally: lazy writes to PSRAM before
board power/validation, and channel-register reads before DMA reset/access grants
including unproven CP channels. Bootstrap records now stay in SRAM; the full
buffer starts explicitly after successful PSRAM power/range checks. Early
snapshots read only global control words. Full DMA snapshots read AP channels
0–3 only after reset/security/privilege setup; CP channel registers are not read.
Both defects have failing-before/passing-after host regressions. No hardware
fault-register capture identifies which defect triggered each original reset.

The original `52577e7b` and PSRAM-only correction `0cf5a0f0` both failed early
startup; their exact images and first traces are preserved. The intermediate
trial was rolled back to `b40a6cb1`; its "looks good" reply referred to that
rollback, not the rejected image. Final source passes 59 focused tests plus
normal/diagnostic release image, CRC, map, config, resource and SRAM-entry checks.
Kernel heaps: 28,960 bytes normal, 28,832 diagnostic (160 above the minimum).
Evidence: reference `artifacts/qspi-dma-startup-fix-20261114/result.json` and
`install-access/`; failed intermediate trace is under `install/first-failure/`.

Observed globals: QSPI inherited 0 / ready 1 / latest 1 (bypass bit 1 clear);
DMA inherited 1 / ready and latest 3 (AP still forces bypass bit 1 on). There is
no redundant QSPI bypass bit to clear on this startup. These are register
observations, not clock-residency or electrical measurements. No clock or panel
policy changed. CP/resources/PFS/bonds were outside every programming write.
Shared DMA gating remains blocked on CP ownership and safe all-channel semantics;
physical mixed DMA and repeated display disable/orientation qualification remain
pending. No electrical saving or soak is claimed.

## Diagnostic AP failed startup; previous AP restored — 14 November

The operator authorized flashing diagnostic AP `52577e7bcd27d8b927865011101150206210e142`.
AP-only programming succeeded in 32.5 seconds, but UART then repeatedly received
bootloader/CP startup and PebbleOS/version banners without reaching a build-ID
or T5QD report. The operator reported "the display isn't working now". This is
a failed hardware qualification, not a successful diagnostic installation.
The cause is undetermined; passing host/image checks did not prove startup safety.

First-failure raw bytes/chunk timestamps and decoded frames are preserved in
reference `artifacts/qspi-dma-diagnostics-20261114/install/first-failure/`.
The exact previously installed `b40a6cb1070c5440f74c1c758b2c0eff8a38290a` AP
was restored AP-only in 32.6 seconds. UART received its exact build, AP/CP reset
sources 0/0, 240 MHz active clock and SD startup, contract 0x78f and 32 AP-only returns with all CP transition/clock/
mask/resource/fault deltas zero. The operator confirms display, touch and buttons
restored. Capture stopped after 150,242 bytes; no extra commanded reset.
Encrypted phone recovery was not established by this capture. Exact evidence:
`install/result.json`. No CP, resource, PFS or bond
partition was written. The diagnostic image is marked rejected and must not be
reflashed until its early-boot failure is explained. No electrical saving or
QSPI bypass-state conclusion was obtained.

## QSPI/DMA first package implemented, not installed — 14 November

[Acquisition safety and bounded diagnostics](qspi-dma-diagnostics.md) now build
on source HEAD `21b1cabcd`. QSPI initialization returns `-EIO` on CP PM failure
before source/controller writes, without uncertain vote rollback. Default-off
`T5AI_QSPI_DMA_PROFILE` retains first transition/failure snapshots and counters
in AP PSRAM; only boot and existing 32-return reports are added when enabled.
No gating or display policy changed. 54 distinct focused host tests pass;
normal `2b89603e5966db8ffdf20e0cc6431e3f2d79edf3` and diagnostic
`52577e7bcd27d8b927865011101150206210e142` builds pass image/CRC/map/config/
resource/SRAM checks, retaining 28,960 bytes of kernel heap. Neither was flashed.
Installed state remains `b40a6cb1` below. No new hardware/electrical result exists.
The matching installed CP97 ELF was not found, so linked CP ownership remains
incomplete and shared DMA gating is blocked. Next is one bounded diagnostic AP
capture and functional check; first-startup qualification remains open.
Evidence: reference `artifacts/qspi-dma-diagnostics-20261114/`.

## Original QSPI/DMA scoping checkpoint — 14 November

[The scoped work](qspi-dma-power-scope.md) starts with bounded diagnostics,
CP/AP ownership inventory and safe QSPI PM-acquisition failure handling. Source
shows DMA init forces global bypass while QSPI init preserves inherited bypass;
neither observation establishes a measured saving or safe shared-gate policy.
Conditional local QSPI and GDMA auto-gating experiments, then later deferred
QSPI source release, each have separate proof/test/measurement gates. No runtime
source, configuration, installed image or hardware state changed during scoping.
The installed AP remains the checkpoint below; no new electrical claim is made.

## Latest AP reinstalled after comparison — 14 November

On explicit operator request, AP `b40a6cb1070c5440f74c1c758b2c0eff8a38290a`
was reinstalled AP-only over `96b2e1aa`; programming succeeded in 32.6 seconds.
Current runtime source, frozen hashes, image CRC/layout/map and metadata checks
passed. CP97 readback was verified earlier in this session; no CP write occurred.
CP/settings/calibration, resources, PFS and bonds remain outside all writes.

First post-programming boot received AP=0x4f/CP=0 and closed sleep admission;
its raw capture is preserved in `reinstall-latest/first-failure/`. One explicitly
commanded same-image hardware reset then received exact build, AP/CP sources 0/0,
SD startup, contract 0x78f and 32 AP returns with all CP transition/clock/mask/
resource/fault deltas zero. No further reset was issued. Normal dim/WOM policy
is unchanged. This is not clean post-programming startup qualification, and the
earlier operator-reported splash/apparent restart remains unresolved.

The bounded UART capture received 8,277 bytes and is stopped. Encrypted phone
recovery was not captured on this final reinstall; the first install verified it
on the same image. No new physical comparison result is inferred from the
operator's request. Raw/protected SHA256 remain
`d8502e8e8303fe2a3b96cbe5fc94b41f46be6dd81b4c4ec2e10064c115117f73` /
`197bd6ba6534d313e2068901cba4b35073748bb695bcd1a07a4fdcf239e197e6`.
Exact logs/results:
reference `artifacts/standby-display-20261114/reinstall-latest/result.json`.
This installed-state checkpoint supersedes the rollback checkpoint below.

## Previous AP restored for operator comparison — 14 November

At the operator's explicit request, the previously installed AP
`96b2e1aabc0e5f1748c6bbdaa765efe168f1a01b` was restored AP-only after hash,
protected CRC/layout/map and identical configuration/resource checks. Programming
succeeded in 32.6 seconds. UART received the exact restored identity, AP/CP reset
sources 0/0, 240 MHz clock, contract 0x78f and SD startup. No additional reset was
issued. The bounded capture is stopped; subsequent physical comparison is for
the operator. No new 32-return or phone-recovery milestone was captured during
this short rollback check.

CP97 was fully read back/CRC-verified earlier in this session and has not been
written since. CP, settings/calibration, resources, PFS and bonds were outside
the rollback write. Production source and the `b40a6cb1` candidate remain intact;
only installed AP changed. The earlier splash/apparent-restart evidence remains
unresolved and preserved. Exact rollback images, logs and capture result:
reference `artifacts/standby-display-20261114/restore-96b2e1aa/result.json`.

## Standby/display candidate installed — 14 November

At the operator's request, normal AP
`b40a6cb1070c5440f74c1c758b2c0eff8a38290a` was AP-only programmed successfully
in 32.6 seconds (first programmer attempt). Current source matched the frozen
candidate. Full current CP97 readback matched the frozen package byte-for-byte,
with protected CRCs verified before the AP write. CP firmware/frequency/closed
settings, calibration/identity, resources, PFS and bonds were not written.
Normal dim, QMI WOM and CP-awake AP retained sleep remain enabled unchanged;
Sleep In/raw-zero remain disabled, and shutdown now rejects before teardown.

UART received the exact candidate build, AP/CP reset sources 0/0, 240 MHz AP
clock, SD startup with the existing volume, contract 0x78f, and 32 AP retained
returns with CP transition/clock/mask/resource/fault deltas all zero. Bonded
encryption and the phone protocol session recovered. One initial BLE 0x23e
connection failure and a connection-parameter retry are retained. The readback's
commanded restart of the preceding `96b2e1aa` AP received AP=0x4f/CP=0 and closed
sleep admission; that precedes candidate flashing and is preserved separately.

The operator reports a long boot-splash delay followed by an apparent automatic
restart, then confirms display/dim, motion wake, touch/hold/release, both buttons
and companion operation all normal. UART captured one candidate boot banner and
no second boot/reset sequence or assertion/panic record. The reported restart is
not independently corroborated or explained; this is an unresolved startup issue
and blocks clean startup/release qualification. Do not silently replace that
report with the later successful recovery checks or label it a proven reset cause.
No additional reset or rollback was issued by the agent.

The bounded capture received 8,485 bytes and is stopped. Raw first-failure bytes,
operator reports, full CP readback, programmer logs and decoded/verified results
are in reference artifacts `standby-display-20261114/install/`; see
`flash-result.json` and `first-failure/`. Known-good `4545e34a` rollback hashes
and CRCs were rechecked and preserved. No shutdown/panel-policy experiment,
electrical measurement or overnight soak was run. Next: investigate the startup
splash/apparent restart using the preserved trace, without claiming clean startup.

## Standby/display audit — 14 November (pre-install preparation)

The [standby/display review](standby-display-review.md) records the source/circuit
audit and ranked options. Entry HEAD is `4176671a9`, newer than the handoff;
the latest recorded installation at audit entry was touch AP `96b2e1aa`,
not `4545e34a`.
Today's receive-only CH342 capture received 221 bytes in 30.08 seconds and is
stopped, but did not receive an exact running identity. Operator confirms USB
through FNB58 and supply-cycle recovery; battery/debug back-power remain unverified.

Prepared normal AP `b40a6cb1070c5440f74c1c758b2c0eff8a38290a` rejects unsupported
shutdown before teardown (`-ENOTSUP`) through a board-local implementation.
The prior path set a Pebble boot bit and reset; no supported vendor standby flag
contract exists, and VBUS bypasses the P19 battery latch. Battery-off implementation
and low-battery protection remain unqualified. Stock shared runtime is unchanged;
only board-conditioned build selection replaces the generic standby object.

Separately, CO5300 caches successfully applied raw brightness and Display On/Off,
retries failures, invalidates on reset/sleep and propagates reset GPIO errors.
Normal dim mapping/timeouts, refresh policy, synchronous transfer completion,
shared clocks, QMI WOM and CP-awake retained sleep are preserved. Sleep In and
raw-zero controls stay default off. This is less redundant work, not measured power.

Eleven focused checks pass, including actual-source ASan/UBSan fault tests and
32 modeled partial-redraw sleep/wake cycles with alternating orientation. Another
54 regression checks pass; three obsolete DMA extractors fail because they target
a retired function (preserved in the evidence). Normal build, image/map/CRC,
SRAM wake-gate, style and whitespace checks pass. Text +32 B, data/BSS unchanged;
kernel heap 28,960 B. Normal configuration and resources are byte-identical to
the preceding build. Resource SHA256 remains
`62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46`.
Raw SHA256: `d8502e8e8303fe2a3b96cbe5fc94b41f46be6dd81b4c4ec2e10064c115117f73`;
protected: `197bd6ba6534d313e2068901cba4b35073748bb695bcd1a07a4fdcf239e197e6`.

At this pre-install checkpoint no firmware had been flashed or power-off requested;
no new physical wake, storage,
bonds, DMA coexistence or electrical qualification is claimed. CP/readback,
frequency/closed settings, resources, PFS and bonds were not written. No overnight
soak ran. Frozen candidate, preceding image, independent standby/display patches,
first build/test failures and validation: reference-workspace
`artifacts/standby-display-20261114/`. Next: bounded normal AP-only qualification
with exact CP/readback and rollback checks, retaining dim; battery-only shutdown
and deeper panel sleep require their separate hardware gates.

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
