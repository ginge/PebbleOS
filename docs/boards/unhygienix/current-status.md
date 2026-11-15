# Unhygienix current status — 14 November 2026

The latest installation checkpoint below is the current installed state. Dated entries preserve
the state and evidence at their recorded checkpoint.

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
