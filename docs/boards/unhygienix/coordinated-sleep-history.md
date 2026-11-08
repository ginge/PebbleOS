# CP sleep investigation history

Historical evidence only. CP deep sleep is deferred; production uses
[CP-awake AP-only sleep](ap-only-sleep.md).

# Coordinated retained sleep — 31 October 2026

### Safe refusals isolated; direct CP timing trial — 2026-11-02 06:14 UTC

AP110 and AP112 returned safely without CP WFI or analog-timer arming. Their
HAL reject was permit (0x10), with no CP fault or reset. AP110's one-AP-return
quota then reported a zero-CP-entry qualification failure; this was not a failed
hardware wake. AP111 made zero requests because its worker re-applied the idle
publication reserve. All first records are frozen under first-failure/v110–v112.
The lead explanation was inferred from AP timing and the nonfault CP preparation
path; those revisions did not expose exact CP preparation lead.

AP112 measured idle maximum 1443 ticks = 45.09375 ms, worker maximum 1440 =
45 ms, one worker refusal, request maximum 21 = 0.65625 ms, AP suspend 17 =
0.53125 ms and earliest-alarm headroom 1016 = 31.75 ms. ACK/drain completion
consumed more of the interval than the request itself. Zero CP clock cycles,
zero late deadline/fault/reset. One commanded Honor off/on control, verified in
UI and returned on, preceded encrypted protocol recovery at 06:01:33 and the
06:04 slot. AP111/AP112 meter recordings each closed 8972 samples/exit0, but
contained no sleep and no qualified savings comparison. Visible content remains
unconfirmed. No display/animation schedule has been changed.

AP admission now spends publication reserve once, separately from the worker's
remaining-time check. Bounded safe refusals are allowed, with immediate stop
after the first returned CP WFI. Host tests cover queued delay, request overrun,
caller deadline/wrap and the stop rule. The caller's 8 ms wake margin, final
entry lead, IRQ/owner/flash/retention checks and independent watchdog guard remain.

CP69 ABI43 adds read-only exact analog-prepare lead and rejection reason. The
explicit diagnostic uses a 1 ms request reserve and 1 ms lead between the nominal
32 ms fallback and private alarm, with refusal on actual timing overruns. The
24 ms timer-setup-to-entry budget is unchanged. 39 focused host methods pass;
pinned build, package replay and linked audits pass. Full physical readback
matches all 1114112 bytes and active CRCs (partition SHA256
ec7ed8ece27ba00165313af32bbef830aa258c5ecdfadb29ba485cbe8d20095e).
CP69 is installed with the exact normal AP; AP113 (da9bad92) is flashing for
06:18:05–25, at most64 safe AP returns/128 requests and stop on first CP return.
CP68/67 source/packages remain preserved. UART40 has actual received data;
phone34 contains actual Bluetooth events. Captures are bounded, not overnight.

Global retained sleep, physical wake and counter continuity remain unqualified.
Next: verify the expected AP startup, run the bounded exact-lead diagnostic,
preserve its first result, and restore normal AP on any fault. Active CPU is
240 MHz, normal retained/tickless defaults off, display fixed, overnight deferred.


### Entry/wake narrowed; scheduler limiter measured — 2026-11-02 05:20 UTC

Global retained sleep is still unqualified. CP65/AP104 reset at 03:45:10;
committed reset snapshot 0x4f000933 preserves CP stage 19 (CP_WFI) and no
stage 20 (CP_Woke). This brackets the failure between the pre-mask breadcrumb
and immediate post-WFI return; it does not prove WFI executed. CP readback
matched. First-failure/v104 is preserved. Exact normal AP was restored and
recovered encrypted protocol naturally at 03:51:53, before the later phone cycle.

CP66 ABI41 adds the explicitly selected 32 ms analog fallback. Its full physical
partition readback matched, and AP106 received state 0x70: timer disabled,
period selector 7, clock selector 0, latch clear. The 04:33:05–25 slot made zero
requests/preparations/entries: its 80 ms scheduler admission window never
occurred. This was a safe refusal, not an analog wake failure. First-failure/v106
is frozen. The meter closed cleanly with 8976 ordinary-idle samples; visible
content was unconfirmed and no savings comparison is valid. AP105 never flashed.

AP107 (f7eb8834), on unchanged CP66, measured 38468 scheduler observations in
05:07:05–25: maximum 1459 AON ticks = 45.59375 ms. NewTimer was the nearest
timed thread at 47 kernel ticks; no kernel timer was pending. Zero CP arms,
entries, faults or resets. Encrypted protocol recovered before the slot.
First-failure/v107 is frozen; its phone log ends 04:59:33 and does not cover the
profile window. This identifies the admission limiter without changing deadlines.

Implemented and host-tested, not hardware exercised: CP67 adds an analog
resource-preparation deadline check after delayed AP publication. A short or
expired lead refuses before any analog register write and rolls back normally.
72 modeled CP cases and 37 AP alarm cases pass, including exact boundary, wrap
and unchanged other-owner fields. Isolated build, package replay and mandatory
linked/entry audits pass. Wire ABI remains 41; current physical CP remains 66.
CP67 raw SHA256 15411c319ed624122e8473d7bc4fe6c8f877ba16dbaa5fa47d67601b2049bdab
is preserved in packaged-v67-final and is not installed.

AP108 (d2f25bee), on unchanged CP66, completed its 05:23:05–25 read-only
profile with 37704 observations and maximum 1481 AON ticks = 46.28125 ms.
NewTimer was due in 48 ticks; the nearest registered callback has a 48 ms
animation-service interval, confirmed against the exact ELF's callback/literals.
The QMI8658 backup callback was due later (100 ms period). Zero untracked
registrations, CP arms, entries, faults or resets. Encrypted protocol recovered
at 05:21:55 before the slot. First-failure/v108 and callback resolution are frozen.
The T5AI-local wrappers preserve shared timer source and real schedules. A first
oversized buffer was withheld by the heap guard; compact records now migrate to
spare AP-owned PSRAM only after validation. Heap/PSRAM bounds, linked wrapper
calls, API failures, boot migration and timestamp-wrap tests pass.

AP109 (03ccd47d) completed 16 AP/15 CP WFI returns and 16 resource/clock
cycles without reset, fault, abort or late caller deadline. Hardware mask and PMU
residency counters remain zero: no global sleep. Request maximum 20 AON ticks =
0.625 ms; suspend maximum 929 ticks = 29.03125 ms includes WFI, not pure restore
time. Latest CP preparation 4 ticks = 0.125 ms. First-failure/v109 is frozen.
The exact normal AP was restored before the CP contract changed.

CP68 ABI42 is installed; full 1114112-byte physical readback matches the frozen
package and active CRCs (partition SHA256
40f6a269b2bca2c74b9525a36d61c275eb8867a5ab555e669731f1d7aa370678).
The explicit analog diagnostic now admits a 44 ms caller window: 32 ms nominal
fallback, 2 ms CP deadline lead, 2 ms request reserve and unchanged 8 ms AP wake
margin. Overrun, publication delay, IRQ, ownership, flash and retention checks
still refuse entry; the existing 24 ms timer-setup-to-entry budget is unchanged.
38 focused host methods pass, including boundary/wrap and actual ARM leaf tests.
Pinned build, package replay and linked audits pass. CP67 never flashed.

AP110 (ece15fe9) is running for 05:46:05–25 with at most one AP return/four
requests. Expected build/storage startup and disabled analog state are received;
startup encrypted protocol is not yet received at 05:43:52. Power recording
remains conditional/unarmed. UART39 received a fresh valid reply before CP flash;
phone33 has actual Bluetooth data. Captures are bounded, not an overnight soak.
Visible display state is unconfirmed. Next: capture the single analog-assisted
attempt, preserve its first result, restore normal AP if needed, and check actual
CP return, global residency and counter continuity separately. Active CPU remains
240 MHz; normal retained/tickless defaults off, display cadence/settings unchanged,
overnight deferred. No completed global-sleep workload or new saving is qualified.

Correction to the AP103 account below: explicit PFS new=0 was not received in
that frozen trace; SD readiness, resources/apps and later encrypted protocol
were received. That attribution control is not storage qualification.

### Durable failure attribution; global sleep still unqualified — 2026-11-02 03:40 UTC

CP64/AP102's audited five-instruction mask-to-WFI leaf still reset at 03:03:10.
Full CP readback matched; no startup encrypted session was received and the meter
skipped unarmed. This was an entry/wake diagnostic, not Bluetooth or energy
qualification. First-failure/v102 is frozen; its clarification corrects one
reference to the earlier Wi-Fi-on trial (AP101, never-flashed AP100).
Exact normal AP was restored afterward with existing PFS received.

Source review found native AP reset-reason writes omitted the pinned AP/CP
R0/R25 two-key sequence. The T5AI-local normal writer and SRAM reset helper now
include both named keys and a barrier, preserving other owners' bits. Nine reset
and sleep-guard methods pass. Guard-only AP103 (56c8fdb0), with CP kept awake,
performed its planned watchdog reset and recovered at 03:19:08 with boot AP
reason 0x4f. This is one positive hardware attribution control, not global sleep.
Its SRAM guard/reset closure and literal dependencies were inspected, and the
result is frozen under first-failure/v103. Existing PFS reopened; exact normal AP
was restored before the next CP change. General cold-boot/reconnect regression of
the updated driver remains pending. R7a snapshots alone remained uninformative.

ABI40 CP65 adds an explicitly selected, default-off persistent-stage operation.
It borrows only the existing CP reset-reason field while native sleep owns the
lease, stops recording before AP resumes, and restores the original CP field on
safe cancellation using the latest other-owner bits. Unexpected CP cause changes
are preserved and fault the contract. Its traced ITCM leaf records a WFI return
before stack/SRAM use and masks its argument to the CP field. All previous
ownership, IRQ, deadline, raw GPIO, flash, timer, heartbeat and retention guards
remain. The same full native/vendor-LPO policy is used; no relaxed admission.
33 focused methods pass, including 64 modeled CP and 37 AP alarm cases. The actual
assembled trace leaf also passes an ARM instruction test with stack, flash and
PSRAM unmapped; this emulates a return, not physical sleep. Both package replay
and mandatory linked closure/entry audits pass. The failed Wi-Fi aggregation SRAM
relocation was removed from production source; frozen v62 artifacts remain.

CP65 raw SHA256 d37fab8c9e47ab4fd97102c5c4a380a9530a58f1c7f4839fc49187fdab3bf1b8
is installed with the exact normal AP; full physical readback is in progress.
AP104 (8dfa57f5) is built for the 03:45:05–25 bounded durable-stage trial, waiting
for identity checks. UART35 actual 746-byte reception was verified at 03:32:49;
phone31 is separate and all captures are bounded. No overnight monitor is claimed.
Active CPU remains 240 MHz, normal retained/tickless defaults off, display fixed,
overnight soak deferred. No new usable energy saving or completed global-sleep
workload is qualified. Next: readback, traced trial, preserve the first result,
then use the durable stage to isolate entry versus restoration before changing
more power policy. Visible display confirmation remains necessary for energy
comparisons and later functional qualification.

### Masked architecture-ready control still resets — 2026-11-02 01:33 UTC

AP95 / byte-verified CP58 reset01:25:11. Before failure its complete snapshot had
AP halted0x7422, CP SCR0xC, cache0x85, memory shutdown0, RTCcontrol0x4C, sleep
condition0x240882c0, wake0xdc0002b0, power0x691113bb, HF0x6c0fa76c. One prior
preparation restored safely; no CP_Woke/flash-restored marker after the failing
one. Both architectural checks are insufficient by themselves. Boot PMU snapshot
again valid1/wake0/reset3/power0/config232; ROM reset may clear transition evidence.
First-failure/v95 is preserved; diagnostic meter closed8936 samples, failed reset
workload only. Exact normal AP restored, PFS new0 plus encrypted protocol01:27:48.

AP96 55f18cd7 full software-HF-off candidate is installed for01:35:05–25 on the
same CP58, with both-core readiness, raw GPIO, IRQ/deadline, flash ownership and
retention guards. Expected build and existing PFS received01:29:50; encrypted
protocol not received at last review01:32:38. Meter conditional, not receiving yet;
no fixed-display savings claim. All19 focused methods pass against current source.
AP97's complete native condition (CPU0 WFI + AP WFI + flash idle, bus idle required)
is rebuilding for01:45:05–25 after the concrete architecture/flash fixes. Unflashed;
no new policy bypass, CPU2 remains verified reset/powered down. This separates the
complete contract from the vendor global-only diagnostic condition. Global sleep
and wake remain unproved. Defaults off, display unchanged, overnight deferred.


### Both-core architecture preparation validated — 2026-11-02 01:22 UTC

Source review found AP published its WFI permission before setting SLEEPDEEP.
AP now sets/read-checks the bit first, restoring SCR/IRQ routes on refusal without
publishing. Two ownership variants plus refused-readback cases pass host tests.
AP93 was withheld/unflashed. Unmasked AP94 76242278 completed53 AP/41 CP WFI
returns,1 safe AP abort,0 fault/reset; the20-second slot expired before64 entries.
AP reports SCR before0/prepared4, rejected0.53 external complete cleanup sequences
and41 CP_Woke/FlashControllerRestored pairs arrived. CP snapshots remain SCR0xC,
cache0x85, memory shutdown0, RTCcontrol0x4C. PMU counters0/mask0: no global sleep.
Positive evidence is frozen under first-failure/v94, with a count clarification
supplement. Meter skipped unarmed; pre-trial encrypted protocol was not received.

AP95 6659f8f2 passed SRAM audit and is installed on unchanged byte-verified CP58.
It differs from AP94 only hardware mask and UTC slot; global-only condition,
software HF and CP-owned AP wake policy remain. Boot/PFS new0 are received;
bonded encrypted protocol recovered01:21:53 before01:25:05–25 trial. Conditional
meter armed01:24:25, not yet receiving. Actual display unconfirmed, so diagnostic
only, no matched energy claim. AP96 full software-HF-off follow-up is built for
01:35 but unflashed; only considered after reviewing this result.

Delayed UART review corrected the phone interpretation: AP92 naturally recovered
an encrypted protocol session01:06:54 BEFORE companion restart, which commanded
its own disconnect01:07:39/recovery01:07:45. This does not prove a GATT stall.
The later live phone screen also showed Connected. Keep these commanded actions
and late recovery separate from pre-trial qualifications.
UART30/phone26 closed. Fresh UART31 and phone27 received data, bounded to about
01:52/01:53. Normal defaults off, active240MHz, display resources unchanged,
overnight deferred. No newly qualified energy saving or global sleep.


### Architecture control passes — 2026-11-02 01:07 UTC

CP58 ABI37 full physical readback matches package bytes/CRCs; partition SHA256
6fb3d4774ec7fd47be8deb559828359ab4b954e8901a72c28af7c40d7e8b826c.
It sets/read-checks SLEEPDEEP before hardware masking and restores exact prior
SCR on every abort/return. Refused readback rejects entry. It also captures PMU
wake/reset/power/wake configuration once before vendor reset_reason_init consumes
its snapshot; AP's read-only startup report confirms valid=1. Initial programmed
warm boot read wake0/reset3/power0/config232. ROM may already have cleared earlier
state; do not treat zeros as proof no PMU transition occurred.

Unmasked AP92 139d70e6 completed64 AP/43 CP WFI returns,2 safe AP aborts, no late
wake/fault/reset.63 complete prep/cleanup sequences and43 external CP_Woke plus
FlashControllerRestored markers received. All63 admission snapshots have SCR0xC,
cache-retention config0x85, memory shutdown0, RTCcontrol0x4C and AP halt status0x7422.
PMU fresh wake/ambiguity/clear-held counters remain0, mask0: no global-sleep proof.
First-failure/v92 stores the positive control. Startup encrypted session absent,
meter unarmed/skipped; no Bluetooth or energy qualification. A documented phone
companion restart after the frozen control is being used to diagnose its stalled
GATT startup, separately from sleep results.

AP93 769345d3 is built/SRAM-audited, not flashed, for01:13:05–25. It changes only
hardware mask and UTC slot relative to AP92, retaining global-only condition,
software HF and CP-mailbox AP wake. Mandatory/expanded ITCM audits, package replay
and19 focused host methods (59 CP/36 AP cases) pass. AP91/CP57 mailbox-only wake
previously reset00:51:11 despite AP halted; wake route alone was insufficient.
Normal firmware/PFS restored afterward; all first failures preserved. CP58's
architecture/memory/RTC snapshots replace further blind policy changes.
Normal defaults off, active240MHz, display unchanged; screen unconfirmed, overnight deferred.


### CP-owned AP wake comparison flashing — 2026-11-02 00:48 UTC

CP57 ABI36 is installed; full physical readback SHA256
`59392470e6f03519848199dd3e999c2c9aee8603cbfc4f0831ee2e533e7f92b7`
matches active raw/protected bytes and CRCs. Normal AP reopened existing PFS and
recovered encrypted protocol00:48:10 after readback restart. Mandatory and expanded
ITCM audits, package replay,56 CP/35 AP modeled cases and11 native/coordinator
methods pass. AP91 `917f354f…` passed SRAM audit; AP-only flash underway for
00:51:05–25. It uses CP mailbox-only AP wake plus CP57's raw GPIO pending guard.
AP90 remains unflashed. GPIO wake/latency and complete qualification are pending;
no event is acknowledged by CP or hidden solely by AP route masking.

UART29 closed; fresh UART30 received the read-only help reply and subsequent
boot/PFS/radio logs. It is bounded to about01:26; phone26 to about01:12. Conditional
diagnostic meter scheduled00:50:25, not receiving yet. Display resources unchanged;
current visible state unconfirmed, so no matched energy claim. Normal defaults
remain off, active240MHz, overnight deferred; no global-sleep qualification.


### Pre-mask state captured; CP-owned AP wake path — 2026-11-02 00:44 UTC

AP89 / byte-verified CP56 reset00:39:12. Full pre-mask snapshots now survive in
first-failure/v89. First preparation returned safely; second snapshot shows sleep
condition0x240c82c0, wake0xdc0002b0, PMU parameter0x691113bb, CPU status0x7422
(AP halt bit set), HF0x6c0fa76c. No CP_Woke/flash-restored marker after the second
snapshot. These values prove the low-clock preparation reached the final helper,
not that global sleep entered or why wake failed. Original verdict misidentified
0x00800000 as RTC: it is GPIO bit23, sampled during restoration, not proof of an
early wake source. An immutable-evidence interpretation supplement records this.
Meter closed8976 samples, diagnostic reset workload only. Normal AP restored;
expected ID/PFS new0 received00:41:56 (review pending for full recovery).

Source review found native AP permits direct RTC/GPIO wake while pinned vendor
AP waits for a restored CP mailbox. A bounded default-off CP_WAKE_ONLY control
now temporarily routes only mailbox to AP, retaining original GPIO configuration
and restoring all AP IRQ routes on return. CP AON alarm and PMU wake sources stay
in use. It needs event/wake qualification; this is not promoted production policy.
AP90 was built but withheld/unflashed: routed AP IRQ status alone could hide raw
GPIO pending work. CP57 ABI36 now independently rejects raw GPIO status without
acknowledgement, including an event arriving during pre-mask tracing. 56 CP/35 AP
cases and11 native/coordinator methods pass; AP route ownership/cleanup executes
for both wake configurations. CP57 and AP91 builds underway for00:51:05–25.
Build/package/readback/hardware results pending. Normal sleep/tickless off,
240MHz active, display unchanged, screen confirmation pending, overnight deferred.


