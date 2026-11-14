# QSPI/DMA diagnostics and acquisition safety — 14 November 2026

This is the first package from [the power scope](qspi-dma-power-scope.md), built
on production commit `21b1cabcd`. It adds an acquisition correction and an
independent, default-off diagnostic option. No clock-gating policy, panel
brightness, refresh cadence, sleep admission or CP firmware has changed. Normal `faae34b9` is restored after the bounded mixed-I/O test;
with failed predecessor traces preserved below. See [current status](current-status.md).

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

## Acquisition correction

Native `drivers/spi/t5ai.c` now checks the QSPI CP PM helper before writing SYS
memory/source controls or accessing the QSPI controller. A nonzero result returns
`-EIO`. The helper first requests AHBP QSPI power, then QSPI0 clock; power failure
short-circuits the clock request. Clock failure can leave an acquired power vote.
No retry, release, controller access or uncertain shared-resource rollback follows
that failure. The existing bounded mailbox timeout/poison handling is retained.
This is fail-closed initialization, not an implemented recovery or power saving.

Pinctrl, DMA-device and DMA-configuration failures retain their original error
returns and do not enable the QSPI IRQ or configure the QSPI controller. The
optional diagnostics distinguish these stages. A successfully acquired power/
clock vote remains held if a later stage fails; exclusive ownership has not been
proved, so undoing it would be unsafe. Existing caller boot-failure handling is
unchanged. QSPI reset-release still preserves inherited bypass bit 1.

## Diagnostic option and storage

`CONFIG_T5AI_QSPI_DMA_PROFILE=y` depends on Unhygienix, native CO5300 and logging.
It is off in the normal board configuration. When off, event/timing hooks compile
away and no profile object is linked. When on, `g_t5ai_qspi_dma_profile` lives in
AP-only `.t5ai_board_cache` PSRAM. Explicit board activation zeroes this NOLOAD
storage only after successful PSRAM power acquisition and both existing range
validations. Events/reports before activation never access it. Compact SRAM
bootstrap storage retains four first global-clock words/timestamps and one first
failure snapshot/status. Other early events increment `skipped_before_ready`;
activation imports the selected evidence and starts full counters. It neither
moves board power-up earlier nor lazily powers PSRAM from IRQ context. It contains fixed-size counters and eight snapshots, with no
allocation, timer, polling task or per-transfer logging.

Reports use `T5QD` debug records once after rendering the boot splash (phase 0)
and once at the existing 32 AP-retained-return milestone (phase 1). The latter
only happens if normal sleep guards admit enough returns. Reports run outside
the profile IRQ lock, copy bounded rows and contain at most 53 rows per phase.
IRQ event hooks update counters and take only the first selected snapshots.
Instrumentation and these report bursts perturb timing/power; do not use their
UART/report windows as an idle-power comparison. UART capture must use the
matching image's log-hash dictionary.

Each event has cumulative 32-bit `count`, `amount` and `ticks`. Decode event IDs
using the exact built `include/pbl/soc/t5ai/qspi_dma_profile.h`, not a historical
numeric list. `amount` means command length including its four framing bytes,
submitted DMA bytes (including refused attempts in the separate refusal event),
channel-start buffer length, raw status/error value or zero, depending on event.
Error values use unsigned 32-bit representation. Channel bytes are submitted
capacity; cyclic buffer length is counted once per start, not bytes actually
transferred on each period. Never sum these fields as completed workload bytes.

Times use low 32 bits of the configured 32,000 Hz AON RTC with unsigned subtraction. Command
and final-drain events accumulate their polling duration, QSPI END accumulates
bus-enable-to-disable duration, and each AP channel accumulates start-to-stop
owner duration after activation. `partial_owner_mask` identifies channels already
active at activation: their observed interval begins there, with unknown earlier
duration/bytes. Unmatched QSPI END events do not fabricate an interval from tick
zero. Cyclic half/finish callbacks keep ownership active. Repeated stop
does not double-count owner duration. Channel reports separate completed-owner
`closed` ticks and current-owner `open` ticks; overlapping channels must not be
summed as controller residency. Counters wrap modulo 2^32; intervals spanning an
entire RTC wrap are not supported by this bounded experiment.

Snapshot slots:

| Slot | Captured state |
| --- | --- |
| 0 / 1 | First QSPI inherited / ready global word (SRAM bootstrap) |
| 2 / 3 | First DMA inherited / ready global word (SRAM bootstrap) |
| 4 | First initialization, command, submission, DMA or drain failure; never overwritten |
| 5 / 6 | First QSPI bus-enable / AP DMA-start state |
| 7 | Latest explicit report state; overwritten by phase 1 |

“Inherited” means after existing power/source/memory setup and settle, before
native controller initialization. It is not the raw bootloader SYS state.
Snapshots contain SYS offsets 0x30/0x38/0x3c, QSPI offsets 0x08/0x60/0x70,
DMA global 0x08, read-only enable/finish/error masks for AP channels 0–3,
and AP software active/cyclic masks. `valid` bit 0 permits QSPI mode/wire fields,
bit 1 AP DMA channel fields, bit 2 QSPI global, bit 3 DMA global. Global validity
can coexist with full validity. Bootstrap slots retain only bit 2 or 3; zero
unrecorded fields are unknown. Before local controller setup only global words
are read, after the existing power/settle sequence. Full fields require completed
local setup (including DMA AP reset/security/privilege grants). A failed QSPI PM
acquisition leaves all QSPI snapshot reads disabled.

