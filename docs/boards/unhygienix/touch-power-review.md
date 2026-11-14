# Touch active/idle review — 13 November 2026

## Recommendation and installed state

Keep normal raw touch enabled whenever the existing service has subscribers.
Keep reset-held disable when touch is globally off or all subscribers disappear.
Do not couple touch reset to display dimming or AP retained sleep. Navigation,
backlight wake and application subscribers may still require the first contact.
No new controller power/gesture command is justified by the available evidence.

The driver-local fault-backoff image
`96b2e1aabc0e5f1748c6bbdaa765efe168f1a01b` is now installed AP-only under the
operator's standing flash authorization. See [current status](current-status.md)
for exact deployment and validation. Programming succeeded in 32.6 s. The first
post-programming boot reported AP=0x4f/CP=0 and closed sleep admission; the trace
is preserved. One commanded same-image restart received AP/CP reset sources 0/0,
contract 0x78f, SD and 32 AP returns with all CP transition/fault deltas zero.
The operator confirms the companion connected normally. Encrypted phone-session
messages were not captured on UART; that recovery is operator-observed.
The operator reports normal taps, swipe/release, held contact, first touch after
ten seconds idle, motion wake and both buttons. This does not qualify forced
physical fault recovery, QMI/RTC bus concurrency or long-term behavior. No
current measurement or overnight soak was run. The earlier `4545e34a` baseline
is preserved as a known-good rollback.

## Readability cleanup — 13 November (unflashed)

Cleanup build `9780fbc065bfc133635e8c0bc58ec87f9b871f88` separates report decoding,
transfer recovery, polling policy and generation checks into driver-local helpers.
The read/address/ACK sequence uses one short-circuit result and derives the ACK
address from the same register bytes. Ordinary raw delivery now takes one
current-generation critical section instead of two; palm delivery retains the
second check because a listener can disable/re-enable touch before raw delivery.
No timer, retry/reset threshold, controller command, report interpretation,
subscriber lifecycle or bus policy changed. Shared code is unchanged.

All 47 targeted ASan/UBSan tests pass (leak detection disabled for the sandbox
tracing limitation), including a new actual-source case where the palm listener
disables touch and the following raw sample must be canceled. A separate seeded
10,000-step comparison of the installed and cleaned-up actual driver sources
produces identical I2C/reset activity, input reports and driver state through
report/IRQ/timer/queue/failure/lifecycle sequences. This is modeled equivalence,
not proof over every interleaving or a hardware latency benchmark.

Normal build, ARM IO-profile compilation, CRC/image/map/SRAM and style checks pass.
Linked text/data/BSS are unchanged at 1,638,051 / 5,064 / 241,776 B; kernel heap
remains 28,960 B. Normal configuration and resources are byte-identical to the
installed image. No speed, current or battery-life improvement was measured.

This is a separate source cleanup checkpoint; it has not been flashed. The
installed/physically checked image remains `96b2e1aabc0e5f1748c6bbdaa765efe168f1a01b`.
Cleanup-only diff, baseline sources, modeled traces, frozen build and exact
validation: reference-workspace `artifacts/touch-cleanup-20261113/`.

## Hardware and reference confidence

The retained 30 October live diagnostic identified address 0x5a, checkcode
0xcaca4e20, chip 0x9217, project 0x5734, resolution 466 x 466, controller firmware
0x01000001 and checksum 0x693275ee. This is prior fitted-device evidence; no new
identity probe or controller firmware update was performed this session.
Evidence: reference-workspace `artifacts/cst9217-investigation-20261030/`,
including `hardware-results.txt`, raw UART and source/hash provenance.