### Drained-snapshot candidate installed — 2026-11-02 00:37 UTC

CP56 actual physical partition SHA256
`99f043f6d3401aed5c8831828bb86e49344aa22f4442df180a9e803654585803`
matches all active bytes/CRCs. AP89 b705b42b booted00:36:30 and recovered encrypted
protocol00:36:52. Its bounded00:39:05–25 CPU0-WFI-required masked trial adds the
pre-mask snapshots and UART drain; final admission follows tracing. Outcome pending.
Resources unchanged. Conditional meter capture is armed for00:38:25, not yet
receiving. Current screen unconfirmed; no fixed-display energy claim. UART29 and
phone26 have received data, bounded to about01:14/01:12 respectively. Normal
retained/tickless defaults off; no global sleep or new savings qualification.


### Admission-order trial reset; drained pre-mask trace — 2026-11-02 00:35 UTC

AP88 / byte-verified CP55 reset at 00:29:10, despite actual CPU0-WFI being added
to the global condition and the breadcrumb/cache clean moved before the mask.
Only CP_HAL arrived; no returned marker or PMU endpoint. Pre-trial encrypted
protocol was established at 00:24:37. First-failure/v88 preserves the full trace.
Diagnostic meter recording closed successfully (8,976 samples), spanning reset;
visible state unconfirmed and pre-trial input ~172mW, so no savings/workload claim.
Exact normal AP restored; expected ID and existing PFS new=0 received 00:31:29.
The admission-order change is insufficient; causation remains unresolved.

CP56 ABI35 adds five pre-mask register snapshots: sleep condition, wake/halt
configuration, PMU power parameter, CPU status and HF enables. The existing
exclusive UART lease drains with a 2ms AON budget plus finite stalled-clock poll
cap, then recomputes all final deadline/IRQ/flash/UART checks. This avoids writing
trace traffic after the final mask. New modeled stalled-UART, pending-tick and
expiry-during-trace cases reject entry. 54 CP/35 AP cases and19 focused unittest
methods pass; mandatory/recursive ITCM audit and package replay pass. CP56 raw
SHA256 c57ca4978f43ba80a75353b2f3e09d4cc715f6b5c101167d7bfaa38f34924100.
Installed on normal AP; physical readback is in progress. AP89 b705b42b is built,
SRAM-audited, not installed yet, for 00:39:05–25 UTC. No global-sleep qualification.

UART27/phone25 closed. UART28 received zero bytes and was excluded. Fresh UART29
reception is proved by746 bytes of the vendor read-only help reply; phone26 also
received data. Both bounded (UART29 to about01:14, phone26 to01:12). Candidate
meter recording conditional for00:38:25; not receiving yet. Normal defaults off,
active240MHz, display unchanged; overnight deferred; current screen confirmation pending.


### CPU0-WFI admission trial installed — 2026-11-02 00:26 UTC

CP55 ABI34 full physical readback matches active bytes/CRCs, partition SHA256
`f157c363d08b352ff1594dddb09ce7f2b866c85577a3d613a54d73f4eec179b8`.
Normal AP reopened existing PFS and recovered encrypted protocol after CP install,
then again after the documented readback restart. AP88 `b2bf5954…` is installed;
expected build received 00:23:56 and encrypted protocol 00:24:37. Its CPU0-WFI-
required masked admission trial is scheduled 00:29:05–25, not yet completed.
AP87 remains unflashed, superseded only by AP88's earlier UTC slot. Resources and
active 240 MHz configuration unchanged. Bounded diagnostic meter recording is
armed for 00:28:25; waiting, not receiving yet. UART27/phone25 remain bounded to
about 00:35. Visible-state confirmation is pending; no fixed-display/workload
energy comparison or global-sleep qualification. Default retained/tickless off.


### Masked HF-retained reset; WFI admission control — 2026-11-02 00:22 UTC

AP86 c6c3a7ca on byte-verified CP54 reset at 00:13:11 after the received CP_HAL
marker. No CP_Woke or FlashControllerRestored marker arrived. Its startup never
established an encrypted/protocol session; the commanded phone cycle did not
recover it before the slot. This is an unmatched functional diagnostic, not a
Bluetooth-compatible or energy-qualified comparison. Conditional meter capture
skipped unarmed. First failure is frozen under deep-sleep-review-20261101/first-failure/v86.
Normal AP 2cc3478b was restored; expected build and PFS new=0 received at 00:14:59.
Honor UI independently showed Connecting and Android zero GATT connections;
a documented companion-level disconnect/connect is recorded separately.

CP55 ABI34 adds a default-off, bounded CPU0-WFI-required masked control, retaining
software HF. This tests admission ordering: selected vendor global-only condition
could admit sleep without CPU0 WFI, while native code still cleaned a cache-backed
breadcrumb after setting the hardware mask. The breadcrumb/count now precede the
mask, and the new condition adds actual CPU0 WFI without relaxing bus idle, AP
publication, IRQ, deadline, flash/retention or owner checks. Causation is unproved.
49 CP and 35 AP modeled cases plus coordinator, timer/retention, flash bootstrap
and mandatory ITCM audit regressions pass; 19 focused unittest methods total.
CP build, mandatory/recursive linked ITCM audit and package replay pass.
Raw CP SHA256 63a677c7f53739c09c727a67e48b703db746199dbdd83253415c89b6be14ebb4.
CP55-only flash is underway on normal AP; physical identity and hardware result pending.
AP87 is built/SRAM-audited but unflashed; AP88 advances the same candidate's slot
to 00:29:05–25. Display resources unchanged; visible-state confirmation pending.
Normal retained/tickless defaults remain off; no new qualified saving; overnight deferred.


### Software-HF control and external trace pass — 2026-11-02 00:07 UTC

CP54 ABI33 actual bytes/CRCs match the package, physical partition SHA256
`2e784605a9bafb60cdbcb3e137caf6fa998b8fbf2f4844b24c4a2faa9029f84a`.
Normal AP reopened existing PFS; after a documented phone cycle encrypted protocol
recovered at 23:59:42. AP85 `7f094078…` then booted and recovered encrypted
protocol at 00:02:02 before its bounded unmasked vendor-HF control.

The 00:03:05–25 slot completed 46 AP returns and 40 CP WFI returns, one safe
abort, no contract fault or reset; it did not reach the 64-entry ceiling before
the slot expired. There are exactly 40 externally received CP_Woke and 40
FlashControllerRestored markers, plus all 46 cleanup sequences. Thus the leased
UART tracing is qualified for this control; SRAM breadcrumbs alone returned zero
after the earlier resets. HF register `0x6c0fa76c` confirms software HF retained;
PMU fresh wake, ambiguity/clear-held counters and hardware mask remain zero.
This is not global low-voltage evidence. Post-window documented phone cycle
recovered encrypted protocol at 00:05:07. Complete evidence frozen in
`deep-sleep-review-20261101/first-failure/v85`.

AP86 `c6c3a7ca…` passed SRAM audit and matches AP85 except hardware mask and UTC
slot; display resources identical. AP-only flash underway for 00:13:05–25.
No masked software-HF result yet. Conditional diagnostic power capture armed for
00:12:25; waiting, not yet receiving. AP85's initial meter schedule skipped while
unarmed; separate capture actually started 00:02:48, before the trial, and closed.
No fixed-display or useful-work energy claim; operator confirmation still pending.
Default normal retained/tickless off, display unchanged, overnight deferred.
UART27/phone25 bounded to about 00:35, verified actual received bytes.


### Masked reset persists; matched software-HF isolation — 23:56 UTC

AP84 / verified CP53 reset at 23:47:10. Automatic startup CP-stage query returned
stage/generation zero with status OK, so the SRAM breadcrumb did not localize the
failure. Existing PFS and encrypted protocol recovered on the restarted candidate;
complete first failure was frozen before programmer rollback under
`deep-sleep-review-20261101/first-failure/v84`. Exact normal AP was then restored,
with existing PFS and encrypted protocol recovery received at 23:50:46.
Early ITCM flash bootstrap repairs an unsafe XIP dependency and passes unmasked
controls, but is insufficient to resolve masked wake. No global-sleep claim.

The WFI-to-flash-probe ITCM audit is now mandatory in CP sleep-contract builds.
Six dedicated regressions reject direct/conditional flash calls, flash literals,
indirect branches and missing symbols. Installed CP53 also passes this check.

CP54 ABI33 adds matched selected-vendor-condition unmasked/masked real-WFI
controls that skip software HF shutdown, plus flash-restored/failure UART markers
using the existing exclusive trace lease. Hardware PMU halting is unchanged;
this does not guarantee hardware HF retention. Dedicated default-off Kconfig
`T5AI_RETAINED_SLEEP_VENDOR_KEEP_HF` selects these controls while preserving
legacy Kconfig exclusions. Early native flash bootstrap and all ownership,
interrupt, deadline, timer, retention, heartbeat and watchdog guards remain.
22 focused host test methods passed; updated AP selection/native tests pass.
Mandatory/expanded ITCM audits, flash-transition audit and package replay pass.
CP raw SHA256 `97f5e358762bcd316c4ce973828c8f2f9a295267c224a245fad8ac000d04cd4b`.
CP54-only flashing on normal AP is underway; physical readback pending.
AP85 `7f094078…` unmasked control is built for **2026-11-02 00:03:05–25 UTC**,
not installed or hardware-qualified. A masked pair follows only after control pass.

UART27 (Python3.12)/phone25 replace closed UART26/phone24; actual bytes verified
after a documented normal-firmware phone cycle. Both bounded to about 00:35 UTC.
No matched/new qualified energy saving; AP83 visible-state confirmation remains
pending and its power capture missed the start. Display unchanged, active clock
240 MHz, normal retained/tickless defaults disabled, overnight deferred.


### Early-flash bootstrap control passes; masked comparison flashing — 23:40 UTC

AP83 `c8a19f68…` / byte-verified CP53 ABI32 completed 64 AP and 58 CP WFI
returns, one safe AP abort, no late wake, contract fault or reset. PMU fresh wake,
ambiguity and clear-held counters are zero, R43 before/after zero and actual
hardware mask zero. The helper therefore passes the unmasked functional control;
this is not proof of global low-voltage entry. Full evidence is frozen under
`deep-sleep-review-20261101/first-failure/v83`. A documented post-window phone
cycle recovered encrypted protocol at 23:38:59. Operator post-flash visible-state
confirmation remains pending; the power capture began after the contract log.
Exclude this diagnostic recording from fixed-display/full-workload energy claims.

AP84 `f9476841…` passed its SRAM audit and has identical T5AI configuration to
AP83 except hardware mask and UTC slot. Resources are byte-identical. AP-only
flash succeeded for 23:47:05–25; expected build and encrypted protocol recovery
at 23:41:40 are received. No masked CP53 result yet. A bounded diagnostic meter
capture is armed for 23:46:25, not yet running/receiving samples; operator visible
state confirmation remains pending. This tests the
previously failing boundary after the native ITCM flash-controller bootstrap fix.
All ownership, IRQ, RTC, deadline, clock-restore, heartbeat and watchdog guards
remain. Default normal retained/tickless disabled; display unchanged; overnight
deferred. Current bounded captures UART26/phone24 have verified received bytes
and end about 00:06 UTC. No new measured/qualified energy saving.


### Native flash bootstrap repair — 23:29 UTC

AP82 was withdrawn before its 23:19 masked trial: a broader linked audit found
flash veneers in the CP wake HAL before flash-controller restoration. The pinned
flash driver explicitly forbids flash access before restoring those settings.
This is a concrete critical-path defect; it is not yet proved to be the only
cause of the masked resets. AP82 has no masked-trial hardware result. Exact normal
AP was restored; UART confirms existing PFS and encrypted protocol at 23:19:43.
The late operator Nadir/BT confirmation answered the earlier AP82 setup question.

CP53 ABI32 implements native-only early controller restoration from ITCM before
returning from the WFI wrapper to the vendor HAL. Cache mode, quad bits and probe
address/value while XIP is available; establish QE before entry. The wake helper
uses retained data and controller operations, no RTOS or XIP configuration calls,
then checks its probe after reconnect. Admission rejects missing cached settings;
a failed wake probe stays in ITCM until the independent watchdog resets it.
No application flash/PFS writes are added to the wake sequence.

16 host test methods pass, including a protected/inaccessible-XIP-page restore,
dual/quad modes, ownership guards, bad probe and fail-closed admission. Recursive
linked ITCM audit covers the WFI wrapper through the early restore/probe and all
its callees; the separate flash transition audit and package replay pass.
CP raw SHA256 `fbb40df3e7ab22c4ec677c54e6eeb96e5a5ce3634cd01f1bb6578dfddb7b05e3`.
CP actual protected bytes/CRCs match the package across the full partition,
physical SHA256 `3f3c159290d72440461606a2dd6a8c7e73eaa0e9f829ea5023975ee3713efea0`.
Normal AP reopened existing PFS and recovered encrypted protocol at 23:30:18.
AP83 `c8a19f68…` unmasked control is installed and SRAM-audited for 23:37:05–25.
UART confirms its expected boot, successful read-only startup CP query and
encrypted protocol recovery at 23:32:31. Its PFS new=0 startup line is absent
from received frames; normal-AP PFS recovery before this flash was verified.
Operator display confirmation is pending. AP84 masked comparison is building
for 23:47:05–25, conditional on this control passing; not installed.
No global-sleep or saving qualification. Source normal defaults off; display fixed;
overnight deferred. UART26 (explicit Python3.12)/phone24 replace closed UART25/
phone23; actual received data verified, bounded to about 00:06 UTC.


### Clear-register hypothesis excluded; CP-stage recovery repeat installed — 23:13 UTC

AP81 / byte-verified CP52 completed 64 AP and 61 CP WFI returns, one safe AP
abort, no late wake, fault or reset. Fresh PMU wake count/sources/ticks and
ambiguity remain zero. R43 before/after, aggregate clear-held count and actual
hardware mask at WFI are all zero. There is no evidence for a held wake-clear
bit; do not add a speculative clear pulse. Complete positive-control evidence
is frozen under `deep-sleep-review-20261101/first-failure/v81`.
A documented post-window phone cycle recovered encrypted protocol at 23:09:28.
Operator confirmed Nadir/Bluetooth before the trial; visible state afterward is
not yet separately confirmed.

Settled baseline 23:03:31–23:05:31: 11,972 USB-input samples, 144.2115 mW mean,
logger exit 0, versus AP80 control 143.9996 mW. The 90-second AP81 diagnostic
trial capture closed cleanly at 143.8046 mW overall; the short bounded sleep
burst does not establish a qualified sustained or useful-work saving.

AP82 `5e8f00ac…` is installed for 23:19:05–25 with the same masked/global-only hardware
sequence as failed AP80, plus the host-tested startup CP previous-stage query.
This should preserve the next failed entry's last CP stage before any programmer
reset can overwrite it, if the CP breadcrumb survives. No new CP build or
admission change is selected. SRAM audit passes and T5AI configuration matches
AP80 except the UTC slot; resources are identical. UART verifies startup at
23:12:04, read-only CP previous-stage query (stage/generation zero, status OK),
SD ready and encrypted protocol recovery at 23:12:32. The expected PFS new=0
line is absent from this received startup trace; do not claim that boot line was
captured. Operator display confirmation is pending. Source defaults retained/
tickless off; display unchanged; overnight deferred. UART25/phone23 bounded to
about 23:29 UTC and have verified received data.


### Masked/global-only trial reset; exact normal AP restored — 22:59 UTC

AP80 / verified CP52 reset uncommanded at 22:57:10 during the 22:57:05 slot,
without a completion report or R43/PMU telemetry. Received boot reset sources
are AP=0, CP=0; this does not identify the cause or prove global entry.
Full first-failure UART/phone/stage/snapshot evidence is frozen under
`deep-sleep-review-20261101/first-failure/v80` before rollback.
The vendor global-only condition did not make the final hardware-mask path safe.

Exact normal AP `2cc3478b…` is restored; UART verifies its build and existing
PFS (new=0), encrypted protocol recovery at 22:59:03 and operator-confirmed
Nadir/Bluetooth recovery. CP52 remains byte-verified. AP81 `151696ae…`, the
unmasked comparison with identical CP52 telemetry, passed the SRAM audit and
is now installed for 23:07:05–25. Its expected boot ID is received at 23:00:14;
UART verifies existing PFS and encrypted protocol recovery at 23:01:30;
operator confirms dim Nadir updating/Bluetooth connected at 23:02:01. The
matched 120-second baseline follows 90 seconds of settling.
It will distinguish the wake-clear instrumentation from the failing masked path.

Implemented, not flashed: bounded test builds always queue a read-only CP
previous-stage diagnostic at startup, including when AP reset reason/breadcrumb
are lost. All 14 native/coordinator/mailbox test methods pass, with a new
coordinator case for the lost-AP-evidence path. Default normal policy unchanged.

Settled baseline after operator confirmation plus 90 seconds: 22:53:18–22:55:18,
11,968 USB-input samples, mean 143.9996 mW at 4.99479 V / 28.82996 mA,
logger exit 0. The 90-second trial recording received data and closed cleanly,
but includes the hang/reset and rollback; exclude it from saving claims.
About 99 mW while unresponsive before reset is not a successful optimization.
Display unchanged; default retained/tickless off; overnight deferred.
UART25/phone23 bounded to about 23:29 UTC; actual reception verified.


### Mask-only candidate installed — 22:51 UTC

AP80 `6aa0df66…` is installed on byte-verified CP52 ABI31 for the bounded
22:57:05–25 UTC trial. AP raw SHA256
`22717ce6d62994f3d85ff7350a1a19af74f154336f597909bdfa59518b600879`.
SRAM instruction/literal audit passes; resources are byte-identical to the normal
control. UART confirms the expected ID, existing PFS (new=0) and active 240 MHz.
UART confirms encrypted/protocol recovery at 22:51:09; operator confirms dim
Nadir updating and Bluetooth connected at 22:51:48. A 90-second settling period
precedes the two-minute diagnostic baseline.
The CP hardware comparison changes only the final CPU0 hardware mask relative to
AP79's selected global-only vendor sleep condition; CP52 additionally reads R43
and the actual mask at WFI. A telemetry-matched unmasked AP81 control is building
for 23:07 UTC, not installed. Neither candidate is global-sleep qualified.

UART23/phone22 were closed and preserved before renewal. UART24 received no
bytes and is excluded: default host Python 3.14 reports B460800=460800 rather
than the working Python 3.12 value 4100. UART25 now uses explicit Python 3.12;
actual bytes are verified after a documented phone reconnect. Phone23 also
receives data. Both captures are bounded to about 23:29 UTC. Display settings
fixed, retained/tickless defaults off, no savings claim, overnight deferred.


### Aggregate result and masked comparison — 22:47 UTC

AP79 / byte-verified CP51 completed 64 AP and 60 CP WFI returns, no abort,
late wake, timer/retention fault or reset. All aggregate fresh PMU wake counters
(count, sources, ticks and preexisting-status ambiguity) are zero. The complete
report is preserved under `deep-sleep-review-20261101/first-failure/v79`.
This demonstrates safe coordinated returns, not global low-voltage entry.
The 90-second diagnostic USB-input capture received 8,980 samples, mean
143.325 mW; it is not a matched energy-saving qualification.