AP DMA status observations are read-only; CP channel registers are not accessed.
No CP IRQ acknowledgement, configuration, stop or reset occurs. Read-only access
alone does not establish permission to read an unowned or reset-held channel.
AP IRQ locking does not freeze hardware. A snapshot is neither a coherent
hardware transaction nor proof of
inactivity, exclusive ownership, automatic clock gating or electrical savings.
The diagnostic access flags assume the current always-held domain/source policy;
any future source/domain gate must revise this access protocol first.

## Ownership inventory and limits

| Owner | DMA channel / completion boundary |
| --- | --- |
| CO5300 QSPI | 0; DMA finish then bounded QSPI wire drain, SPI disable and callback |
| Microphone | 1; cyclic periods remain active until terminate/error |
| SD TX / RX | 2 / 3; DMA completion plus separate SD/card-ready ownership |
| CP or unproven | 4–11; no diagnostic channel-register reads; ownership/access remain unproven |

QSPI global 0x46040008 bit 1 is preserved by current init. GDMA global
0x45020008 bit 1 remains forced on. Neither SYS source nor domain/memory votes
are released. AP-last-owner does not establish permission to clear a shared
controller bypass or power it down.

The installed CP97 package/readback identity is preserved. Its manifest records
ELF SHA256 `33e5c03cb43a9b7a961d98d509e83b50d1dc37dfd37a71d3e90051e46ab366a2`.
No matching ELF was found among available `app.elf`/CP-named ELF files in the
reference artifacts/builds, reference `.vendor` build, `/tmp` or production
vendor tree. Exact candidate paths and hashes are recorded in
`ownership-inventory.json`. Vendor source includes potential DMA consumers and
PM writers, but their presence does not establish linkage or runtime ownership
in CP97. The linked-use inventory is incomplete. Shared DMA gating remains
blocked until matching linked evidence and safe all-channel semantics are proved.
Do not substitute an older CP ELF or rebuild/change CP to bypass this limit.

## Validation and next hardware step

Actual-source ASan/UBSan tests cover CP power/clock short-circuiting, acquisition
and later init failures with diagnostics on/off, inherited bypass preservation,
command/quad segmentation, pending and late callbacks, submission refusal, DMA
error/timeout and final-drain timeout. Profile tests cover NOLOAD initialization,
first-failure retention, bounded snapshots over 10,000 repeated events, RTC wrap,
cyclic lifetime and bounded reports. Concurrent-channel tests keep three AP owners
and a modeled CP channel 7 active while stopping display channel 0. Snapshots
preserve registers/W1C status and do not read CP channel registers. Access-order
tests reject reads before reset/AP grants, and PROT_NONE-backed PSRAM tests reject
any premature full-buffer access. Existing display, touch, SD,
mailbox and retained-sleep host checks also pass: 54 distinct tests in total
(the initial 52-test suite plus two added failure-path checks). These are modeled hardware
endpoints executing production C, not physical concurrency or panel qualification.

Original, superseded host-only build checkpoint: both release builds passed protected CRC, image/map/layout, syscall/log-hash,
configuration/resource equality and SRAM retained-entry audits. Normal build
`2b89603e5966db8ffdf20e0cc6431e3f2d79edf3` adds 16 text bytes versus installed
`b40a6cb1`, with data/BSS unchanged. Diagnostic build
`52577e7bcd27d8b927865011101150206210e142` adds 1,384 text bytes versus normal.
Its 816-byte object occupies 832 extra PSRAM cache bytes with alignment;
`.t5ai_board_cache` totals 59,680 bytes and leaves 3,776 bytes in the aperture.
Both retain a 28,960-byte kernel heap (288 bytes above the 28 KiB minimum).
Framebuffer/DMA staging and the SRAM WFI entry/call limits are unchanged.
Resources remain SHA256
`62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46`.
The two effective configs differ only in `T5AI_QSPI_DMA_PROFILE`.

Evidence is in reference `artifacts/qspi-dma-diagnostics-20261114/`, including
separate acquisition/diagnostic patches, pre-change source, tests, both frozen
builds, exact configuration, log dictionaries, hashes, image/map and SRAM audits.
The first sandbox run encountered LeakSanitizer's ptrace limitation; final runs
use `ASAN_OPTIONS=detect_leaks=0` with address/undefined checks active. No heap
allocation is introduced. Earlier obsolete DMA extractor failures and panel/
startup first-failure evidence remain preserved in the preceding artifacts.

Next: complete bounded mixed DMA, pending-transfer cancellation, repeated explicit
disable/enable, partial/full redraw and orientation qualification while retaining
WOM and input checks. The normal candidate is available to remove instrumentation
before matched electrical measurement. Do not enable another QSPI bypass policy:
this hardware startup already has bypass clear. DMA work still requires matching
CP97 linked-use/access evidence and safe all-channel semantics. Do not write CP
or gate domains/memory to bypass that requirement. No shutdown or overnight soak
belongs to this diagnostic validation.

Use matching, settled, non-report windows for later electrical comparisons and
measure energy per completed redraw/wake workload as well as idle input power.
Account for USB/debug back-power. Panel power has not been isolated and no
whole-device or panel electrical saving is claimed by this increment.


## Separate DMA inheritance experiment

See [the default-off clock-policy experiment](dma-clock-policy.md). It never
writes shared global control and rejects held reset; it does not authorize a
forced bypass clear or a domain gate. Existing failures remain preserved and
the operator has deferred startup/Bluetooth investigation for this work.