The [manufacturer board schematic](https://files.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75/T5-E1-Touch-AMOLED-1.75_schematic.pdf),
page 1 pin table, agrees with native GPIO20 SCL, GPIO21 SDA, GPIO42 TP_INT and
GPIO43 TP_RESET. The touch module uses 3V3; there is no new independent supply
control in this implementation. Preserve the qualified shared 100 kHz bus,
0x6b QMI and 0x51 RTC. Do not mistake driving reset low for proven rail removal
or a measured reset-state current.

Pinned TuyaOpen `e82840dc9b629cb77fa922fb04e3f820dc2e78b4` uses 0x5a,
D000 reports, 5/50 ms reset timing and 100 kHz. Its board reference names GPIO42
as reset, conflicting with the schematic/native implementation; do not copy that
pin definition. Its close operation does nothing; the native driver already
holds reset on disable. Native post-read D0 00 AB acknowledgement, palm latching,
coordinate mirroring and release/error handling are retained. ACK/palm have prior
fitted-device evidence; the reference's omission is not a reason to remove them.

The archived Hynitron CST9217 V1.0 datasheet (SHA256
`74cf16e6dc45fbbb75c06a79c9d285d6f34c3202c4ff5aef9e24dd53ef9ee3bc`),
pages 1, 4 and 6–7, describes active/monitor/sleep and standby gestures. Monitor
scans slowly and switches to active on touch. Sleep requires a wake command or
external reset. Its typical controller currents (1.3 mA active at 100 Hz,
12 uA monitor at 30 Hz, 2 uA sleep) are neither board measurements nor evidence
of the fitted firmware's actual idle state. It supplies no complete register
protocol for enabling/decoding touch-preserving monitor or gesture wake here.

Archived SensorLib declares low-power/LP-scan modes and D10F but does not
implement those setMode cases. Its sleep implementation enters debug mode and
sends D105, with reset/power-cycle wake requirements. The current upstream
[implementation](https://raw.githubusercontent.com/lewisxhe/SensorLib/master/src/touch/TouchDrvCST92xx.cpp)
was also checked. CST328/CST816 commands or gesture codes are not evidence for
this CST9217 firmware. No speculative mode write was added.

## Lifecycle, scheduling and AP sleep audit

- Init allocates one retry timer, configures reset, probes with at most three
  reads and reset retries, then holds reset. Detection failure logs but does not
  fail init; later enable/retry remains the existing recovery mechanism.
- Generic touch class refcounts users. The service powers touch iff globally
  enabled and subscriber count is nonzero. Navigation takes a permanent system
  hold; backlight touch wake and raw/gesture listeners also subscribe. Explicit
  touch-off releases the active service contact/reset state. Losing the last
  subscriber disables the driver. Display dimming and AP WFI do not disable it.
- Enable resets for 5/50 ms, arms falling-edge INT and the one-second regular
  watchdog, and queues one read even if INT was already low. Disable masks IRQ,
  increments generation, stops retry/watchdog work, clears contact/palm/backoff
  state and holds reset. Repeated calls are idempotent. Stale-generation work
  cannot read or reset a newly enabled controller.
- ISR/timers only queue coalesced droppable KernelBG work. The watchdog reads
  the GPIO pad; healthy idle with INT high does no periodic I2C/reset. It remains
  registered at one second to recover queue drops/missed asserted IRQs; removing
  it would lose that recovery and does not necessarily remove the shared timer.
- Active or palm-suppressed contact, including partial multi-finger release and
  ACK echo while held, retains 20 ms fallback. A valid release ends contact;
  invalid framing does not synthesize liftoff. Transfer/ACK failure releases raw
  contact, retries at 80 ms and resets after three failures, then waits 1 s.
- Driver mutex serializes reset and its report transactions; input listeners run
  after unlocking to avoid service-lock inversion. The bus mutex serializes each
  I2C transaction among clients. No shared bus lock spans the reset sleeps.
- Touch has no permanent AP sleep veto. I2C holds its existing sleep owner while
  transfers/drains are active; uncertain STOP retains the blocker. NewTimer work
  sets deadlines and queued work prevents idle. GPIO is an AP external wake
  source. Sleep owner, GPIO wake routing, CP exclusion checks and SRAM gate are
  unchanged; fewer transfers do not prove greater electrical sleep residency.

## Approaches and tradeoffs

| Approach | Expected benefit | Behavior / decision |
| --- | --- | --- |
| Current IRQ-driven enabled state | Healthy idle already has zero report traffic | Best supported default; actual controller monitor residency is unknown |
| Existing reset-held disable | Stops touch work when service does not need it; electrical effect unmeasured | Already integrated with global-off/no subscribers; preserve |
| Reset on dim/idle | May reduce controller consumption; unmeasured | Loses touch wake and the first gesture; requires button/motion wake plus 55 ms reset settling and scheduling; not implemented |
| Sleep D105 | Possible lower controller current | Touch wake unsupported by evidence; no advantage established over existing reset-held disable; not implemented |
| Forced monitor / gesture standby | Potential controller saving with touch wake | Requires firmware-specific command, status/readback, gesture decode, IRQ and first-contact qualification; deferred |
| Fault polling/retry backoff | Bounds empty-report bus/worker churn and repeated failure handling | Installed AP-only with bounded checks; no healthy-idle electrical saving claimed |

Candidate behavior: after successful reads with no active/palm contact and INT
still low, fallback delays are 20, 40, 80, 160, 320, 640, then capped at 1,000 ms.
A real falling edge still queues an immediate read. Contact, deasserted INT,
transfer failure and enable/disable reset the idle backoff. There is no automatic
reset for an otherwise readable stuck-low INT and no synthesized contact.
At the cap, empty stuck-low polling is nominally 1/s rather than 50/s; that is
software activity, not measured current or total interrupt reduction.

**User-visible fault tradeoff:** if INT is stuck low or a falling edge is lost
while backed off, a new contact may wait up to 1 s plus scheduling/transfer time;
a short tap between probes can be missed. Healthy falling edges and known held
contacts retain their previous path. Failed transfers now enforce the existing
80 ms / post-reset 1 s retry interval: IRQ storms and already-queued callbacks
cannot force early I2C/reset attempts. Touches during bus-failure recovery may
be delayed/missed. Buttons and motion paths do not change. The operator authorized the reviewed policy flash; forced physical fault qualification remains open.

## Validation and remaining qualification

Exact logs, frozen candidate/rollback and manifests are in reference-workspace
`artifacts/touch-power-20261113/`. The source diff contains only the CST92xx
T5AI driver, its actual-source tests and board documentation; no pure-cleanup
change is mixed into the power policy.

Host validation includes 13 full-driver cases, four newly added, covering native
contact/coordinate sequences, release without edge, long idle then first touch,
held/palm/multi-contact suppression, invalid/ACK echo, stuck-low backoff and cap,
queue rejection, failed write/read/ACK, IRQs during a failed transfer and storms,
three-failure reset, stale work and ten disable/enable cycles. The harness
executes the actual production C with modeled IRQ/timer/I2C/input boundaries;
it does not execute the physical controller or prove scheduler latency.

The broader sanitized run passes 46 tests across touch, sensor startup, I2C,
QMI WOM/FIFO, retained sleep, sleep guard and first-failure I2C traces. ASan and
UBSan are enabled with leak detection disabled: the first five related startup/
I2C tests failed because LeakSanitizer cannot run under sandbox tracing; that
failed attempt is retained. Independent QMI/I2C harnesses do not establish real
shared-bus concurrency with the RTC. Formatting/lint and whitespace checks are
recorded separately. Normal release candidate build ID is
`96b2e1aabc0e5f1748c6bbdaa765efe168f1a01b`. Linked text is 1,638,051 B
(+112 B versus installed baseline); data 5,064 B and BSS 241,776 B are unchanged.
The linked memory assertions and image/map/protected CRC checks pass. Normal
configuration and resources are byte-identical to the installed baseline;
QMI WOM, AP-only retained sleep and dim-display flags are preserved. The actual
WFI entry and PC literals remain in SRAM with its only call to the SRAM reset
helper; 46 host tests include CP transition rejection/guard invariants. These
static and modeled checks are separate from the bounded candidate hardware
evidence recorded above.

Candidate raw SHA256:
`aee7bd2d0f712a8ebc2848a9b3044c69f4367092462cabcf85fa6b57e0d9736f`;
protected SHA256:
`b19bafc8cca1b87ce77862ec8a31d04f619a164669a396f1d20f1aaf3ad111b6`.
The `rollback/` images are copies of the known-good installed QMI cleanup
(raw `954a9820690b617f2c0da9b8234054dde111af1afa395fdcba0329d12b79aa97`,
protected `c99c9b10e503b676303409a4a7fbe32ee7ffacc2327ef7363b599e948c992171`).
No source commit was made; the reviewed candidate was subsequently flashed AP-only.

Before any deployment, freeze/hash the normal AP candidate and rollback; keep
the current CP firmware, frequency/closed settings, calibration, BT identity,
resources, PFS and bonds. The operator authorized this AP-only flash; preserve
the exact deployment record. No overnight soak is authorized by this handoff.

For subsequent hardware validation: verify host UART bytes (enumeration alone
is insufficient),
preserve the first failed CH342/programming/boot attempt, request reconnect if
protocol errors stop reception, and receive exact build/reset/contract plus
retained returns and phone recovery. Physically exercise taps/swipes/release,
first touch after idle, held/palm contacts, repeated touch-off/on, button/motion
wake and QMI/RTC use under shared-bus load. Fault injection must check INT low,
queue/I2C/ACK failures and recovery without a phantom contact/reset loop.
Measure matched fixed-supply dim-Nadir idle and completed-workload energy before
claiming savings; distinguish USB-input and battery-side measurements.