CP52 ABI31 passes 46 CP / 32 AP native cases plus coordinator/mailbox tests
(14 methods), ITCM/flash closure audits and package replay. Actual CP bytes and
CRCs match the package: raw SHA256
`28d77908803ff251ca418f46f64bc86b79a46f0bcb1f05fb0ca4cbbc459512e8`,
physical partition SHA256
`b1f54d3ed5eb14a5a53d5fa29bca6ccc3caafacfa66c69789448ab0de94e9cc4`.
The first readback handshake failed before reading/writing; separate retry passed.
CP52 adds read-only R43 clear-register and actual mask-at-WFI telemetry.
AP80 is building for a bounded 22:57:05–25 comparison: selected vendor global-only
condition with the final CPU0 hardware mask enabled. All software flash ownership,
IRQ/deadline checks and AP suspend/restore guards remain. Not yet installed or
hardware-qualified. No global-sleep/savings claim; display fixed, normal retained/
tickless defaults off and overnight deferred.


### Aggregate repeat installed; next telemetry prepared — 22:42 UTC

CP51 ABI30 raw `b2bc7f3ebbea33f63ba15a41ffdb361d3a50f68d3cd4eb4a66903725449a7370`
passes 45 CP / 31 AP cases, coordinator/mailbox, ITCM/flash audits and package
replay. Physical readback matches bytes/CRCs, partition SHA256
`0666bfecef192a4dc1cd2fef492789c7219558d8b8f55592fc01d48299e544e2`.
AP79 `c12ac38b…` is installed for 22:43:05–25 with hardware masking off.
First AP flash attempt failed download handshake before erase/write; the separate
retry succeeded. UART confirms existing PFS and encrypted protocol recovery at
22:39:52. The extra standalone read-only R43 request was **not sent**: the
capture owns the UART exclusively (EBUSY). No capture ownership was changed.

CP52 ABI31 read-only clear-register before/after, clear-held count and actual
mask-at-WFI telemetry is being host-tested/built independently; it is not installed.
No further hardware admission change selected until AP79 aggregate result.
No global-sleep/savings claim. Display fixed, default retained/tickless off,
UART23/phone22 bounded to about 22:59, overnight deferred.

## Aggregate global-wake evidence before another admission change — 22:33 UTC

AP78 `d29c6e94…` / verified CP50 returned 64 AP and 61 CP WFI waits, two safe
AP aborts, no late wake/fault/reset. Sleep config `0x240882c0` confirms the selected
vendor global-only condition. Final wait PMU status before/after was zero; this
is only the last wait and cannot exclude global entries earlier in the batch.
First positive evidence `first-failure/v78`; post-trial documented reconnect
recovered encrypted protocol at 22:31:43, separately from the sleep slot.

CP51 ABI30 adds aggregate fresh PMU wake count, source bits and AON ticks, plus
an ambiguous-preexisting-status count. Vendor code clears wake status before
every resource preparation; the native post-WFI read precedes HAL/resource
restoration. AP79 repeats unchanged unmasked hardware admission at 22:43:05–25
to resolve the whole batch. A separately selectable final-mask comparison is
implemented but not selected for this repeat. No default sleep policy change.
Host/build/ITCM/readback qualification ongoing; no global-sleep/savings claim.
Display fixed, overnight deferred, UART23/phone22 bounded to about 22:59.

### Selected-vendor-condition candidate installed — 22:24 UTC

CP50 ABI29 raw SHA256
`dd55e62691a36b739952126dcc32c1fc58dedd3f4ccd0ccba6f25c2f6bc0d57b`
passes 44 CP / 30 AP host cases plus coordinator/mailbox, ITCM/flash closure,
lint and package replay. Full readback matches active bytes/CRCs; physical
partition SHA256
`4d00a9f3b86f2993402fd6850eb9729809038e98f912e8db645690938481b821`.
AP78 `d29c6e94…` is installed for 22:29:05–25; SRAM/resources audits pass.
Startup reception has begun; PFS/encrypted protocol recovery and slot outcome
remain pending. CP49/AP77 never flashed. UART23/phone22 received actual data
before installation, bounded to about 22:59 UTC. No global-sleep/savings claim.

## Match the selected vendor sleep condition — 22:21 UTC

AP76/CP48 returned 64 AP and 63 CP WFI waits without fault/reset; CP wait
96,645 AON ticks (3.020156 s), AP 98,384 ticks (3.0745 s). Final busy/IRQ/mask
rejection zero, one safe final CP skip. Sleep config `0x240e82c0` confirms only
the flash-idle predicate changed. The complete PMU tail still reports before/after
zero; flash predicate alone did not establish global low-voltage entry. Frozen
positive control `first-failure/v76`; post-trial reconnect is separate.

The actual SDK selects `CONFIG_PM_LV_SUBCORES_ON=y`: its pinned vendor branch
uses global-enable-only hardware admission, without either CPU-WFI predicate.
CP49/AP77 were built but superseded **without flashing** after checking this
selection. CP50 ABI29 / AP78 explicitly match that branch while preserving native
software AP-suspend publication, actual AP/CP WFI, SRAM wait, global bus-idle,
flash owner leases/final busy rejection and IRQ/deadline guards. 22:29:05–25
trial is pending; host/build/ITCM/readback qualification ongoing. Renewed
UART23/phone22 captures are bounded to about 22:59; verify actual reception before
flashing. Default retained/tickless off, no global sleep/savings claim, display
unchanged and overnight deferred.

### Flash admission predicate diagnostic — 22:10 UTC

CP48 ABI27 / AP76 isolate the hardware flash-idle predicate against pinned Tuya.
The diagnostic clears the extra hardware flash-idle requirement only; AP/CP WFI,
global bus-idle, software flash ownership and deadlines remain. A final raw
flash-controller busy_sw check rejects active operations before WFI in every
native mode. Host tests verify busy rejection without masking or entering WFI,
and ordinary modes retain the stricter predicate. 42 CP / 28 AP cases plus the
coordinator/mailbox suite pass; ITCM/flash closure and package replay pass.
CP48 raw SHA256 `ed491e89f7107abba4ddd9c4a9d53b6296df78eda4504d11434f371a2f806277`.
AP76 `4f94942c…` is installed for 22:15:05–25; SRAM/resources audit passes.
CP48 readback matches active bytes/CRCs, physical partition SHA256
`bcf1016353d12957df949348a0f6c68a565951596920e43c5136731603a37a12`.
UART confirms existing PFS and encrypted protocol recovery at 22:11:04.
No global-sleep or energy qualification yet; display fixed, overnight deferred.

## Full clock-shutdown WFI returns; global sleep still unproved — 22:07 UTC

AP75 `28aa9949…` / byte-verified CP47 completed 64 AP WFI returns and 61 CP
WFI returns: 90,928 CP AON ticks (2.8415 s), 92,643 AP ticks (2.895094 s), no
abort, late wake, fault or reset. Low-HF register `0x6c0fa700` and low core mode
`0x00100000` match the full software clock transition. Final mask/pending/HAL
reject were zero, with three safe final CP skips. Frozen evidence: `first-failure/v75`.

Delayed UART now proves bonded/encrypted protocol recovery at 22:01:28 before
the slot, following the documented startup phone cycle. The operator confirms
Nadir updating and Bluetooth connected after it. Earlier notes saying pre-slot
Bluetooth was unconfirmed are superseded; retain the original frozen record and
append this correction. AON power parameter `0x691113bb`, but wake status before
and after WFI both zero: no independent global low-voltage entry proof and no
savings claim. Next: bounded vendor flash-idle predicate isolation, preserving
flash owner leases, final controller-busy rejection and CPU/global bus-idle checks.
CP48/AP76 build/host validation ongoing for 22:15:05–25. Default sleep/tickless off.

### Full software-clock trial installed — 21:59 UTC

CP47 ABI26 raw SHA256
`d765b6154f9155dc621002c1b744a19863f51119005f1e8e2f152c20ca07dd51`
passes ITCM/flash closure and package replay. Full physical readback matches active
bytes/CRCs; partition SHA256
`a12c5c2e1583777390074a377bf5dbde176a88ee508cc85c83f93f578f05a8a7`.
AP75 `28aa9949…` is installed for 22:03:05–25, software HF shutdown enabled,
no CPU0 hardware masking, SRAM audit passed, identical display resources. It
reopened PFS, but startup GATT retry preceded sleep; a documented pre-slot phone
cycle is being used for recovery. This startup is not an uninterrupted reconnect
qualification, and no sleep result exists yet. UART22/phone21 received data;
captures bounded to about 22:22 UTC. Default retained/tickless remains off.

## CPU0 hardware-mask isolation succeeds — 1 November, 21:54 UTC

AP74 `60ae52b0…` on byte-verified CP46 completed 64 AP WFI returns and 55
actual CP WFI returns, 20,331 CP AON ticks (0.635344 s), 23,444 AP AON ticks
(0.732625 s), three safe AP aborts, no late wake, fault or reset. All 55 raw
CP_Woke markers arrived; 64 restoration/resume sequences returned. Final CPU0
mask, pending IRQ/NVIC and HAL reject were zero. Nine final CP skips were safe.
The operator confirms dim Nadir updating and Bluetooth connected during the slot.
Frozen positive evidence: `deep-sleep-review-20261101/first-failure/v74`.

This mask-only comparison strongly implicates the CPU0 hardware mask in the
prior reset. It does not independently prove PMU global low-voltage residency
or software HF shutdown, and the short diagnostic USB trace is not a savings
comparison. CP47 ABI26 / AP75 extend the same unmasked WFI path to software HF
shutdown and expose PMU power parameters plus before/after wake status. 41 CP /
27 AP host cases and the coordinator/mailbox suite pass. Build/ITCM/package and
22:03:05–25 hardware qualification are pending. Default retained/tickless stays
off; display unchanged; overnight deferred.

### Review correction: software HF bypass versus PMU hardware halt — 21:47 UTC

The keep-HF diagnostic bypasses software HF shutdown and the software en_cb
clear. It does not clear AON PMU r40 halt_xtal (bit 25) or halt_flash (bit 27),
which the vendor low-voltage power parameter still programs. Therefore AP73
has not established failure with hardware HF continuously retained. AP74 remains
a valid mask-only comparison against AP73. If required, explicitly isolate PMU
halt bits using named constants and hardware readback. No sleep/saving claim.
Artifact: `pmu-hardware-hf-review.json`.

## CP WFI still resets with HF retained — 1 November, 21:43 UTC

AP73 `f858c9a7…` on byte-verified CP45 reset during the 21:39 trial.
Only the pre-HAL raw marker arrived; no CP_Woke, completion or residency proof.
First failure is frozen under `deep-sleep-review-20261101/first-failure/v73`.
The 60-second USB trace received 5,976 samples, logger exit 0, but spans reset
and rollback and must not be used as savings. Normal AP `2cc3478b…` was restored;
UART confirms existing PFS and encrypted/protocol recovery at 21:40:53.

CP46 ABI25 adds an explicit default-off, bounded HF-retained real-WFI control
without the CPU0 hardware interrupt-mask transition. PRIMASK and the deadline,
pending IRQ, AP wake and UART-empty guards remain. This isolates the vendor's
warning that the hardware mask prevents normal-WFI wake; it does not establish
that this warning caused the reset. Forty CP / 26 AP host cases pass; recursive
ITCM and flash-disconnect audits and package replay pass. CP46 readback matches active bytes/CRCs; physical partition SHA256
`04244241a69a49c601ed8d7016fb4e31c91482363252981b9ddc4fc66a6e52be`.
AP74 `60ae52b0…` is installed for 21:51:05–25; SRAM/resources audit passes.
Nine sanitizer-enabled coordinator/mailbox methods pass on the host; the sandbox
run was unable to execute LeakSanitizer under ptrace. Hardware trial is pending. Fresh UART22 and
phone21 captures received actual data through one documented phone cycle.
Display resources/configuration remain fixed; retained sleep/tickless default
off; no successful real CP retained sleep or savings claim. Overnight deferred.

## HF-retained real-WFI isolation installed — 1 November, 21:35 UTC

The operator confirms both devices were untouched during the 21:17 failure and
that dim Nadir/BT recovered. Exact normal AP rollback received existing PFS and
bonded/encrypted protocol recovery at 21:21:52. The first failure and power trace
are preserved; the latter's low interval is explicitly not a saving.

CP44 was built/packaged but never flashed: stale-reception preflight stopped
before device access. CP45 (same ABI24) adds an explicit nonempty trace-UART
rejection before WFI and hardware masking; native busy/drained tests pass.
39 CP / 25 AP native cases pass, alongside unchanged coordinator/mailbox checks.
ITCM/flash closure and package replay pass. Actual CP45 readback received the
full 1,114,112-byte partition and matches active bytes/CRCs. Raw SHA256:
`a12c651f7a52d0ea3d99156e1bd7d03322b326ed946270450c07f8957d7e1193`;
physical partition SHA256:
`6d44a1fa7065c69c81198407354e8bb42ee100bc54d861dc09038b5a04d45e93`.
Fresh UART data was verified through a documented phone cycle before installation.

AP72's 21:33 slot was superseded without flashing. AP73 `f858c9a7…` is being
installed for 21:39:05–25: real CP WFI with HF retained, normal modules/domains
and voltage handling, unchanged deadline/pending-IRQ/mask guards. The post-WFI
raw marker adds no pre-entry UART traffic; final UART-empty guard protects the
existing pre-HAL marker. SRAM/resources audit passes. No retained sleep or saving
is qualified. Display fixed; UART21/phone20 bounded to about 21:47; overnight deferred.


## Publication-wait trial resets; first power baseline recorded — 1 November, 21:22 UTC

AP71 `c2c1670a…` / verified CP43 reset during the 21:17 trial. Fresh boot at
21:17:10 reports AP/CP reset sources 0/0, and existing PFS `new=0`; no completion
or residency report survived. HF was off, so raw CP stage markers were unavailable.
Frozen first failure: `first-failure/v71`. Cause remains unassigned. Exact normal
AP was restored, boot/PFS received at 21:18:48; Bluetooth recovery review pending.
No CP retained-sleep qualification and no retry outside the expired slot.

After confirmed dim Nadir/BT, USB/no cell and 90 seconds of settling, the two-minute
baseline received 11,980 samples at 21:14:32–21:16:32 and closed with logger status 0:
mean 4.995279 V, 28.842927 mA, 144.078464 mW at USB input. Preliminary UART review
found no baseline error/disconnect/notification; review delayed data separately.
Probe samples around 0.149 W also agree with the meter's 0.15 W display. Artifacts:
`power-baseline-v71` and `power-trial-v71` in the current audit directory.

During the failed slot, USB voltage remained near 5 V while input fell to about
100 mW for several seconds, then reboot drew about 625 mW during startup. This
is failed-workload evidence, not measured savings or proof of low-voltage residency.
The next isolation retains HF clocks during real CP WFI, keeping modules/domains,
voltage handling and final pending-IRQ/deadline/mask guards. An explicit ABI24
operation and default-off bounded configuration avoid changing clock-only semantics.
A post-WFI raw marker will distinguish a returned wake from later restoration.
CP44/AP72 host/build/qualification work is ongoing; display fixed, overnight deferred.


## CP GPIO ownership fixed; AP GPIO wake still prevents entry — 1 November, 21:07 UTC

AP70 `d08e4283…` / byte-verified CP42 completed 64 AP WFI returns and eight
CP clock cycles without reset, deadline or retention failure. CP pending and
NVIC status are now zero, confirming the unwanted CP GPIO route was removed.
All eight final admissions instead rejected AP IRQ (`0x10`); captured AP high
status is GPIO55 `0x00800000`. Zero CP WFI/residency; expected NoControllerSleep
fault 7 stopped the pilot. User confirmed dim Nadir/BT. Frozen: `first-failure/v70`.
A documented post-trial phone cycle restored bonded encryption/protocol by
21:05:19; keep it separate from the trial and startup qualification.

CP43 (ABI23) adds the pinned vendor's 3 ms bounded AP-publication wait, ending
early on CP pending IRQ/SysTick, insufficient deadline lead or invalid lease.
The prior native path returned immediately, potentially missing an idle window
until a later interrupt. New captured per-pin GPIO status diagnostics distinguish
late CP admission from a persistent external pin source. No GPIO event is cleared
or ignored. Fourteen host methods pass, including 37 CP / 24 AP native cases,
publication timeout/wrap/IRQ/lead and actual secure/nonsecure GPIO routing.
CP43 / AP71 `c2c1670a…` are installed for 21:17:05–25; hardware outcome
remains pending. ITCM/flash closure, package replay, AP SRAM, ruff and diff checks
pass. Actual CP readback matches active raw/protected bytes and CRCs. CP raw:
`cea157a125555fd2381882dbf253a5f6acdd5804be3519d193b6162ea956a1b3`;
physical partition SHA256:
`0f56dca4914a2818e67a8ee37e1981113d3f7426ff7f2f9f4f39196713c677f8`.
Readback was a commanded warm reset. AP71 boot/PFS `new=0` received at 21:10:22;
bonded encryption/protocol recovered at 21:11:03. UART20/phone19 closed after
freezing evidence; UART21/phone20 actual bytes were verified using a documented
reception cycle before flashing. Bounded expiry about 21:47 UTC. Display fixed;
meter reception/baseline verification is pending, no saving; overnight deferred.


## Real WFI safely rejected on CP GPIO interrupt — 1 November, 20:54 UTC

AP69 `77a45a2d…` / verified CP41 completed 64 AP returns with one safe abort
and nine CP clock cycles, but every final CP WFI admission rejected pending
GPIO interrupt 55: CP high status and NVIC `0x00800000`, final reject IRQ mask `0x2`,
nine skips, zero WFI entries/residency. Expected NoControllerSleep fault 7 stopped
the pilot; no reset or retention/deadline failure. Bonded encrypted protocol
was received before the slot; user confirmed updating dim Nadir/BT. Evidence
is frozen in `first-failure/v69`. This is no-admission, not successful CP sleep.

Pinned HAL unconditionally enables CP's GPIO route during low-voltage entry,
even though native GPIO wrappers preserve AP pin configuration/ack ownership.
CP42 (ABI22) now omits that CP route for prepared native sleep; non-native
behavior is unchanged. AP keeps its already-owned GPIO interrupt as a wake
source; pin status is not acknowledged or discarded by this change. Final CP
admission also rejects pending AP work and exposes two explicit AP IRQ status
diagnostics. This preserves pending-event guards rather than ignoring GPIO.

14 host test methods pass (36 CP / 24 AP native cases, coordinator/mailbox,
and the actual HAL routing fragment in both secure modes). ITCM/flash closure,
package replay and AP70 SRAM audits pass; ruff and diff checks pass. CP42 and
AP70 `d08e4283…` are installed for the bounded 21:03:05–25 repeat. Actual CP
readback matches all active bytes and protected-block CRCs. CP42 raw SHA256:
`2ead18da6dd327eaebdd0f6d92fe3cd715d3c2c513b453cd0cfb67d9e310c58f`;
physical partition SHA256:
`22de71d526736f58ecbde3f92013431600467d781b5867ab13592a30a1436ee6`.
Readback caused a documented warm reset. AP70 boot/PFS `new=0` received at
20:58:15; bonded encryption/protocol recovered automatically at 20:58:35.
Real CP residency, radio startup stability, reconnect/cold-power and energy
remain unqualified.
UART20/phone19 have actual reception and are bounded to about 21:19 UTC.
Display fixed; no energy capture; overnight deferred.


## HF shutdown/restore passes; real CP WFI next — 1 November, 20:45 UTC

AP68 `22b2aeb7…` on CP40 passed the 20:43 clock control: 65 requests,
64 AP WFI returns, one safe abort, 48 CP clock cycles, 249 ready samples,
no contract/deadline/retention fault or reset. Normal modules/domains and HF
shutdown/restoration were exercised; CP WFI deliberately remained zero. The
low-clock snapshot changed HF register from `0x6c0fa76c` to `0x6c0fa700`.
UART received bonded encryption at 20:42:56 and protocol recovery at 20:42:57,
before the slot. The operator confirmed updating dim Nadir/BT. The earlier
startup delay is retained separately. Frozen evidence: `first-failure/v68`.

CP41 is now installed. Read-only physical readback received the complete
1,114,112-byte partition, verified every active protected block CRC and matched
both active raw/protected bytes. Raw SHA256:
`298f5864684b77e3b2eb98baafee9e5664d8c16d9aae1bfdc5259c0825b5700f`.
Physical partition SHA256:
`78c2416f6519037e8afa86de36d051581c374538506faefe768c722a22becdd9`.
This caused a documented warm restart, not cold-power qualification.

AP69 `77a45a2d…` is being installed for 20:49:05–25 with real CP WFI enabled,
normal modules/domains and HF shutdown. CP41 defers the native hardware mask
until final WFI admission; rejected entries leave it untouched. Require actual
CP residency/tick evidence and full restoration, not merely a WFI attempt count.
No real CP sleep or energy savings claimed yet. Display fixed, overnight deferred.


## Normal domain/isolation setup passes — 1 November, 20:39 UTC

AP67 `13104c7e…` on unchanged CP40 completed the 20:38 control: 66 requests,
64 actual AP WFI returns, two safe aborts, 10 CP clock/restore cycles and
58 ready samples, with no contract/deadline/retention fault or reset. Normal
module invalidation and domain/bus-isolation setup were enabled; HF clocks
were retained and CP WFI skipped. All completion/endpoint queries arrived.
Frozen evidence: `first-failure/v67`. User confirmed updating dim Nadir/BT.

Preserve separate startup radio evidence: the phone reported GATT failure before
any sleep attempt; one documented cycle was performed at 20:35. UART verified
bonded encryption at 20:36:42 and protocol recovery at 20:36:42.868 before the
trial. This control does not qualify startup stability or real CP sleep.

UART19/phone18 closed cleanly after freezing the trial. UART20/phone19 started
at 20:38:50; actual bytes from a documented reception-check cycle were received
before AP68 installation. Bounded expiry about 21:19 UTC. AP68 `22b2aeb7…`
now tests HF shutdown with CP WFI still skipped at 20:43:05–25. Real-WFI AP69
`77a45a2d…` is built/audited but uninstalled, pending the HF control and CP41
installation/readback. Display fixed; no energy measurement; overnight deferred.


## Actual AP WFI control passes — 1 November, 20:34 UTC

AP66 `d1e142c4…` on CP40 completed 66 requests, 64 actual AP WFI returns,
two safe aborts and 13 CP clock/restore cycles with 79 ready samples. No
contract/deadline/retention fault or reset; completion and endpoint queries
were received. Existing PFS and bonded/encrypted protocol startup were received;
the operator confirmed updating dim Nadir and Bluetooth before the 20:33 slot.
Evidence is frozen in `first-failure/v66`. CP WFI was deliberately zero.

AP67 `13104c7e…` now tests normal domain/bus-isolation setup with HF clocks kept
on and CP WFI skipped at 20:38:05–25. AP68 `22b2aeb7…` is built, not installed,
for the subsequent HF-shutdown clock control. Both SRAM closure audits pass;
resources remain identical. Actual CP WFI will use CP41's deferred final mask
after these controls. No deep-sleep, energy or overnight qualification yet.


## Normal module restore passes; actual AP WFI next — 1 November, 20:24 UTC

AP65 `2c19915d…` on unchanged CP40 completed its 20:23 control: 67 requests,
64 AP polling returns, three safe aborts, 23 CP clock/restore cycles, 104 ready
samples and no contract/deadline/retention fault or reset. Normal module-state
invalidation was enabled; the hardware-mask bypass remained the only CP fix.
The operator confirmed updating dim Nadir and Bluetooth before the trial.
Completion and all endpoint queries were received; evidence is frozen in
`first-failure/v65`. CP WFI was deliberately zero. This qualifies the bounded
normal-module clock control, not retained sleep or energy savings.

AP66 builds the same CP40 control with actual AP WFI replacing polling for
20:33:05–25. HF retention and domain bypass remain fixed for this comparison.
CP41 is separately built, host-tested and packaged, not installed: native entry
now defers the CPU0 hardware mask until final real-WFI admission, after lead,
IRQ, SysTick and pre-existing-mask checks. Aborted entry never changes it;
post-WFI HAL restoration retains the saved-mask contract. Native tests pass
35 CP / 24 AP cases; ITCM/flash closure and package replay pass. CP41 raw SHA256
`298f5864684b77e3b2eb98baafee9e5664d8c16d9aae1bfdc5259c0825b5700f`.
Real CP WFI, normal domain/HF transitions, reconnect/cold-power qualification
and energy remain pending. Display fixed; overnight remains deferred.


## Hardware-mask bypass control passes; CP bytes verified — 1 November, 20:14 UTC

AP64 `f184ca3f…` on CP40 completed the 20:11 bounded polling control:
66 requests, 64 AP returns, two safe aborts, nine observed CP clock cycles,
37 CP readiness samples, no contract/deadline/retention fault and no reset.
All completion and endpoint PM queries were received. The operator confirmed
updating dim Nadir and BT before the trial; encrypted protocol recovery was
received at 20:05:16 after the documented controlled phone cycle. Evidence is
frozen in `first-failure/v64`. CP WFI was deliberately zero, so this is a
positive no-WFI clock/restore control, not deep-sleep or energy qualification.

Compared with CP39/AP63's channel-2 reset, CP40 changes the hardware-mask
transition only for explicit no-WFI controls. The passing full-HAL result
strongly localizes that failure to the transition warned about by the vendor.
Do not generalize this yet to real-WFI entry or an aborted real-sleep attempt.
The next real-sleep contract must avoid asserting the hardware mask before a
late/pending-IRQ rejection can return to ordinary WFI.

Read-only physical CP readback at 20:11:52 received all 1,114,112 partition
bytes; active protected bytes, decoded raw bytes and all active block CRCs
match frozen CP40 (`61329a63…`). This programmer operation caused a recorded
warm restart: PFS `new=0` at 20:12:08, startup Bluetooth retries at 20:12:46
and 20:12:54, then encrypted/protocol recovery by 20:13:14. Keep this startup
sequence separate from the trial and retain it as unresolved radio evidence.
No cold-power or overnight qualification is implied.

AP65 is building the same quiet 64-return control on unchanged CP40 with normal
module-state invalidation restored (operation 42 rather than 43), for
20:23:05–25. The mask guard remains unchanged. Proceed incrementally through
normal restoration, actual AP WFI and real CP WFI/clock shutdown. Display fixed;
no measured saving or overnight soak. UART19/phone18 bounded to about 20:41 UTC.


## Quiet module-state control still resets; mask isolation next — 1 November, 20:03 UTC

AP63 `28e25e73…` on CP39 had verified bonded/encrypted protocol recovery at
19:55:32 after one controlled phone cycle. Quiet 64-return/128-request trial
reached CP HAL, restoration, ready revocation and resume queue at 19:59:06,
then reset at 19:59:12 with AP `0x52`/CP `0x10` (PM channel 2). The twelve-frame
snapshot received UART config `0x371b` (TX enabled) and FIFO `0x180051`
(write-ready), CP mask zero, original AP routes and cleared coordination.
Module-state preservation did not fix the post-HAL reset. UART observations
apply at the snapshot, not to every later instruction. Frozen: `first-failure/v63`.
No CP WFI or retained-sleep qualification.

Exact normal AP rollback received boot and existing PFS at 20:00:21.
CP40 keeps ABI21 and bypasses the CPU0 hardware interrupt-mask transition only
for explicit no-WFI clock controls. Non-native and real-WFI masking remain
unchanged. This isolates the vendor HAL's documented normal-WFI wake warning;
it is not a proven fix. Native tests pass (34 CP/24 AP cases), recursive ITCM
and flash closure audits pass, and package replay passes. Linked assembly proves
the mask setter is skipped when the helper returns false. Raw CP40 hash:
`61329a63f621fd6bc9b21cad7be8abcf206e7e981be0ba3e1912e7db49f30361`.
CP40 is installed under the unchanged normal AP; startup recovery is pending
review. AP64 builds the same quiet/module-state control for 20:11:05–25.

A separate possible admission race remains: native CP readiness returns false
immediately before AP publishes WFI, while the pinned vendor path waits up to
3 ms for publication. Falling back to normal WFI before publication can miss
the opportunity until alarm expiry. Do not conflate that with the observed
post-HAL channel-2 failure; preserve both admission and full-cycle evidence.

UART18/phone17 closed before UART19/phone18 started at 20:01:15. Actual bytes
from both new captures were verified before CP40 installation; bounded expiry
about 20:41 UTC. Display fixed, no energy capture or saving; overnight deferred.


## Quiet extended-budget CP admission repeat — 1 November, 19:54 UTC

AP61 `0469ca85…` completed twelve AP polling returns and matching-event cleanup
without reset, but CP samples, preparation and clock cycles all remained zero.
Endpoint contract fault 1 correctly rejects this result. Frozen `first-failure/v61`
is inconclusive for module-state invalidation and real sleep. The operator
confirmed dim Nadir/BT before the slot; no new encrypted/protocol recovery was
received in that phase. A controlled phone cycle after the slot did not produce
fresh watch UART data. Exact normal AP rollback then received boot, existing PFS
and fresh UART at 19:52:50. No resources/PFS were written.

Per-entry cleanup logs have been removed because they generate mailbox traffic
between sleep opportunities. AP62 was built but never flashed; superseded by
AP63 `28e25e73…`, with explicit Kconfig budgets of 64 AP returns and 128 requests.
Default budgets remain 12/24. The 20-second absolute slot and CP-cycle requirement
are unchanged. Nine mailbox/coordinator test methods pass, including 64 exact-once
tick returns; AP63 SRAM closure/map/resources pass. AP63 is being installed on
unchanged CP39 for 19:59:05–25. This is not a module-state or retained-sleep pass.

Pinned CP HAL normal-sleep code explicitly warns that calling the CP interrupt
mask setter can prevent subsequent normal-sleep wake. Current snapshots prove
mask bit zero after restoration but do not rule out this warning. If the quiet
control reaches HAL and still fails, isolate that transition next. CP entry
also depends on AP publication arriving before CP falls back to normal WFI;
a zero-cycle result must be treated as missing admission evidence, not a pass.
Display unchanged, no energy measured; overnight deferred. UART18/phone17 bounded
to about 20:15 UTC, with actual reception verified; refresh before expiry.


## Module-state invalidation isolation — 1 November, 19:37 UTC

AP60 `b4c673ef…` on CP38 reset at 19:31:11 with AP `0x5e`/CP `0x10`.
UART confirmed bonded/encrypted protocol recovery at 19:31:02 before the slot;
operator confirmed updating dim Nadir and Bluetooth. CP reached full-HAL
restoration, ready revocation and resume queueing. Frozen: `first-failure/v60`.
No saved terminal metadata appeared after reset; SRAM-record survival across
whole-chip reset remains unqualified. The mailbox is marked faulted before
terminal logging, so recursive logging is not established as the reset cause.
Channel 14 can instead identify an ordinary log transaction that stalled first.

Exact normal AP restored existing PFS at 19:32:43 and encrypted protocol service
at 19:32:59. CP39 ABI21 (`0x5042414c`) adds explicit operation 43 for preserving
module restore flags only in a no-WFI/no-domain-shutdown control. Ordinary
sleep still marks modules state-lost. This isolates the unconditional full-HAL
module invalidation skipped by the passing core/BSP control. Final CP snapshots
replace CPU-status/IRQ-status duplicates with UART configuration/FIFO status;
frame count remains twelve. Native tests pass (33 CP and 24 AP cases), as do
recursive ITCM/flash closure, formatting, lint and package replay validation.
CP39 raw hash: `b57b3d6cb0b8c8ed228b754a88d4e5b5217d276d77b91f55825b32a6a7ca4cf3`.
CP39 is being installed under the unchanged normal AP; AP61 builds for
19:45:05–25. No real CP WFI or measured energy saving is qualified.

UART17/phone16 were closed before starting UART18/phone17 at 19:34:53;
actual new bytes were verified from both captures. They are bounded to about
20:15 UTC. Display configuration unchanged; overnight remains deferred.


## Retained failure metadata replaces recursive logging — 1 November, 19:25 UTC

AP59 `996f8df4…` received four AP polling returns with matching native events
and system-task cleanup before a CP full-HAL cycle. It reset at 19:17:11 with
AP `0x5e`/CP `0x10`; evidence is frozen in `first-failure/v59`. Channel 14 is
CP logging. Terminal diagnostic logging itself uses this mailbox, so this
reason cannot identify the original blocked transaction. Earlier channel-2
failures remain unresolved. These polling returns do not qualify CP WFI.

The bounded-test terminal path now saves channel/state/sequence/header,
scalar payload, age, FIFO/control, routes and NVIC state in checksummed retained
SRAM, then resets without logging. The next boot consumes and reports that
record. Native regression verifies no logging before reset, original channel-2
reason preservation, one-time reporting and corrupted-record rejection.
Eight mailbox/coordinator test methods pass. AP60 is building for 19:31:05–25
on unchanged CP38, using the same polling/domain-bypass control.

Exact normal AP rollback received existing PFS at 19:21:35 and encrypted
protocol recovery at 19:21:51. UART17/phone16 actual reception is verified;
their bounded captures expire around 19:41 UTC. No actual CP sleep or energy
saving is qualified. Display unchanged; overnight deferred.


## Diagnostic assertion isolated; deferred metadata repair — 1 November, 19:11 UTC

AP58 emitted AP return generation 2/stage 3/one entry at 19:07:05, then reset
at 19:07:06 with AP `0x71` and Assert LR 0, rather than mailbox `0x52`.
No CP stage/snapshot was received for that attempt. Added idle-context logging
is the diagnostic regression; freeze `first-failure/v58` separately and do not
count it as a retained-sleep or PM-timeout qualification.

Return/event diagnostics now run only from the existing system-task cleanup
callback. The AP controller-event timeout also records the fault and resets
without invoking the logger from idle. Terminal mailbox failure metadata stays
in mailbox task context. The native harness now rejects logging during idle
service; eleven test methods pass, including 47 coordinator cases, 23 AP alarm
cases, ownership/clock gates and mailbox failure regression. Formatting, lint
and whitespace checks pass. AP59 is building for 19:17:05–19:17:25 on CP38.

Normal AP rollback succeeded after preserving evidence. UART received build
`2cc3478b…` and existing PFS `new=0` at 19:08:54; Bluetooth recovery review is
pending. No full retained-sleep pass or measured saving. Display remains fixed;
overnight deferred. UART17/phone16 bounded captures expire around 19:41 UTC.

## Resume barrier still resets; terminal transaction diagnostics — 1 November, 19:04 UTC

AP57 `7c8de411…` on unchanged CP38 reset at 18:55:11 with AP `0x52`/CP `0x10`.
The sampled CP state remained restored. There is no received evidence yet
showing whether the AP event barrier ran or which PM operation timed out; do
not attribute this reset to a particular cleanup call. Frozen: `first-failure/v57`.

AP58 adds bounded-test-only AP-return/event-acceptance logs and terminal
mailbox timeout metadata (channel/state/sequence/header, scalar command payload,
transaction age, FIFO/control, IRQ routes and NVIC pending). These are normal
AP-context logs after restored hardware; no SRAM-helper logging or CP requests.
Nine native test methods, C formatting, lint, whitespace and SRAM audit pass.
AP58 `5ad73ad1…` is being installed for 19:07:05–19:07:25; CP38 unchanged.

Two normal rollback attempts failed with serial protocol errors before any
erase/write; both logs are retained. The operator reconnected PC-side debug
USB and reported normal Nadir/BT. UART17 received actual bytes after a controlled
Bluetooth cycle; normal rollback then succeeded, receiving PFS at 19:02:49 and
encrypted protocol at 19:03:04. UART17/phone16 are bounded to about 19:41 UTC;
UART17 actual reception is verified, phone capture must be rechecked before
claiming ongoing phone monitoring. Display unchanged, no energy measurement
or complete retained-sleep pass; overnight remains deferred.

## Native resume barrier implemented; hardware pending — 1 November, 18:49 UTC

CP38/AP56 removed the legacy wake FIFO entry (mailbox IRQ zero, FIFO empty),
but AP still reset at 18:41:11 with AP `0x52`/CP `0x10`. That candidate did not
fix the PM mailbox timeout. Full evidence is frozen in `first-failure/v56`.
Normal AP restored PFS at 18:43:33 and encrypted protocol at 18:43:52.

AP now distinguishes restored hardware clocks from a responsive CP task:
after a real helper return, wait for a valid matching-generation deferred
resume/alarm event before issuing PM cleanup. Wake the mailbox owner once to
drain any frame queued while AP IRQs were masked. The wait is bounded to one
second using wrap-safe AON accounting; timeout faults and resets the complete
chip without attempting a PM transaction. Pre-entry abort cleanup remains
unchanged. The barrier is serviced even after the absolute test slot expires.

Native tests pass: nine methods, 47 coordinator cases and 23 AP alarm cases,
including stale/corrupt/cancelled events, delayed delivery, exact timeout,
counter wrap and slot-end cleanup; existing CP/SRAM ownership cases also pass.
C formatting, Python lint and whitespace checks pass. AP57 is building for
18:55:05–18:55:25 on unchanged CP38. This is an unqualified sequencing repair;
full CP WFI, retained sleep and measured savings remain unqualified.

## Native-only resume candidate installed — 1 November, 18:38 UTC

CP38 raw `850f592b…`, protected `17e9459f…`, passed recursive ITCM and
flash-transition audits, package replay and exact vendor replacement-count
checks. Linked assembly confirms the legacy wake call is skipped only when
native preparation is true. CP38 booted the normal AP/PFS at 18:35:32 and
recovered bonded/encrypted protocol at 18:36:04. There is no physical CP-byte
readback qualification in this continuation.

Matching polling AP56 `8db5fe32…` is installed; received PFS `new=0` at
18:37:04, encrypted protocol at 18:37:20. SRAM audit passes. Slot is
18:41:05–18:41:25. Fixed-display operator confirmation/result remain pending.
This is a warm transition after the cold-preceded CP37 failure; a successful
result must be repeated from cold before qualification. CP WFI remains skipped
and HF retained. No full retained-sleep pass or measured saving is claimed.

## Cold-preceded polling repeat confirms mailbox timeout — 1 November, 18:34 UTC

AP55 `07d21efa…` on CP37 had received encrypted protocol and operator-confirmed
fixed Nadir/BT. At 18:31:05 CP control was `0x02000010` (interrupt mask clear),
IRQ-high enable `0x80400380`, pending zero. AP publication cleared and original
routes restored (`0x80800000`). Reset at 18:31:11 again records AP `0x52`,
CP `0x10`: mailbox channel-index 2 timeout. Frozen evidence: `first-failure/v55`.
This confirms AP SRAM return progress and rules out a stuck CP mask or absent
CP high IRQ enable at the sampled boundary, not later interrupt delivery.
Normal AP restored after evidence preservation.

CP38 is a narrowly scoped candidate: suppress the full HAL's legacy Tuya wake
message only while a native contract is prepared. Keep the native resume event
after resource restoration; preserve the non-native vendor wake path. The
positive core/BSP-only controls already bypass this legacy HAL wake message.
The legacy/native collision is a hypothesis, not an established root cause.
CP38 and matching polling AP56 are building for 18:41:05–18:41:25; audits,
package validation and hardware results remain pending. CP WFI remains skipped,
HF retained, display fixed. No full sleep pass or measured saving is claimed.

## Cold CP37 baseline verified; AP55 installed — 1 November, 18:25 UTC

The operator completed the requested cold power removal. UART15 attachment 3
received normal AP `2cc3478b…`, existing PFS `new=0`, bonded encryption at
uptime 44.075 seconds and protocol service at 45.329 seconds. AP54 expired
unflashed. Renewed UART16 and phone15 captures have both received actual data;
they are bounded to approximately 19:03 UTC. A separately recorded single
Honor Bluetooth cycle on the normal AP recovered encryption at 18:23:59 and
protocol at 18:24:00; this is functional evidence, not an energy measurement.

AP55 `07d21efa…` is installed for 18:31:05–18:31:25. It passes linked SRAM
audit, including absence of AP WFI from the polling helper. CP37 is unchanged;
startup review and bounded CP interrupt/ACK diagnostic result remain pending.
Full sleep, reconnect qualification and measured savings remain unqualified.

## CP interrupt diagnostic installed; cold control pending — 1 November, 18:19 UTC

CP37 raw `793c6f9d…`, protected `8dde9b6d…`, passed native tests, recursive
ITCM and flash-transition audits, and package replay validation. It is installed
with exact normal AP `2cc3478b…`; UART received PFS `new=0` at 18:18:05.
This is tool CRC/package validation, not independent physical CP-byte readback.
The only CP36-to-CP37 source change selects three CP control/IRQ snapshot fields;
no sleep behavior or ABI change. Frozen source and pins are in `v37-*` artifacts.

AP54 `3bdea732…` passes SRAM audit with AP WFI absent. Slot remains
18:23:05–18:23:25. The operator has been asked for full watch power removal;
do not flash during that pending physical action. Verify fresh boot/PFS and
encrypted protocol recovery afterward, rebuilding the slot if necessary.
UART15/phone14 have received data and expire around 18:26 UTC. Start new
bounded captures before any later test, verifying actual received bytes.

## Polling control reaches AP restore; mailbox timeout identified — 1 November, 18:16 UTC

AP53 `e4c4e645…` had several startup connection failures, then encrypted
protocol recovery at 18:09:20 and operator-confirmed dim Nadir/BT. Before its
18:10:05 trial, no sleep entry had occurred; startup failures are separately
frozen in `first-failure/ap53-startup`.

The linked polling helper contains no WFI. CP snapshot at 18:10:06 shows
CPU status `0x7420`, AP publication cleared (`R3=0`), and restored AP routes
`0x80800000`. Thus AP progressed past its SRAM restoration gate. Reset at
18:10:11 records AP reason `0x52`, CP `0x10`: the AP deliberately reset for
mailbox logical channel-index 2 timeout, not the sleep resume watchdog (`0x4f`).
This is a distinct failure from AP WFI wake loss. Full evidence and post-reset
reason are frozen in `first-failure/v53`; no complete sleep pass is claimed.
Normal AP restored PFS at 18:11:58 and encrypted protocol at 18:14:43.

CP37 changes only three fields of the existing twelve-frame read-only snapshot
to capture CP interrupt mask/control, high IRQ routes and high pending status.
ABI remains v20. Native alarm tests pass (four methods). Matching AP54 repeats
active wait for 18:23:05–18:23:25. Builds/audits/package and hardware results
remain pending. Display is fixed; no energy measurements, full CP WFI or
Bluetooth/overnight qualification are claimed.

## Domain bypass failed; AP execution isolation — 1 November, 18:04 UTC

AP52 `c8c5429d…` booted with existing PFS and bonded/encrypted protocol
recovery, and the operator confirmed dim Nadir/BT before the 18:01:05 slot.
CP reached restore/veto/revoke/resume markers at 18:01:06. Domain bypass was
observed (`SleepConfig=0x200f02c0`); original condition bits were zero and
restored value was exactly `0x200002c0`. Mailbox interrupt output was `2`,
AP routes `0x80400000`, AP pending zero, FIFO contained one message. AP reset
at 18:01:10. Full first failure is frozen in `first-failure/v52`. This rules
out domain/isolation setup alone as the explanation; the complete wake cause
remains unresolved. Exact normal AP was restored and reopened PFS at 18:02:10.

A new default-off AP active-wait diagnostic publishes the coordination flag
but polls from SRAM instead of executing AP WFI. It depends on domain bypass,
which skips CP WFI and retains HF. All restore, timeout, canary, tick and
cleanup checks remain. Native retained tests pass (five methods, including
15 actual SRAM gate cases); lint and whitespace checks pass. AP53 is building
for 18:10:05–18:10:25. A polling return is diagnostic evidence only, never a
sleep or energy-saving result; require observed CP clock cycles for inference.
CP36 is unchanged. Full sleep, measured savings and overnight remain unqualified.

## Cold baseline received; bounded slot repeated — 1 November, 17:56 UTC

The operator completed full watch power removal for at least ten seconds.
UART15 attachment 2 received the normal AP build, PFS `new=0`, encryption
and protocol recovery. Startup also included a discovery error/disconnection
followed by recovery; preserve `cp36-cold-reconnect-reviewed.json` rather than
classifying this as clean Bluetooth qualification.

AP51 was flashed after that recovery, but its startup settling period extended
past the 17:55:25 slot end. Received report confirms zero requests/preparations/
entries. This is an expired control, not a sleep failure. The identical domain/
isolation control AP52 is building for 18:01:05–18:01:25. CP36 remains installed.
No measured saving or complete retained-sleep pass is claimed.

## Clock bypass ruled out; domain/isolation bisection — 1 November, 17:50 UTC

CP35/AP50 `10dfd22d…` had verified PFS/encrypted startup and fixed Nadir/BT.
At 17:41:06 mailbox HCLK bypass was physically confirmed (`global control 7`),
mailbox IRQ output asserted (`2`), AP routes `0x80400000`, AP pending zero,
and exact condition restoration `0x200002c0`. AP reset at 17:41:10. Failure
is frozen in `deep-sleep-review-20261101/first-failure/v50`. Thus mailbox
automatic HCLK gating is not the fix. The unsuccessful lease is removed
from production source; its complete CP35 source/package/tests remain frozen.

CP36, ABI v20 (`0x5042414b`), adds an explicit default-off bounded control
that skips the HAL power-domain, ROM/subdomain and bus-isolation setup but
preserves register backup and balanced restore, keeping the voltage/analog,
core/flash clock and resource callback path. It skips CP WFI and keeps HF on.
This bisects the failing full path; it is not a power-saving implementation.
Native tests pass (32 CP/21 AP cases); recursive ITCM/flash audits pass.
Linked assembly confirms the volatile register backup precedes the bypass
branch. Matching AP51 `00c119bc…` passes SRAM audit. No common/display code
changed. Full CP WFI and measured savings remain unqualified; overnight deferred.

CP36 raw `8e4350ca…`, protected `001602f1…`, is installed with exact normal
AP `2cc3478b…`; received PFS `new=0` at 17:49:36. The operator has been asked
for full watch power removal for at least ten seconds to avoid carrying
always-on configuration from the failed path into the bypass comparison.
AP51 is unflashed, with a 17:55:05–17:55:25 slot. Do not flash during the
requested physical unplug; if the slot expires, rebuild for a future slot.
Verify fresh boot/PFS and encrypted recovery after cold power before the
bounded trial. UART15 and phone14 captures received actual data and are
bounded to about 18:26 UTC; inspect attachment and received-byte timestamps
before any monitoring claim or subsequent flash.

## Asserted mailbox IRQ isolated from AP wake — 1 November, 17:32 UTC

After the operator's cold reconnect, UART14 received bonded/encrypted protocol
recovery (earliest boot/PFS were missed because UART13 had expired). Operator
confirmed dim Nadir and BT. One automated Honor UI off/on control on normal
AP recovered encryption at 17:20:52 and protocol at 17:20:53. Evidence is in
`deep-sleep-review-20261101/cp34-cold-reconnect-result`; this does not qualify
retained sleep or establish the cause of previous startup connection failures.

AP48 expired unflashed. AP49 `f4f09b99…` on CP34 received PFS `new=0` at
17:22:31 and bonded/encrypted protocol at 17:23:36; operator confirmed fixed
Nadir/BT. At 17:27:05 CP reached all final markers. The mailbox's own global
channel-1 IRQ status was asserted (`2`) and FIFO held one message, but AP
system pending was zero with routes `0x80400000`. Mailbox global control was
`5` (automatic HCLK gating); AP control `0x02120031`, CPU status `0x7422`.
Original sleep-condition bits were zero and restored configuration was
exactly `0x200002c0`. AP reset at 17:27:10. First failure is frozen in
`first-failure/v49`. Thus condition restoration is verified for this entry,
and wake loss lies after the mailbox's interrupt output; exact cause remains
unproven. Normal AP restored PFS at 17:29:16; post-rollback BT review pending.

CP35 (ABI v19, `0x5042414a`) adds a board-owned mailbox HCLK bypass lease:
acquire before publishing readiness, retain through CP restore and AP wake,
release only on cleanup after AP publication clears. It saves/restores only
the bypass bit, preserves unrelated controls, and rejects premature release.
Native tests pass (33 CP/19 AP cases), including pre-enabled bypass, failed
arm, ordinary alarm and premature cancel. This is an unqualified candidate;
linked audits/package/hardware checks remain pending. Matching AP50 is
building for 17:41:05–17:41:25, with CP WFI skipped and HF kept on as before.
No display change, full CP WFI pass or measured saving is claimed. Overnight
remains deferred. UART14/phone13 have received actual data; captures are
bounded until about 17:58 UTC and must be rechecked on resumption.

## Current checkpoint — 1 November, 16:29 UTC

Retained sleep is still unqualified. CP33/AP47 `7b213a37…` preserved both AP
RTC/mailbox routes (`0x80400000`), reached all final CP markers, and reset at
16:17:12. AP system pending remained zero with one mailbox FIFO entry. Thus
neither corrected sleep-condition ownership nor the added AP RTC route has
resolved the failure. The first failure is frozen in
`deep-sleep-review-20261101/first-failure/v47`. AP47 had no received encrypted
startup recovery, so it is not a connected-Bluetooth qualification.

The exact normal AP `2cc3478b…` was restored (PFS `new=0` at 16:19:41).
CP34, ABI v18, raw `98478163…`, protected `5e743a65…`, is now installed with
that normal AP: received boot/PFS at 16:24:16 and advertising enable status
zero at 16:24:42, but encrypted/protocol recovery remains unverified. Honor
records connection failures. Preserve `first-failure/cp34-normal-startup`
separately from sleep-entry failures; do not infer the radio cause from these
observations. The operator has been asked for full watch power removal for
at least ten seconds and automatic Nadir/BT recovery. Flashing is held until
that action is reported, avoiding overlap with physical disconnection.

CP34 changes only the bounded read-only diagnostic payload: global mailbox
IRQ/control, original sleep-condition bits, restored sleep configuration,
CPU/AP state and final mailbox FIFO. Twelve frames/five markers remain
bounded to 116 bytes. Four native test methods (31 CP/19 AP cases), linked
ITCM/flash audits and package replay validation pass. The AP RTC-route SRAM
helper passes eight ownership/cleanup cases plus the existing restore-gate
and coordinator suite (five test methods); linked AP SRAM audits pass after
replacing an outlined flash CMSIS wrapper with NVIC write-one aliases.
AP46 was not flashed. AP48 `cfc985c9…`, built for 16:33:05–16:33:25, is
unflashed; if that slot expires, rebuild with a future slot rather than
bypassing expiry or the failed-sleep latch. No full CP WFI pass or measured
saving is claimed. Display work and overnight qualification remain deferred.

UART13 and Honor phone12 captures received actual data and are bounded to
about 17:03 UTC. Recheck host processes, attachment and received bytes on
resumption; a process or device node alone is not monitoring evidence.
Next: receive the cold-power result, verify fresh PFS/encrypted recovery,
then run the global-mailbox/condition diagnostic with a valid future slot.
If its condition restore is exact but mailbox IRQ output is asserted without
AP system pending, isolate interrupt/clock gating; if the output itself is
absent, isolate mailbox/global power configuration. Do not promote either
experimental change or proceed to full CP WFI/HF shutdown until the surrounding
return contract passes. Reconnect, cold-boot and deferred overnight tests
remain required after a working transition.

## AON RTC AP wake-route control — 1 November, 16:12 UTC

CP33/AP45 applied the corrected sleep prerequisite value `0x240f82c0`, reached
all five final markers, and still reset at 16:03:11. AP mailbox FIFO contained
one message with its channel interrupt enabled; AP system pending remained
zero. The condition-ownership correction is insufficient to fix wake delivery.
First failure is frozen in `deep-sleep-review-20261101/first-failure/v45`.
A separate pre-slot Bluetooth timeout/recovery at 15:59:49–16:00:03 is frozen
in `first-failure/v45-startup-drop`; it is not attributed to sleep entry.
Normal AP reopened existing PFS at 16:04:35 and encrypted protocol at 16:05:00
on CP33. No complete retained CP sleep or measured saving is qualified.

The next AP borrows the unused AP NVIC/system RTC route while in WFI, alongside
the mailbox route, using the existing CP-owned AON alarm. It does not touch RTC
acknowledgement or CP alarm ownership. Existing enabled/pending AP RTC use is
rejected, and all AP routes/pending state are restored before unmasking. The
clock-restore proof and independent reset guard remain. Host SRAM tests cover
ownership conflicts, aborts, pending races and cleanup. AP46 was never flashed:
its linked audit found an outlined flash CMSIS IRQ-enable wrapper. AP47 uses
NVIC write-one set/clear aliases and is rebuilding for 16:17:05–16:17:25.
Linked SRAM audit and hardware qualification remain pending; display fixed,
overnight deferred.

## Sleep-condition ownership correction — 1 November, 15:56 UTC

CP32/AP44 `1f710876…` reached all final markers at 15:47:05, then reset at
15:47:10. All twelve snapshots were received. AP mailbox FIFO configuration
was unchanged (`7`); FIFO changed from empty (`2`) to one entry (`4`), its
control changed from `0x902` to `0x80902` (nonempty), but AP system IRQ pending
remained zero. Sleep configuration was `0x240882c0`, AON wake `0xdc0002b0`.
First failure is frozen in `deep-sleep-review-20261101/first-failure/v44`.
Normal AP `2cc3478b…` restored existing PFS at 15:51:56 and encrypted protocol
at 15:52:25 on experimental CP32. No full CP sleep pass or saving is claimed.

Source review identified an independent ownership gap: vendor HAL enables the
global sleep condition before taking the power-domain register backup, so its
restore preserves that enable. CP33 (ABI v18, `0x50424149`) saves the original
condition bits before HAL, requires AP/CP WFI plus flash idle while entering,
and restores exactly the owned bits before legacy AP wake, preserving other
power and tick fields. Named masks replace numeric bit combinations. Native
regression (31 CP cases and 19 AP cases) and recursive linked ITCM audits pass.
This is an implemented candidate, not a proven cause or hardware-qualified fix.
AP45 is building for a bounded keep-HF/CP-WFI-skipped control at
16:03:05–16:03:25. Display remains fixed; overnight qualification is deferred.

## Combined sleep-condition/mailbox probe — 1 November, 15:41 UTC

CP30 with normal AP reopened PFS and recovered encrypted protocol at 15:35:43;
a preceding startup disconnect at 15:35:41 is retained separately from sleep
failures. CP31 replaces ambiguous global TX/ACK counters with the captured
low-voltage sleep and AON wake configuration, keeping twelve snapshots and
the same 116-byte bound. Only CP raw diagnostics changed; AP scalar ABI v17
is unchanged. Native tests, recursive SRAM/flash audits and packaging pass.

CP31 `8ed876f3…` (protected `28a2184d…`) is installed with normal AP
`2cc3478b…`, received PFS at 15:39:15 and encrypted protocol at 15:39:40.
AP43 was never flashed; its 15:41 slot was too close for startup settling.
AP44 is building for 15:47:05–15:47:25. The archived BSP-only control reported
sleep configuration `0x200002c0`; the next full-path trace will compare the
hardware prerequisite configuration and mailbox FIFO/control before/after.
No transition fix, CP sleep, measured saving or overnight pass is claimed.

## Drain prerequisite insufficient; hardware mailbox isolation — 1 November, 15:34 UTC

After clean cold recovery, CP29/AP42 reached all final markers and again
reset at 15:29:10. All twelve snapshots were valid: physical PWC busy
`0x00420001`, TX/ACK counters both 712, CP control `0x02000010`, AP mailbox
pending zero and coordination `2`. Therefore the new drain prerequisite did
not resolve wake delivery; do not call it a qualified fix. First failure is
frozen in `deep-sleep-review-20261101/first-failure/v42`. Normal AP `2cc3478b…`
was restored on CP29, reopening existing PFS at 15:31:22 and encrypted protocol
at 15:31:48.

Pinned source confirms IRQ 63 routing. CP30 adds read-only AP mailbox channel
control and FIFO status before resource suspension and after final restore;
receive-ID/data registers are never read because they consume FIFO entries.
The twelve-frame/five-marker burst stays bounded to 116 bytes. Four native
test methods and linked SRAM/flash-transition audits pass; package CP30
`214161bc…` (protected `7e486a11…`) is validated but unflashed. AP43 for
15:41:05–15:41:25 is building. No CP sleep or measured saving is claimed.

## Expiry-before-drain ordering corrected — 1 November, 15:22 UTC

CP28/AP41 reset at 15:17:07 before any HAL marker, then latched AP resume
fault 4. Source review found the new mailbox-busy check preceded invalid or
expired lease handling, so it could leave ready asserted after alarm expiry.
Regression CASE29 reproduced this assertion failure before the fix; its log
is preserved as `deep-sleep-review-20261101/v29-expiry-before-fix.log`.
First hardware failure is frozen in `first-failure/v41`. This ordering bug
belongs to the new drain implementation, not evidence against the original
clock transition. Normal AP rollback reopened PFS at 15:18:49 and restored
encrypted protocol at 15:19:15.

CP29 moves expiry/permit validation and veto/ready cleanup before the drain
wait. All four native test methods, recursive SRAM/flash audits and package
validation pass. CP29 `3e12b823…` (protected `a8c8a3ed…`, ABI v17) was installed with
the normal AP, recovering encrypted protocol at 15:22:01. Operator confirms
full watch power removal for at least ten seconds and automatic Nadir/BT
recovery. USB re-enumerated: UART11 attachment 2 received fresh PFS at uptime
0.902 seconds and encrypted protocol at 27.855 seconds; the earliest build
banner was missed. This clears the failed-sleep-state prerequisite without
bypassing the guard. AP42 `b473650e…` is then installed for 15:29:05–15:29:25,
received existing PFS at 15:25:40 and encrypted protocol at 15:26:09. Operator
confirms dim Nadir/BT fixed. Trial pending; UART11 and Honor phone10 have both
received actual data. No CP sleep, power saving or overnight qualification is claimed.

## Physical mailbox ownership blocks wake — 1 November, 15:11 UTC

AP40/CP27 reset at 15:05:10 after all five final markers and all twelve
snapshots were received. Physical mailbox state `0x00420001` means busy on
PWC channel `0x42`, while the logical PWC buffer is idle. Global TX and ACK
counters are both 774 (not proof that the current transaction was accepted).
CP control `0x02000010` has its system interrupt mask restored; CP IRQ enable
is `0x80400380`, and CP/AP mailbox pending, CP NVIC pending and CP ICSR are
zero. AP publication remains set and CP ready is revoked. Evidence is frozen
in `deep-sleep-review-20261101/first-failure/v40`. Outstanding physical
ownership is now a concrete wake-delivery lead; its origin is not yet proven.

Normal AP `2cc3478b…` was restored on CP27: received existing PFS at 15:06:25
and bonded/encrypted protocol at 15:06:53. CP28 adds a board-owned drain
prerequisite for physical TX and all logical senders before releasing APP
sleep veto, retaining the permit for retry while ACKs drain. ABI is v17
(`0x50424148`). Four native test methods, recursive SRAM and flash-transition
audits, and package validation pass. CP28 `2214a81a…` was installed with the normal AP, recovering existing
PFS and encrypted protocol at 15:11:02. AP41 `87dc00c0…` (raw `39933fe7…`,
protected `bba316ba…`) was then installed for 15:17:05–15:17:25. Received
existing PFS at 15:12:30 and encrypted protocol at 15:12:58; trial pending. No full CP
sleep, measured saving, reconnect qualification or overnight pass is claimed.

## CP27 repeat did not exercise the transition — 1 November, 15:00 UTC

AP39/CP27 completed one AP WFI return with no contract fault or reset, but
reported zero CP ready samples, resource preparations, clock cycles and CP WFI.
The 14:57 slot is inconclusive for the failing transition, not a sleep pass.
Frozen evidence: `deep-sleep-review-20261101/mailbox-v39-result`. Operator
confirms dim Nadir updating and Bluetooth connected. AP39/CP27 remain installed.

AP40 (`c9e66307…`) is installed for 15:05:05–15:05:25 on the same CP27.
Received startup reopened existing PFS at 15:01:04 and recovered encrypted
protocol at 15:01:34. The bounded slot is extended to twenty seconds, still
shorter than thirty-second startup settling so resets cannot re-enter. The
twelve-entry/twenty-four-request limits and independent watchdog guard are
unchanged. All four retained coordinator host tests, SRAM audit, ruff and
whitespace checks pass. Trial remains pending. No measured saving, CP sleep pass or overnight qualification.

## Final-stage trace visibility corrected — 1 November, 14:52 UTC

AP38/CP25 reset at 14:47:11 after raw stages through ready revocation. No
register snapshot or queue marker was received. Their absence is ambiguous:
raw emission depended on AP publication, and the larger nonblocking UART burst
could also run out of FIFO space. This is not proof of a fault in the snapshot
collector. First failure is frozen in `deep-sleep-review-20261101/first-failure/v38`.
Normal AP `2cc3478b…` is restored on CP25 `2faf3c9d…`; existing PFS at 14:48:57
and bonded/encrypted protocol at 14:49:25 were received.

CP26 was built but never flashed. CP27 preserves the claimed shell trace lease
through final handoff while CP PRIMASK remains set, and limits raw output to
five final markers plus twelve read-only snapshots (116 bytes). Native tests
and linked SRAM/flash-transition audits pass. CP27 (`e88bf445…`, protected `d74f111e…`) was installed after UART10
received bytes at 14:53:32. The normal AP recovered encrypted protocol at
14:54:44. AP39 (`7dd49bd4…`, raw `3b6bcffa…`, protected `c13c5a48…`) was then
installed for 14:57:05–14:57:15; existing PFS at 14:55:43 and encrypted protocol
at 14:56:11 were received. The trial is pending. UART10 and Honor phone9 have
both received data. No measured saving or overnight result.

## Wake delivery isolated after final CP handoff — 1 November, 14:40 UTC

AP37/CP24 reset at 14:33:10. Checksum-valid stages prove the complete restore,
APP veto restoration, ready revocation and resume-event queueing. The read-only
snapshot has AP control `0x02120031`, mailbox enable `0x80000000`, mailbox pending
`0`, coordination `2` (AP published, CP ready cleared), core clock `0x00100031`,
memory clock `0x05038570`, CPU status `0x7422` (AP halt status set). CP WFI and
HF shutdown were skipped. This is a wake-delivery/return failure after final
CP handoff, not proof of a stuck callback or failed ready revocation.
First failure is immutable in `deep-sleep-review-20261101/first-failure/v37`.

Normal AP `2cc3478b…` is restored on CP24 `07dc19cf…`: received existing PFS
at 14:34:46 and bonded/encrypted protocol at 14:35:11. CP25 (`2faf3c9d…`, protected `48c6fb93…`) adds read-only CP
IRQ and mailbox physical/logical TX/ACK snapshots to distinguish deferred
mailbox transmission from CP interrupt-state failure. Its native tests and
linked SRAM/flash audits pass. It was installed with the normal AP, recovering
existing PFS and encrypted protocol at 14:40:59. AP38 (`fdd4e714…`) was then
installed for 14:47:05–14:47:15, reopening PFS at 14:42:08 and encrypted protocol
at 14:42:38. The bounded trial is pending. No new power saving, full
CP sleep pass, overnight result or production qualification is claimed.

## Final handoff probe prepared — 1 November, 14:27 UTC

CP24 (`07dc19cf…`, protected `ce19ac91…`) has been installed with the known
normal AP `2cc3478b…`; received boot at 14:26:52 reopened existing PFS
and recovered encrypted protocol at 14:27:20. CP24 adds final APP-veto, ready
revocation and queue stages, plus read-only AP IRQ/clock snapshots. Native
AP/CP tests and recursive ITCM/flash-transition audits pass. The fixed eight-byte
trace encoder uses a pinned-GCC-supported per-function option to avoid a flash
stack-failure handler; other stack protection remains enabled. CP22/23 failed
this linked audit and were never flashed. AP36's 14:23 slot expired unflashed.
AP37 (`35a64953…`, raw `f9b3a608…`, protected `7828c5db…`) was then installed
for 14:33:05–14:33:15. Received startup reopened existing PFS at 14:28:17,
then encrypted protocol at 14:28:45. The operator confirms dim Nadir updating,
Bluetooth connected, USB/no cell and notifications quiet. Trial is pending.

UART9 and Honor phone8 have received actual data. No new sleep completion,
energy saving, reconnect qualification or overnight pass is claimed.

## All restore callbacks returned; final handoff isolation — 1 November, 14:16 UTC

CP21/AP35 reset at 14:05:10 after checksum-valid UART markers proved both
active callbacks (UART1, device 10; mailbox heartbeat, device 23) returned,
and `CP_CallbacksRestored` was reached. This supersedes the earlier suspicion
of a stuck resource callback. CP WFI and HF shutdown were skipped. Final
CP ready revocation, wake notification and AP restored-state proof remain
unresolved. Immutable evidence: `deep-sleep-review-20261101/first-failure/v35`.

Known normal AP `2cc3478b…` was restored on CP21 `1a11e0c4…`: existing PFS
at 14:11:59, bonded/encrypted protocol recovery at 14:12:24. UART9 and Honor
phone8 captures have received data after rotation at 14:13:36. CP22 source
adds final APP-veto/ready/queue stages plus read-only AP clock/IRQ snapshots;
its public final restored status is preserved. Native snapshot encoding and
ownership tests pass; isolated build and AP36 are in progress for
14:23:05–14:23:15. CP22/AP36 are not flashed at this checkpoint.

The Honor UI switch was verified off/on at 13:59:22/13:59:32. Normal AP on
CP20 eventually recovered encrypted protocol at 14:01:25, confirmed by the
operator. This was a delayed recovery with phone intervention, not a timely
or untouched reconnect pass. Direct shell-disable attempts never demonstrated
Bluetooth off and are excluded. No full sleep residency, measured saving,
production qualification or overnight result is claimed; display work deferred.

## Failure localized past clock restoration — 1 November, 13:46 UTC

CP19/AP33 emitted sixteen checksum-valid raw stages through
`CP_IrqRestored` before resetting at 13:41:10. Voltage/HF/BSP/core/flash
restoration completed; CP WFI was skipped. The final main-mask/tick/HAL exit
and resource restoration remain to be localized. Evidence:
`deep-sleep-review-20261101/first-failure/v33`. Do not assign this failure to
entry clocks or claim a completed low-voltage residency.

The normal AP rollback succeeded: `2cc3478b…` reopened existing PFS at
13:45:12; protocol recovery is being checked. CP19 remains installed. CP20
adds later boundary markers only, and AP34 is building for 13:53:05–13:53:15;
neither is flashed at this checkpoint. Current UART8/phone7 captures have both
received actual bytes. CP18/AP32 were never flashed, and their slots expired
unused. No energy measurement, full sleep pass or overnight qualification.

## Normal recovery verified; corrected stage probe building — 1 November, 13:33 UTC

Operator full watch power removal (all watch-side connections, at least ten
seconds) cleared the failed programmer/startup state. The exact known-normal
AP rollback then succeeded: build `2cc3478b…` at 13:31:44 reopened existing
PFS (`new=0`) and recovered bonded/encrypted protocol at 13:32:27. Current CP
remains CP17 `07f9e924…`. This qualifies recovery of this incident, not
coordinated-sleep cold boot. The bootloader loop followed the failed rollback
handshake; it is separate from the earlier 13:24:10 guarded test reset.

CP18 and AP32 were never flashed. CP18's marker admission incorrectly required
UART automatic clock-gating bypass. Source CP19 instead checks actual UART1
system clock enable and reset release, while preserving the shell's balanced
TX suspension/drain ownership. The marker writer never changes UART clocks,
RX or interrupt settings. CP18 passed 38 host tests; corrected CP19 native
ownership tests and isolated build are being checked. AP33 is being built for
13:41:05–13:41:15. No electrical saving or full sleep pass is claimed.

## Keep-HF failure and rollback recovery pending — 1 November, 13:30 UTC

CP16/AP v30 reset at 13:13:10 with CP WFI and oscillator shutdown skipped.
Its normal AP rollback succeeded; existing PFS and encrypted protocol recovered
at 13:17:22. First failure is immutable under `first-failure/v30`.

CP17/AP v31 `38de4aa8…` repeated the reset at 13:24:10. No raw stage markers
were received because the diagnostic targeted unused AP UART0; runtime logs
actually use the CP mailbox and CP UART1. This probe does not locate the
failing stage. CP17 passed package/linked audits and 38 affected host methods
(32 native plus six Kconfig methods after correcting test dependencies).

The following normal-AP rollback failed its download handshake and wrote
nothing. At this checkpoint, UART receives a repeating Tuya bootloader jump
loop, **not normal AP recovery**. Last successful writes remain CP17
`07f9e924…` and AP31 raw `35fc7d84…`. The expired slot cannot arm another
lease if AP starts. Preserve `first-failure/v31` and
`first-failure/v31-rollback-handshake`. Operator full watch power removal is
requested before the next known-normal rollback attempt. Candidate flashes
are held until recovery is verified.

Source CP18 corrects markers to UART1 and uses the shell TX suspend/IRQ-disable
and drain path for a bounded diagnostic ownership lease. It retains physical
TX only for keep-HF non-core-only probes while AP is published; ordinary
suspend/resume remains unchanged. It never changes RX/IRQ/clock configuration
from the stage writer. Corrected native ownership tests pass; isolated build,
package audit and broader host validation are in progress. CP18 and the next
AP32 are not flashed. No full sleep, power saving or overnight pass claimed.

## BSP restore boundary passed — 1 November, 13:10 UTC

Repeated BSP-only AP v29 `a9dbbe3a…` on CP v16 completed 12 AP returns,
three CP resource/core/flash/BSP restore cycles and 14 readiness samples at
13:07:05, with no reset, late wake, clock/timer/canary or contract fault.
The final CP alarm lead was 111 nominal 32 kHz ticks (3.469 ms). CP WFI and
oscillator shutdown were deliberately skipped. Immutable evidence is in
`deep-sleep-review-20261101/bsp-control-v29-result`; the operator confirms
Nadir updating and Bluetooth connected. This supersedes the inconclusive
v28 callback trial, not the earlier full-path failures.

The companion is now the Honor LLD-L31, not the Samsung. ADB is authorized;
phone6 and watch uart7 captures have both received actual data. The operator
confirms dim Nadir/BT with USB/no cell and quiet notifications. Cross-phone
results must not be presented as a matched energy comparison.

Keep-HF AP v30 `e3b1bcce…` is installed on CP v16 (`0ed1b59c…`). It reopened
existing PFS at 13:08:28 and recovered bonded/encrypted protocol service at
13:09:10. Its 13:13:05–13:13:15 bounded slot adds the remaining HAL power and
voltage lifecycle, while retaining AP domains, keeping high-frequency clocks
on and skipping CP WFI. Result pending. No new source change since the 38-test
host pass; AP map/CRC/resource and linked SRAM checks pass. No electrical
measurement, full coordinated-sleep qualification or overnight pass claimed.

## Restore isolation checkpoint — 1 November, 13:01 UTC

Full low-clock control v22 reset at 12:15:11 despite AP BAKP/AHBP retention;
keep-HF control v27 reset at 12:39:10 with CP WFI skipped. Immutable first
failures are retained under `deep-sleep-review-20261101/first-failure/v22`
and `v27`. Domain retention and exact interrupt-mask restoration repair real
ownership gaps but have not resolved the reset.

Core plus ordinary flash-exit control v26 completed 12 AP returns, one abort
and one CP clock/flash callback cycle at 12:32, without a contract fault or
reset. It ran on the original ABI v14 CP before the flash placement repair.
The callback boundary passed; full coordinated sleep has not passed.

Installed CP v16 (ABI v15) is raw `0ed1b59c…`, protected `1814926a…`.
Its flash-mode transition and two-wire helpers now reside in ITCM; quad-mode
configuration/QE setup occurs before flash disconnection. Linked critical-path
inspection and all 38 affected host tests pass. These are implementation and
host-validation results, not measured savings or hardware qualification.
Normal AP `2cc3478b…` reopened existing PFS and recovered encrypted protocol
at 12:49:21 before BSP-restore AP v28 `15fd087b…` was installed.

The 12:52:05–12:52:15 BSP-only trial returned five AP waits safely but recorded
zero CP readiness samples, resource preparations and clock cycles. Its callback
was never reached: result **inconclusive**, not a pass. Evidence is frozen in
`deep-sleep-review-20261101/bsp-control-v28-result`. The expired pilot cannot
start another lease. A second bounded callback trial is being built.

Phone capture ended at 11:57:18; Samsung USB/ADB restoration and a fresh visual
setup confirmation remain pending. UART received later encrypted protocol
recoveries; USB absence does not establish a Bluetooth outage. UART had a
12:27:23–12:29:13 capture gap. The receiver was rotated again at 13:00:21;
its process alone is not proof of received data. No electrical measurement,
full sleep pass, cold-sleep qualification or overnight result is claimed.
Display work remains deferred and no display settings were changed.

## AP-owned domain retention implemented — 1 November, 12:12 UTC

ABI v14 now requires AP SDIO/DMA ownership votes and BAKP powered before a
native sleep lease, and keeps BAKP/AHBP out of the HAL power-down mask. The
existing AP driver context is not discarded. All 37 affected host tests,
package replay, image checks and recursive ITCM helper audit pass. This is an
implemented ownership repair, not a measured or hardware-qualified saving.

Installed CP is `8a427f8b…` (protected `db16fa4e…`). Normal AP `2cc3478b…`
reopened PFS and recovered encrypted protocol at 12:11:38. AP control `db7668c6…`
is being installed for 12:15:05–12:15:15 UTC; result is pending. It runs the full
low-clock path while retaining AP domains and skipping CP WFI.

Phone USB/ADB is absent; phone capture stopped at 11:57:18. Neither failed
reconnect command changed phone Bluetooth. UART later received encrypted
protocol recovery, so disconnected operation must not be inferred from absent
USB alone. The visual setup is not newly confirmed; this is functional UART
evidence only. The 12:04 flash-restore AP `v20` was never flashed and its slot
expired unused. No energy measurement, overnight pass or display change.


## Full clock control still fails; flash/domain isolation — 1 November, 12:01 UTC

Repaired control `18a3f7fb…` / ABI v12 CP `8417cc01…` reset at 11:54:10
with CP WFI skipped. Exact mask restoration did not resolve the failure; the
first trace is preserved under `first-failure/v18`. Prepared full AP `v19` was
not flashed because its prerequisite failed. Known normal AP was restored and
received PFS/encrypted protocol recovery at 11:56:10.

ABI v13 adds only the ordinary flash power-saving exit to the passing core-only
control, preserving power domains, voltage and high-frequency clocks. All 36
host tests, strict package replay and linked helper audits pass. CP `b3ac65ca…`
is installed with normal AP `2cc3478b…`; PFS startup is received. AP control
`1dcd7410…` is built for 12:04:05–12:04:15 UTC, not yet flashed at this checkpoint.
The linked SDK flash exit is in flash despite its SRAM-placement comment.
Separately, the HAL power-down mask includes BAKP despite AP SDIO/DMA votes;
native drivers do not save/restore that whole domain. These are explicit
contract concerns; the failing boundary is still under investigation.
No low-voltage residency, electrical saving or hardware qualification claimed.


## Core clock boundary passed; wake contract repair — 1 November, 11:47 UTC

Core/flash-only AP `ce392802…` / ABI v11 CP `e85e8537…` completed 12 AP
returns, one abort and one observed CP low-clock switch/restore cycle. No CP
WFI, clock/timer/canary fault or late wake was recorded. Final CP clock mode
was low, DPLL remained enabled, final SYS/NVIC pending snapshots were zero,
and CP restored stage was received. Operator confirms Nadir updating/BT connected.
The final alarm lead was 19 AON ticks (about 594 microseconds at nominal 32 kHz).
Immutable evidence: `deep-sleep-review-20261101/core-control-v17-result`.

Normal AP `2cc3478b…` is restored on CP v11, with PFS/encrypted protocol recovery
at 11:45:33. ABI v12 now saves/restores the exact CP main interrupt mask and
rejects final WFI when the alarm is late/too close or an interrupt is pending.
All 36 affected host tests, package replay, AP map/CRC and recursive linked
ITCM/SRAM helper audits pass. CP v12 `8417cc01…` is installed; normal AP
PFS/encrypted protocol recovery was received at 11:50:17. Installed AP is now
control `18a3f7fb…` (raw `8d9adbff…`), with PFS/encrypted protocol recovery
at 11:51:43. Its 11:54:05–11:54:15 UTC result is pending. This
addresses explicit contract gaps; neither gap is yet a proven reset cause.
The next control repeats the full low-clock no-CP-WFI path and verifies mask
readback. No electrical savings or full coordinated-sleep qualification.


## Keep-HF result — 1 November, 11:35 UTC

Keep-HF AP `06bb8ebd…` / ABI v10 CP `526b0fe6…` reset at 11:33:10,
with CP WFI and high-frequency-clock shutdown deliberately skipped. The failing
path therefore includes core/flash clock switching, power-domain/voltage setup
or restoration; oscillator shutdown is not a necessary trigger. First failure
is preserved under `deep-sleep-review-20261101/first-failure/v16`.

A pre-slot app-to-system event queue overflow killed/relaunched watchface
`16183a4f…` at 11:31:24; it is separate evidence (`v16-preslot-app`), not caused
by a sleep entry. The operator reports dim Nadir updating and Bluetooth connected.
The CP v10 full cold-power startup was received with PFS and encrypted protocol
recovery, and automatic Nadir/BT recovery was operator-confirmed. That limited
cold-start observation does not qualify coordinated sleep.

Known normal AP `2cc3478b…` is restored on CP v10; received PFS and encrypted
protocol recovery at 11:35:07. ABI v11 adds an explicit core/flash-only control,
bypassing power-domain/voltage/HF shutdown and CP WFI. All 36 affected host tests, isolated package replay, map/CRC and linked SRAM
helper checks pass. CP v11 `e85e8537…` is installed; normal AP reopened PFS
and recovered encrypted protocol at 11:38:22 before AP control `ce392802…`
(raw `c3100657…`) was installed for 11:42:05–11:42:15 UTC. Its boot is received;
the control result is pending. No electrical savings or retained-sleep pass.


The ABI v10 normal-AP startup also recorded encrypted setup followed by a
connection-parameter/service-discovery failure at 11:27:27, before any sleep
lease. It recovered an encrypted protocol session at 11:28:36. This is preserved
separately under `first-failure/v10-normal-radio`; a clean cold-power startup
check is pending before the next control. No cause is assigned to that drop.


## CP clock-only failure — 1 November, 11:24 UTC

Clock-only AP `beae660b…` / ABI v9 CP `506807a8…` reset at 11:19:10 UTC
following the 11:19:05 slot, with CP WFI deliberately skipped. This excludes
CP WFI as a necessary trigger and narrows the fault to clock/voltage setup or
restoration after resource preparation. The specific failing write/call is not
identified. First-failure evidence is immutable under reference-workspace
`artifacts/deep-sleep-review-20261101/first-failure/v15`.

Known normal AP `2cc3478b…` is restored with ABI v9 CP. Received UART verifies
PFS (`new=0`) and encrypted protocol recovery at 11:20:47; the operator confirms
dim Nadir updating and Bluetooth connected. ABI v10 adds an explicit default-off
keep-HF control to exercise core/flash low-clock setup and restoration while
preserving high-frequency clocks and skipping CP WFI. Native control tests pass;
source builds and hardware trial are pending. No electrical saving or complete
retained-sleep qualification is claimed. Display remains fixed, overnight deferred.


## Clock boundary isolation — 1 November, 11:10 UTC

The full coordinated trial AP `b9c2bdcf…` on ABI v8 CP reset at
10:55:31 UTC after the 10:55:26 slot began. Its first-failure evidence is
preserved under `artifacts/deep-sleep-review-20261101/first-failure/v13` in the
reference workspace. The normal AP `2cc3478b…` was restored and verified, then ABI v9 CP
`506807a8…` was installed with normal-AP PFS/encrypted recovery at 11:12:08.
The installed AP is now clock control `beae660b…` (raw `05dda772…`), with
received PFS (`new=0`) and bonded/encrypted protocol recovery at 11:13:27.
The operator confirms dim Nadir updating and Bluetooth connected. The bounded
slot is 11:19:05–11:19:15 UTC. This supersedes older installed-pair statements.

AP-only deep-WFI and CP resource rollback controls passed, but full CP retained
sleep remains unproved. ABI v9 adds a bounded clock-only control that skips CP
WFI, plus final deadline/clock/wake/interrupt snapshots. Its isolated CP build,
package replay and linked ITCM helper audit pass; hardware trial is pending.
No electrical savings or cold-power/overnight qualification is claimed.
Overnight remains deferred and display configuration stays fixed.


## Sleep boundary controls — 1 November, 10:49 UTC

Installed pair is resource-control AP `0d8e6616c4693c38bdfb38af0ffd426cd0879a34`
(raw `6b9b336d…`) and ABI v8 CP raw SHA
`1dffb4030445374e38019f6e853e2cceca132a6d9b0425acb21457989f2fbae0`
(protected `55ae8bce…`). Its UTC slot is expired. The normal AP was checked on
this CP first: existing PFS (`new=0`) and bonded/encrypted protocol recovery at
10:42:42.799. AP-only writes preserved resources/PFS and display settings.

Two bounded controls now pass on hardware:

- AP deep-WFI wake with CP APP veto held: `f0a7e70e…` / ABI v7 CP `c272678c…`,
  12 AP returns, two aborts, zero timer/clock/canary faults, zero CP low-voltage
  entries. Operator confirmed dim Nadir updating and Bluetooth connected.
- CP preparation/rollback with HAL entry deliberately rejected before clocks:
  `0d8e6616…` / ABI v8 CP `1dffb403…`, 12 AP returns, zero aborts/faults/late
  wakeups, two observed CP preparations, zero CP entries. Last preparation was
  four AON ticks (about 125 microseconds at the board's nominal 32 kHz); HAL rejection was the explicit probe
  bit `0x20`, with no pending IRQ or missing module votes in the final sample.

This narrows the earlier reset to the path after resource preparation; it does
not prove a specific clock/wake failure or CP residency. The v8 coordinated
reset trace and all controls are immutable under
`/home/baz/Documents/ChatGPT/pebble/artifacts/deep-sleep-review-20261101`.
The full coordinated AP is building for 10:55:26–10:55:36 UTC on the unchanged
ABI v8 CP. All 36 affected native tests, package replay/provenance, AP map/CRC and
linked SRAM audits pass for the controls. A full trial result is pending.

Uncommanded pre-slot Bluetooth drops from the earlier ABI v6 run remain recorded
and unqualified. Samsung capture stopped at 10:18:22 during USB absence and was
restarted after actual reattachment; UART capture was rotated with received-byte
verification. No electrical savings, full low-voltage qualification, cold-power
or overnight pass is claimed. Overnight remains deferred; display power is last.

## Bounded review pilot — 1 November, 10:10 UTC

Installed AP is now `a41dcf11b8c05f04aa4849ac1fedf44472cefbce` (raw SHA
`0f63d157172c1d765436e51a782bbdadb4168981fb1096bd0dd1a10960aa5be0`),
with v6 CP raw `8bb57099…`. UART verifies 240 MHz, existing SD PFS (`new=0`)
and encrypted/bonded protocol recovery. Its bounded retained slot is
10:13:12–10:13:22 UTC; results are pending. This supersedes installed-pair
statements below. The first preflight refused stale UART data before writing;
after a deliberate reconnect produced new received bytes, the AP-only write passed.

The preceding `11b27c02…` pilot emitted readiness at 10:03:55 without a received
second boot. A commanded Bluetooth reconnect recovered protocol at 10:04:35;
the operator confirmed Nadir updating and connected. No WFI/residency inference
is possible because that image omitted its summary when the slot expired before
the attempt limit. The new pilot reports once after cleanup even in that case.
It also restricts the PSRAM restore mask to source/divider bits 4/5. The test-only
guard is four seconds; the SRAM restore timeout remains 700 ms. Thirty-six
host tests, image checks and the linked SRAM instruction/literal audit pass.

These are diagnostic changes and bounded functional observations, not hardware
qualification or measured power savings. Cold-power and overnight qualification
remain open; overnight is deferred by the operator. Display power remains last.


## Deep-sleep review in progress — 1 November, 09:50 UTC checkpoint

This supersedes the older installed-pair statements below. The v6 GPIO-preserving
CP (`8bb57099…`) and retained AP `e092edb9…` were installed. Its ten-second UTC
trial slot expired after a received reset at 09:49:47; it cannot automatically
retry on the next boot. The first-failure trace is preserved under
`/home/baz/Documents/ChatGPT/pebble/artifacts/deep-sleep-review-20261101/first-failure`.
A later uncommanded Bluetooth failure/recovery at 09:51 is also retained.

Implemented and host-tested: exact AP interrupt-route masking/restoration,
pending-interrupt aborts, expiring retained-pilot admission, CP preservation of
AP-owned GPIO configuration, and HAL rejection/residency diagnostics. Thirty-six
affected host tests pass; CP provenance/CRC and AP map/CRC/SRAM audits pass.
Earlier pilots returned from AP WFI, but CP low-voltage count remained zero.
The v6 reset has not distinguished lost WFI from the SRAM restore deadline.
A test-only four-second independent guard, with the existing 700 ms restore
deadline unchanged, is being prepared to separate those failure modes.

No successful CP low-voltage residency, electrical savings, final reconnect,
cold-power or overnight qualification is claimed. No resources or PFS were
selected for writing. Display settings remain fixed; display power is last.


## Guard admission repair — 1 November, after 09:09 UTC

Installed AP is now `ed05ff87…`, raw SHA
`b5a62423bc24c25d66c7e35699190576839ea1eb29ece7c59a37c99c5219add7`, with the
unchanged v5 fenced CP `1ed83dfb…`. Its UTC arming slot has expired; retained sleep
remains disabled. This supersedes the installed-image statements below.

The guard now polls admission every 20 ms within the same two-second slot,
records owner/transport blockers on abort, and rechecks the slot at the arm
boundary, including backward clock movement. Expired startup attempts reject
before acquiring the CP lease. Watchdog arm additionally checks readback of the
period, reset mask, AP reason and LPO selector; rejected settings roll back.
Thirty-six affected host tests and both ARM probe/map/CRC/SRAM audits pass.

Received second boots at 09:00:33.007 (`f027538f…`) and 09:08:26.006 (`ed05ff87…`)
follow their respective slots. Their timing is consistent with the independent
guard; the latter image requires accepted register readback before WFI. These
are stronger observations than the earlier cancelled slot, but neither boot
preserved AP reason or the SRAM attempted marker (both zero). Independent reset
attribution/period comparison and reset-surviving evidence remain qualification
gates. No retained low-voltage trial was run.

Final UART verifies 240 MHz, SD initialization, system resources and bonded/
encrypted protocol recovery at 09:08:49.477. After reboot, the expired slot was
rejected before another CP lease, explicitly no WFI. The operator confirmed dim
Nadir running normally and Bluetooth connected after both trials.
No resource/PFS write, electrical measurement, cold-power or overnight test was
performed. Display settings stayed fixed. Evidence and capture closure:
`/home/baz/Documents/ChatGPT/pebble/artifacts/guard-admission-20261101`.

Next: qualify reset source with independent timing/period evidence and establish
a reset-surviving failure record before the next coordinated-sleep pilot. Then
complete timer/clock/retention, reconnect, cold-power and deferred soak checks.
No new measured savings; display remains last.

Implementation scope: `soc/t5ai/sleep_guard_test.c`,
`drivers/watchdog/t5ai.c` and guard native tests only; stock common/vendor code
was preserved. The prior window sampled only once per second; finer polling and
a longer settled idle both changed in these trials, so the precise original
blocker remains unknown. New tests prove transient ownership can clear within
the slot, distinguish physical/logical/HCI readiness, bound continuous busy
retry, reject a late/backward arm-boundary clock, reject expired startup before
CP preparation and restore settings after rejected register writes.

Both probe images retain the APP-awake veto and normal active clocks. The CP
alarm expires without publishing AP WFI or entering low voltage. A reset seen
at the expected time does not independently identify its source; neither the
post-WFI timeout nor the unretained SRAM marker is a complete recovery contract.
First reset bytes, frozen images, host receive timestamps and filtered Android
Bluetooth logs are preserved. No post-flash byte readback was performed.

## Revised guard flashed — 1 November, after 08:49 UTC

Installed AP is now expiring-slot guard probe `1163df17…`, raw SHA
`1b79372fd0e9bdf5312c26b0be7b90502a87d24f4cbe4e0d094e515c30a94bf8`, with
v5 fenced CP raw SHA `1ed83dfb3d6829dee9cf7a07dfbc3dfa3036aa9ab49cbe9e5dca245ff12427fb`.
This supersedes the installed-known-pair statement in the earlier checkpoint.
CP package provenance, AP hashes/CRC/map and SRAM loop checks pass. Five guard
host tests were repeated successfully. CP-only and AP-only writes succeeded;
resources and PFS were not selected for writing. No post-write byte readback was
performed in this run.

The unchanged normal AP first verified existing PFS (`new=0`) and encrypted
protocol recovery on v5 CP. An arming-disabled probe then booted and recovered
Bluetooth. The one-shot probe acquired the CP lease at 08:47:22.597 device UTC,
reporting LPO/control `0x230/0xa50000`. Its 08:47:29–08:47:30 UTC slot expired:
the delayed received diagnostic at 08:47:31.635 explicitly says **no WFI**. The
exact admission blocker or callback delay is not logged. This exercises the abort
path; it does not test watchdog expiry/reset attribution or retained sleep. The
slot has now expired. One deliberately commanded post-slot Bluetooth cycle
recovered bonded 0/1/1 encryption and protocol service at 08:49:26.177. The
operator confirms the visible reboot was the flash restart at about 08:46:52,
consistent with the single received boot banner, rather than a later guard reset.

Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/guard-flash-20261101-0840`.
Initial slot-flash preflight rejected stale UART before any write; a recorded
pre-slot Bluetooth cycle refreshed data before the successful retry. UART and
filtered phone Bluetooth captures received data and were closed after review.
No electrical measurement, cold-power, 25-cycle or overnight qualification was
performed; display settings were held fixed. Retained sleep remains disabled.

Next: expose final admission blockers and arm/register evidence, then qualify a
fresh expiring slot after settled idle before any retained pilot. Exact known
AP/CP rollback remains preserved. No new measured savings.

## Guard and restore-order investigation — 1 November, after 00:20 UTC

This checkpoint supersedes older installed-image, guard and marker claims below.
**Known AP `2cc3478b…` / CP `d4d65559…` are restored.** Final received startup at
00:14:09 device UTC confirms the AP identity and existing SD PFS (`new=0`), then
240 MHz and bonded/encrypted protocol recovery at 00:14:32. CP was never replaced
in this continuation. No resource/PFS/provisioning write was selected. Operator
confirmation of the final visible watchface remains pending. Captures were stopped
at 00:20:38 UTC; this is not overnight monitoring or cold-power qualification.

### Implemented and host-tested; not hardware qualified

- CP now fences ready-bit revocation across **all resource restoration**, not
  just PLL restoration. An alarm expiring after resource preparation could clear
  ready from the final admission check while flash bus mode/callback restoration
  was still pending. AP could then observe unchanged clock registers and return
  to flash too soon. A resource-active fence holds ready through restore; cancel
  during that interval fails closed. Actual CP code tests cover both races.
  This is a concrete ordering defect; attribution of the earlier freeze remains
  a hypothesis until a controlled hardware repeat succeeds.
- AP preserves PRIMASK and fully masks interrupts inside the SRAM transition.
  BASEPRI alone does not exclude every configurable interrupt. Exact saved
  clock/bus checks, tick catch-up, transport drain and owner guards remain.
- CP memory readiness checks the real APP PSRAM owner bit, AHBP_PSRAM controller
  vote and powered LDO, plus the retained-domain configuration. Vendor heap-count
  cleanup does not release the controller reservation while APP owns PSRAM.
  No fake vendor allocation count or other owner's vote is installed.
- The default-off AON watchdog guard preserves its prior period, reset mask and
  reason; keeps the existing DIVD/external selector; and rejects unsupported or
  halted LPO. Isolated CP overlays fence normal AON driver **and direct NMI HAL**
  stop/feed writes during the lease, hold the AON clock request through native
  sleep, and restore its prior halt policy on cancellation. Fatal reset remains
  available. Hardware period units/expiry, ownership and reset attribution still
  require direct qualification; the nominal 1,000 ms API value is not a measured
  one-second deadline.
- AP and CP have separate aligned NOLOAD SRAM stage records. AP distinguishes
  an unfinished entry from completed cleanup and can query the previous CP stage
  without arming another alarm after a failed boot. Linked sections avoid AP
  data/RAM-function copy and BSS zeroing ranges and CP BSS/heap. These properties
  do **not** prove that records survive a whole-chip watchdog reset.

ABI-v5 CP source is built from an untouched pinned SDK archive and structurally
packaged with exact source/link/config/controller/relocation/CRC guards. Final
base link `ab912902…` and `packaged-v5-fenced` remain **unflashed**. Retained AP
`c53f2898…` is also built and **unflashed**. All experiments remain default off.
Final non-armed leased/UTC-slot probe `20c02b86…` and normal all-experiments-off
verification build `f762d429…` pass the ARM image/map/CRC/resource/log/syscall
checks. Both remain unflashed. The latest native suite uses the target's signed
32-bit time representation; 36 methods pass. Source whitespace and Ruff checks pass.
Normal active frequency stays 240 MHz; fixed 120 MHz remains removed. Hardware
fields, periods, stage values and budgets use named constants.

### Failed independent-guard qualification

The hardware probes used the **known CP**, with APP sleep vetoed and no AP-WFI
publication. Initial live readback showed LPO DIVD (`R41=0x230/0x2b0`) and a disabled
AON period (`0xa50000`). An external-only prerequisite rejected entry. A later
analog-enable prerequisite was not valid proof of the DIVD clock and was removed;
no oscillator selector was changed to make a test pass.

Subsequent probe images rebooted, but AP boot reason remained zero. A separate
SRAM attempted marker also reported zero after reboot, with raw live/snapshot
PMU values `3`. Neither reset attribution nor marker survival was established.
The earlier probes did not establish the CP AON-write lease, so they also did not
isolate AON expiry from CP interference/fallback reset. **A reboot is not a guard
qualification pass**, and no new retained low-voltage pilot was run.

Normal recovery initially met a serial download-handshake failure before any
write; the exact retry succeeded. All first-failure bytes, retries and final
recovery are preserved in:
`/home/baz/Documents/ChatGPT/pebble/artifacts/sleep-guard-20261031`.
Older automatic probe images there must not be replayed.

The revised source requires an explicit **two-second UTC arming slot**, default
zero/disabled, a trusted progressing wall clock, bounded settling/drain/wait, and
an applied CP guard-write lease. It obtains the alarm/sleep lease without ever
publishing AP-WFI, so CP retains the APP veto; an alarm wake cannot execute a
reset in the masked SRAM-only WFI loop. Abort paths cancel the lease. The saved
SRAM marker is best-effort evidence, not the retry bound. These latest lease/slot
changes are host-tested, not flashed. Thirty-six affected sanitizer/config test
methods pass, with no skips; failed sandbox LeakSanitizer, dependency/import,
compile and test-fixture attempts remain recorded separately.

### Remaining gates and next concrete actions

1. Qualify the revised leased, UTC-slotted independent guard on the exact v5 CP,
   first with APP held awake. Record actual arm acceptance, programmed registers,
   an independent timing reference and reset source; compare periods to distinguish
   AON expiry from CP fallback. Prove slot expiry prevents retry after reset.
2. Establish reset-surviving first-failure evidence for coordinated sleep; current
   SRAM and boot-cause observations are insufficient. Do not promote the SRAM
   latch or post-WFI timeout as a complete recovery contract.
3. Only then run a bounded v5 retained pilot to test the restore fence, stages,
   PSRAM/peripheral retention, clocks and exactly-once timer/heartbeat behavior.
   Preserve the first failed stage and restore the known pair on failure.
4. The rejected-WFI control also recorded Bluetooth disconnect/discovery failures
   at 23:43–23:49 UTC on known CP, including `0x0208`/`0x0228`. No automated cycles
   ran then; operator activity is still unconfirmed. Preserve this control without
   assigning it to sleep or excluding earlier clock/state carry-over hypotheses.
   Reconnect/cold-power, workload energy and overnight qualification remain open.

No meter capture, new electrical saving or battery-life result is claimed.
Display configuration/power code was not changed in this continuation; display
work remains last and overnight remains deferred by the operator.

## Bounded retained-sleep failure — 31 October, after 23:25 UTC

This checkpoint supersedes older unflashed-candidate and installed-image statements
below. Coordinated sleep remains experimental, default off and **not qualified**.
Known normal AP `2cc3478b…` and known CP `d4d65559…` were restored after a frozen
pilot. Received UART verifies 240 MHz, existing SD PFS (`new=0`), encryption/bond
status `0/1/1` and protocol recovery at 23:25:29 device UTC. The operator confirms
dim Nadir running and Bluetooth connected. These are successful recovery checks,
not a cold boot, overnight soak or physical flash readback of this restoration.
No resource-bank, PFS or provisioning write was selected during these pilots.

Implemented and host-tested in board/SoC/driver-local code:

- Exact saved core/bus, AP/CP speed and flash/PSRAM clock checks before returning
  from SRAM to flash; an audited SRAM-only emergency whole-chip reset helper,
  with a distinct resume-failure boot reason and sleep inhibition after that reset.
- Correct active-low CPU2 reset interpretation, plus actual power-down ACK.
  The earlier contract rejection was explained by received raw control/status
  registers, not bypassed. CP ABI v4 adds read-only readiness/missing-vote samples.
- Separate applied-response and drained-transport readiness phases, current-run
  CP residency accounting, cancellation when a scheduler deadline shrinks, and
  separate 8 ms preparation and wake budgets. The previous budget necessarily
  rejected short windows after RPC latency. Final quiet/epoch/deadline guards stay.
- A qualification-only 30 s startup delay, at most 12 AP returns or 24 requests,
  and stop on the first reported error. Admission failures cannot retry forever.

The affected suite ran 35 methods: **27 passed, 8 optional fixture tests skipped**.
Normal/default-off and experimental images build and pass image/map/CRC/resource,
log and syscall checks. The latest actual linked retained routine is 180 bytes
at `0x28010054`; its sole call is the audited 84-byte SRAM reset helper at
`0x28010000`. Static code/literal checks pass. They do not establish physical
wake, retention or clock restoration.

Hardware results are failures or limited observations:

- ABI v3 produced 12 AP WFI returns but **zero CP low-voltage entries**; an earlier
  entry-only test cap allowed 1,913 aborts. This motivated the request cap.
- The first ABI v4 pilot stopped at 24 requests with zero entries, reporting the
  deadline guard. This motivated the separate preparation budget.
- Corrected pilot AP `5fa1d854…` / CP raw `f1aabe49…` booted, reopened PFS and
  established encrypted protocol service. Its last reports show one AP WFI
  return, zero CP low-voltage entries, ten post-publication CP readiness samples
  and zero missing module votes. UART then stopped; the operator reported Nadir
  frozen or Bluetooth disconnected. No cleanup result from the next attempt or
  emergency reset banner was received. **No completed retained sleep is proven.**

First-failure UART bytes, phone logs, operator confirmation, phase offsets,
source/build hashes, static audits and recovery records are preserved under
`/home/baz/Documents/ChatGPT/pebble/artifacts/coordinated-suspend-20261031`.
The phone capture ended at 23:19:45 UTC; it is filtered Bluetooth logcat, not full
HCI snoop. No meter recording or new electrical saving is claimed. Display
configuration stayed fixed at USB/no cell, dim Nadir; overnight remains deferred.

Required next steps before another retained pilot:

1. Establish an independent bounded wake/reset safeguard that still works if AP
   WFI never returns. The current 700 ms check executes only **after** WFI;
   the pinned CP low-voltage HAL stops its watchdog. A C loop after WFI is not
   proof of recovery from a lost wake. Qualify the safeguard independently first.
2. Complete native AP PSRAM/AHBP ownership across CP low-voltage preparation.
   The current memory-ready bit checks compile-time domain policy only. Pinned
   `pm_psram_malloc_state_and_power_ctrl()` may release AS_MEM and AHBP_PSRAM
   votes when vendor CP/AP heap counts are zero, while native AP allocations use
   another allocator. Native startup requests the APP PSRAM lease (module 9).
   Preserve and verify the real owner leases; do not invent vendor heap counts
   or force shared domains. `bk_pm_get_psram_ctrl_state()` returns any-owner
   boolean state, not the ownership bitmap. This is an identified contract gap,
   **not a proven cause of the freeze**.
3. Instrument the CP final admission, HAL entry/return and AP wake using retained
   stage markers and a recovery path that preserves them. Zero missing module
   votes does not prove Bluetooth protect-time admission or HAL entry. Separate
   lost wake, CP stall, clock restoration and memory/peripheral retention failures.
4. After those gates pass, repeat bounded timer/heartbeat/PFS and reconnect tests,
   full cold-power qualification and the deferred overnight soak. Only then take
   fixed-display idle and energy-per-completed-workload measurements. Display
   power work remains the final phase.

The implementation is experimental and default off. It is **not hardware
qualified or deployable**. The normal active vote remains 240 MHz. The fixed
120 MHz experiment remains removed. Display content, brightness, transfer
completion and power policy were held unchanged during this investigation.

## Expanded firmware layout — 31 October, after 22:20 UTC

The operator authorized a 64 KiB expansion of the Pebble firmware subdivision.
Firmware is now 1,664 KiB (`0x02120000..0x022c0000`); the unchanged 1 MiB system
resource bank moves to `0x022c0000..0x023c0000`. Factory AP/CP boundaries, NOR PFS
at physical `0x500000` (size `0x2c0000`), active 32 MiB SD PFS, Shared PRF and
crash/scratch/debug reservations are unchanged. No filesystem migration or format
is involved. No display configuration or runtime power policy changed.

Retained qualification build `45e5e5ca…` now fits with **64,568 B** FLASH headroom;
normal default-off build `2cc3478b…` has **67,592 B** headroom. Both pass map/image,
protected-block CRC, resource-size, log and syscall checks. The actual linked
188-byte retained WFI routine is at `0x28010000`, with internal branches and
SRAM literal loads only, no BL/BLX or external calls. This resolves the previous
1,000 B size blocker and isolated-only SRAM audit limitation; it does not qualify
physical clock restoration, wake routing, retention, deadlines or Bluetooth.
Thirty-nine affected image/packaging/runner/config test methods pass. Additional
map tests cover synthetic BSS load addresses and initialized-data end bounds.
Failed compiler-size experiments were reverted; the original compiler policy stays.

A retained ROM layout record is checked by AP flashing and packaging tools.
Missing, duplicated or legacy layout records fail before writes. The first
hardware transition must write **matching firmware and resources together**;
AP-only flashing assumes the matching resource bank is already installed. Return
to the old entry AP likewise requires its old resource placement. Frozen normal,
retained and exact-entry rollback pairs are CRC/contents/hash-validated under
`artifacts/retained-layout-20261031` in the reference workspace. The qualification
helper accepts only locked normal/rollback pairs and defaults to validation only.

**Normal layout transition, full readback and paired legacy rollback passed.**
The first normal pair booted as `2cc3478b…`, at 240 MHz, reopening existing SD PFS
(`new=0`) and restoring bonded/encrypted protocol service; the operator confirmed
normal dim Nadir. Actual readback of all 2,924,544 physical bytes matches the
locked firmware/resource pair, including every protected-block CRC. The paired
legacy rollback then booted exact entry AP `86d1dcba…`, reopened PFS (`new=0`) and
restored bonded/encrypted service. The expanded normal pair was restored at
22:15:59 UTC, with expected build/PFS/240 MHz and protocol recovery received.
Receiver rotation left a six-second trace gap before that final warm protocol
session. The subsequent full cold-power check passed: the operator confirmed all
watch-side power removed for at least 10 seconds and automatic dim Nadir/Bluetooth
restoration. Despite USB re-enumeration, the new attachment captured build
`2cc3478b…`, 240 MHz, existing SD PFS (`new=0`), encryption/bond status `0/1/1`
and protocol recovery. The reset device RTC initially read 2020; host receive
records place this cold startup at 22:19 UTC. No received error or disconnect
appeared in the bounded cold-start trace. This is not a soak qualification.
Known CP `d4d65559…` is unchanged; no PFS write or provisioning was selected.
The retained candidate remains unflashed. The sleep-capable-CP radio timeout
remains unresolved, no retained entry has been attempted and overnight stays
deferred. No new power measurement or saving is claimed. Detailed evidence is
`artifacts/retained-layout-20261031/qualification.json` in the reference workspace.

## Radio and transport follow-up — after 21:15 UTC

The later evidence supersedes the installed-image and unflashed-v2 statements in
this page's earlier checkpoint. Evidence is under
`artifacts/coordinated-sleep-radio-20261031` in the reference workspace.

- Verified known CP `d4d65559…` completed five short reconnects after the operator's
  full power removal. Sleep-capable ABI v2 CP `1ad2087f…` then booted the exact
  entry AP, reopened existing PFS and restored bonded/encrypted protocol service.
  It also completed five short reconnects after full cold power removal.
- Alarm-only AP `0d13aae2…` received successful steps 0–4, including arm/fire and
  cancel. The final step was not received; this is not a complete diagnostic pass.
  Five further short reconnects completed. Each of these pilot cycles used only
  a ten-second recovery tail; they do not replace 25-cycle or long-idle gates.
- After restoring exact entry AP `86d1dcba…` while leaving v2 CP powered, an
  established session timed out at 21:09:00 device UTC (reason `0x0208`) and
  recovered at 21:09:47. The operator confirmed both devices were untouched.
  Phone logs independently show HCI supervision timeout reason 8 for the watch
  peer ending `0a:ab`, followed by connection-establishment failures. No automated
  cycles, retained-sleep requests or fixed-120 MHz operation occurred then.
  UART and phone first-failure data, hashes and confirmation are preserved.
  This narrows the investigation to the sleep-capable CP/alarm and warm-restore
  path; it does not establish a root cause or prove a clock transition occurred.
- The exact-adapter ModemManager ignore rule is now installed and its udev
  properties were verified. Prior host journal entries prove serial probing
  and a baud change caused capture interference. This explains those capture
  gaps, not the Bluetooth timeout. Actual UART and selected phone Bluetooth
  log bytes were received before hardware-test claims.
- New guarded HCI readiness checks cover queued/active ACL work, controller reset,
  logical request/response leases, retained receives and faults. A latched work
  semaphore replaces the quiet five-millisecond HCI poll only with retained
  sleep enabled; busy retries and the default polling path remain unchanged.
  Mailbox RX holds a sleep owner through callbacks even after FIFO consumption;
  final readiness also checks the physical receive FIFO. These changes have
  host-model coverage, but have not run with retained sleep on hardware.
- The bounded alarm diagnostic now uses a monotonic kernel timer and task-context
  work. Host tests cover normal completion, scheduling drops and stop behavior.
  Its built AP is unflashed; the absent old final step's cause is unresolved.
- Latest retained AP sources compile but link 1,000 B beyond the fixed partition.
  No candidate image is flashable, and the final linked SRAM audit remains open.
  Normal default-off release `54845a3a…` passes image/map/CRC/resource/log/syscall
  validation with 2,056 B FLASH headroom and is unflashed. Twenty-four relevant
  native test methods pass with sanitizers. No partition,
  display policy, stock shared code or pinned vendor source was changed.

Known CP `d4d65559…` is restored with exact entry AP `86d1dcba…`. Two bounded
six-minute phases (CP restoration, then AP-only warm return with CP powered)
completed without received disconnects. The first had no received errors; the
second had one known ATT subscription error. Both negotiated 60 ms/latency 4.
These short controls narrow reproduction but do not establish long-term radio
stability. The operator confirmed dim Nadir and Bluetooth. UART captured 73,165
bytes across four attachments; phone capture received 25,063,540 bytes. Both
stopped at 21:27:49 UTC, and host receiver/ADB child absence was verified.
No capture or overnight soak remains running.

The next discriminating run is a long-lived fresh-cold v2 connection without any
alarm request, followed separately by alarm exercise and warm AP return. Capture
CP-owned votes, actual clocks and native state without changing clocks or votes.
Keep the existing first failure immutable. The historical v1 reconstruction did
not match its frozen packager source hash; its replay guard stopped before any
write. Do not weaken that guard to repeat the older image.

No new power measurement was taken. No measured saving or hardware-qualified
retained sleep is claimed. Overnight qualification remains explicitly deferred.


Post-capture host check: the phone no longer enumerated over USB, so a final ADB
Bluetooth-state query was unavailable. The debug adapter remained present. No
later Bluetooth-state observation is inferred from the completed captures.

## Implemented contract

`CONFIG_T5AI_NATIVE_ALARM` uses a board-owned CP worker and an ABI-tagged scalar
mailbox protocol. CP owns one named, generation-checked alarm on its existing
many-user 64-bit AON RTC. Replies follow actual installation/cancellation and
propagate failures. RTC callbacks enqueue events without mailbox retries or
blocking. Foreign alarm names and radio deadlines remain owned by their users.

ABI v2 adds prepare/state/resume operations. CP rejects preparation unless the
sibling AP is physically reset, powered down and reset-acknowledged, and the
build retains PSRAM/AHBP. CP publishes readiness while retaining the APP veto.
Only AP WFI publication allows its idle check to release that veto; other CP
module/radio votes remain authoritative. Expiry revokes the permit. A final
pre-clock check rejects expired alarms or changed sibling state. CP revokes
readiness after its clock, memory and peripheral resource-restore callbacks.
Cross-core AON register writes follow a single-writer handoff; repeated ready
revocations cannot resurrect an AP WFI bit from a stale read.

`CONFIG_T5AI_RETAINED_SLEEP` adds nested, epoch-checked blockers for SD, flash,
I2C, GDMA (including microphone/display users), UART RX/TX and PM transactions.
Physical mailbox ACK state and logical IPC response/buffer leases are checked
separately. Uncertain completion, poisoned IPC, owner imbalance or counter
wrap fails closed. Enabled UART RX conservatively prevents retained entry;
its retained wake routing has not been qualified.

The idle path captures an absolute scheduler deadline before queueing work,
prepares the alarm in task context and rechecks kernel idle, pending interrupts,
peripheral ownership, clock state and the bounded wake margin with IRQs masked.
SysTick stops only after those checks. AP publication, WFI and the restored-clock
wait run from SRAM. The CP uses its pinned retained low-voltage sequence to
switch shared clocks to 26 MHz and restore DPLL; AP does not issue independent
PLL, voltage or flash-power commands. Wake restores the periodic timer and uses
AON accounting once, retaining fractional ticks. A private aligned PSRAM canary
and a CP low-voltage entry counter support qualification. This is retained WFI,
not reset-on-wake deep sleep or standby/off.

A lost controller transaction disables further entry. If restored clocks cannot
be proven, the SRAM stub records timeout and continues waiting rather than
returning to unproved XIP. Recovery/supervision of that failure still needs
physical qualification. A warm AP reset with stale published sleep state fails
closed and may require full cold power. Neither path is production-qualified.

## Evidence and limitations

Evidence root in the reference workspace:
`artifacts/coordinated-sleep-20261031`.

- Alarm-only ABI v1 CP and bounded AP build `27b17582…` completed six received
  diagnostic steps: applied arm, scalar fire and cancellation with no cancelled
  event. Normal ticks, APP veto and 240 MHz remained in place. This verified an
  awake AP event path, not deep-WFI wake. Step 2's fired field sampled the old
  generation; future diagnostics now log the checked generation separately.
- After warm restoration of entry AP `86d1dcba…` on that CP, uncommanded Bluetooth
  failures occurred at 20:17:58–20:19:57 UTC, with protocol recovery at 20:20:21.
  The operator confirmed both devices were untouched. Full first-failure bytes,
  decoding, hashes and operator confirmation are preserved. No retained sleep
  or 120 MHz operation was enabled. The cause remains unresolved.
- Exact corrected-off CP `d4d65559…` was restored. UART received entry AP build,
  existing PFS (`new=0`), 240 MHz and bonded/encrypted protocol recovery at
  20:21:59. A later ATT-handle subscription error is retained in the trace;
  short subsequent windows do not qualify radio stability or storage integrity.
- The v2 CP builds from pinned vendor inputs. Strict packaging verifies the
  Bluetooth-only PHY/calibration, controller relocation and latency correction,
  flash contract, partition bounds and protected-block CRCs. Exact regeneration
  passes; changed image/config bytes are rejected. It has **not been flashed**.
- Combined supported suite: 35 test methods pass; eight linked-artifact methods are
  skipped. Actual coordinator/blocker tests include 14 fault/race scenarios;
  CP alarm tests include eight scenarios. Retained tick wrap/pending/exact-once,
  native wake/resume routing, physical/logical IPC leases and combined UART
  gating/retained lifetimes are exercised with sanitizers. Retained owner tests
  also execute the current shared GDMA and I2C STOP/recovery paths. These are
  host models. A broader run found two pre-existing legacy display-DMA tests
  referencing `prv_pixel_stream_finish_dma`, absent from HEAD as well as current
  source. Those tests failed extraction; current GDMA tests pass. They are not
  counted as passing or silently skipped; their original failure log is retained.
- Every retained AP source file compiles. The full AP link exceeds the fixed
  partition by 1,840 B with profilers, or 728 B without profilers. No image from
  those attempts is flashable. Partition bounds were preserved. The isolated
  production SRAM stub compiles for Cortex-M33 with no BL/BLX; the final LTO
  image, literal placement and clock-transition audit remain blocked by the link.
- Normal default-off release AP `f11ba2d4…` builds and passes image/map, protected
  blocks, resources, logs and syscall checks, with 2,056 B FLASH headroom. It is
  built, not installed. Earlier cleanup build `44c977ff…` was non-release and
  had 328 B headroom; those configurations must not be conflated.

The bounded UART capture stopped cleanly at 20:39:22 UTC; the receiver's absence
was checked on the host. It recorded 41,924 bytes, last received at 20:37:23.
The post-rollback review contains 106 valid frames, no received disconnects and
one ATT subscription error. No capture or overnight soak remains running.

No new power recording was made. No idle-current, energy-per-workload or battery
life saving is claimed. Current/voltage unit calibration from previous USB
measurements remains separate. Overnight qualification is deferred by the user.

## Next gates

1. Reduce AP size locally without changing the partition, display policy, radio
   compatibility or storage diagnostics; rebuild both configurations and audit
   the final SRAM stub and all callees/literals in the linked image.
2. Establish a full-cold known-CP control, then isolate the alarm-only CP radio
   regression with matched cold/reconnect runs. Do not attribute it to a new
   clock transition: that transition was never exercised in this increment.
3. Qualify alarm wake from actual AP WFI, deadlines, pending/early wake and
   bounded CP retained entries with physical memory/PFS checks. Resolve any
   continuously held owners and CP module votes rather than bypassing them.
4. Complete app/SD/flash and mixed DMA stress, 25 reconnections, actual cold
   bond/watchface restoration and the deferred overnight connected soak.
5. Only then compare matched idle input power and energy per completed workload,
   holding dim Nadir/USB/no cell and notification state fixed. Leave display
   optimization to its final phase.
