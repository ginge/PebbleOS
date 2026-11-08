# Power investigation history through 8 November 2026

This is preserved investigation evidence, not the current installed state or
production policy. Use [current status](current-status.md) and the
[power TODO](power-todo.md) for the active plan. Full original files, working-tree
patch and hashes were preserved before cleanup in reference-workspace
`artifacts/power-production-cleanup-20261108/working-tree-before.tar.gz`.

## Former current-status journal

# Unhygienix current status — 2026-11-08

### Previous dim display policy restored — 2026-11-08 10:06 UTC

At the operator's request, AP `f1f862e8…` replaces `efca4e76…` with exactly two
Kconfig changes: `T5AI_AMOLED_ZERO_BRIGHTNESS=n` and
`T5AI_AMOLED_SLEEP_ON_DISABLE=n`. Logical zero again uses the existing minimum
dim floor, and explicit display disable no longer selects the experimental
panel Sleep In path. All other configuration values match the combined image:
continuous CP-awake AP sleep, I2C/SD/UART local gating, quiet battery successes,
existing FIFO and Wi-Fi-off CP remain enabled. CPU election remains240 MHz;
CP deep sleep and shared source/domain shutdown remain excluded.

The AP flash succeeded and UART received the expected build/clock and valid
alarm contract. Only the AP partition was selected for write. CP actual full
readback/CRCs match CP97; resources retain hash`62b695d7…`. Configuration-delta,
layout/CRC and linked SRAM WFI/call/literal audits pass. No driver/source policy
was changed for this brightness revision. One companion restart recovered bonded
encryption and protocol service (received10:08:19/10:08:21 UTC); phone state
confirms a connection. The new revision's32-return milestone was not received
in this short capture, so enabled configuration is not a new activity or energy
qualification. Bounded captures are closed. The prior combined image's Bluetooth
failures and missing electrical/cold/reconnect/soak qualification remain open;
this revision is not a claim to fix them or demonstrate savings.

Evidence: reference-workspace `artifacts/ap-savings-dim-display-20261108/`, with
exact frozen image under `coordinated-sleep-radio-20261031/ap-savings-dim-display-20261108-built`.

### Requested combined savings image flashed — 2026-11-08 09:55 UTC

The operator explicitly requested enabling working savings even without full
qualification. AP `efca4e76…` is flashed with continuous CP-awake AP sleep,
I2C/SD/AP-UART local bypass lifetimes, successful battery-log suppression,
existing I2C receive FIFO, raw-zero AMOLED brightness and panel Sleep In on
explicit display disable. The existing byte-verified Wi-Fi-off CP97 is unchanged.
No CP, resource or PFS partition was selected for writes; the resource pack hash
remains `62b695d7…`. Active CPU election remains240 MHz; CP deep sleep, shared
clock/domain shutdown and the failed fixed120 MHz experiment remain excluded.
The old display DMA IRQ experiment is not enabled.

Continuous WAKE_ONLY now works outside TEST, retains the bounded control's30s
startup settling period, and checks CP exclusion counters after every return.
Settling latches once across AON wrap. The time-slot/twelve-return cap is absent;
all ownership, deadline, tick accounting, retention, watchdog and wake guards
remain. A single task-context milestone reports activity after32 returns.
UART received that milestone at09:55:58 (device09:55:33): entries32, CP sleep0,
clocks0, masks0, resources0, fault0. This verifies activity beyond configuration,
not physical clock-gating or measured energy savings. Runtime faults still close
sleep admission. Ordinary tick-extension mode is excluded because retained sleep
already owns tick suspension and elapsed-time accounting.

Relevant native/driver regressions and seven Kconfig methods pass; the final
coordinator suite passes16 methods, including257 modeled continuous returns,
startup/later timer wraps and four CP exclusion failures. Linked SRAM entry/calls,
layout, CRC and frozen resource checks pass. Evidence and exact source/config/
image snapshots: reference-workspace `artifacts/ap-all-savings-20261108/` and
`coordinated-sleep-radio-20261031/ap-all-savings-continuous-20261108-built`.

The operator confirms the screen wakes/updates and Bluetooth recovered. Startup
needed a companion restart and one GATT discovery failure/retry. After received
protocol recovery, UART also recorded a0x0208 parameter-update timeout/drop and
renewed encryption. Preserve `ap-all-savings-20261108/first-failure`; do not call
this a stable connection or attribute it to a specific toggle. The explicitly
requested image remains installed. Zero brightness may blank the panel at timeout. No matched energy result, combined
cold/reconnect qualification or overnight soak is claimed. Source defaults stay
opt-in; this is the operator-selected experimental image.

### First AP-only return contract passes — 2026-11-08 09:28 UTC

AP `25418acf…` on actual-byte/CRC-verified alarm-server CP97 completed twelve
AP deep-WFI returns in the 09:27:48–09:28:08 slot: twelve preparations,
zero aborts, drops, deadline misses or contract faults. CP low-voltage,
resource-suspend, clock-transition and native mask-barrier deltas were all zero.
AP SCR changed from0 to4 with no architectural rejection. All final diagnostic
queries succeeded. A successful ADC sample followed completion; the phone still
reported a connection afterward. This is bounded AP wake/restore evidence,
not physical clock-gating residency, electrical savings or full qualification.

The pre-slot AP restart needed one companion restart; another pre-slot
connection loss/recovery is preserved. Automatic reconnection remains open.
Evidence: reference-workspace `artifacts/ap-only-sleep-20261108/`, including
`trial-a-review.json`, classification and actual CP readback. Host suites pass
32 methods; the linked SRAM WFI/literal/call audit and fixed resource hash pass.
The completed bounded AP remains installed with the CP kept awake; its slot has
expired and it admits no further sleep. Production defaults remain off.

CP deep-sleep investigation is deferred, with historical findings preserved.
The operator confirms dim Nadir updating and Bluetooth connected after the
trial. The short recovery trace has no received reset or peripheral error;
bounded captures are closed, not an overnight soak. Next: repeated/longer AP-only trials,
reconnect/cold qualification and a matched energy comparison before promoting
continuous AP-only idle. Shared clocks remain active; do not assume a saving.
Display remains fixed and last; overnight soak is still deferred.

### Active path: AP deep WFI, CP kept awake — 2026-11-08

The operator has deferred CP deep sleep. Preserve all coordinated-sleep findings
and first-failure evidence; do not continue the planned analog/HF CP transition.
The active implementation and qualification sequence is [AP-only sleep](ap-only-sleep.md).
`WAKE_ONLY` now takes operation-selection precedence and its cleanup rejects any
CP low-voltage, resource, clock or mask counter increment after each AP return.
The CP APP veto remains held; shared clocks/voltage/memory policy stays active.

The earlier twelve-return AP-WFI control is historical evidence, not current
qualification or measured savings. A clean unarmed AP-only build and host tests
are the first gate; fresh device trials, reconnect/cold/soak and matched energy
remain pending. No CP source or display configuration is changed by this step.

### Cold reconnect needed companion recovery — 2026-11-07 22:45 UTC

On restored normal AP `2cc3478b` and byte-verified known CP `d4d65559…`,
the operator reported a cold boot with Bluetooth disconnected. UART received
existing PFS `new=0`, 240 MHz, successful advertising-enable completions and
periodic successful ADC samples. The phone reported zero connections; there
was no received encrypted/protocol session before intervention. Preserve
`resume-20261107/cold-known-bt-failure-2244` as the failed cold baseline.

One companion force-stop/start, with no firmware, bond, app-data or Bluetooth
state change, recovered bonded encryption at22:44:10 UTC and protocol service
at22:44:11. Phone state independently confirmed one connection. Evidence is
`resume-20261107/cold-known-companion-recovery-2244`. Automatic cold reconnect
remains unqualified; this recovery does not identify its root cause. No timed
sleep image is installed. The next sleep mode described below is still
unimplemented; require a stable connected baseline before another global trial.

### Real-WFI reset preserved; verified normal rollback, cold baseline pending — 2026-11-07 22:30 UTC

AP176 `f98e94a4…` on byte-verified CP97 selected actual AP/CP WFI, retained
domains and the analog wake fallback. The 22:24 trial reset at22:24:10; boot
reported CP`0x93`, committed stage19 (pre-mask/WFI), valid1 and APguard`0x4f`.
There is no returned-WFI marker or completed coordinated return. Reset word was
`0x4f000933`, boot PMU wake0/power0/config`0x232`. A committed boundary is not
PC evidence or PMU residency proof. Preserve `first-failure/v176`; raw UART
snapshots are absent in this HF-off mode, which cannot prove that no code ran.
The real-wake-handshake hypothesis remains unproven.

Normal AP2cc3478b and exact calibrated CPd4d65559… are restored. The first CP
rollback handshake failed before write; the settled retry succeeded. Final full
CP readback matched active raw/protected bytes and CRCs; normal AP boot and
existing PFS `new=0` are received. No timed diagnostic remains installed. A full
watch-side power-off≥10s with PC adapter kept powered is requested before any
further global transition, to establish a fresh always-on baseline.

Next is an explicit analog-fallback/HF-retained actual-WFI diagnostic with a
read-only pre-mask snapshot and unchanged global predicates. This mode is NOT
implemented yet: the current AP analog selection shadows `WFI_KEEP_HF`, so
merely setting that flag would not test it. UART snapshot admission must support
owned analog/HF mode without changing the vendor subcore predicate or weakening
UART/deadline guards. Require host tests, exact new CP build/bindings, actual
readback and a fresh future-slot AP. No global-sleep/energy claim, display change
or overnight qualification. UART9/phone13 remain bounded diagnostics expiring
~23:31/~23:18 UTC; recheck actual reception if the operator replies later.

### Mask sequence alone reproduces peripheral failure — 2026-11-07 22:09 UTC

AP173 `8675ca61…` on verified CP97 executed one mask barrier and one AP polling
return, zero WFI, and completed cleanup/all final queries with fault0. QMI then
failed and ADC READ_RAW timed out after5000ms (22:00:50 host receipt). Software
clock/voltage transitions and resource callbacks were skipped; HF remained
`0x6c0fa76c`, clock mode `0x100031`, and sampled R40/CP interrupt enables/device
gates matched. PMU global enablebit19 was clear in `0x200002c0`, so do not claim
unintended global sleep. This isolates the failure to the mask-only suspend/mask/
cleanup sequence, not necessarily the mask store in isolation. Evidence and tail
are frozen in `first-failure/v173`. AP174 clock-only control is withdrawn.

The normal AP was restored; PFS, automatic encrypted protocol recovery and a
successful ADC sample are received after rollback. CP97 remains unchanged.
AP175 `c94f3ade…` reproduced the failure. Its first I2C software timeout(-116)
latched status`0xe511` (IRQ and ACK set), route`0xc40`/NVIC enable`0x100c40`
(I2C enabled), but both pending words were0. CP control`0x02000010`, AP
control`0x02120031`; BASEPRI`0xa0` was sampled in abort critical context. The
pre-cleanup evidence and ADC timeout are frozen in `first-failure/v175`. Six
I2C tests cover first-failure retention and deferred ISR-safe reporting.

Normal AP was restored. AP176 `f98e94a4…` is installed for22:24:05–25 using the
existing actualAP/CPWFI path, retained domains and analog fallback. This tests
the hypothesis that the vendor mask requires a real wake handshake; the vendor
path has no paired softwareunmask. It is not an asserted hardware mechanism.
Clock/flash/timer/IRQ/reset guards remain; no veto is removed outside the lease.
No display change or energy claim; deep sleep and reconnect/cold/overnight
qualification remain incomplete. UART9 expires~23:31; renewed phone13 actual
states are received and it expires~23:18. These are bounded diagnostics.

### Mask-only safe refusal: required flash snapshot restored in source — 2026-11-07 21:49 UTC

AP171 `5dad7edc…` on actual-byte-verified CP96 recorded65 requests,64 AP returns
and62 CP final checks at21:45, but every final check rejected FLASH (`0x40`).
No hardware-mask barrier or CP WFI executed. It failed the endpoint contract;
this is a SAFE REFUSAL, not a physical mask failure or sleep result. Its missing
flash-readiness snapshot was caused by the new resource-callback bypass. Preserve
`first-failure/v171-safe-refusal`, including the complete pre-rollback prefix.
The normal AP is restored and PFS `new=0` is received. AP172's complementary
21:57 control is withdrawn and remains unflashed.

CP97 source gives mask-only mode a snapshot of the current flash configuration
and an unchanged readback-probe check, without QE writes or flash-controller
mode changes. The existing flash-ready/busy/IRQ guards remain required; other
sleep modes keep their original flash transition/restore path. All32 relevant
native/retained/flash host methods pass, including98 CP cases, corruption and
unlocked-IRQ rejection, unchanged hardware-write counters and ordinary resume
regressions. The isolated CP97 build/link/ITCM/controller/calibration/package
checks pass; full actual readback and active CRCs match. AP173 `8675ca61…`
is installed for22:00:05–25 using the exact CP97 binding. PFS reopened;
one companion-only recovery is recorded before the slot. Actual hardware-mask
execution and healthy recovery remain pending. No energy measurement/display
change; overnight remains deferred.

### UART recovered; CP96 actual bytes verified — 2026-11-07 21:36 UTC

The operator reconnected the cable/hub (direct port unavailable). Known-CP
readback then succeeded and fresh UART9 boot bytes arrived at21:33:38. CP96
was installed under the unchanged normal AP. Its first download handshake
failed before read/write; the settled retry received the entire partition and
matched active raw/protected bytes and CRCs. Evidence is in
`resume-20261107/cp96-actual-readback-retry`; PFS `new=0` is received afterward.

AP171 is building for21:45:05–25 with the installed CP96 identity and linked
runtime bindings. It is the no-clock/no-voltage/no-WFI mask-only isolation,
not an energy or retained-sleep qualification. AP170 remains unflashed and
expired. No new display changes; overnight remains deferred.

### Host UART recovery blocked before device writes — 2026-11-07 21:34 UTC

The operator confirmed all connections. Host enumeration still exposes the exact
CH342 (`3-1.1.2`, both ACM ports), but UART8 had no bytes after21:19:26. Its
receive process was stopped cleanly and UART9 reopened at21:31:23; zero bytes
have arrived. Two known-CP read-only recovery attempts immediately failed with
`serial I/O: Protocol error`, before flash read/write. No candidate was installed.
Kernel descriptor errors on adjacent `3-1.1.3` identify a USB audio device and
must not be presented as CH342 descriptor failures. Exact adapter runtime state
is active; the programming port has no competing process.

The requested next physical action is moving the CH342 PC-side USB cable from
the hub to a direct PC port, keeping watch power connected. Preserve the failed
attempt directories and `uart9-host-kernel.txt`/USB topology in the continuation
artifacts. Known AP/calibrated CP remain installed; CP96/AP170 remain unflashed.
UART9 is bounded to ~23:31 UTC and has not proved reception; phone12 expires
~22:28. Neither is overnight qualification. No new savings or display changes.

### Mask-only diagnostic implemented; hardware held for UART — 2026-11-07 21:27 UTC

CP96 adds default-off private operation101 and AP selection
`T5AI_RETAINED_SLEEP_MASK_ONLY_CONTROL`. It skips resource suspend/restore
callbacks and the clock/voltage transition, while keeping final IRQ/deadline/
flash admission, the stack-free mask barrier, IRQ/timer restoration and AP cleanup.
Neither CPU executes WFI. This isolates the final mask from the failed AP169
clock transition; it is a diagnostic, not a power-saving fix.

All30 relevant host methods pass, including98 CP cases, AP operation selection,
IRQ rejection, balanced mask/IRQ/tick restoration and normal versus skipped
resource callbacks. Isolated CP build/link/ITCM/controller/calibration/package
checks pass with unchanged SDK config. CP96 raw `73495d24…` / protected
`b4321fcd…` is packaged but UNFLASHED. The paired AP170 `c20641e6…` host build and SRAM audit pass with unchanged display
resources. It uses an expired slot and host-package hashes, not a device-readback
identity; it cannot qualify or authorize a hardware trial.

The operator reported cold boot/reconnection; UART attachment002 received the
known normal AP boot and existing PFS `new=0`, and the phone later reported a
connection. Phone Bluetooth toggles were also recorded; automatic reconnect is
not qualified. UART's latest actual bytes remain21:19:26; encrypted protocol
recovery has not arrived. Hardware writes are held pending fresh reception and
the requested check of the updating watchface/debug leads. Known normal AP and
calibrated CP remain installed, sleep disabled. No energy measurement/display
change; overnight remains deferred. Bounded UART8/phone12 expire ~22:28 UTC.

### Restore words match but peripherals still fail — 2026-11-07 21:13 UTC

AP169 `f1d36808…` on byte-verified CP95 completed the 21:10 clock/mask/Finish
control and every final query, with zero reported contract faults. AP and CP WFI
were both deliberately absent. PMU R40 was `0` before and after; CP interrupt
enables were unchanged (`0x208008`, `0x80400380`), as were shared peripheral clock
gates (`0x83588491`). These are cached boundary observations, not an atomic state
snapshot or proof that all clock sources/peripheral internals were restored.

QMI configuration nevertheless failed immediately after completion, followed by
ADC READ_RAW status -12 after 5000 ms (host receipt 21:11:11). This is a FAILED
peripheral restore control. First prefix, complete recovery tail, decoded frames,
phone state and comparison are preserved in `first-failure/v169` under the deep
sleep review artifacts. Bluetooth was disconnected before the slot; this attempt
was explicitly peripheral-only and supplies no Bluetooth qualification.

The exact normal AP 2cc3478b and calibrated CP d4d65559… were restored by 21:12;
normal AP boot is received. Fresh post-rollback protocol/visible-face recovery is
not yet established. A full watch-side power removal for at least ten seconds is
requested before another transition, to avoid carrying always-on state across
warm resets. The next isolation should exercise the final CP hardware-mask
operation with clocks/voltage unchanged, before another HF-shutdown/full restore
trial. This is a proposed diagnostic, not an implemented fix. AP168 was never
flashed; AP169 has no future slot pending. No measured savings or deep-sleep
qualification, no display changes; overnight remains deferred. UART8/phone12 are
bounded to ~22:28 UTC, not an overnight soak.

### Baseline recovery and verified restore diagnostics — 2026-11-07 21:01 UTC

CP95 full physical readback matches the frozen image and active CRCs, including
its second install at 20:58. Two read-only download handshakes failed before any
read/write; the settled retry succeeded. Preserve all three records. The normal
AP remains 2cc3478b, with PFS `new=0`; no timed sleep AP is installed yet.

AP168's 20:47 slot was withdrawn before flash because baseline protocol recovery
was absent. The known calibrated CP return later opened bonded protocol sessions
at host receipts 20:55:16 and 20:57:20, with a supervision timeout between them
after repeated connection-parameter request timeouts. The expected slow-advertising
restart is present in delayed UART data: the earlier absence in live output was
not a confirmed handoff fault. Phone GATT registration/handle observations remain
an unproven lead. These baseline failures occurred without a native sleep request;
they do not explain away the separate QMI/ADC failures after clock/mask controls.

Next is a future-slot AP169 no-AP/CP-WFI restore snapshot control on verified
CP95, once normal protocol recovery is received. Compare PMU R40, CP interrupt
enables and shared clock gates before resource entry and after restore callbacks;
completion counters alone are not a healthy result. No measured savings, display
changes or global retained-sleep proof. Cold/reconnect qualification remains open;
overnight remains deferred. UART8/phone12 are bounded captures expiring ~22:28 UTC.

### Read-only restore-state instrumentation prepared — 2026-11-07 20:39 UTC

CP95 adds private scalar reads93–100 for PMU R40, CP interrupt-enable low/high
banks and shared peripheral-clock gates before resource preparation and after
restore callbacks, before AP release. Snapshot collection writes only diagnostic
RAM; normal clock/mask/ownership policy remains CP94's. Values are observations
at those two boundaries, not an atomic whole-system snapshot or global-sleep proof.
The completed-control and snapshot-validity context must be retained when reading
zero values. The AP final bounded report includes all eight reads; no logging was
added inside the transition. Framing, native ABI and Bluetooth identity remain.

All44 host methods pass, including97 modeled CP cases. The new executable case
checks exact before/after values and unchanged hardware after every read; the AP
whitelist test covers each added read and continues rejecting mutating operations.
SDK config is unchanged; isolated build/link/ITCM/controller/calibration/package
checks pass. CP95 has booted the unchanged normal AP, existing PFS (`new=0`) and
bonded/encrypted protocol automatically (host receipt20:39:27.755852). Full actual
readback and paired AP bounded control are pending; nothing is sleep-qualified.

The earlier operator cold boot needed companion-only recovery; preserve that as
failed automatic reconnection, with missed boot-prefix capture. No power samples
or energy claim in this work. Display configuration/resources remain fixed;
overnight is deferred. UART8/phone12 have verified received data and expire about
22:28 UTC; these are bounded diagnostic captures, not an overnight soak.


### Operator cold start needed companion recovery — 2026-11-07 20:29 UTC

The operator reported a cold boot with Bluetooth unable to connect on the known
normal AP/calibrated CP. UART7/phone11 had expired at 19:39, so the initial boot
prefix is missing; do not call this captured cold-boot qualification. New UART8
and phone12 captures started at 20:28; presence alone was not called reception.
The phone initially showed Bluetooth ON, GATT Connections0 and no connection
events. Its failure snapshot and companion-only logs were preserved before action
in `resume-20261107/cold-after-ap167-reported-failure`.

One recorded companion force-stop/start recovered bonded encryption (host receipt
20:29:00.050516) and an open protocol session (20:29:02.093528); the phone then
reported Connections1. No firmware, pairing or app data was changed. Early frames
used the 2020 RTC plus eight minutes until phone time sync, so use host receipt
and raw offsets for this recovery. This is failed automatic cold reconnection
followed by recovery, not a pass and not evidence that sleep caused this baseline
failure. Sleep remains disabled; no timed probe is pending. New captures are
bounded to about 22:28 UTC and are not an overnight soak. No measured savings.


### PMU restore warm control still loses peripherals/radio; cold baseline required — 2026-11-07 17:50 UTC

Actual CP94 raw `caf4d838…` / protected `66b114d7…` matched full readback and CRCs.
AP167 `caf8206d…` automatically recovered PFS and encrypted protocol before the
17:48 slot. It completed clock1/mask1/Finish at 17:48:06 with fault0 and all final
CP queries successful. QMI configuration failed from 17:48:08, phone Bluetooth
disconnected, and ADC READ_RAW failed after 5000 ms at 17:48:49. No WFI was
executed. This is a FAILED health control, preserved in `first-failure/v167`;
cleanup completion must not be called functional sleep success.

The CP94 R40 save/restore omission is host-proven (44 methods pass, including a
failing original-code regression and restore-readback fault handling), but is not
a proven hardware fix. CP93's unsuccessful extra settling delay remains reverted.
Earlier failures may leave always-on PMU configuration across warm resets; its
fresh initial value has not been established for this comparison. Require full
watch-side power removal before assigning the next result to this change. Do not
force a guessed PMU reset value or advance to AP/CP WFI from this failure.

Normal AP `2cc3478b…` and the exact known calibrated Wi-Fi-off CP are restored.
Actual readback retry matched raw `d4d65559…`, protected `36e9118f…` and active
CRCs; first handshake failure is preserved. Existing PFS reopened (`new=0`), and
bonded/encrypted protocol recovered at 17:53:07 after one recorded companion
restart. No pending timed test remains. The operator cold-power check is pending;
no further sleep test will run until that prerequisite is verified.
Next: cold power removal with PC-side adapter kept powered; verify boot/PFS/bond
recovery; record PMU before/after values and interrupt/clock state in the next
bounded control before advancing to real WFI. Display phase, energy savings,
reconnect qualification and overnight remain pending. No new power recording,
no savings claimed; overnight explicitly deferred.


### Settling delay insufficient; PMU R40 restoration regression fixed in source — 2026-11-07 17:40 UTC

Byte-verified CP93/AP166 `0c181d85…` completed clock1/mask1/Finish at 17:35:07.
The configured extra delay was exercised (AP interval 133 AON ticks versus 44),
but QMI configuration failures and a Bluetooth drop followed again. This is a
FAILED health result, preserved in `first-failure/v166`. The timing experiment
was reverted. Normal AP/PFS and encrypted protocol recovered at 17:37:02.

A further source audit found PMU R40 overwritten by low-voltage setup but absent
from native rollback. CP94 now snapshots the CP-owned delay/halt policy before
resource preparation, restores it after the sleep-condition rollback, and marks
the generation faulty if readback differs. The regression fails against CP92
and passes after the fix; all44 relevant methods pass, covering normal return,
early rollback and rejected restore writes. This proves the source omission,
not that it caused the hardware failure. R41 halt controls are bypassed in this
particular retained-domain mask control; existing LPO-bit restoration remains.

CP94 SDK/controller/calibration/config/link/package checks pass; hardware control
is pending. No APP veto, deadline, shared-owner, display or CPU-frequency policy
change. No measured savings or actual retained-sleep qualification. Reconnect,
full captured cold boot and overnight qualification remain open; overnight is
deferred. UART7/phone11 replace the bounded prior captures after a recorded gap;
actual UART bytes, encryption and valid phone snapshots were verified again.


### AP165 completes cleanup but peripheral/radio recovery fails — 2026-11-07 17:26 UTC

Actual CP92 readback matched raw `09a3cb54…` and protected `f3faf370…`.
AP165 `564f47e8…` completed one clock/mask control and FinishCleanup at
17:12:06, with zero CP WFI and no reported contract fault. This is NOT a passing
control: QMI8658 configuration began failing at 17:12:08; the phone recorded a
reason-8 disconnect at 17:12:13; ADC READ_RAW later failed after 5000 ms. AP stayed
alive without the previous delayed reset. `first-failure/v165` preserves received
UART and phone prefixes, exact binding and actual CP identity. The earlier result
classifier omitted peripheral warnings and phone disconnects; its completion
result must not be used as a health/qualification gate.

Normal AP `2cc3478b…` was restored at 17:19:02, reopened existing PFS (`new=0`),
sampled ADC successfully at 17:20:34, and recovered encrypted protocol at
17:21:14 after one recorded companion restart. This is warm recovery only.

CP93 is a pending board-local timing experiment: wait for the configured 1800 us
26 MHz hardware startup plus vendor 1040 us XTAL/DPLL restoration before BSP
restoration, rather than relying on the 290 us post-BSP delay in a no-WFI control.
The source/configuration discrepancy is real; its role in the radio/peripheral
failure is unproven. APP veto, ownership/deadline guards and fixed display settings
remain intact. No new power measurement or savings, actual deep sleep, reconnect,
cold-power or overnight qualification is claimed. Overnight remains deferred.


### CP91 completed control, then delayed reset; CP92 preserves ordinary SysTick — 2026-11-07 17:01 UTC

AP164b3a833ed/CP91 completed real clock1/mask1 control16:50:20.918 with entries1,
CP WFI0, fault0, canary/time validation and FinishCleanup. Every final CP query
returned status0. NormalSleepSkips92 reached0x655f3=415219 at16:50:26.139, proving
the shallow-WFI workaround was exercised. It then reset16:50:36 during ordinary
traffic; last port word15=Returned. This is partial progress, NOT a passing
control or sleep/energy qualification. first-failure/v164 preserves both portions.
Normal AP2cc/PFSnew=0 restored; encrypted protocol recovered16:55:48 after one
recorded companion restart.

CP91 skipped WFI after entering the tickless wrapper, repeatedly stopping and
reinitializing SysTick in a rapid idle loop. CP92 also refuses tickless entry at
the existing guard before SysTick is touched once the mask-used owner is restored
and no coordinated sleep is prepared. FIFO software re-pend, cleanup holds and
permitted coordinated low-voltage entry remain intact; the HAL fallback guard
still protects normal WFI during an armed-but-not-ready interval. Counter92 now
includes early normal-idle deferrals. The actual wrapper refusal test preserves
SysTick control/load/current; all44native/mailbox/coordinator methods pass.
CP build/config/link/ITCM/controller/calibration/package checks pass; SDK config
unchanged. CP92 is installing under normal AP; hardware result remains pending.
The configured CP interrupt watchdog is8000ms; starvation is a plausible delayed
failure mechanism, not a confirmed reset cause. No new power measurement; awake
CP idle cost and full WFI/reconnect/cold/overnight qualification remain open.


### CP91 actual identity/normal recovery; AP164 workaround trial — 2026-11-07 16:46 UTC

Full1114112-byte actual readback verifies CP91 raw7af7024e236b9085ea944861e4b8788b9f6b722ae67c5a43e134f299cebac4b0
and protectedf9e40fee9d6b22033f4041c7019aa0b75a8e390e2b29360fff3f8a9646726131,
including active CRCs. Normal AP2cc/PFSnew=0 and encrypted protocol recovered
automatically before16:39:51 and after16:41:03 readback restart. This is warm
recovery, not cold/reconnect/soak or sleep qualification.

AP164b3a833ed is frozen, SRAM/map/fixed-resource checked and flashing for16:50:05–25.
It retains the clock/mask/no-AP/CP-WFI control with exact CP91 port-word binding.
The only functional CP workaround skips normal/shallow WFI after actual native
system-mask use; low-voltage entry remains available and tick/IRQ/veto policy
unchanged. Private read-only query92 reports NormalSleepSkips in the final slot
report, to establish whether the bypass was exercised. Require real clock/mask
cycles, canary/time/Finish validation and healthy generic log/HCI traffic before
claiming a functional result. A no-entry refusal is not success. The potential
awake-idle energy cost and full coordinated-sleep qualification remain unmeasured.
No new power recording or display change. Prior release blockers remain open.


### Pinned HAL warning yields narrow post-mask shallow-WFI workaround — 2026-11-07 16:35 UTC

AP163afd19dae/actual-byte-verified CP90 reset16:25:13 with port progress6:
PM call entered, no later PM-return observation. This is NOT the prior runtime
idle/scheduler flag encoding. It does not itself distinguish the initial native
PM call from a later ordinary-idle PM call. first-failure/v163 preserves the raw
trace before normal rollback. Normal AP2cc/PFSnew=0 and encrypted protocol recovered
16:31:49. No completed clock/mask/Finish or sleep/energy qualification.

The pinned BK7258 sys_hal_enter_normal_sleep contains a TODO warning that normal
sleep cannot wake after calling the CPU0 system interrupt-mask setter. That aligns
with masked controls failing and the last-boundary PM observation, but is not yet
a confirmed hardware root cause. CP91 tests a board-local workaround: after an
actual native system-mask store, skip subsequent normal/shallow arch_sleep until
reset; keep coordinated low-voltage PM available. Mask-rejected and unmasked paths
retain normal WFI. No APP veto removal, tick disable, IRQ enable or clock-policy
change is added. Existing tickless return/tick-accounting paths remain intact.
A private matched-pair read-only NormalSleepSkips counter(op92) records whether
the bypass actually runs; wire framing/Bluetooth protocol remain unchanged.

All44native/mailbox/coordinator methods pass (96CP/39AP modeled cases plus actual
HAL normal-WFI gating, mask-rejection/real-mask/cleanup ownership and counter
read-only/saturation tests). CP91 is building; not installed or hardware-qualified.
Potential awake-idle energy cost must be measured after functional sleep works.
The shared RTC unregister audit confirms the selected64-bit driver disables the
alarm compare IRQ when its list empties; it does not stop the free-running counter.
Display, stock common source and reference sources remain unchanged.


### CP90 actual identity/normal recovery verified; AP163 port probe — 2026-11-07 16:22 UTC

Actual full1114112-byte readback matches CP90 rawda19890cdaddd1697f8a468c03c653fcf66d4d966540e5ca67fd0f72a0670535
and protectede5e4ef14a37a3179a4b00f56a5a2f23534b11c6cb6b7bd0de97d91bc7ffbe7bc,
including active CRCs. Normal AP2cc/PFSnew=0 and encrypted protocol recovered
automatically before16:16:42 and after16:18:01 the readback warm restart.
This remains warm recovery only; full cold/reconnect/overnight qualification and
all sleep/energy claims remain pending.

AP163afd19dae passed image/map, SRAM and fixed-resource checks and is flashing
for16:25:05–25 on the unchanged CP90. The exact ELF-bound aligned4-byte
s_native_port_progress word is sampled through the explicit alternate diagnostic
binding. Reset0x40–0x4f encodes its low4 T5AIPortProgress value: PMEnter6,
PMReturned7, first tick enter/return8/9, IRQ window returned10, second tick11/12,
critical exit13, AP release14, wrapper returned15 (entry/refusal/tick-stop/critical/
abort0–5). This is a passive last-boundary word, not a PC or integrity-checked
multi-field record. It is NOT the previous runtime task/scheduler flag encoding.
Require received evidence before locating the stall. No clock/IRQ/tick operation,
APP veto, display setting or resource content changed for the probe.


### CP idle scheduler-held failure; passive port progress CP90 — 2026-11-07 16:16 UTC

AP162b5d59f5b reset16:04:11. Its frozen runtime code0x46 samples current CP task
matching the private idle-TCB value and scheduler suspended, with no valid CP
exception-sync entry and no pending-tick backlog beyond one second. These are
independent reads and do not identify a PC or prove the precise function stalled.
Together with AP161 raw gates0x43 and AP159/160 queued/unrelated-ACK observations,
this focuses the next probe on the idle/tickless wrapper and its IRQ windows.
first-failure/v162 retains the raw trace before rollback. Normal AP2cc/PFSnew=0
recovered; encrypted protocol recovered16:11:50 after one companion restart.
No completed clock/mask/Finish, WFI or energy saving is qualified.

Implemented CP90 passive native-generation port phases0–15 around entry, guard
refusal, SysTick stop, critical entry, PM call/return, both tick updates, IRQ window,
critical exit and AP release/return. One aligned volatile word is read by AP;
no log, tick, IRQ, mask, clock or sleep-policy operation is added. Existing ISR
progress remains observable after cleanup closes. The generated wrapper minus
observer lines matches CP89 nonblank source; one blank-line delta is recorded.
Observer timing can perturb races; a successful instrumented run is not a fix.
14native and29AP methods pass, including exact normal/refusal/abort operation
ordering. Build/link/ITCM/controller/calibration/package checks pass; SDK config
unchanged. CP90 is installing beneath normal AP; readback/functional recovery and
paired bounded port-phase trial remain pending. Display and PFS resources fixed.


### Raw delivery gates open; renewed receivers and CP runtime probe — 2026-11-07 15:59 UTC

AP161f10f4f7a reset15:52:12. Frozen raw CP IRQ code0x43 reports system mailbox
route enabled, mailbox interrupt gate enabled, CP system interrupt mask clear,
and core0 not halted. These are independent MMIO samples; they do not show CP
NVIC enable, PRIMASK/BASEPRI or scheduler progress. first-failure/v161 retains
the raw trace. Post-reset zero counters/fault4 describe recovery, NOT a safe
non-entry pass. CP89 queued guard still has no successful hardware control.

Normal AP2cc/PFSnew=0 and encrypted protocol recovered automatically15:54:58.
UART5/phone9 closed cleanly(exit0). UART6/phone10 started15:55:56 for7200seconds;
actual received UART frames/encrypted protocol15:56:13 after a recorded companion
restart and valid phone state verify reception. The renewal gap is recorded.
This is a bounded diagnostic capture, not an overnight soak or full cold capture.

AP162b5d59f5b is frozen, SRAM/map/resource checked and flashing on the unchanged,
actual-byte-verified CP89 for16:04:05–25. Runtime timeout bits0–3 sample CP exception,
idle-TCB equality, scheduler-suspended and pending ticks beyond one second.
Exact CP ELF/config/layout/readback bindings are included. CP private TCB storage
is never dereferenced by AP. Competing receive/raw-IRQ/release/cleanup encodings
are disabled. It remains the same no-AP/CP-WFI clock/mask/Finish control; no result
or saving is claimed. Next requires full cleanup/generic traffic before real WFI.


### CP89 control still queued; raw CP delivery isolation next — 2026-11-07 15:45 UTC

AP160be6df0cd/actual-byte-verified CP89 reset15:43:11 with the same owner code0x4c
as CP88/AP159: valid last unrelated ACK, CP RX FIFO queued. The post-Finish guard
correction passes its exact-baseline host regression but did NOT resolve the
hardware stall. first-failure/v160 preserves the complete first trace before
normal AP rollback. No complete clock/mask/Finish, WFI or saving is qualified.
Normal AP2cc/PFSnew=0 are restored; warm protocol recovery is being checked.

The existing read-only CP IRQ diagnostic now applies to every physical owner,
including the log client, before default per-channel attribution.15 mailbox
failure methods pass, including every16 route-flag combinations across16 owners.
AP161 is building the same control with raw CP route/gate/system-mask/halt encoding
for15:52:05–25. CP89 and display configuration remain unchanged. The initial
configuration draft rejected its missing PM-FIFO parent; corrected build retains
the existing dependency and was not flashed. This probe samples MMIO flags only;
it enables no route or IRQ and assigns no root cause before received evidence.
Prior hardware/cold/reconnect/overnight and external restart blockers remain.


### CP89 actual bytes verified; paired AP160 prepared — 2026-11-07 15:37 UTC

CP89 active raw a553d997fe87657cad30a9e32d41648c67783bb0434515d6075ca09be4cac8f4
and protected4296a433e60884d950bcc7692553ce285476f39428c06c59f2beafcef52a8766
match the full1114112-byte actual readback and active CRCs. Normal AP2cc reopened
existing PFS/new=0 after install and readback restart; encrypted protocol recovered
15:35:24 after a recorded companion restart. This is warm recovery only, not a
cold/25-cycle/overnight pass. No alarm was armed by the normal image.

AP160be6df0cd is frozen and passed image/map, SRAM and fixed-resource checks.
Its resolved configuration differs from AP159 only in the scheduled slot;
the exact CP89 ELF/readback binding is included. It is flashing for15:43:05–25,
the same no-AP/CP-WFI clock/mask/cleanup control. No hardware result is claimed.
Require actual clock cycles, one mask barrier, matched masks, valid timer/canary,
FinishCleanup and healthy generic log/HCI traffic before advancing to real WFI.
The queued-FIFO host correction remains a hardware hypothesis until then.
No new power capture, measured saving or display change.


### AP159 queued/unrelated-ACK evidence; CP89 post-finish delivery guard — 2026-11-07 15:34 UTC

The identical AP159030963d7 repeat reached its15:29 window and reset15:29:11.
Frozen owner comparison AP0x4c means a valid committed receive record whose
channel and sequence differ from the timed-out physical owner, ACK set, and
CP RX FIFO queued in the later independent sample. This is not PM Other or
receive-stage12. It points to undelivered queued work; it does not prove the
core/route state or that CP was inside a callback. first-failure/v159 preserves
the raw trace, exact CP88 producer/ELF binding and phone state before rollback.
Normal AP/PFS recovered; encrypted protocol recovered15:32:04 after one recorded
companion restart. No full cleanup or sleep/energy qualification.

CP89 extends queued-RX idle refusal/software re-pend past FinishCleanup for the
native owner's generation. Cleanup still holds service throughout its lease;
a closed lease may idle when RX is empty. Queued work refuses tickless entry;
only enabled core/NVIC/route delivery with no pending IRQ re-pends the existing
ISR. No route/mask enable, second consumer, APP vote removal or SysTick change.
Exact frozen CP88 fails the new post-finish regression; CP89 passes all13 native
methods (95CP/39AP modeled cases). SDK configuration is byte-identical; build,
ITCM/controller/calibration/package checks pass. CP89 installed under normal
AP and reopened PFS/new=0; actual full-byte readback is in progress. It has not
passed hardware cleanup, sleep, energy, cold/reconnect or soak qualification.


### AP158 safely made no request; identical repeat prepared — 2026-11-07 15:22 UTC

AP1588249f205 on unchanged byte-verified CP88 reopened PFS/new=0 and recovered
encrypted protocol15:18:18 after the recorded companion restart. Its15:20 window
reported requests=prepared=entries=CP clock/mask cycles=0, fault0, phaseIdle;
all final read-only CP query status values were0. Maximum idle budget1092 AON
ticks (~34.1ms) is below the intact44ms alarm/admission contract. This is a safe
non-entry result, NOT a transition, cleanup, owner-diagnostic or savings pass.
controls/v158-safe-refusal preserves its received trace and binding.

The AP-only owner comparison passes all14 mailbox failure methods and image/map,
SRAM and fixed-resource checks. Its alternate encoding is0x40 plus matching
channel(bit0), matching sequence(bit1), ACK(bit2), CP RX queued(bit3);0x50 is an
invalid committed record. AP159 is building the identical later20-second repeat;
no deadline, wake margin, timer, CP or display setting has been relaxed. If idle
admission remains insufficient, use existing read-only deadline profiling to
identify the blocking timer/thread before changing any peripheral policy.
No new power measurement or sleep qualification. Prior cold/reconnect/overnight
and externally triggered restart blockers remain open.


### CP88 no-WFI control still fails; owner comparison prepared — 2026-11-07 15:14 UTC

AP15706215ff9 on byte-verified CP88 reset15:08:11 with committed receive
stage14/DriverReturned (AP0x4e under this frozen selector), matching CP87/AP156.
The queued-FIFO/zero-status ISR correction passes its host regression but did
not resolve the hardware failure. No complete clock/mask/Finish report, WFI or
saving is qualified. first-failure/v157 preserves the raw trace before rollback.
Exact normal AP2cc/PFS(new=0) are restored; protocol recovery is being checked.

A default-off AP-only owner diagnostic compares the committed last receive
header's channel/sequence with the active physical transaction, and samples
ACK and CP RX FIFO queued properties. It preserves the existing FIFO/ACK owner
and changes no clock, sleep, display or Bluetooth policy. All14 mailbox failure
methods pass, including all16 property combinations. AP158 is building; it is
not installed or hardware-qualified. The record and later FIFO sample are not
one atomic observation; matching headers alone do not prove delivery.

No new power recording or savings. Full cold capture, reconnect and overnight
qualification remain pending; the earlier reported cold prefix was missed.


### CP87 receive probe failed; CP88 byte-verified with normal recovery — 2026-11-07 15:02 UTC

AP156/CP87 reset14:50:11 with a valid committed receive stage14 (DriverReturned,
AP code0x4e under this frozen encoding). This is not PM Pending. The last observed
receive frame returned from the registered callback; the old cleanup-gated marker4
was not proof of a callback hang. This snapshot alone does not prove the exact
later failure. first-failure/v156 preserves the trace and exact producer binding.
No complete mask/clock/Finish report or sleep/energy result was received.

CP88, differing by exactly the tested ISR-entry condition, is now installed under
the normal AP. Actual1114112-byte readback verifies active raw/protected bytes and
CRCs (raw2f97c02d…55e4c1). Existing PFS/new=0 and encrypted protocol recovered
automatically14:53:35 before readback and14:55:29 afterward. This is normal warm
recovery, not cold/overnight or sleep qualification. AP15706215ff9 is built/frozen,
passed map/SRAM/fixed-display checks and is installed for15:08:05–25, still the
no-AP/CP-WFI mask/clock/cleanup control. Startup preflight/PFS are received; a
recorded warm phone-cycle/companion recovery is being used before its slot.

The shared/common source and display path remain unchanged. CP88 uses the original
ISR's FIFO/ACK owner; no new consumer was added. Host proof is recorded in
cp88-irq-regression-baseline.log (CP87 fails, CP88 passes). This correction remains
hardware-unqualified until the full control and subsequent generic log/HCI traffic
pass. The diagnostic record writes remain enabled after first arm; production
energy qualification will require controlling/removing that instrumentation.
No new power measurement or savings claim. All cold/reconnect/overnight and prior
hardware release blockers remain open.

### Software re-pend recovery gap reproduced; CP88 remains separate — 2026-11-07 14:48 UTC

The cleanup idle guard can software-pend the CP mailbox IRQ for queued FIFO data,
but CP87's ISR returns immediately when the hardware IRQ status latch is zero.
The original ISR therefore ignores that software recovery request. CP88 changes
only that entry condition: return only when status is zero AND RX FIFO empty;
otherwise use the existing ISR receive loop/callback/ACK owner. No second consumer,
FIFO pop helper or IRQ ownership transfer was added. The exact frozen CP87 recipe
fails the new queued-RX/zero-status regression; CP88 passes the same test, including
exactly-once callbacks and empty/spurious IRQs. Full13native methods pass; CP88
build/package/controller/calibration checks pass. cp88-isr-delta.json confirms one
entry-condition replacement and identical SDK configuration. Hardware effect is
unqualified; CP88 is packaged but NOT installed.

Current-source sleep-off155adecae1d on CP87 recovered protocol14:41:39 after the
recorded phone cycle plus companion restart, and remained connected beyond the
earlier14-second post-connect failure interval until the next flash. Retained
AP156a7694290 on unchanged, actual-byte-verified CP87 has the same versioned receive
probe and a14:50:05–25 no-WFI slot. After the matching recovery procedure it opened
encrypted protocol14:46:53. Require a stable received session through the slot;
no transition result or saving is claimed yet. AP154's earlier startup failure
remains preserved and its slot was withdrawn. UART5/phone9 have received actual
data; no power measurement or overnight qualification is running.

### AP154 withdrawn before its slot; matched awake control prepared — 2026-11-07 14:32 UTC

CP87 actual1114112-byte readback matches the packaged active raw/protected image
and CRCs. Exact normal AP2cc/PFS/encrypted protocol recovered before and after
readback14:18:28/14:19:57. AP154c828b8a6 passed SRAM/map/fixed-display checks and
read the empty versioned receive record safely at startup. It failed automatic
Bluetooth startup, then one documented companion restart recovered encrypted
protocol14:24:34 followed by parameter timeout0x0208/disconnect14:24:48, before
any alarm/sleep request. ap154-pre-slot-bt-failure preserves raw trace, identity,
record binding and phone state. Normal AP2cc booted14:27:44 before the14:28 slot;
AP154 never ran its transition. Do not classify this as a mask/ACK probe result.

Restored normal AP2cc also failed to reconnect after a companion restart, so
retained-sleep causality is not established. A recorded phone off/on recovery
cycle is underway. A current-source retained/native-alarm-off control155adecae1d
is built and frozen. Its complete resolved configuration differs from AP154
only in retained/native-alarm options; display pack is identical. A stale CMake
cache rejected the first configuration before build; clearing retained-test
options fixed it. This control is not flashed yet. Require received recovery and
a matched awake comparison before another bounded transition. No power recording
or new savings claim; all qualification limits and prior blockers remain open.

### Resumed after operator cold boot; versioned receive observer implemented — 2026-11-07 14:18 UTC

Operator reports cold boot and reconnection. UART5 opened afterward, so the cold
startup prefix is missing; this remains a qualification limit. Actual Honor GATT
state was connected. One recorded phone cycle produced received disconnect and
bonded/encrypted protocol recovery14:10:43, establishing live UART reception.
Known normal AP/calibrated CP stayed unchanged through implementation/build.

CP87 adds a passive, one-cache-line receive record with magic/version, committed
sequence/inverse, generation, original header/payload and named copy/ACK/callback
stages. The mailbox ISR remains sole writer and owner; observation continues after
cleanup retirement. No FIFO drain, ACK retry, callback ordering, APP vote or sleep
policy was changed. The AP default-off diagnostic reads only its exact ELF-bound
record and rejects odd/changed sequences, invalid formats, generation0 or invalid
stages before encoding a timeout boundary. Snapshot validity is not sleep proof.

All13native-alarm methods (including94CP/39AP modeled cases) and13mailbox/snapshot
methods pass. The first ownership harness lacked the new passive-observer stub;
its corrected full rerun passes. A reader fixture address shadowed a local variable;
correcting the fixture made torn-record/format/wrap tests pass. CP87 build/package
checks pass against pinned vendor/source/controller/calibration inputs. CP87 is
being installed with the exact normal AP first; actual readback and normal startup
must pass before any bounded trial. Display/PFS remain fixed; no new saving is
claimed. Cold/reconnect/overnight and prior release blockers remain open.

### Hardware trials paused for a fresh cold baseline — 2026-11-07 13:25 UTC

Restoration completion —13:26 UTC: the actual1114112-byte CP readback matches
known calibrated active raw/protected hashes and verifies CRCs. The normal AP2cc
reopened existing PFS(new=0); a documented companion restart recovered bonded,
encrypted protocol service13:25:06 (received13:25:08). Visual state is not newly
confirmed. UART4/phone8 diagnostic captures closed with exit0 after confirmed recovery;
no overnight soak or continuing monitor is claimed. No source changes were staged or committed.

AP153a0acaef1 still reset13:20:15 with active physical log-channel reason0x5e,
despite routine cleanup reports being deferred until after validation/Finish.
No pre-reset report or completion was received. The identity of that log caller
(fault report or another logger) is unproven; do not claim Finish succeeded or
that report ordering fixes transport. First-failure/v153 preserves the complete
prefix and exact CP86 binding. Normal AP2cc/PFSnew=0 and automatic encrypted
protocol recovery13:22:39 were received before known-CP rollback. No power
recording was started for AP151–153; no sleep or measured saving is qualified.

The normal AP is retained and the known calibrated Wi-Fi-off CP is restored;
actual-byte/CRC readback and its warm-start recovery are being checked. Further
transition trials are held pending a full watch-side power removal and fresh
baseline. The operator previously permitted halting when physical work is needed;
no overnight, cold or reconnect qualification is inferred from warm recovery.
Display resources/PFS remain untouched. The external cable-triggered restart is
still a separate release blocker.

Source changes this continuation: test-only timeout diagnostics identify the
active physical owner when a queued client expires; the existing exact-ELF CP
snapshot can cover non-PM owners; successful test cleanup reports follow Finish.
Latest12mailbox and14retained-sleep methods pass. Earlier native-alarm regression
methods passed with unchanged CP/native-alarm source. Image-map, fixed display
resource hash and SRAM call/literal audits passed. None is a hardware sleep pass.

Next: establish the fresh known baseline, then instrument a versioned CP receive
record with generation/header and adapter-copy/ACK/completion stages. Keep that
record valid independently of cleanup retirement; the current gated marker4 can
be stale when a lease closes. Inspect ACK completion and adapter copy before
changing IRQ ownership. Exact CP package/readback binding remains mandatory.
Repeat the no-WFI clock/mask control through Finish plus ordinary log/HCI service
before actual AP WFI and coordinated CP sleep. Full PMU sleep evidence, matched
idle/workload energy, reconnect, cold and overnight qualification remain pending.
See artifacts/deep-sleep-review-20261101/resume-20261107/callback-audit-after-v153.json
in the reference workspace for the reviewed call chain and concrete next checks.

### Defer routine reports until the cleanup lease is finished — 2026-11-07 13:14 UTC

AP152f99d4e78/unchanged byte-verified CP86 reset13:08:16 after one no-WFI
iteration. Its selected ELF-bound transport diagnostic recorded AP0x44, last
observed driver callback entry4. The adapter copy and ACK/command callback path
remain inside that boundary; this is not proof of a particular function hang.
The observer is cleanup-gated, so lease retirement can also stop later markers.
First-failure/v152 retains raw data, the exact binding and phone-state prefix.
Normal AP/PFS restored; automatic encrypted protocol recovery13:10:15 is received.
No WFI, energy or reconnect qualification is claimed.

The successful test cleanup path now completes all existing validation queries
and FinishCleanup before routine report logging. Logging itself uses CP IPC;
placing it inside an incomplete service lease interleaved unrelated mailbox
clients. Post-finish report traffic remains required before publishing successful
test completion. Error/contract checks remain strict. Fourteen retained-sleep
methods pass with a new assertion that these reports follow successful Finish.
AP153 is building for13:20:05–25, same no-WFI control and default operation/owner
timeout attribution. CP86, display and APP veto behavior are unchanged. This is
an implementation candidate, not a demonstrated fix; full hardware qualification
and all previously recorded release blockers remain pending.

### Physical owner confirmed: log channel, not a queued PM client — 2026-11-07 13:03 UTC

AP1515362d7d0 repeated one no-WFI iteration, then reset13:00:16 with AP reason
0x5e. The corrected diagnostic resolves queued failures to their active physical
owner; channel index14/logical0x1e therefore remains the blocked physical log
request, not a pending log behind native PM. The two summary messages arrived,
but complete cleanup/Finish was not reported. First-failure/v151 preserves the
raw trace, CP86 readback binding and phone-state prefix. No power recording was
started for this localization run; no WFI or savings claim. Startup PFS and
bonded/encrypted service had recovered automatically before the slot.

Normal AP/PFS restored after failure; a documented companion restart was needed
again and its recovery is being checked. AP152 is building the same control for
13:08:05–25, using the existing CP transport progress word (ELF-bound to unchanged
CP86). The default-off diagnostic now covers non-PM physical owners too. Twelve
mailbox failure methods pass, including both PM/log owners across every progress
value. The preceding26sleep/native-alarm methods passed on the host. Require the
next actual trace to distinguish callback stall from ACK loss; no CP change,
APP veto removal or display change was made. All hardware sleep/energy and
release qualification blockers remain open.

### Entered cleanup; log-client timeout needs physical-owner attribution — 2026-11-07 12:55 UTC

AP1503fa8ffcc/byte-verified CP86 passed awake preflight and entered one
no-WFI control iteration at12:48:05. Cancel and the initial counter/readiness
queries returned; entries=1, CP low-voltage entries=0, fault=0 at that checkpoint.
It then reset at12:48:12. Durable AP reason0x5e identifies mailbox channel index14
(logical log channel0x1e), not a native-PM operation. A queued client's timeout can
precede the active physical owner's watchdog, so this does not identify which
operation blocked transport. No full cleanup, FinishCleanup, WFI or saving is
qualified. First-failure/v150 preserves the trace and closed41984-sample diagnostic
power recording, excluded from savings.

Normal AP2cc3478b restored and reopened PFS(new=0). Automatic Bluetooth recovery
was absent; a recorded companion force-stop/start at12:52:41 recovered bonded,
encrypted protocol service at12:52:46. This is recovery, not a reconnect pass.
The operator's delayed AP145 answer confirms its pre-slot startup disconnect;
that earlier run remains excluded and was rolled back before its slot.

The test-only timeout diagnostic now records an active physical owner when the
expired caller is queued; an active caller retains its own attribution. Eleven
mailbox failure tests pass, including pending log behind PM/non-PM owners and
no-owner cases. AP151 is building for13:00:05–25, repeating the same no-WFI
clock/mask control. Require complete cleanup and healthy generic mailbox traffic
before advancing. Display remains fixed; physical appearance is not newly
confirmed. Cold/reconnect/overnight and measured savings remain pending, as does
the externally triggered adapter-cable restart release blocker.

### Safe refusal retained; awake preflight separates startup from idle budget — 2026-11-07 12:41 UTC

AP149bf6858ef/CP86 survived its12:32 slot and completed all final read-only
report queries, but requests=entries=CP clocks=0. Worker admission refused once;
phase returned Idle with fault0. This is NOT a transition/Cancel/Finish success.
controls/v149-safe-refusal retains the trace, identity and closed41984-sample
diagnostic power(exit0); no savings claim.

One-time CP contract/state/counter validation was still inside the first
idle-deadline worker. It now preflights on the awake startup callback, without
arming an alarm or releasing the APP veto. Missing peripheral votes at startup
defer readiness rather than fault; the normal worker fallback retains strict
validation. Every entry still checks current blockers and the original complete
wake margin/deadline. Tests cover startup query latency, transient missing votes
and preserved warm CP counter baselines.14 retained methods pass; wider checks
and final image build are pending. CP86 is unchanged and byte-verified.

AP150 is rebuilding for12:48:05–25 (the12:44 build was never frozen/flashed).
It remains the same no-WFI clock/mask control with Cancel, validation and explicit
FinishCleanup. Require actual transitions and a healthy post-finish mailbox
before advancing to AP WFI or full coordinated CP sleep. Hardware sleep/savings,
visual/reconnect/cold/overnight qualification remain pending; display unchanged.
UART4 and phone8 have received actual data; prior capture gaps are explicit.

### Cancel fixed, next query exposed; full cleanup finish contract implemented — 2026-11-07 12:27 UTC

CP85/AP1489d6a0044 reset12:16:11 with operation label0x4b, LowVoltageCount.
The worker calls this only after native Cancel returns success. Extending the
service hold through Cancel's reply ACK therefore advanced past the original
Cancel timeout but did not cover the remaining post-wake validation traffic.
First-failure/v148 and closed41980-sample diagnostic power(exit0) are retained.
No full control, WFI or saving is claimed. Normal AP/PFS restored; subsequent
12:19–12:20 parameter/discovery instability is separately preserved. One
recorded companion restart recovered protocol12:21:55, after an initial retry.

Implemented paired CP86/AP FinishCleanup operation91: Cancel disarms the alarm
while retaining CP service; AP performs post-wake validation, then explicitly
finishes. The matching finish-reply ACK retires the guard. Read-only queries
remain read-only; a new arm cannot reuse a held cleanup. Finish validates the
owner generation and restored resource/APP/LPO/analog state. AP validates the
generation echo and closes an orphaned warm-start owner with Cancel+Finish.
Wire framing/ABI identity and Bluetooth protocol remain unchanged; this optional
extension is used only by the exactly pinned paired sleep candidate. Unknown
operation/peer failure fails closed. Default production sleep stays off.

37 focused host methods pass, including94 native CP and39 native AP cases;
retained-worker tests cover the finish API, and adapter lifetime schedules pass.
First-draft fixture failures (old two-message warm-init expectation) are retained.
CP86 build/link/ITCM and package checks pass. Actual installed CP raw/CRC readback
matches728795a8ba82af1b8c218796d6daf1e23aedf7986627a56f44d0ed4ba7ebf303 /
c0fe8d89315e0cb5718b31451832d8d6dde1cda07801de427b2cb16e1a759b08.
Normal AP automatically recovered PFS/encrypted protocol both before and after
readback (12:25:02 /12:26:23). UART4 receives actual bytes after an explicitly
recorded warm-capture renewal gap. Phone7 receives valid Bluetooth-state data.

AP149 is building for12:32:05–25, still the no-WFI clock/mask control. The
actual-AP-WFI draft remains deferred and unflashed. Require actual transitions,
all validation queries and FinishCleanup before advancing. Display and APP veto
remain fixed; measured savings, cold/reconnect/overnight qualification remain
pending, along with the externally triggered cable-replacement restart blocker.

### Cleanup service now spans reply ACK; candidate installed — 2026-11-07 12:08 UTC

AP147b899cf33 on CP84 reset11:56:19 with FIFO diagnostic reason0x45:
CP receive FIFO queued, AP receive FIFO empty, AP route enabled, and no sampled
AP PRIMASK/BASEPRI. This is a last snapshot, not a packet-type or continuous
state proof. First-failure/v147 includes the companion recovery phase, actual
binding/identity, UART/phone5 prefixes and closed41980-sample power(exit0).
Normal AP/PFS recovered; one recorded companion restart recovered encrypted
protocol12:00:02. UART3 renewal has actual received bytes; gaps are explicit.
Earlier AP125/129/132 operation labels identified native Cancel physical ACK
timeout. CP84's newer checkpoint shows logical reply submission also returned
success; AP/CP delivery is still not proven by either observation alone.

Implemented CP85 retains cleanup service independently of the prepared flag,
then releases it only on the existing PWC completion callback for the matching
native reply generation/ABI/command and successful communication state. Cancel
no longer clears the service hold before its reply. Wrong/failed ACKs fail
closed; a new arm cannot reuse a held cleanup session. The vendor ISR keeps
FIFO/ACK ownership. No wire ABI change, APP-veto removal or direct SysTick
change.37 host methods pass, including94 native cases and100 adapter lifetime
schedules. First-draft fixture failures are retained; older fixtures now model
transport ACK receipt, while dedicated cases delay/reject/race it.

CP85 build/link/ITCM and package checks pass. Installed actual raw/CRC readback
matches642a937d469928160aee156abe6e94f479ffd9d8a3c6d68eaeadfbfaacd04be0 /
83e31a6c9bf71e73bc96ba68be31818d660a6c87116f6572afc455d109e67230.
Unchanged normal AP automatically reopened PFSnew=0 and encrypted protocol
12:06:22. The readback commanded another warm restart. AP148 is building for
12:16:05–25 with the same no-WFI control and operation-specific timeout labels.
Hardware fix, actual sleep and savings are not yet qualified. Display remains
fixed; reconnect, cold and overnight qualification remain outstanding.

### Cancel reply accepted; transport timeout isolated further — 2026-11-07 11:51 UTC

AP1467889b36b on byte-verified CP84 recovered encrypted protocol11:39:14,
then remained connected through the settled control baseline. Its11:45
no-WFI clock/mask control reset11:45:11 with AP reason0x4d. The exact bound
Cancel checkpoint13 is ReplySubmitted: cancellation cleanup reached completion
and bk_pm_cp0_response_cp1 returned BK_OK. This means the logical response was
accepted; it does not establish physical transmission, ACK or AP receipt.
Do not describe this as a stall inside RTC alarm cancellation. The AP physical
mailbox timeout remains unresolved. No coordinated-sleep success is claimed.

First-failure/v146 retains raw UART/timestamps, exact binding and CP identity,
phone4 valid GATT-state prefix and closed41984-sample diagnostic power(exit0).
Normal AP2cc3478b reopened PFSnew=0 and automatically recovered encrypted
protocol11:47:54. No further companion restart or phone BT cycle was needed.
The next bounded AP147 profile selects existing FIFO/route timeout diagnostics,
with unchanged CP84 and no AP/CP WFI, to distinguish delivery gates from queued
traffic. Its11:56 slot is building; not flashed. One Kconfig draft was rejected
for inactive diagnostic addresses, corrected by unsetting those addresses.

CP84's named Cancel checkpoints and90 native host cases remain implemented;
36-method suites plus the separately corrected adapter-lifetime method pass.
This is diagnosis/host validation, not qualified power saving. Display and APP
veto remain fixed. Reconnect/cold/overnight release qualification is incomplete.
UART2 receives actual bytes; phone5 valid state snapshots replace expired
phone4 with an explicit gap. Filtered phone3 logcat was headers only.

### Cold recovery passed; warm recovery needs companion restart — 2026-11-07 11:36 UTC

The operator confirmed a full >=10s cold cycle on normal AP/CP84. UART2
attachment2 received the complete RTC2020 boot, PFSnew=0 and bonded/encrypted
protocol11:23:56. AP145166c861e then started but did not establish stable
Bluetooth before its11:30 slot. Startup parameter/discovery errors and the
operator's disconnected report are preserved in ap145-startup-bt-failure,
with closed47980-sample diagnostic power (exit0). Normal AP was restored
before the slot, so AP145 did not test clock/mask/WFI/Cancel. AP144 remains
unflashed after the expiry guard rejection.

Known calibrated CP + normal AP were restored and actual active bytes/CRCs
verified. Warm startup also did not reconnect immediately. One recorded
phone off/on cycle did not recover it; companion foregrounding followed by
one recorded force-stop/start recovered encrypted protocol11:34:08, confirmed
by phone GATT state. This is a recovery observation, not proof of root cause.
Android history additionally shows Settings Bluetooth toggles11:26–11:28 and
11:31–11:32; only11:32:41–55 was the host's documented cycle. Do not label those
other windows untouched. CP84 normal11:20–11:21 remains a separate trace.

CP84 is reinstalled on the unchanged normal AP, with fresh actual readback
matching its frozen package. The same companion recovery step will be applied
if needed before a later AP146 no-WFI Cancel-progress control at11:45 UTC.
UART2 receives actual bytes. Phone4 receives actual read-only Bluetooth-state
snapshots; phone3 filtered logcat received headers only and was stopped.
All energy remains diagnostic/excluded. No retained-sleep or savings claim;
reconnect, cold/soak release qualification and external cable-reset blocker
remain open. Display configuration and APP veto are unchanged.

### Normal CP84 Bluetooth drops preserved; pilot held — 2026-11-07 11:24 UTC

CP84 installed with unchanged normal AP2cc3478b and passed full actual CP
readback/CRCs (rawce075a790cf1dae8e464ed4286a2e9757557a8a4c316defcc32d33fe483eb63d,
protected29bce3ba59ae44b6bfff4bc284bc5d13e3be5efd1d53c3d868241510ac7bbefd).
Normal startup and warm-readback recovery opened encrypted protocol, but the
renewed UART then captured parameter failures0x0208 and disconnects11:20–11:21.
No host Bluetooth cycles or sleep trial ran; operator activity is not separately
confirmed. Protocol recovered11:21:44. Preserve cp84-normal-bt-drop including
raw receive timestamps, exact identity and diagnostic power. A full physical
cold baseline is requested before further sleep writes; carried-over state,
phone, hardware and code causes remain unresolved.

AP1440daa1725 is frozen/SRAM-audited but UNFLASHED: the expiry guard rejected
its11:23 slot before write. The earlier live commentary saying flashing was
corrected. AP145 is rebuilding for11:30:05–25 and remains held until the cold
baseline/recovery is verified; rebuild again if that slot expires. UART2 has
received actual data after a documented capture renewal gap. ADB remains
absent. No coordinated-sleep success, savings or overnight qualification.

### Cancel dispatch reached; completion trace added — 2026-11-07 11:18 UTC

AP143 reset at11:11:11 with retained AP reason0x4e on byte-verified CP83r3.
In this profile, low nibble14 means native Cancel dispatch was reached.
The mailbox breadcrumb stops updating when cleanup is released, so it does
not distinguish an internal Cancel stall from completed cleanup/reply loss.
First-failure/v143 preserves raw UART, exact binding, CP identity, decoded
records and the closed diagnostic power recording (35980samples, exit0).
Normal AP2cc3478b/PFSnew=0 and encrypted protocol recovered11:13:20.

CP84 adds a separate named Cancel checkpoint word covering generation match,
veto/ready restoration, disarm, RTC unregister return, persistent-state check,
cleanup release and reply submission/failure. Submission is not peer receipt.
No suspend policy or wire ABI changes. Host alarm cases90 pass; the completed
36-method suites plus separately corrected mailbox-lifetime module pass.
The original aggregate invocation's mistyped-module error is retained.
CP build/link/ITCM and exact-source packaging checks pass; installation on the
normal AP is underway. A bound AP144 control is prepared but not flashed.
No retained-sleep success or measured saving is claimed. Display remains fixed;
physical cold/reconnect/overnight qualification and the cable restart blocker
remain open.

### Cable-triggered restart retained; no-WFI progress control installed — 2026-11-07 11:08 UTC

AP143 (575fedb1) is installed on byte-verified CP83r3. UART confirms the
expected AP, existing PFS and bonded/encrypted protocol recovery at11:05:19.
Its bounded11:11:05–25 UTC control exercises clock/mask restore and Cancel,
without AP or CP WFI. Success requires a real clock/mask transition and
successful cancellation/status; safe refusal with zero transitions is not
success. Diagnostic power capture has received samples; no savings claim.

The requested CP83r3 cold cycle lost its UART prefix when the CH342 stopped
enumerating (USB error -71). Phone data supported recovery, but the cold
control remains incomplete. Replacing the adapter cable restored UART and
caused a fresh normal boot; the operator confirms restart/flicker. This is
an externally triggered restart/release blocker, excluded from clean cold
qualification and distinct from a sleep failure. Complete evidence remains
in resume-20261107, including the earlier connection-parameter timeout.
ADB is absent after the hub disruption; phone-side monitoring is not active.
Display settings/resources and APP veto remain unchanged. Reconnect, cold
and overnight qualification remain pending.

### Known cold control received; CP83r3 warm startup recovers — 2026-11-07 10:55 UTC

The operator repeated the requested full cold power cycle with reception
active. Despite adapter USB detach/reattach, attachment2 captured a fresh
normal AP2cc3478b boot with initial RTC2020, existing PFSnew=0, bonded/encrypted
Bluetooth and automatic protocol recovery (received10:50:05). No host reset
or write occurred during this baseline. The earlier replug preceded capture
and is not used as cold-prefix evidence. UART1/phone1 are receiving data.

Pinned CP83r3 was then installed with the unchanged normal AP; full actual
readback/active CRCs match raw43c4588b7227585827aea5dd6d5feeaa69c5fcc218ffb8bfab40d7576b7f71c0,
protectedc014893383ef0a558069cd9792f4e51a631d8359c43ac67967f4e81fe23bf1b0.
Its first warm boot recovered encrypted protocol automatically10:53:55,
without a companion restart or phone Bluetooth cycle. This does not reproduce
last night's normal-startup stall after the new known cold baseline; carried-
over state is a hypothesis, not a proven cause. The readback commanded another
warm restart, retained separately. CP83r3's matching physical cold control is
next, before any retained-sleep pilot. No new savings, WFI or soak claim.
Display configuration and APP veto remain fixed. AP142 remains unflashed and
expired; AP143 tooling is prepared but not built/flashed. Source/package hashes
still match the previously passing37-method/88-native-case qualification.

### Hardware trials halted for cold-baseline check — 2026-11-07 01:47 UTC

The watch is restored to normal AP2cc3478b and the known calibrated Wi-Fi-off
CP, raw d4d65559bf66a93e65aa832549d89b799fbbad21293a9f58949718ba55f0dc71,
protected36e9118fc4d40dff111fd7bab28c44616ce64ec5c231425205ffc9302ad6e605.
Actual full1114112-byte readback and active CRCs match; physical SHA
9d2f1338264eb35c5593ba374199e8a339ea5bbee4ccadef5499f79a229b8f4e.
Normal PFS reopened new=0. One recorded companion restart recovered bonded/
encrypted Bluetooth01:46:15 and protocol01:46:16. This is warm recovery,
not cold or visual qualification. The operator is asleep; new hardware sleep
trials are halted until a fully cold matched baseline can be established.

Findings retained: CP80 cleanup hold kept the scheduler unsuspended, but
AP139 still timed out. CP81's conditional IRQ re-pend also failed (AP140);
its last sample had all four delivery gates open and FIFO data queued.
CP82/AP141 proved a cleanup ISR entry with nonzero status before the reset,
without sampled FAULTMASK, pending or zero-status flags. These are separate
last observations, not continuous/atomic state or proof of a stuck ISR.
First-failure/v139,v140,v141 and their closed power records are preserved.
All diagnostic energy is excluded; no new measured savings are qualified.

Implemented CP83r3 progress tracing covers receive, logical callback, PM
queueing, ACK send and native Cancel dispatch. 37 host methods pass,
including88 native cases,100 adapter lifetime schedules and the actual ISR
ownership test. Rejected unflashed drafts and fixes remain recorded. CP
build/link/ITCM and AP SRAM/image checks pass; CP83r3 actual readback matched.
Its normal-image startup did not recover a received protocol session after
companion restart and one phone Bluetooth cycle. That recovery failure is
preserved in resume-20261106/cp83r3-normal-unrecovered; do not attribute it to
code versus retained always-on/phone state without a matched cold comparison.

AP142 5e4b3794 is frozen and SRAM-audited, UNFLASHED. Its01:47 test slot must
be rebuilt for a future time; the pre-flash expiry/address guards remain.
Production candidate pins describe CP83r3; hardware is the known CP above.
No AP/CP WFI or coordinated retained-sleep success is claimed in these trials.
APP veto, wake/deadline guards, CP supervision, PFS layout and display resource
pack62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46 remain.

Next: with the operator awake, remove every watch-side power/debug lead for
>=10seconds while keeping the PC-side adapter powered, then verify a complete
cold boot, existing PFS, visible Nadir and encrypted protocol. Compare known
CP and CP83r3 normal startup from matched cold states. Only then rebuild the
bound AP142 pilot to locate the receive/dispatch/ACK stall. Require an actual
clock transition plus successful cancellation/status before incremental AP
and CP WFI tests,25 reconnects,cold and overnight qualification. Sensor,
shared-memory and CPU/radio follow-on qualification depend on that contract;
display power remains last. UART11 and phone13 captures closed cleanly after the recovery tail;
no overnight monitoring or soak qualification is claimed.

### ISR entry proved; receive/dispatch/ACK trace prepared — 2026-11-07 01:39 UTC

AP141 on verified CP82 reset01:32:26 with AP44/CP10. In this delivery
encoding, bit2 means the cleanup mailbox ISR was seen with nonzero status;
FAULTMASK, pending IRQ and zero-status flags were absent at the last sample.
This is not proof of continuous mask state or a stuck ISR. First-failure/v141
and closed power retained, excluded from savings. Normal AP/PFS recovered;
encrypted protocol returned automatically01:34:26. No sleep success claimed.

CP83r3 adds bounded progress breadcrumbs through ISR receive, logical callback,
PM queueing, ACK send and native Cancel dispatch. Two earlier unflashed drafts
were rejected by actual ISR null-callback and read-only-query tests; corrected
scoped instrumentation preserves both behaviours. No CP83 image has been
written yet. Build/link/ITCM audits pass; final host tests/package are being
checked. Next AP142 binds the progress word and retains the same no-WFI
clock/mask control. APP veto, deadline/ownership guards and display remain
unchanged. Operator asleep: visual, cold and overnight qualification pending.

### IRQ gates open at timeout; delivery observer prepared — 2026-11-07 01:28 UTC

AP140 on byte-verified CP81 reset01:20:17 with AP4f/CP10. The last CP cleanup
sample had PRIMASK/BASEPRI clear, NVIC enabled, FIFO queued and combined
hardware/peripheral/system routes enabled. Conditional IRQ re-pending did
not fix cancellation. FAULTMASK and actual ISR execution were unobserved.
First-failure/v140 and its closed35980-sample power record are retained;
energy excluded. Normal AP reopened existing PFS; protocol recovered01:24:58
after a read-only CP identity check/commanded warm reset. CP81 bytes/CRCs
remained intact. UART10/11 renewal and initial UART11 silence are recorded;
actual UART11 reception resumed after the read. This was not a cold boot.

CP82 observes FAULTMASK, pending IRQ, cleanup ISR entry and zero-status
entry without changing vendor ISR status checks, drain or ACK ownership.
Its core pending guard now also respects FAULTMASK. 37 host methods pass,
including87 native cases,100 adapter schedules and the actual patched ISR
ownership test. Link/ITCM audits pass. Full readback/active CRCs match raw
7df7623baa696c2717c932157259a243b2f5ff6c41633275b293bcb9cf4e9c98,
protected6a351f8625a488c7047dce88239be5e76d2e740acc45707e52e70eb7f2cf9dd0.
Normal AP PFS and encrypted protocol recovered automatically01:27:18.

AP141 is building for01:32:05–25 with an exact-ELF delivery-word binding.
Its AP4x encoding differs from AP140: bits0 FAULTMASK,1 NVIC pending,
2 ISR seen,3 ISR saw zero status. These are last observed/latched facts,
not an atomic live state or proof of continuous masking. Still no AP/CP WFI,
retained global sleep, new savings, cold or overnight qualification. Operator
asleep; display configuration unchanged but visible state cannot be verified.

### Conditional mailbox delivery control installed — 2026-11-07 01:16 UTC

CP81's actual full readback and active CRCs match raw
8999e94f0705f25be6380972e8a4fe9e585fb346568ae93d001f8118a7ec1bd0,
protected9539605ef9e22594804bc1e5d727401a7c78eaeba73da4e7a0501e46a94dc5ee.
36 host methods pass, including86 native cases and100 adapter lifetime
schedules; CP link/ITCM and AP SRAM audits pass. The failed first build used
an unavailable IRQn_Type; corrected to the pinned vendor uint32_t API.

AP140 4f66e408 is installed for01:20:05–25, still no AP or CP WFI. Its startup
IRQ snapshot self-check is checksum-valid0x40; protocol recovered01:15:17
following one recorded companion restart. The earlier normal-AP startup had
encrypted retries without a protocol session before this flash, so it is not
a clean reconnect qualification. Phone11/12 renewal gap is retained; UART10
and the diagnostic meter are receiving actual bytes/samples. Operator asleep:
visible screen state cannot be qualified and power is excluded from savings.
No global sleep, cold boot or overnight soak success is claimed.

### Cleanup guard changes scheduler state; cancellation still fails — 2026-11-07 01:08 UTC

AP139 on byte-verified CP80 reset at00:55:17 with AP4a/CP10. The sampled
runtime flags show current-generation final release53 and the CP idle TCB,
with scheduler suspension now zero. Native Cancel still physically timed
out. This supports the cleanup guard changing the targeted scheduler state;
it does not prove interrupt delivery or retained sleep. First-failure/v139
and the completed power capture are preserved; diagnostic energy is excluded.
Normal AP2cc3478b reopened existing PFS and recovered encrypted protocol at
01:05:19 after one recorded companion restart. Visual state is unavailable
while the operator sleeps. No cold-boot or overnight qualification is claimed.

CP81 is being host-tested: during the post-release cleanup lease only, re-pend
the existing mailbox IRQ when FIFO data is queued, core/NVIC/peripheral/system
routes are enabled and the hardware mask is clear. It neither unmasks IRQs
nor drains or acknowledges the FIFO outside its existing ISR. New counters
observe the route and pending kicks. No AP/CP WFI or global sleep is qualified;
APP veto and deadline/ownership guards remain. Display configuration is fixed.

### Cleanup-window contract candidate installed — 2026-11-07 00:53 UTC

AP138r2's pre-reset AP4e snapshot proves same-generation final release53,
CP idle-TCB match and scheduler suspended; no valid CP exception-entry flag.
First-failure/v138r2 and closed power retained. Normal PFS/protocol recovered
00:39:10. CP80 now skips CP tickless entry only during post-release cleanup,
before any SysTick/IRQ change; matching Cancel cleanup closes the lease.
Normal idle remains allowed otherwise. APP veto stays held through cleanup.
Read-only counter84 observes engagement; new Arm invalidates stale trace.

36 methods pass, including85 native cases, actual port early-return checks
and100 adapter lifetime schedules; link/ITCM/package audits pass. CP80 actual
full readback/CRCs match raw38fdaa024a717aed359bb9fc4247c391fe09a634e15bc24c7a1a679d4e88fa88,
protected3da4787bae63694437d97a3053274b8f7d6f733b8ee35007997278c052eaeb93.
Normal PFS/protocol recovered00:49:33 after one recorded companion restart.
UART9/10 startup split and phone10/11 renewal gaps are retained explicitly.

AP139 e4ec741d is installed for00:55:05–25, no AP/CP WFI. Expected build/PFS
received; pretest Bluetooth recovery is pending. Accept only actual clock/
mask transition plus cleanup-counter evidence and successful Cancel/status
queries. Safe refusal alone is not a fix. No global sleep, savings, cold or
soak qualification is claimed. Display resources/settings remain unchanged.

### Corrected AP138r2 installed and self-check received — 2026-11-07 00:33 UTC

AP138r2 541071d4 passed the unchanged exact-address pre-flash guard and is
installed for00:35:05–25. Correct exception base0x2809f700, CP stage record
0x28075a20 and idle TCB comparison value0x20001e7c match the verified CP79.
Expected build/PFS received; startup runtime record checksum is valid (0x40).
A recorded companion restart recovered encrypted protocol00:31:22. The prior
rejected AP138 artifact was never written. No global sleep or savings claimed.

### Pre-flash binding guard rejected AP138; normal remains installed — 2026-11-07 00:28 UTC

AP138 was not written. The pre-flash guard caught a builder variable reuse:
its exception-base option contained the stage-record address instead of the
bound power-management shared block. Original frozen artifact and rejection
JSON are retained. AP138r2 uses the bound exception-base explicitly and is
rebuilding for00:35. The exact identity/address guard is unchanged. Normal
AP2cc3478b/CP79 remains installed; no new hardware result or savings claimed.

### CP scheduler remains suspended; idle-bit correction — 2026-11-07 00:26 UTC

AP137 reset00:15:12 with runtime AP44/CP10: scheduler-suspended counter
nonzero, pending ticks<=1000, no validated CP exception entry. The idle-task
bit is invalid evidence: configBK_FREERTOS uses a char pointer at TCB offset52,
not an inline four-byte name, and the idle TCB is private CP DTCM. Do not infer
nonidle execution from AP137. Raw first-failure/v137 remains unchanged with an
added interpretation erratum. Normal PFS/protocol recovered00:17:59.

The corrected default-off probe compares pxCurrentTCB's value against exact
ELF symbol xIdleTaskTCB.35 (0x20001e7c), without dereferencing CP-private DTCM.
Its optional bit3 now validates current-generation InterruptsReleased53 in the
same snapshot instead of classifying pending-tick backlog. Twenty-four tests
pass, including private-pointer non-dereference, all flags/generations and
corrupt/torn trace checks. Startup now exercises runtime reads before arming.

AP138 040076de is installing for00:35:05–25 on unchanged verified CP79.
No clock, guard, wake-margin or display behavior change is included. Current
normal baseline completed a recorded encrypted reconnect00:24:11. Phone9
capture ended00:21:55; Phone10 started00:22:16 and received data00:22:19;
this is an explicit diagnostic capture gap, not a continuous soak.
No global sleep or new savings qualified; cold/soak qualification pending.

### Repeat prepared; CP flash integrity remains intact — 2026-11-07 00:09 UTC

Post-trial full CP79 readback and protected-block CRCs again match the package;
physical SHA90887765005f50163ee300bd872681963ddd026906a9a9bc4212102e9e1457da.
Normal PFS/protocol recovered00:06:51. UART8 closed00:07:07 and UART9 attached
00:07:10; exact gap retained. A recorded normal-image reconnect then failed
GATT discovery; one preserved companion restart recovered protocol00:08:44.
This startup/reconnect issue is separate evidence, not a clean qualification.

AP137 f70e1722, otherwise identical to the AP136 runtime probe, is flashing
for00:15:05–25 on unchanged CP79. SRAM/hash/ELF/config binding audits pass.
No admission guard or wake margin was relaxed after AP136's safe refusal.
No completed target clock transition, global sleep, savings or soak claimed.

### Runtime probe safely refused the target transition — 2026-11-07 00:06 UTC

AP136 returned without reset: one AP prepared/returned transaction, no fault,
CP resource preparation1 and final stage53. Detailed CP queries show clocks0,
low-voltage0, analog deadline reject2 (lead1022ticks), HAL permit reject16.
This is safe refusal/cleanup evidence, not a successful clock or mask-barrier
transition. Runtime fatal snapshot was not exercised. Saved under
controls/v136-safe-refusal with closed power (exit0), excluded from savings.
Normal AP recovered encryption at00:05:43; repeat AP137 f70e1722 is frozen
and SRAM-audited for00:15:05–25, otherwise unchanged. CP79 integrity readback
is in progress. Wake/deadline guards remain intact; no global sleep qualified.

### Final CP release proved; later execution remains stuck — 2026-11-06 23:53 UTC

AP135 reset at 23:45:12 with AP45 in the boundary encoding: valid matching-
generation stage53, CP_InterruptsReleased. This distinguishes the completed
final hook from initial restoration21. CP still failed native cancellation
later. Earlier AP134 sampled critical nesting2 unchanged >=1second; that is a
separate run, not an atomic combined snapshot. First-failure/v135 and closed
power are retained. Normal AP reopened PFS and recovered encrypted protocol
at 23:47:13 without physical intervention.

AP136 runtime snapshot is building for 00:02:05–25 Nov7 on unchanged CP79.
It reads existing CP exception-entry, idle-task, scheduler-suspended and
pending-tick state independently before reset. Exact ELF symbols, generated
shared block, non-cacheable MPU map and compiler-derived TCB name offset52,
name size4 and tick1000Hz are bound to the frozen artifact. Twenty-four
relevant tests pass plus targeted corrupt-record sanity cases. No CP behavior,
display, active-frequency or global-sleep change is included. Operator remains
asleep; diagnostics cannot qualify visual state, savings or an overnight soak.

### Distinct final-release marker installed — 2026-11-06 23:43 UTC

CP79 marker-only build is installed with full physical CP readback/CRCs
matched (first readback handshake failure retained; unique retry succeeded).
Raw1866c62c94942dc3dcf9baf421ae12c09be3a71b8be14a83406889371df36a14,
protected1dc80b983b59ad2ff5743fad308f28910bb08ed27cd609aa5d130dc33cdde179.
Normal AP recovered PFS/protocol23:35:56 and one recorded phone reconnect
23:36:54. UART7 was closed and UART8 renewed with actual received bytes.

AP135 13173a4a is installed for 23:45:05–25 UTC. Startup GATT recovery
failed before the test; a recorded phone cycle alone did not recover it.
One recorded companion force-stop/relaunch recovered encrypted protocol at
23:40:43. Pairing/app data were not cleared. This is not clean-startup or
visual-state qualification. Power is diagnostic only. The read-only boundary
encoding checks valid/current-generation records and distinguishes initial
restoration21 from final interrupt-release53. No WFI or savings claimed.

Pinned vendor CP reset code0x10 means NMI watchdog. The AP forced-reset path
can induce it; it is not independent proof of the original CP failure site.
The critical-history evidence remains count2 sampled stable >=1second.

### Sustained sampled CP nesting of two — 2026-11-06 23:33 UTC

AP134 reset at 23:32:19 with AP4a in its critical-history encoding:
critical nesting exactly two, sampled unchanged for at least one second with
no sampling gap over 100 ms. This is not a continuous register-mask trace.
First-failure/v134 preserves UART, phone, actual CP78 identity and power.
Normal AP rollback is in progress. Display and no-WFI controls unchanged;
no savings or global sleep qualified.

CP79 changes the final breadcrumb to a distinct InterruptsReleased stage53;
initial restoration remains stage21. Earlier AP131 stage21 cannot distinguish
these boundaries. Eleven native methods/84 cases and nine mailbox methods
pass, as do build/link/ITCM/package audits. CP79 remains unflashed here.
AP135 13173a4a for 23:45 is frozen and SRAM-audited, requiring actual CP79
readback before flash. Its default-off encoding distinguishes each current-
generation restoration boundary and rejects invalid/torn/stale records.

### CP remains prepared with nonzero critical nesting — 2026-11-06 23:24 UTC

AP133 reset at 23:18:11 with AP4a/CP10: cancelled=false,
prepared=true, resources=false and critical nesting nonzero. These are
independent live samples; the reset field does not contain the exact count
or prove a sustained interrupt mask. First-failure/v133 includes pre-rollback
UART, phone, CP identity and closed diagnostic power (exit 0). Normal AP
reopened PFS new=0 and recovered encrypted protocol at 23:19:49.

AP134 856ca73d is being installed for 23:32:05–25 UTC, unchanged CP78.
A default-off read-only diagnostic samples critical nesting during the active
native Cancel. Bits 0–2 saturate the count at seven; bit 3 requires the same
nonzero count sampled for at least one second with no gap over 100 ms.
Eight mailbox failure methods pass, including gap, wrap, count-change and
transaction isolation cases. The operator is asleep; further trials use UART
recovery checks without visual confirmation and cannot qualify fixed-display
energy. Halt hardware trials if physical recovery becomes necessary.
No AP/CP WFI, new savings, global sleep or overnight qualification is claimed.

### Live CP state probe installed — 2026-11-06 23:14 UTC

AP133 e69d1411 on byte-verified CP78 is installed for 23:18:05–25 UTC.
The CP-state diagnostic passed 20 relevant tests, SRAM execution audit and
ELF-address/size/hash binding checks. Received startup emergency record has a
valid checksum with cancelled/prepared/resources/critical all zero. Existing
PFS reopened and encrypted protocol recovered at 23:13:43; operator confirms
dim Nadir updating and Bluetooth connected. UART7 and bounded power capture
have received data. This is a no-WFI diagnostic; no sleep or savings claimed.

### Adapter correction insufficient; live CP state diagnostic building — 2026-11-06 23:07 UTC

Physically verified CP78/AP132 still reset at 23:02:11 with native Cancel
physical timeout AP48/CP10. The reproduced adapter lifetime defect is corrected
in this candidate but is not sufficient to resolve the hardware failure.
First-failure/v132 retains the matched setup, UART/phone, actual CP identity
and closed diagnostic power (exit 0). Exact normal AP restored PFS new=0 and
encrypted protocol at 23:03:58. No AP/CP WFI or new savings qualified.

AP133 for 23:12 is building, unflashed. Its default-off read-only diagnostic
binds existing CP cancellation/prepared/resources flags and RTOS critical
nesting to the exact verified CP78 ELF symbol addresses and sizes. The pinned
selected MPU map makes private CP SRAM non-cacheable; AP invalidates its own
read cache lines before sampling. Independent live reads are not an atomic
snapshot and one nonzero nesting sample cannot prove a sustained mask.
Twenty relevant tests pass, including all flag combinations, multiple nonzero
counter values and read-only behavior. CP78 behavior is unchanged for this
next control. Display remains fixed; global sleep and soak remain unqualified.

### Current-generation restoration proved; adapter lifetime candidate installed — 2026-11-06 22:59 UTC

AP131 reset at 22:48:11 with AP47 in its CP SRAM encoding: valid record,
matching transaction generation, last stage CP_Restored. CP10 alone did not
identify that generation. This proves restoration was reached and redirects
investigation to later mailbox service/acknowledgement. It does not prove that
no earlier copy of the queued request was consumed. First-failure/v131 and
closed diagnostic power retained. Normal AP recovered PFS/protocol at 22:50:10.

The pinned CP adapter rotates two payload buffers although AP FIFO holds three
pointers. Host execution of the actual sender reproduces the third send
overwriting the first queued payload. CP78 board overlay uses four slots:
three queued plus one popped-but-not-yet-copied AP snapshot. The actual patched
sender passes 100 wraparound pop/publish/copy schedules and full-FIFO refusal;
12 methods pass including 84 native CP cases. Link/ITCM/package audits pass.
This is a reproduced lifetime defect, not yet the proven hardware sleep cause.

CP78 full readback/CRCs match raw d36525b32e4c3f2eced5658336a6e490e1ca29eb1ebaacafeca85185c99c1c17,
protected dd6f4f511f695df949d62f75003189aaff1dec998390eda77286ec5011b22c9a.
Normal AP reopened PFS new=0 and recovered encrypted protocol at 22:58:09.
UART renewal split the received prefix/recovery across UART6/7, both retained.
AP132 7edb53b2 is flashing for 23:02:05–25 UTC, same no-WFI clock/mask control;
SRAM audit passed. Display resources/settings unchanged. No global sleep,
new savings, cold qualification of CP78 or overnight qualification claimed.

### CP77 ordering change insufficient; next diagnostic held for recovery — 2026-11-06 22:45 UTC

AP130 0b8d82d0 on physically verified CP77 reset at 22:38:11 UTC with the same
native Cancel physical timeout AP48/CP10. The full tickless-boundary handoff is
not a hardware fix. This reset field does not prove the failed generation
entered restoration. First-failure/v130 preserves raw pre-rollback evidence,
closed power (exit 0), phone logs and the separately recorded recovery cycle.
No AP or CP WFI occurred in this control; no savings are accepted.

Exact normal AP 2cc3478b is restored and reopened existing PFS new=0 at 22:39:16.
Encrypted/protocol recovery remains absent from received data as of 22:44:13.
One explicitly recorded phone off/on cycle completed at 22:42:44; recovery is
still unverified. The operator has been asked for current state/full cold
power if disconnected. No next flash occurs while that physical action is pending.

An AP-only default-off diagnostic now invalidates and reads the CP-owned,
cache-cleaned stage record, checks stable magic/inverse and transaction
generation, and encodes valid/same-generation/restored/veto-restored bits.
It never writes CP memory or consumes FIFO. The address is derived from the
verified CP77 ELF symbol; a frozen binding and flash guard tie it to CP77 raw
SHA256. Nineteen relevant tests pass, including torn/invalid/stale records
and read-only behavior. AP131 d4c5b123 for 22:48 is built and SRAM-audited,
but unflashed and held until recovery; rebuild if its slot becomes too close.
CP77 remains installed. Display unchanged; global sleep/soak remain unqualified.

### Full tickless-boundary handoff installed for control — 2026-11-06 22:36 UTC

CP76/AP129 reset at 22:30:11 UTC with native Cancel physical timeout AP48/CP10.
Moving release past the inner PM critical section was insufficient. Evidence
is in first-failure/v129, including closed diagnostic power (exit 0). Exact
normal AP restored PFS new=0 and encrypted protocol at 22:31:35 UTC.

CP77 two-phase handoff is now physically verified against the complete CP
partition and active CRCs: raw 47aea9495478be2d8e5b8af6d322ba13e2c2d01c28c648c12abfbd6caf8c952c,
protected 37ce05d2cc08b6f36101c9b1add52bd5ccf670cf02f6829c3114d9be667db0d2.
Its normal AP reopened PFS new=0 and recovered encrypted protocol at 22:32:56.
Eleven host methods pass, including 84 CP cases, masked-release rejection,
one-shot release, Cancel before release, and actual tickless-wrapper normal
and abort execution. Twenty-two additional overlay tests pass (two skipped),
and linked ITCM/package checks pass. These are not hardware sleep qualification.

AP130 0b8d82d0 is installed for 22:38:05–25 UTC: no AP or CP WFI, same clock/mask
control, final release after the full tickless critical exit. Expected build,
existing PFS and encrypted protocol at 22:34:02 were received. Visual state is
requested. Display resources/settings stay fixed; failed/control intervals do
not establish savings. No global retained sleep or overnight qualification yet.

### Full tickless boundary identified; two-phase candidate building — 2026-11-06 22:28 UTC

CP76 was physically read back and matches raw d967743162745282b653c9bbc042fef0a64a6c8f8db38605002af9b245df54c2.
Normal AP reopened PFS new=0 and encrypted protocol at 22:22:06. AP129
0e8a1695 then recovered protocol at 22:23:23; operator confirms dim Nadir/BT.
Its 22:30:05–25 control tests release after the inner PM critical exit only.
Further caller review found a second, outer critical section in the selected
FreeRTOS tickless wrapper, so CP76 does not establish globally unmasked service.

CP77 splits resource restoration/veto from AP release. It keeps AP ready held
until the tickless wrapper completes both original time-accounting calls,
restarts SysTick and exits its final critical section. The release hook checks
PRIMASK and BASEPRI and refuses release while either remains masked; it is
one-shot and Cancel clears pending release. The original tick update code is
unchanged. Eleven host test methods pass, including 83 native CP cases and
execution of the actual transformed wrapper's normal and abort paths. CP77
and matched AP130 (22:38 slot) are building, unflashed and unqualified. CP76
remains installed. No new global-sleep or savings claim. Display stays fixed.

### CP hardware IRQ state intact; interrupt-release ordering candidate — 2026-11-06 22:19 UTC

AP128 19f27ea4 reset at 22:16:11 UTC with AP43 in its frozen CP IRQ encoding:
CP mailbox system route and peripheral IRQ enabled, hardware interrupt mask
clear, core running. These flags match startup and do not measure CP PRIMASK,
BASEPRI, NVIC or RTOS critical nesting. Combined with AP127's queued CP request,
this narrows the next investigation to CP software service after restoration.
First-failure/v128 preserves raw data and closed diagnostic power: 45980 samples,
exit 0; excluded from energy savings. Exact normal AP restored, PFS new=0 and
bonded/encrypted protocol recovered at 22:17:38 UTC. Display remains fixed.

The overlay released AP from native readiness before CP exited its outer RTOS
critical section. CP76 now places native resume/release after that exit, so AP
cannot be released by that hook while the outer critical section remains held.
An execution test of the actual transformed pm_low_voltage_process verifies
restoration occurs under the lock and AP release afterward. Ten native-alarm
test methods pass, including prior callback/unregister race coverage. CP76 is
building; this is a contract-ordering candidate, not a proven root cause or
hardware fix. CP75 remains installed. No global sleep or new savings qualified.

### CP does not consume the queued request; CP IRQ snapshot installed — 2026-11-06 22:12 UTC

AP127 62351993 reset at 22:04:11 with AP45 in its alternate PM FIFO encoding:
CP RX pending=1, AP RX pending=0, AP mailbox system/NVIC/peripheral route enabled,
AP PRIMASK and BASEPRI both zero. This matches the previous physical-timeout
cadence. CP had not consumed the request; an AP-side mask or disabled route did
not explain it. Do not infer entry into the failed CP handler from CP10, which
may reflect an earlier generation. First-failure/v127 retains full evidence and
closed power. Exact normal AP was restored; PFS new=0 and bonded protocol at
22:06:50 received. No global sleep or savings are qualified.

AP128 19f27ea4 is now installed on byte-verified CP75 for 22:16:05–25 UTC.
It keeps the same no-WFI control and reads CP system mailbox routing, peripheral
IRQ enable, hardware interrupt mask and core-halted status into the AP reset
reason. The frozen configuration selects this alternate four-bit encoding.
Its startup route-check record is received with valid checksum and baseline43:
CP route/IRQ enabled, hardware mask clear, core running. Full baseline words
are preserved in ap128-startup-route.json. Twenty-one tests pass, including all
bit combinations and read-only behavior. The operator confirmed dim Nadir/BT.
These are diagnostics, not completed sleep or energy results. Display stays fixed.

### CP75 cancellation cleanup insufficient; FIFO diagnostic pending — 2026-11-06 21:59 UTC

CP75 full physical readback matched the packaged bytes and CRCs. On the normal
AP it restored PFS new=0 and bonded protocol at 21:48:53. AP126 306417ef then
received bonded protocol at 21:50:48; the operator confirmed fixed dim Nadir/BT.
The no-WFI control reset at 21:52:11 with the same submitted native Cancel
physical timeout (AP48, CP10). The cancellation lock change is not sufficient.
CP10 does not establish cleanup progress for that exact generation; an earlier
transaction may have restored it. First-failure/v126 preserves raw evidence,
actual CP identity and the closed power recording. No new savings are claimed.

Exact normal AP restored and PFS new=0 received; encrypted recovery is not yet
in the trace. Current operator status is requested and the next flash is held.
CP75 remains installed. A default-off AP-only diagnostic now snapshots, without
consuming data or changing routes: CP RX pending, AP RX pending, enabled AP
mailbox route/NVIC and nonzero AP PRIMASK/BASEPRI. It encodes four bits in the
AP-owned 0x40–0x4f range before attempting CP-dependent emergency output. The
frozen configuration distinguishes this alternate encoding from normal restore
reasons; all values quarantine the following retained attempt. Twenty tests
pass, including every bit combination, BASEPRI-only masking and read-only
register behavior. AP127 is building for 22:04:05–25 UTC; unflashed.
Global sleep/energy savings/overnight remain unqualified. Display stays fixed.

### Durable trace identifies native cancellation timeout — 2026-11-06 21:45 UTC

AP125 68c94ae7 reset at 21:38:11 UTC with AP reason 0x48: a submitted native
alarm Cancel transaction timed out on physical completion. This rules out
pending transmission and the other classified PM operations, but does not prove
which CP instruction stalled or that this was the first cancellation. CP reason
was 0x10. The following boot refused another retained attempt as intended.
First-failure/v125 preserves the received boot cause, UART/phone/readback and
closed power (exit 0). No AP or CP WFI occurred in this polling control.

Exact normal AP restored, PFS new=0 and bonded protocol recovery received at
21:40:11. The CP cancellation routine called RTC unregister while holding an
outer interrupt-disabled section, although the pinned RTC driver manages its
own critical sections. CP75 now disarms native callbacks under its state lock,
releases that lock for RTC unregister, then reacquires it to finalize status.
A callback racing unregister is ignored. Durable trace restoration occurs only
after successful unregister; failure preserves evidence, faults the contract,
holds the APP veto and prevents rearming. This is an implemented candidate,
not yet a hardware-proven explanation or fix.

Nine native-alarm test methods pass, including 81 CP cases with callback racing
and unregister failure. CP75 builds, passes linked audits and packages with
unchanged ABI48, Bluetooth controller and calibrated Wi-Fi-off configuration.
Raw SHA256 d02ff2f4b7e572dd377874e70599d4bd88c25fe969fb05b7f4a56e4d3f6f2736.
It is not yet flashed. AP126 is building for 21:52:05–25 UTC; UART capture was
renewed and actual reception must be verified before flashing. Global sleep,
energy savings and overnight remain unqualified; display work stays last.

### Emergency route verified; fatal CP response still absent — 2026-11-06 21:31 UTC

AP124 538e54cf received a valid checksum-protected startup route-check record
through the emergency CP transport, and bonded protocol recovered at 21:21:47.
The operator confirmed dim Nadir/BT before the 21:26 slot. The no-WFI polling
control again reset at 21:26:11 with AP52 / CP10. No fatal record arrived, although
the startup route check worked before and after reset. This localizes a loss of
CP output/service after transition; it does not identify the exact failed PM
transaction or prove sleep. First-failure/v124 retains UART/phone/readback and
closed power (exit 0). All failed/reset intervals are excluded from savings.

Exact normal AP restored, PFS new=0 received, encrypted protocol recovered
21:28:24. CP74 remains physically verified. Source now adds default-off retained-
test transaction categories in the AP-owned reset-reason range 0x48–0x4e,
without touching CP-owned fields or allocating unknown persistent registers.
The categories distinguish native cancel, mask preparation, state/count query,
other native/PM commands and pending transmission. Ordinary mailbox reasons
remain 0x50–0x5f. All 0x40–0x4f sleep diagnostic reasons quarantine the next
retained attempt. 20 focused tests pass; these new categories are not yet flashed.
AP125 is building for 21:38:05–25 UTC. Its purpose is a durable failure label,
not energy qualification. Global sleep, savings and overnight remain unqualified;
display configuration remains fixed.

### Polling timeout reproduced; emergency output route corrected — 2026-11-06 21:17 UTC

AP123 168545fb reproduced AP122's PM-channel timeout at 21:10:11 UTC:
AP reason 0x52, CP vendor reason 0x10, no durable native stage. Neither core
entered WFI in this control. First-failure/v123 preserves raw UART, phone data,
CP readback identity and the closed 90,112-sample power recording (exit 0).
The operator confirmed dim Nadir/BT before the slot; encrypted protocol was
received at 21:00:40 after one recorded pre-slot phone Bluetooth cycle.

No fatal UART record arrived. Review found an implementation mistake: the dump
used AP UART0, whose pins are owned by four-bit SD. Actual runtime logs use the
CP mailbox backend. The unsuitable direct-UART dump and its API were removed;
the superseded source is preserved in resume-20261106/before-fatal-route.

A default-off retained-test diagnostic now uses the SoC's existing emergency
fault-report transport with independent buffer storage and bounded physical
mailbox publication. It bypasses normal logging and logical-channel workers,
does not drain RX/ACKs, remux pins or change clocks, and never overwrites text
still owned by CP. The 17-word checksum record includes PRIMASK/BASEPRI. A startup
record with reserved channel UINT32_MAX checks this route before a trial; failure
faults the sleep contract. 16 focused host tests pass. Its hardware output has
not yet been verified. AP124 is building for 21:26:05–25 UTC; do not flash until
normal recovery is verified. Slot expiration requires rebuilding.

The exact normal AP was restored, its build/PFS new=0 received. Encrypted
protocol recovery is not yet in the saved trace; operator status was requested.
UART renewal is explicit (uart3 to uart4); uart4 has received bytes. CP74 remains
installed with verified physical identity. No sleep, energy savings or soak
qualification is claimed. Display configuration remains fixed.

### Bluetooth recovery verified; mailbox diagnostic prepared — 2026-11-06 20:55 UTC

After AP122, the operator reported Bluetooth would not connect. The preserved
cold trace subsequently showed bonded encryption at uptime 90.999 seconds and
protocol at 92.581 seconds, before the known-CP rollback. This was delayed
recovery, not proof of a persistent CP74 Bluetooth regression. The normal AP and
qualified Wi-Fi-off CP control then recovered encryption/protocol at
20:50:28–29 UTC; the operator also confirmed connection. PFS remained new=0.

CP74 was reinstalled and its full 1,114,112-byte physical partition read back:
active protected bytes and CRCs match the frozen package. With the normal AP,
it recovered encryption/protocol at 20:54:22–23 UTC. The diagnostic AP remains
unflashed. No bonds, PFS or display resources were erased or changed.

The fatal UART diagnostic now uses a checksum-protected 17-word snapshot,
including PRIMASK and BASEPRI. It copies the snapshot into a word array before
formatting. All 59 focused host tests pass. Earlier AP123 slots expired without
being flashed; a fresh polling control is building for 21:10:05–25 UTC. This
control enters neither core's WFI and is intended to identify AP122's PM mailbox
timeout transaction. It is not a sleep or energy qualification. Deep sleep and
savings remain unqualified; display work stays last and overnight is deferred.

### Polling reached a mailbox failure; bounded fatal evidence added — 2026-11-06 20:44 UTC

AP121 direct wake still reset20:28:10 withAP4f/CPea. A direct sender is not a
proven fix. First-failure/v121 retained. Normal AP was restored; another cold
capture verified normalID/PFSnew=0 and bonded protocol before AP122.

AP1228c0d17ac omitted both cores' WFI while keeping the mask/clock/restore
control. It reset20:35:11 withAP52 (PM channel2 mailbox timeout), CP10 (vendor
reason, durable native trace cleared), rather than AP4f/CPea. This indicates a
different, later communication failure; the exact transaction remains unknown.
Do not claim completed sleep from it. First-failure/v122 and closed power are
preserved. Exact normal AP restored20:36:24, PFSnew=0 received; encrypted protocol
recovery is not yet in saved UART. A full cold recovery is requested before
another control. AP122 visual answer arrived after its slot; record timing as
uncertain rather than treating it as matched display energy qualification.

The old SRAM timeout snapshot did not survive this whole-chip reset. A default-off
retained-test diagnostic now emits its existing15-word/checksum snapshot directly
to the configured T5AI debug UART before reset. It bypasses logger, mailbox,
blockers and clock acquisition. A single10ms AON budget bounds FIFO writes and
last-bit drain; original PRIMASK is restored. Inactive/broken UART returns failure
and the reset still proceeds. 59 host methods pass, including actual formatter,
AON wrap, blocked FIFO/drain and no logger recursion. Common code is unchanged.
AP123 is building for20:48:05–20:48:25 UTC with the same polling mode/CP74 and
fatal diagnostics. No new AP flashed yet. Display remains fixed, no savings or
global sleep qualified, and overnight remains deferred.

### RTC route did not fix AP wake; direct sender implemented — 2026-11-06 20:21 UTC

AP120 reset20:15:10.007 with the same0x4f000ea3: mask returned, APP veto restored,
AP independent guard fired. Enabling the AP RTC/GPIO routes did not resolve the
stall. First-failure/v120 preserves evidence and closed meter capture (exit0);
no failed/reset interval enters savings. Exact normal AP was restored20:16:23,
PFSnew=0 and encrypted protocol recovered20:16:43.302. CP73 remains installed.

Pinned Tuya wakes AP by a direct mailbox send before leaving its sleep return
path. Native resume had deferred that wake to a CP worker. Source ABI48 now
sends the generation-tagged native resume command with one mb_chnl_write after
resource restoration, APP veto and ready release. The existing pinned mailbox
API is nonblocking and returns busy/error directly. No retry, RTOS delay or
worker queue is introduced in that path; send failure faults the sleep contract.
All controls remain default off, and normal APP veto behavior is unchanged.

53 focused host methods pass, including the actual overlay sender's one-attempt
success/busy/error behavior and CP restore/veto/ready ordering. CP74 builds,
passes ITCM/link audit and packages successfully; NOT flashed. AP121 is building
for20:28:05–20:28:25 UTC on the same normal AP wake routes. Next: verify requested
cold power action/current reception, validate/read back CP74, audit and flash
fresh AP121, and run the bounded no-CP-WFI control. A UART renewal gap around
20:20:24–25 is explicit; do not call that gap a received cold-start qualification.
No global sleep, savings or overnight qualification yet; display phase remains
last and display resources/configuration remain fixed.

### Mask return proved; AP wake-route control installed — 2026-11-06 20:11 UTC

AP119 reset20:06:10.007 with PMU word0x4f000ea3. CP durableea decodes trace80,
mask-return40 and APP-veto-restored stage42. The mask leaf really returned and
the CP veto call completed. AP reason4f is the independent guard, rather than
an explicit sampled ready/core/memory or missing-event timeout. This localizes
the remaining failure to the AP wake boundary; it does not prove global sleep.
First-failure/v119 preserves raw UART, receive timestamps, phone logs, physical
CP identity and power prefix. Closed meter capture47508 samples/exit0 is kept
alongside it. Failed/reset intervals are excluded from savings.

Exact normal AP was restored20:07:05, reopening PFSnew=0. A second full cold
check then received normal boot, existing PFS, bonded encryption and protocol;
operator confirmed the requested power action. CP73 bytes remain unchanged.

AP119 deliberately disabled the AP local RTC/GPIO system wake routes through
CP_WAKE_ONLY. AP120 restores those normal routes on the same CP; configuration
diff contains only that selector and the UTCslot. AP120d50a49b2 passes SRAM,
image-map and unchanged-resource checks; encrypted protocol recovered20:10:56.
Its bounded slot is20:15:05–20:15:25 UTC. Result and visible setup confirmation
pending. Next: use this control to distinguish a missing AP wake route from a
post-wake restoration fault. Do not promote to global sleep or savings without
a completed contract and separate qualification. Display and activeCPU240MHz
remain fixed; overnight remains deferred.

### Cold recovery verified; resume diagnostic installed — 2026-11-06 20:03 UTC

New cold UART attachment confirms normal AP2cc3478b, existing PFSnew=0,
bonded encryption and protocol before RTC synchronization. Operator confirms dim
Nadir updating, Bluetooth connected, USB/no cell and quiet notifications.
This is one cold recovery, not overnight qualification. The previous receiver
survived host suspension; wall-clock coverage must not be interpreted as an
unattended overnight capture. A fresh bounded UART receiver is receiving data.

CP73 ABI47 is installed and its full1114112-byte physical readback matches the
package and active CRCs (physical SHA256
0dbbf70395e7b79ca69ff8036458433f905c7557b481d35993e3c6e5ca052bde).
AP1197cd29a23 passed SRAM/image/resource audits and is running for the new
20:06:05–20:06:25 UTC no-CP-WFI mask/barrier slot. Bonded encryption20:01:17.523
and protocol20:01:18.471 were received; operator confirms visible dim Nadir.
Phone ADB/logging and FNB58 samples are now received. No commanded phone cycle.

Result pending. Preserve first failure, use differentiated AP timeout bits and
CP durable mask-return/veto stage to choose the next fix, and restore exact
normal AP on fault. Display resources/configuration remain fixed. No global
sleep, measured saving or overnight-soak qualification is claimed.

### Resume diagnostics verified; next cold check pending — 2026-11-05 22:33 UTC

CP73 ABI47 and compile-only AP1184b165995 pass 52 focused host tests, pinned
CP link/ITCM audit, independent package replay, image-map and SRAM-call/literal
audits. All eight ready/core/memory blocker combinations and every timeout
startup quarantine are modeled. Mask-return evidence survives through APP veto
restoration; CP stops writing before revoking ready. Vendor CP boot reasonff
remains excluded from valid durable traces. Reserved AP mailbox/Pebble reason
ranges remain separate.

A compact blocker encoding fits the AP SRAM routine within four bytes of AP117.
Final kernel heap is28928 bytes (28KiB minimum retained). An intermediate
statistics-to-PSRAM experiment was removed: final statistics/wake state retain
the previous SRAM layout. Display resources are byte-identical to normal, SHA256
62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46.
The compile-only UTCslot=1 is deliberately expired and must never be flashed.

No new candidate has been flashed: installed normal AP2cc3478b/CP72r2 recovered
bonded encryption22:18:11.107 and protocol22:18:12.203 after AP117 rollback;
PFSnew=0 was verified. The requested full cold-power check after that failed
transition remains pending. Next: verify a new cold normal boot and operator
Nadir/Bluetooth recovery; build a future-slot AP, flash/read back CP73, then run
the bounded mask/barrier control. First-failure/v117 and its closed35980-sample
meter capture remain preserved; no sleep or savings claim. Phone capture2 ended
22:31:34; UART4 is bounded through approximately22:55:29, with an explicit gap
between UART3 closure22:25:17 and UART4 opening22:25:29. Overnight soak remains
deferred; display phase remains last.

### CP restoration reached; timeout causes separated — 2026-11-05 22:24 UTC

AP117 c2f595da reset at22:11:19.007 with durable word0x4f000953: AP resume
failure4f, CP stage21 (Restored). This advances beyond the earlier stage19
boundary but does not prove the mask leaf returned: a safe final-entry refusal
also reaches stage21. First-failure/v117 retains UART, phone and power evidence;
35980 meter samples closed exit0. The complete interval includes reset/startup
and is excluded from savings. The preceding programmer final-response timeout
was recovered by read-only AP readback: all1745288 protected bytes and CRCs
matched before reboot. That separate evidence is first-failure/v117-programmer.

Exact normal AP2cc3478b was restored at22:15:04; existing SD PFSnew=0 and
240MHz active clock were received. CP72r2 remains installed. The next full
watch power-off check is requested, not yet confirmed. Display/resources remain
fixed; no global sleep or new energy saving is qualified.

New source diagnostics are default-off: ABI47 preserves an owned mask-return
bit across CP restoration and keeps the durable stage through APP veto restore,
stopping before ready revocation can release AP. Explicit AP reset causes40–47
encode sampled CP-ready/core-clock/memory-clock blockers;40 alone identifies a
missing resume event. Independent guard4f remains. Every new timeout reason
quarantines subsequent startup; reserved mailbox50 and Pebble60 ranges remain
separate. CP73 builds and passes ITCM/link audit and verified packaging; it has
NOT been flashed. AP118 is compile-only with an intentionally expired slot.
Next: finish host/SRAM checks, verify cold normal recovery, then build a fresh
future-slot AP and physically verify CP73 before the bounded no-WFI trial.

### Safe query refusal fixed; actual AP protocol coverage added — 2026-11-05 22:04 UTC

AP116 refused readiness at21:59:06.242 with Controller fault2/gen0. Its slot
reports zero requests, preparations and entries, with no reset or CP mask stage.
The new MaskBarrierCount query was missing from the AP read-only whitelist;
CP handling and host coordinator mocks had succeeded, but the real AP query
function rejected it. First-failure/v116 preserves this safe refusal. Meter
closed41980 samples/exit0; no sleep or mask execution occurred.

The AP now allows the new read-only query. Review also moved arm generation-echo
validation into the common arm path, covering every arm mode rather than an
outdated subset. Actual AP tests exercise every protocol read-only operation,
reject mutating/unknown/null queries, and reject mismatched generation echoes
for every arm mode. 50 focused methods pass. CP72r2 and its physical readback
remain unchanged. AP117 is building for22:11:05–25 on the same no-WFI control.
No further cold check is needed after this refusal: no alarm or clock transition
was armed. UART3 is renewed with an explicit inter-slot capture gap; actual new
reception must be verified before flashing. Display/resources unchanged; no
qualified global sleep or savings. Next: audit/flash AP117, preserve the first
actual mask/barrier result, then decide the next WFI/wake isolation from evidence.


### Cold recovery verified; no-WFI mask probe installed — 2026-11-05 21:56 UTC

Operator confirmed the requested full cold-power check. New UART attachment2
shows normal AP2cc3478b, existing SD PFS new=0, bonded encryption and protocol,
then phone time synchronization from the default RTC date. Startup BlobDB
retry/token and connection-parameter retry messages are retained; this is one
functional cold check, not soak qualification. See cold-control-qualified.json
under deep-sleep-review-20261101/resume-20261105 for the exact receive evidence.

CP72r2 ABI46 is now installed; full1114112-byte physical readback matches package
and active CRCs (partition SHA256
3c7077eb39d9532cbc48771ef60d3c7be49efdb6dffdc4da72f2dd889d240321).
Fresh AP11629b0a58e passed SRAM/map/resource audits and is installed for the
21:59:05–21:59:25 UTC mask/barrier-only slot. Encryption21:53:51 and protocol
21:53:52 recovered naturally, without a phone cycle. Meter reception is verified;
visual state confirmation is pending. No trial outcome or savings claim yet.

Only a returned mask leaf can increment MaskBarrierCount. Completion requires
exactly one returned probe, zero CP WFI attempts and matching before/after mask.
Stage50/51 distinguish entry/return from the former WFI19/20 boundary. Captures
are bounded (UART2 through about22:05, renewed phone2 through about22:31); no
night soak. Next: preserve the first result, restore exact normal AP on fault,
and use this control to separate mask/store/barrier from WFI/wake routing.


### Mask/barrier candidate built and audited; cold check pending — 2026-11-05 21:48 UTC

The final ABI46 CP72r2 build and compile-only AP116r2 (5b673752) pass link,
SRAM/ITCM and image-map audits. Packaging independently replays. 48 focused
host methods pass, including actual ARM leaf execution with unmapped stack/XIP,
78 modeled CP cases, trace-ownership conflict, and AP safe-refusal retry. A
separate CP MaskBarrierCount increments only after the leaf returns; clock
preparation alone cannot qualify it. Completion requires one returned probe,
zero CP WFI attempts and balanced mask state. All new controls default off.

CP72r2 and AP116r2 have NOT been flashed. AP116r2 deliberately has an expired
UTC slot, and the resumed flash runner was verified to reject it before serial
access. A new future-slot build is required after the cold control is qualified.
CP72r2 raw SHA256 df2b1e8d4c3fea7df8d406e9fe81b737379f86a69c9915f1797a2d1b85035f12
is recorded independently in resume-20261105/checkpoint.json; use the package
manifest/checkpoint as the authority for exact bytes. Source pins now describe
ABI46; preserved CP71 source/pins and physical readback remain available.

Actual device state last verified by received boot/storage data: normal
AP2cc3478b, existing SD PFS new=0, CP71 ABI45. A full cold-power check is requested
and still unconfirmed; the new UART stream has ordinary ADC frames, no new boot.
No successful global sleep, new usable savings or overnight qualification.
Next: operator removes all watch-side power/debug for at least10seconds while
keeping the PC adapter powered, then verify new boot, PFS and encrypted protocol;
rebuild a future slot, install/read back CP72r2 and run the no-WFI isolation.


### Shallow CP WFI also fails; mask/barrier probe prepared — 2026-11-05 21:41 UTC

AP11556ac9a77 on full-byte-verified CP71 ABI45 cleared and read-checked CP
SLEEPDEEP while retaining the same domain/mask/clock path. It watchdog-reset
21:33:11.007 with durable0x4f000933: CPstage19, no stage20. Clearing that SCR bit
did not remove the boundary failure. This alone does not prove the PMU hardware
never gated: its separate global/WFI admission configuration remained enabled.
Pre-slot encrypted protocol was not received for AP115. Post-reset zero request
counters belong to the new boot and cannot erase the pre-reset failed attempt.
First-failure/v115 is frozen; its meter closed29980 samples/exit0. No usable saving.
Exact normal AP2cc3478b was restored successfully; CP71 remains installed.

A new default-off ABI46 mask/barrier control is implemented, not flashed. It
uses a stack-free ITCM leaf to set the mask, DSB, restore the original word and
DSB/ISB, with no WFI. It has separate durable enter/return markers and does not
increment the CP WFI counter. AP stops after one returned clock cycle, requiring
zero CP WFI attempts and balanced mask state. Actual ARM instructions are
emulated with unmapped stack/XIP, preserving other-owner register fields.
Host tests also cover counter initialization and trace-ownership refusal; final
checks/builds and source pins remain separate from hardware qualification.

A full watch cold-power check was requested before another live diagnostic to
clear always-on state carried across failures. The fresh bounded UART receiver
has received CRC-valid ordinary data; no new cold boot has yet been observed.
Normal retained/tickless defaults remain off, CPU240MHz, display/resources fixed,
overnight deferred. Next: verify cold boot, storage and encrypted recovery, finish
and audit the mask/barrier image, then run one bounded isolation with rollback.


### Domain-retention real-WFI control still resets — 2026-11-05 21:24 UTC

CP70 ABI44 passed pinned build, linked ITCM audits and independent package replay.
Its full 1114112-byte physical readback matched the package and active CRCs
(partition SHA256 861c8c7985cc82e485c757d940e73dc36bdac76611930061f27639106573c126).
AP114 b29a67a0 retained domains but still selected actual masked CP WFI and the
full clock transition. The 21:21:05–25 trial watchdog-reset; reboot21:21:23.007
reported durable0x4f000933, CPstage19 without immediate post-WFIstage20. Omitting
that HAL domain/isolation shutdown was insufficient. No completed global sleep.

First-failure/v114 preserves raw UART, phone events, stage/snapshot review and
power data before rollback. The diagnostic meter closed32980 samples; failure
and reboot energy do not establish usable savings. A commanded pre-slot phone
cycle preceded delayed received encryption21:20:20 and protocol21:20:21. Visible
content remains unconfirmed after the flash. Exact normal AP2cc3478b was restored;
UART verifies existing SD PFS new=0. CP70 remains installed. Normal retention and
tickless defaults remain off; display configuration/resources are unchanged.

Next isolation is an explicit default-off shallow CP WFI control: same hardware
mask, analog fallback, domain retention and clock transition, but CP SLEEPDEEP
cleared and read back, with original SCR restored on all returns/refusals. This
separates architecture/global admission from the masked wake path. Host tests
and hardware qualification are recorded separately in resume-20261105. Preserve
CP70 and the normal AP rollback. No overnight qualification is running.


### Resume record: analog wake failure and domain isolation — 2026-11-05

The completed AP113 (da9bad92) / byte-verified CP69 trial on November 2 passed
final admission with the owned 32 ms analog fallback enabled, then watchdog-reset
at 06:18:28 UTC. Durable reset word 0x4f000933 preserves AP guard 0x4f and CP
stage 19 (pre-mask/WFI), with no immediate post-WFI stage 20. This brackets the
failure; it does not prove execution of WFI or global sleep. First-failure/v113
and its power/reset supplement are immutable under the deep-sleep-review-20261101
artifact directory. The temporary current reduction preceded reset and is not
qualified savings. Exact normal AP 2cc3478b was successfully restored afterward;
CP69 was the last installed CP. No newer physical image is assumed after the crash.

The interrupted AP114/CP70 domain-retention diagnostic was never flashed. Its
explicit ABI44 operation retains domains while still selecting actual masked CP
WFI and the full HF clock transition; the older domain control also bypassed WFI
and therefore did not isolate this boundary. The new option defaults off. Builds
and host checks are being recovered in resume-20261105, with completion and
hardware results to be recorded separately. Display settings/schedules remain
unchanged; normal retained/tickless defaults remain off, active CPU 240 MHz.
Overnight qualification is deferred. No new qualified energy savings.

Next: finish and audit isolated CP/AP builds, reconcile package source pins,
verify received UART and setup, then run one bounded domain-retention control
with independent watchdog recovery and exact normal AP rollback available.


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

Known AP `2cc3478b…` / CP `d4d65559…` are restored. Final UART verifies 240 MHz,
existing SD PFS (`new=0`) and bonded/encrypted protocol recovery at 00:14:32 UTC.
CP was unchanged throughout this continuation. Final visible Nadir confirmation
is pending; captures stopped at 00:20:38 UTC. No overnight monitoring is running.

Implemented: CP-ready held until flash/peripheral restoration finishes, unsafe
cancel rejection, full SRAM PRIMASK protection, actual PSRAM/controller ownership
checks, AON-write/clock leases and stage diagnostics. Thirty-six affected tests
pass. V5 CP and retained AP builds/packages are **unflashed and unqualified**.
The early-alarm restore-order fence is covered by native-code tests; its relevance
to the hardware freeze remains unproven.

Independent watchdog probes rebooted, but boot cause and SRAM attempt markers
did not establish reset attribution or survival. They lacked the CP AON-write
lease and do not qualify the watchdog. The revised source requires that lease,
keeps APP awake, and arms only in an explicit two-second UTC slot (default zero,
disabled). These revisions are host-tested, not flashed. Do not replay the older
automatic probe images. First failures and exact normal recovery are preserved.

Next: qualify the leased, expiring-slot guard and reset-surviving evidence before
another retained pilot, then verify clocks, retention, timers, reconnect/cold-power
and the deferred soak. Known-CP rejected-WFI control radio failures also need
operator/context correlation. No new measured savings; display remains last.
See [coordinated sleep](coordinated-sleep.md) for implementation, evidence and the ordered gates.


## Bounded retained-sleep failure — 31 October, after 23:25 UTC

The latest corrected pilot AP `5fa1d854…` / CP `f1aabe49…` froze/disconnected.
Its final diagnostics report one AP WFI return, zero CP low-voltage entries,
ten post-publication readiness samples and no missing module votes. The next
attempt has no received cleanup result. No completed retained sleep is proven.
First-failure UART/phone bytes and the operator confirmation are preserved.

Known AP `2cc3478b…` / CP `d4d65559…` are restored. Received UART confirms
240 MHz, existing SD PFS (`new=0`), bonded/encrypted protocol recovery; the operator
confirms dim Nadir running and Bluetooth connected. No PFS/resource/provisioning
write was selected. This warm recovery does not qualify cold power or overnight.

The local coordinator now checks exact saved clocks in audited SRAM, has an
SRAM reset helper and reset-loop inhibition, correct active-low CPU2 reset and
power ACK checks, drained-transport readiness, separate preparation/wake budgets,
shortened-deadline cancellation and a 12-return/24-request qualification cap.
The affected suite has 27 passing methods and 8 optional fixture skips; normal
and experimental builds pass image/map checks. These are implemented/static/host
results, not physical wake or retention qualification.

Before another pilot: establish an independent safeguard for WFI that never
returns, complete native PSRAM/AHBP ownership, and preserve final CP/AP stage
markers. The current 700 ms timeout executes only after WFI; the CP memory-ready
bit checks policy rather than actual owner retention. Neither is sufficient
physical proof. See [coordinated sleep](coordinated-sleep.md) for the full failure sequence, ownership findings,
artifacts and ordered gates. No new measured savings; display and overnight
work remain deferred.

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

## Coordinated-sleep and radio checkpoint — 31 October, after 21:15 UTC

Default-off coordinated-sleep transport work now includes HCI request/response
leases, queued/active ACL and reset guards, a latched quiet-work wait, and mailbox
RX ownership through callbacks. Twenty-four relevant native test methods pass
with sanitizers. These guarded paths are implemented and compiled, not hardware
qualified. The monotonic bounded alarm AP also builds but is unflashed.

ABI v2 CP `1ad2087f…` passed five short reconnects without alarm requests and five
after alarm exercise, but then reproduced an uncommanded `0x0208` supervision
timeout after warm return to exact entry AP `86d1dcba…`: 21:09:00 device UTC,
protocol recovery 21:09:47. Both devices were untouched; phone logs independently
record HCI reason 8 for this watch. First-failure UART/phone bytes, hashes and
operator confirmation are preserved. Neither retained sleep nor fixed 120 MHz
was active. The radio cause remains unresolved; short reconnect pilots are not
stability qualification. The adapter-specific modem-ignore rule is installed and
verified; prior modem probing explains capture interference, not this dropout.

Known CP `d4d65559…` was restored without changing the AP. Its six-minute connected
idle control received zero disconnects/errors and the same 60 ms/latency-4
parameters. The separate six-minute AP-only warm-return phase also completed
with no received disconnects and one known ATT subscription error. The operator
confirmed dim Nadir stayed selected and Bluetooth connected. Both UART and phone
captures stopped at 21:27:49 UTC; host receiver processes were verified absent.
No new electrical saving is claimed. Latest retained AP sources compile but
exceed the unchanged partition by 1,000 B, blocking final linked SRAM audit and
physical retained entry. Normal default-off release `54845a3a…` passes image/map,
CRC, resource, log and syscall validation with 2,056 B headroom; it is unflashed.
Installed AP remains `86d1dcba…`, with known CP `d4d65559…` and 240 MHz active.
Overnight qualification remains deferred. See
[coordinated sleep evidence and gates](coordinated-sleep.md).


Post-capture host check: the phone no longer enumerated over USB, so a final ADB
Bluetooth-state query was unavailable. The debug adapter remained present. No
later Bluetooth-state observation is inferred from the completed captures.

## Failed frequency experiment removed — 31 October

`CONFIG_T5AI_POWER_CPU_120` and `CONFIG_T5AI_POWER_FREQUENCY_PROBE` are removed,
including their startup branch, timed frequency transitions and callback/API.
The normal startup path always requests 240 MHz. Read-only clock profiling and
actual-clock tick calibration remain. Clock-register addresses, field masks,
source selections and frequencies are named constants; the SD failure snapshot
also uses named clock addresses. No shared/common/vendor code or display behavior
is changed by this cleanup.

Seventeen relevant host tests pass: active-vote failure/retry, startup ordering,
clock decoding, tick accounting/WFI guards, board configuration, SD completion race
and one/four-bit SD clock/fault paths. Normal ARM build `44c977ff…` passes image,
protected-block, map, resource, log and syscall checks with 328 B FLASH headroom.
It is **built, not installed**; installed entry AP `86d1dcba…` and CP are unchanged.
Evidence: `artifacts/clock-sleep-cleanup-20261031` in the reference workspace.
There is no new hardware run or measured saving; captures remain stopped.

Focus is [PR2-style coordinated retained sleep](retained-clock-policy.md): keep
240 MHz active, use T5AI's CP-owned low-power clock/DPLL sequence during sleep,
restore clocks before work resumes. Next dependency is the CP-applied alarm
owner/generation contract, followed by wake/time-accounting qualification. The
application veto, normal SysTick and deferred overnight gate remain in place.
Earlier fixed-frequency images and measurements below are historical evidence.

## Clock policy and radio follow-up — 19:30 UTC, 31 October

The intended policy now explicitly keeps **240 MHz while active** and reduces
clocks only inside coordinated retained sleep, restoring them before normal work.
PR2 saves its DLL1 configuration, switches to HRC48 and disables DLL1 during deep
sleep; the pinned T5AI equivalent uses CP-owned 26 MHz/DPLL and voltage sequencing,
with both APs idle. See [review and implementation order](retained-clock-policy.md).
Fixed 120 MHz and the bounded frequency probe were diagnostics; the subsequent
cleanup above removes their code and Kconfig.
No retained sleep, new PLL shutdown or change to the application veto is enabled.

The operator reports dropouts only following the 120 MHz trials. A warm AP restore
leaves CP powered; later entry-AP timeouts cannot establish independence from the
frequency transition. A true operator-confirmed cold-off check recovered entry
AP/Nadir/Bluetooth automatically. Received expected build/240 MHz/protocol startup
has USB reattachment gaps, so full cold storage qualification is still open. Over
six minutes of subsequent cold-control idle had no received unsolicited disconnect;
`0x022a` parameter-update failure and the known ATT error remain recorded.

Single-boot diagnostic `2ae798cb…` then starts at 240 MHz, requests 120 after two
minutes, returns to 240 after five and completes at eight. Received raw snapshots
confirm **both CP0 and AP 240→120→240 MHz**, shared mode `100031→100033→100031`,
and digital-voltage selector **D→C→D**. This is not an AP-only divider. No disconnect,
SD failure or unexpected AP boot was received during this short probe. It does
not disprove intermittent failure, isolate voltage from clock effects, qualify
Bluetooth or establish power savings. No new electrical measurement was taken.

The **exact entry AP `86d1dcba…` is restored**; its expected build, existing PFS,
actual 240 MHz and bonded protocol recovery are received. CP was not written.
All captures are stopped and phone Bluetooth remains enabled; overnight remains
deferred. Evidence, raw captures, hashes and results are under
`artifacts/bt-frequency-20261031` in the reference workspace.

Six relevant vote/timing host tests pass, including shared/AP-only clock decoding,
one request per probe phase and failure restoration. Normal default-off source
`6c8bb00f…` builds with 1,638,072 B FLASH (**328 B free**), 97,824 B RAM and unchanged
resources; it is not installed. Later documentation/Kconfig-help edits change no
runtime behavior. Next implement the CP-applied alarm owner/generation contract,
then qualify wake delivery/time accounting before native coordinated suspend.
Older checkpoints below describe their historical states.

## SD diagnosis follow-up — 31 October

A foreground SD interrupt-mask RMW could overwrite the DMA callback's TX-empty
enable. A short interrupt lock now protects that update/start. Behavioral
injection reproduces the lost bit without the lock and preserves both bits with
it; six relevant SD/PFS host tests pass. Failure-only diagnostics also retain
setup/IRQ/clock state. No timeout, retry, SD clock or PFS geometry changes.

Fixed diagnostic APs `57aab799…` (240 MHz) and `f2cebc84…` (120 MHz) each completed
five counted encrypted/protocol reconnects plus the full tail, with actual clocks
and zero SD errors received. The initial 120 attempt is excluded because the
watch was already disconnected. Bluetooth discovery/parameter errors in the
retry are retained; this is bounded SD evidence, not full Bluetooth qualification.
The original SD failure's attribution to this race is still **unproven**. No new
energy saving is accepted. See [investigation and evidence](sd-write-completion.md).

Exact entry AP **`86d1dcba…` is restored**, CP unchanged. UART verifies existing
four-bit PFS, 240 MHz and protocol startup. Later entry-AP terminations `0x0228`,
`0x0208` and `0x023e` recover by device 18:52:40. The operator confirmed both
devices were untouched. Final visible-state confirmation is pending. Capture
is now stopped, phone BT enabled.
Normal fixed source `4708694e…` builds with 328 B FLASH headroom, not installed.
Longer SD readback/reconnect, captured cold boot and overnight remain open;
overnight is deferred. Earlier checkpoints below retain their historical state.

## Final power checkpoint — 31 October

**Exact entry AP `86d1dcba…` is restored**, with expected build, four-bit SD,
existing PFS (`new=0`), actual 240 MHz and protocol startup received. CP was not
written this increment. The operator confirmed dim Nadir and Bluetooth restored.
All UART/power captures are now stopped; overnight soak remains deferred.
Reopening PFS is not an integrity proof, and this is not cold/soak qualification.

Further rollout is blocked by the actual-120-MHz trial's CMD25 timeout at device
17:46:46.667 UTC: one sector `0x9b4b`, -116 (`ETIMEDOUT`), SD `00a64480`, DMA
`00040000`, followed by SD-PFS assertion and uncommanded AP reset. Entire
`cpu120-B` excluded; frequency causation is unestablished. SD gating was off;
CP/display were unchanged. Complete first-failure evidence is preserved. The
identical 240 MHz return run is also excluded: kernel-confirmed host hub/UART/meter
outage during cycle five prevented its full tail. Do not assign that outage to
firmware or count the run as qualified.

A temporary normal image `8fdaa366…` had an uncommanded `0x0208` disconnect at
18:06:31 and protocol recovery at 18:06:43; the operator said the phone was
untouched. Final review also finds `0x0208` on the restored entry AP at 18:13:24.
Its later recovery is operator-reported, absent from the saved trace ending at
18:14:22. The operator confirmed the phone was untouched and Bluetooth recovered
afterward. Thus new logging alone cannot
explain the issue. Preserve both traces; Bluetooth qualification remains open.

Accepted `cpu240-A` baseline: **147.965453 mW / 29.614933 mA** idle; five completed
recoveries use **44.768605 J over 290.895 s, 8.953721 J/completion**. Fixed dim Nadir,
USB/no cell and Bluetooth were confirmed and device-time review extends beyond
the full tail. Known ATT subscription error retained. These are nominal USB
observations; **no new comparative saving is accepted**.

The [power continuation](power-continuation.md) records implemented default-off
UART gating, isolated scalar PM wake events, fixed CPU selector, sparse SD/IMU
counters and failure snapshots, and optional quiet successful battery logging.
Defaults preserve original battery log traffic. Final display controls implement
raw-zero brightness and serialized explicit sleep/full-redraw wake, separately
and default off, after non-display blockers were documented. No display candidate
was installed or measured. Shared/common/vendor code, application sleep veto,
SysTick, heartbeat, DMA completion and shared peripheral ownership are preserved.

**50 relevant host tests pass**. Final normal source `816dfa48…` builds with
1,638,072 B FLASH (**328 B free**) and 97,824 B RAM; staging/PFS/resources are
unchanged. It is **not installed**. ARM builds, image checks and Ruff pass; these
are implementation results, not physical qualification. Evidence and exact image
hashes are under `artifacts/power-all-20261031` in the reference workspace.
Next: resolve storage completion/PFS and Bluetooth failures, then qualify CP-applied
alarm routing, retained sleep, sensors and memory/source ownership. Full workload,
cold-power and overnight gates remain open. Older installed-image statements
below describe their historical checkpoints.

## SD local-clock checkpoint — 31 October

The [SD clock experiment](sd-idle-clock.md) is implemented as an Unhygienix/SD-only,
**default-off** option. Sixteen relevant host tests pass. Matched normal images
pass map/protected-image/resources/log/syscall checks: off `86d1dcba…` uses
1,638,072 B FLASH; on `d1a3d567…` uses 1,638,392 B, leaving only **8 B**. Shared,
vendor and display code/policy are preserved. Only the SD option differs.

Both images boot with four-bit SD, the existing 32 MiB PFS (`new=0`), actual
240 MHz and bonded protocol recovery. Accepted fixed-dim-Nadir off/on/off windows
record idle **148.467 / 148.127 / 147.968 mW** and gross
**9.013737 / 8.992238 / 8.989747 J per completed reconnect** (five per batch,
identical holds/tail). Device-time review and operator display checks are
complete; known ATT errors are retained. The notification-interrupted return
attempt is preserved and excluded whole. This single nominal USB comparison
does not establish a causal/repeatable saving or full hardware qualification.
Gate activity was not independently probed. Keep default off.

**Installed AP is the identical off control `86d1dcba…`**, with the preceding
corrected Wi-Fi-off CP selection unchanged; only AP was written. Two reported
all-power-removed checks restored Nadir/BT automatically but have capture gaps:
one expected boot lacks SD/PFS records, and the repeat lacks a boot record.
Full captured cold startup remains open. Storage durability, physical bus/DMA and
mixed-owner tests, a 25-cycle run for this AP and the operator-deferred overnight
soak remain open. All SD UART/power captures are stopped after review. CP's earlier
25-cycle result and its power observations below remain separate.

## Automated workload checkpoint — 15:19 UTC, 31 October

At this checkpoint, corrected Wi-Fi-off CP **`d4d65559…` was installed**, with
then-unchanged AP `c656055a…`, for consecutive reconnect qualification. Its expected
AP startup was received. The first pre-qualification readback handshake timed out after
commanded resets; its complete trace is preserved, with no received CP crash and
normal existing-PFS/bonded-session recovery. A fresh-name retry read all 1,114,112
bytes successfully: active CRCs and exact corrected-off package bytes match, and
the full physical SHA is `4f2fd6448da1a78d9c918676faccfb4c103dc08df0cdb6fbfa62a829fc0ebd6c`.
`adb-off-25-A` completed at 15:15:17 UTC after all **25 consecutive cycles**
and the full recovery tail: exactly 25 disconnects, bonded/encrypted recoveries
and protocol sessions, no extra termination or new error class detected by the
harness. The logger closed 0 at 15:15:20. Four notifications and one known ATT
subscription error were recorded during settling; this functional run's energy
is excluded from fixed-display comparison. The operator confirmed Bluetooth
connected and dim Nadir restored/unchanged. Delayed review extends through device
time 15:15:56, after the 15:15:17 batch end, with no new error class, fault/reset,
app transfer or extra termination. The counted functional gate is complete;
full hardware acceptance remains open. The physical cold-power check is armed
and requested in `cold-off-A`; no fresh boot or operator power-removal result
has been recorded yet. Overnight observation has not started and is deferred by the operator until
a later overnight window; it is not a passed or waived qualification gate.
No experimental image is promoted; production defaults, AP/display policy,
common source and pinned vendor trees remain unchanged.

The controlled on/off/on sequence completed five encrypted/bonded protocol
recoveries per batch, after 90 s quiet settling and identical 5 s user-disabled /
40 s on holds plus a 60 s tail. The operator confirmed dim Nadir throughout each
completed run and Bluetooth connected afterward. Nominal USB measurements:

| Profile | Elapsed | Energy | Energy / completed reconnect | Mean power |
| --- | ---: | ---: | ---: | ---: |
| On A | 293.014 s | 45.586809 J | 9.117362 J | 155.570449 mW |
| Off A | 291.665 s | 44.996220 J | 8.999244 J | 154.265703 mW |
| Return on D | 291.861 s | 45.982879 J | 9.196576 J | 157.540354 mW |

Off uses **0.157725 J/completion (1.7225%) less than the mean flanking controls**.
Return-on drift is **0.079214 J/completion (0.8688%)**. Supply means are
4.988343 / 4.997812 / 4.997622 V. Report elapsed time and mean power as well as
energy; this is a small bounded USB observation, not calibrated savings,
battery-life evidence or full hardware qualification. The clean idle pair is
148.091 / 150.332 mW (2.240 mW, 1.490% lower off), not a quiet idle ABA.

Late CRC-valid device-time review extends beyond the completed return window and
finds no notification, transfer, fault or reset inside the three workloads; all
have the same ten radio parameter updates. Each also records one `No subscription
found for ATT handle 3` error before successful protocol recovery. Do not call the
stack error-free. Return B was excluded whole after a notification; return C
stopped during settling before power or Bluetooth work. Their first traces are
retained. The operator paused that notification source before quiet return D.

Actual full CP readbacks after the initial on and off workloads match active
protected CRCs and exact regenerated raw/package bytes. Programmer reads are
commanded warm reboots, never cold-power checks. `automated-workload-comparison-3.json`
and `buffered-workloads-review-1449/` retain the accepted sequence and late review.
The counted batch is complete; true all-power-removed cold boot,
PFS/application integrity and overnight interaction remain open. The mixed UART
experiment stream has actual received data; it is not an overnight qualification.
Original CP `1713dee…` remains the exact rollback. Earlier current-image statements
below describe their historical phase, superseded by this checkpoint and
`hardware-phases.jsonl`. Display remains last.

## CP implementation and earlier experiment sequence — 2026-10-31

Entry HEAD `26cdf22c6` includes the operator's preceding timer commit. Board-owned
staging now builds Wi-Fi off/on CP profiles using the pinned SDK/GCC 10.3 and
CMake 3.31.6, with effective-config checks and reproducible raw hashes. Vendor
submodules, common Pebble source and AP/display/storage configuration are unchanged.
Strict experimental packaging verifies pinned stock/controller inputs, reviewed
raw/config hashes, affected controller instructions/relocations, compiled native
ABI and ITCM copy-table bytes. RF correction is source-integrated; the existing
latency correction binds verified new ELF symbols. Legacy guards/runner remain.

The installed CP readback verified active `1713dee…` and protected CRCs; a full
physical backup and exact rollback are retained. Its controller flash deadlines,
RAM launch/wait and mandatory real-ACK protections were carried forward without
restoring paused SPI/volatile experiments. Initial off `e48465c3…` faulted before
AP startup through NULL PHY `_get_info_item`. Its full first failure was preserved,
and exact original CP restored with operator-confirmed Nadir/BT recovery. Retaining
the existing pinned read/save callbacks independently of Wi-Fi fixes that path;
25 host tests now include the actual calibration ARM path and null-slot rejection.

Corrected off `d4d65559…` has bounded startup, unchanged AP `c656055a…`, 240 MHz,
existing SD PFS (`new=0`) and bonded/encrypted reconnection evidence. Its first
power window is excluded entirely: the operator transferred/launched an app,
with a notification and two faults for UUID `873ef5ea…`, PC `0x6e69616c`, LR
`0x1b0d`. That application failure is not yet attributed to the CP change.

Device-timestamp review corrected the idle comparison: both earlier on controls
received notifications within the required 90 s quiet settling period and are
secondary evidence. The valid quiet pair is off **29.681765 mA / 148.091477 mW**
and later on **30.131727 mA / 150.331772 mW**, each 120 s. Off is 2.240296 mW
(1.490%) lower in this single pair. There is no fully quiet ABA sequence, calibrated
uncertainty or battery-life claim. The later counted workload sequence is recorded
in the automated checkpoint above.

A phone transfer requires launching Pebblecraft and changes display content; the
attempted workload energy is excluded. The on control also logged a watchface
fault for UUID `16183a4f…`, PC `0`, LR `0xc89`, followed by automatic relaunch.
Its CRC-valid first trace was preserved before restoring the exact original CP
**`1713dee17dd98cd8aea8114a438d24ed87112585c16c45960221c4b96f344efb`**.
At that baseline-reproduction stage the original CP was installed, with unchanged
AP ID, existing SD PFS and bonded/encrypted PPoGATT startup verified. The operator confirmed Nadir/Bluetooth
recovery and repeated the app operation. CRC-valid logs reproduce both the
Pebblecraft `0x6e69616c/0x1b0d` and watchface `0/0xc89` signatures on that original
CP. Wi-Fi removal is not required to trigger either; root cause is unresolved.
No uncommanded AP reset is established by these application faults. Do not promote
either experimental CP based on these bounded tests.

The operator confirms the earlier 13:29:51/13:31:06 disconnects were Bluetooth
toggles or range changes; they are not spontaneous-failure evidence. Both restored
bonded/encrypted protocol sessions. A defined original-CP five-cycle, 240 s batch
then captured five encrypted/protocol completions, but a sixth disconnect began
at 13:41:19 before the window ended, reconnecting at 13:41:36 afterward. Its encoded
reason is `0x0215`; the operator confirms five manual cycles with dim Nadir
unchanged. Five peer-power-off terminations plus one remote-user termination
account for the six link disconnects. Keep the whole window as evidence, **exclude it from controlled energy-per-workload acceptance**.
No CRC-valid app fault or AP build/boot log occurs within that batch. This is not
25-cycle candidate qualification. All power recordings are stopped cleanly.

At that stage precisely timed phone-side cycles were the next step, with Android
USB debugging not yet available. The automated checkpoint above supersedes that
state: ADB work and 25 consecutive candidate reconnects are complete. Application/
return-to-watchface root cause, PFS integrity, true cold power and deferred overnight
interaction remain open. Normal production CP defaults remain unchanged; display
remains last.

The received `baseline-soak-v3` trace stopped cleanly at 13:19:53 UTC, 980,903 B,
SHA `f7e28c8c…`; it includes commanded operations and first failure, not an
uninterrupted overnight pass. Timestamped receive-only capture `uart-workloads`
(PID 892411) received actual bytes, including original-CP startup/repeat faults.
Quiet logs can arrive over two minutes late: per-read host timestamps are not
device event times. A read-only PULSEv2 decoder verifies COBS, CRC, lengths and
embedded device timestamps; its idle review supersedes the initial host-time
classification. No precise workload-energy timing is claimed without RTC/host
alignment. See [the detailed CP record](wifi-power.md) and
`/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-off-implementation-20261031/`.

## Latest timer continuation — 2026-10-31

Production entry HEAD is `198385cb0`, which committed the preceding I2C work;
the worktree was clean before this timer increment. The new SoC/driver-local
`T5AI_TICKLESS_WFI` option remains default off. It extends the already-owned
SysTick during ordinary WFI, keeping the application veto and active vote, and
restores periodic ticks on wake. CP source/divider ordering revealed a transient
160 MHz floor during 240/320 MHz switching; the deadline uses that bound.

The final relevant native driver/storage/timer/PM suite passes 36 tests; normal
and matched release images pass map/protected/resource/log-hash/syscall checks.
Three legacy display DMA test errors still reference removed functions; those
retained failures are separate from the passing current non-display suite.
Two initial candidates increased steady tick IRQ count; first traces are preserved.
Final candidate `f2f070e4…` avoids shortening the periodic timer and avoids a
synthetic IRQ ahead of a still-running periodic tick. Steady counts drop from
60,000 to about 56,300 IRQs per minute with correct elapsed tick totals.

Settled, operator-confirmed dim-Nadir/Bluetooth means: timer on **30.070 mA /
149.888 mW**, off **30.030 mA / 149.690 mW**, both about 4.9846 V. No saving is
demonstrated; this is a clean pair, not a completed ABA/workload-energy matrix.
Earlier different-screen and notification-contaminated windows are excluded
explicitly, not trimmed. A supervision-timeout disconnect/recovery on the off
control is retained; baseline radio stability remains unqualified.

The normal default-off image is `c656055a2c73c30defa3c8939c00b67af2771c95`, FLASH
1,638,072 B (328 B free), RAM 97,824 B. Final startup/capture disposition is in
[the experiment record](tickless-wfi.md). CP, resources and PFS were preserved.
Wi-Fi-on CP now builds in isolation with recovered matching configuration, but
binary SHA differs from pinned stock and guarded off/on variants remain unfinished.
No CP was flashed. Display work remains last. The cold-power redo captures the expected AP boot; the operator reports Nadir and
Bluetooth running. Cold storage/reconnect logs are incomplete after receiver
reattachment, so full cold-start qualification remains open. Current baseline
capture is `baseline-soak-v3/` (PID 760489), with a valid received AP frame verified
at 11:27:48 UTC; earlier receivers/gaps are preserved and stopped. Overnight and
25-cycle qualification are pending. The older installed/capture claims below
describe the earlier run.

## Latest power continuation — 2026-10-31

[Baseline, I2C candidate, measurement and dependency record](non-display-power.md)
supersedes the old A5 installed/source and display-default assumptions below.
The earlier continuation started at `2eacc9eaa`; A5 was committed in `add66a5e5`, followed
by the native-driver migration in `2eacc9eaa`. Entry hardware identity was verified
as native PSRAM build `6b314eb5…`, not A5. The exact installed rollback archive
is preserved. USB-only/no-cell, dimmed Nadir and connected Bluetooth were confirmed
by the operator. CP was preserved; its current hash was not read back.

The board-local `T5AI_I2C_IDLE_GATING` candidate defaults off and releases only
local bypass after STOP/bus-free, restoring it before the next transaction. It
retains bypass on stuck STOP and preserves shared domain/source clocks. Normal
control/on images pass packaging/maps and relevant host regressions; the stale
power-startup fixture was corrected for both native PSRAM ranges. Candidate
startup/SD/bonded reconnection and operator normal-running checks passed.

Matched off/on/off means were **30.011 / 30.072 / 29.996 mA** at about
4.9854 V, each from 11,980 samples (about **0.150 W**). No saving is demonstrated.
The off control is restored: verified installed AP build ID
`7c7d8a292457cb60338229e845a382b1a3329b77`, phone version
`v4.41.2-901-g2eacc9eaa-dirty`. Resources match the previous archive; SD/PFS/apps
and bonded reconnection were preserved. Candidate `1ce0c3b7…` remains opt-in,
not promoted. Normal control/on, final-source default-off and release/on image checks pass.
The final combined host regression passes 37 tests; Ruff passes. All captures
are stopped; no overnight soak is running. Source edits
in this continuation remain uncommitted; existing documentation edits were
preserved. Exact evidence and qualification limits are in the linked record.

Wake ownership/notification routing, complete tickless/retained-sleep contract,
peripheral source/domain ownership, sensors, shared memory and counted-workload
CPU/radio energy remain unfinished. SysTick and application sleep veto remain.
No display configuration/source or CP behavior was changed. The current native
DMA controller centrally owns AP routes; old two-row IRQ timing remains historical
evidence, not a result for the current transport. Physical fault/concurrency,
25-cycle, true cold-boot and overnight qualification remain open. Details and
concrete ordered next steps are in the linked continuation record.

## Historical source and recorded device state — 2026-10-30

Production HEAD is `bf10c6435930c7d0f12ab335faf67c29f1d14b0c` (A4 committed),
following A3 `06079008a`, A2 `f7f7cbd84` and A1 `f3ebbee4c`. A5 remaining local
corrections are implemented and host/image validated in the uncommitted worktree.
The normal configuration was rebuilt and flashed AP-only at the operator's request.

**Latest verified installed AP:** ELF build ID
`901c6c58f0d520c5388b15a0865d4b21c055efb3`, normal A5, boot verified at
16:34:37.008 UTC on 30 October. Phone version:
`v4.41.2-670-gbf10c6435-dirty`. The guarded writer reports Flash OK (32.2 s).
UART confirms four-bit SD, the existing 32 MiB volume (`new=0`), 49 apps,
and encrypted bonded phone reconnection with PPoGATT reopened. CP, resources,
PFS and bonds were preserved; no provisioning or storage-backend switch occurred.

The 119.161-second flash/startup capture received 5,352 bytes and is stopped.
No fatal fault or uncommanded additional boot was observed. One reboot warning
corresponds to the commanded flash reboot. `Animation 3 does not exist` was logged
at embedded time 16:35:26.097 UTC; its cause remains unassigned and it is not
classified as fixed. See the [A5 flash record](/home/baz/Documents/ChatGPT/pebble/artifacts/a5-flash-20261030/RESULTS.md).
Startup/reconnection does not qualify eFuse faults, idle control, concurrent
microphone/display/SD DMA, storage integrity, cold boot or longer soak behavior.

The previous A4 installed ID was `943caf619e5bb3e49f6e852bfe97efe0eb8323f0`.
Its frozen AP/ELF/map/dictionary rollback inputs were hash-checked before A5 flashing.
[A4's earlier boot/reset/reconnection checks](/home/baz/Documents/ChatGPT/pebble/artifacts/a4-flash-20261030/RESULTS.md)
remain historical evidence, including current boot time after one controlled
hardware reset and a connection-parameter timeout recovered 412 ms later.
A4 console clock writes did not execute: the CP-owned runtime UART input backend
is unconditionally stubbed, unlike shared normal/MFG input on other SoCs.
Console integration remains separate work; do not reclaim AP UART0 pins from
four-bit SD. A3's earlier installed ID was `4245c7ab3e16d7c5449b864b355ea1ba22550993`.
All prior captures are stopped; A3/A4 qualification limits remain open.

[A1 is complete and explicitly accepted by the operator](/home/baz/Documents/ChatGPT/pebble/artifacts/a1-sd-reflash-20261030/RESULTS.md).
The previously accepted A1 SD AP had ELF build ID
`d316ed7087345e709826be1605b727bc9d0b1bc6`, flashed AP-only earlier on 30 October.
UART confirmed four-bit SD-backed PFS, the existing 32 MiB volume (`new=0`),
48 apps and an encrypted bonded phone session. Terminal watchface transfer
succeeded; the operator confirmed “all looks good”. Capture stopped. A recorded
first-launch app fault remains unresolved; 25-cycle, cold-power and overnight
qualification were not claimed.

[A2 F02/F06 is implemented, host validated and image validated](/home/baz/Documents/ChatGPT/pebble/artifacts/a2-mailbox-20261030/RESULTS.md)
and is now committed as `f7f7cbd84`. HCI queue-exhaustion diagnostics are bounded
and reported by the HCI worker; mailbox-owner logging and blocking APIs reject
before shared log-state/PULSE or transport locks. PM quarantine is checked under
the transaction mutex before draining responses or publishing another request.
Both failures reproduced against the entry source and pass corrected scheduling
regressions with real contending host threads/locks. Validation: 9 focused tests;
231 port tests plus 75 subtests; 28 shared tests plus 119 subtests. Three optional
CP execution tests and one shared ARM execution test remain unconfigured.
Fresh normal/release images passed packaging, map, resource, log-hash and syscall
checks; linked LTO paths retain the early owner guard. A2 was not flashed or
device qualified in its implementation session; its corrections are included in
the later A3 image. Its validation record describes the pre-commit source state;
the commit above records the subsequent source disposition.

SD PFS, four-bit mode and the existing-volume guard remain enabled by default.
A2's normal effective configuration exactly matches accepted A1; release retains
its existing PULSE default. CP inputs/wire ABI, Gabbro app/companion compatibility
and the one-second heartbeat are unchanged. No provisioning, NOR switch or new
CP image was performed.

[A3 / F03 is implemented, host/image validated and AP flashed](/home/baz/Documents/ChatGPT/pebble/artifacts/a3-hci-pressure-20261030/RESULTS.md).
Receive pressure now retains AP copies in the existing 1,285-byte staging budget
and retries nonblockingly, with explicit recovery on bounded exhaustion/deadline.
The duplicate payload queue was removed: normal has zero net SRAM increase over
A2 (20,192-byte linked heap arena); release adds 32 bytes. Validation: 233 port
tests plus 81 subtests; 28 shared tests plus 119 subtests. Both fresh images pass
protected packaging/maps/config checks. CP ABI, A1/A2 protections and SD defaults
remain unchanged. Runtime pressure/heap/stack and longer device qualification
remain pending; no operator acceptance or resolution of historical app/radio
faults is inferred from this flash.

[A4 / F04 and F05 is implemented, host/image validated and AP flashed](/home/baz/Documents/ChatGPT/pebble/artifacts/a4-rtc-20261030/RESULTS.md).
External STOP/write failures now attempt restart and retain the first error;
readback gates persistence health. Saved-mask tuple publication and ordered
cache-durable retained commits preserve the 136-byte layout and checksum.
Validation: 5 focused tests plus 74 subtests, 231 port tests plus 147 subtests,
and 28 shared tests plus 119 subtests; four existing optional execution checks
remain unconfigured. Normal/release images pass protected packaging/maps.
Committed as `bf10c6435`; its flashed normal image is now superseded by A5.
Device validation is limited to startup, normal reset/current boot time and
bonded reconnection;
console clock updates, actual RTC bus faults and interrupted resets remain open.

[A5 / F07–F09 is implemented, host/image validated and AP flashed](/home/baz/Documents/ChatGPT/pebble/artifacts/a5-local-20261030/RESULTS.md).
eFuse polling no longer underflows and requires operation completion before
READ_VALID data is accepted. T5AI honors the common idle gate while preserving
scheduler confirmation, elapsed accounting and tick behavior. A local GDMA owner
keeps display channel 0, microphone channel 1 and SD channels 2/3 routing independent
of microphone teardown, preserving inherited routing and CP ownership.
Separate F08/F09/F07 patches pass standalone regressions against entry HEAD.
Validation: 10 focused tests plus 8 subtests; 235 port tests plus 155 subtests;
28 shared tests plus 119 subtests. The same four optional execution checks remain
unconfigured. All nine fresh images (normal/release and all eight DMA combinations)
pass packaging/maps; normal/release configurations match A4 byte-for-byte.
Each adds 128 B AP flash over A4 with unchanged linked heap arenas (20,160 B normal,
29,216 B release). Shared code, ABI, geometry and qualified display polling/power
policy are unchanged; experimental IRQ/worker defaults remain disabled.
A5 is uncommitted. Physical eFuse/idle/mixed-DMA qualification remains open.

Power and architectural work remain later packages under the
[handoff](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-30-tuya-port-review/HANDOFF.md).
The standing power sequence remains non-display first and display power last;
these correctness prerequisites do not claim electrical savings.

## Historical SD heap qualification — 2026-10-22

Production HEAD at the 22 October refresh was `87788c85a`. Recent committed changes are
`d4c9c5321` (display optimisation), `1428d0cb9` (SD PFS), `8ce938615`
(SD interrupt-race correction), `c79352159` (CP latency-transition fix), and
`87788c85a` (PFS metadata PSRAM allocation).

AP build `c0e283f0b861e0d0fbcb70fb9c5fae6e86b84065` was flashed AP-only on
22 October. Boot UART and a later live read verified its identity. The watch uses
32 MiB four-bit DMA SD-backed PFS; the existing volume reopened (`new=0`) with
48 apps. Internal-flash PFS remains preserved and independent. No CP change was
included in this AP correction. Capture was stopped and files flushed; this page
is a recorded state, not a claim of live monitoring.

The notification reboot was diagnosed as kernel-heap exhaustion/fragmentation.
The 8,192-byte PFS page-flags cache now uses a dedicated AP-owned PSRAM reservation
at `0x60020000..0x60022000`, outside the first 128 KiB display/board reservation.
Kernel heap capacity remains 21,216 bytes; final observed free space was 11,992 B,
largest block 9,948 B and peak used 13,540 B (7,676 B remaining at that peak).
These are unpaused live heap walks with concurrency/cache limitations, not
universal worst-case headroom measurements. Shared PFS and allocator source are
unchanged. Seventeen focused checks and image/build validation passed.

After an initial USB capture gap, 25.56 minutes of continuous UART recorded no
additional boot, fatal OOM, SD I/O error or watchdog report. Notifications displayed
normally. Seven supervision-timeout disconnects (`0x208`) all reconnected and
reopened the phone session. An optional image needing 12,508 contiguous bytes
was safely dropped when the largest block was 7,728 B. Radio stability and image
allocation limits remain open; this bounded run does not establish release stability.

Evidence: [heap-fix report](/home/baz/Documents/ChatGPT/pebble/artifacts/sd-pfs-heap-fix-20261022/results.txt),
[SD backend](sd-pfs.md), [tooling](tooling.md#sd-pfs-metadata-heap-fix--2026-10-22).

## Wi-Fi power investigation — 2026-10-30

[Ownership and implementation findings](wifi-power.md): the native AP contains
no Wi-Fi stack, but the verified pinned CP initializes Wi-Fi at boot and requests
MAC/PHY power, clocks and RF ownership. Steady-state votes/current remain
unmeasured. Runtime full deinit returns unsupported. A vendor Bluetooth-only
build path exists, but requires a reproducible CP build and variant-aware
packaging/calibration/ABI validation before a supported default-off option can
be delivered. Implementation is blocked at that integration gate; hardware and
electrical qualification are separately pending. No firmware behavior changed
or flashing occurred in this investigation, and no other power work was started.

Existing builds and the last recorded installed AP/CP were copied and hash-checked
in `/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261030/baseline/`
for benchmarking when the current monitor arrives. The old build directories are
not all the recorded installed build; use the snapshot identities. Seventy host
regressions, unchanged CP source-hook build/manifest validation and the preserved
AP map check passed. Host UARTs were enumerated; a six-second runtime read
received 246 B and then stopped. This did not re-establish installed identities,
measure current or qualify a new firmware image. See the report for the next CP
integration task and baseline measurement instructions.

## Wi-Fi revalidation — 2026-10-31

At production HEAD `2eacc9eaa9682f9e30e72abf1211b8675ecdef67`, default-off
Wi-Fi implementation remains blocked and device/electrical qualification remains
pending. [Updated ownership, CP build attempt and measurement](wifi-power.md#revalidation-and-measured-setup-reading--2026-10-31)
confirm the unchanged CP startup/deinit constraints and available Bluetooth-only
PHY libraries. The staged CP-only build generated partitions but stopped before
configuration at missing CMake; vendor GCC 10.3 and resolution of unconditional
project CIF/VNET dependencies are the concrete next steps before variant packaging.
No firmware source/default changed or flashing occurred. The pre-existing
`tooling.md` worktree change was preserved.

The stock-derived CP hook build/manifest and archived AP map validation passed.
Of 70 host checks, 68 passed; the old board-startup fixture errored and old button
source assertion failed against this entry HEAD following the driver API migration.
No fresh AP/full CP build or Wi-Fi-off behavior is validated by these checks.

The now-connected FNB58 returned 1,976 CRC-enabled samples, mean 4.98388 V,
29.486 mA / 146.954 mW. Operator reports USB power, lowest screen dimming and
Bluetooth connected; battery/charging state and installed firmware identities
remain unverified. This is a setup reading, not measured Wi-Fi savings or a
controlled baseline. A six-second UART probe received 168/24 bytes on if00/if02
without a decoded identity log. Both captures are stopped. Evidence:
`/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261031/`.
The implementation and qualification checklist items remain separate and open.
Continue with controlled baseline/CP integration as documented; no remaining
power work was started.

## Completed work and retained configuration

- **Language packs: complete, closed by operator direction on 22 October.**
  Installation, replacement/removal, language selection and optional-font/glyph
  follow-ups are no longer TODOs. This disposition adds no new device-test claims.
  Keep the October 18 language-switch watchdog trace as historical evidence under
  general watchdog/settings-sync stability; it is not a language-pack blocker.
  English/base-font fallback and stock PFS watch/unwatch routing are implemented.
- Full system-bank payload CRC and table bounds/ID validation are implemented;
  recorded validation is 2,653 ms at 240 MHz. Explicit absence of an update bank
  is represented and unsupported updates remain rejected. Sharing the remaining
  bank backend with stock is separate cleanup.
- The October 19 display pass implemented wide copy/fill, native compositor
  copy/clear, two-row DMA batches, touch-move coalescing/acquisition timestamps,
  DMA rotation and elapsed-time deadlines/bounded recovery. Matched native
  compositor time fell from 6.671 to 1.887 ms/call. Eighty-two host checks passed
  and the final normal display image passed bounded operator checks.
  IRQ completion and display worker experiments remain disabled; IRQ was slower
  at this batch size (~26 versus ~20 ms/update). Pixel fast path, text cache,
  resource-wide copying, profiling and fault injection remain off in the selected
  normal baseline. Scalar rotated fallback was not qualified. DMA2D remains
  deferred without a hardware speed benefit. See the
  [display handoff](/home/baz/Documents/ChatGPT/pebble/artifacts/t5-display-implementation-20261019/HANDOFF.md).
- The October 21 CP interval-transition correction clears stale latency state at
  both parameter-application sites while preserving normal peripheral latency.
  Two Pebblecraft transfers passed, including latency 30 to 0. The recorder and
  zero-latency workaround were removed from production and archived. Later radio
  timeouts show this is not complete Bluetooth stability qualification.
  See the [CP notes](../../../boards/unhygienix/tuya/cp/README.md).
- Touch, battery and heartbeat private-worker consolidation, native kernel objects,
  retained boot bits/reboot records, stock reconnect policy and splash integration
  are implemented. Do not repeat these as new implementation tasks.

## Remaining work

Priority update — 2026-10-30: [power optimization](power-todo.md) is the
highest-priority power workstream, following the local correctness packages
recorded above. Complete its non-display phases first and leave all
display power work until its final phase. This changes work ordering only; the
historical measurements above are unchanged; the latest installed state is
recorded in the 30 October section.

1. Complete the [power TODO](power-todo.md), including measured qualification.
2. Diagnose remaining Bluetooth supervision timeouts; qualify sustained phone/app
   transfers, 25 reconnect cycles, cold-boot bond/face restoration and overnight
   connected-idle operation with periodic interaction.
3. Resolve general App/KernelBG watchdog and settings-full-sync stalls. Preserve
   prior first-failure evidence separately from the fixed notification heap OOM.
4. Benchmark SD throughput, CPU cost and tail latency; qualify full-volume,
   interrupted-write/corruption recovery and display/microphone concurrency.
5. Measure startup/peak heap and stack high-water under representative loads;
   address optional contiguous-image allocation limits. A whole-heap PSRAM move
   is not implemented; benchmark allocation/free/reallocation under fragmentation
   and normal display/Bluetooth load before deciding it.
6. Finish low-power/tick suppression, hardware wake/alarm routing, RTC drift,
   privilege/isolation and fitted-cell battery/charging qualification. Further
   accelerometer CPU-cost reduction remains exploratory; do not infer an
   exclusive pipeline baseline from instrumented task CPU figures.
7. Refresh the stock-convergence audit against current source/upstream; finish
   memory/capability/image-geometry contracts and shared-backend consolidation.
   Review shared GATT, PULSE, logging, pairing, serial, MPU and watchdog fixes
   independently for upstream. Complete relevant stock-board/QEMU checks.
8. Define firmware packaging, rollback, interruption recovery and factory restore
   before enabling OTA/recovery. Existing update gates remain in force.

SDK compatibility-cache removal/benchmarking, pixel implementation removal and
app-cache reserve/admission-policy changes remain deferred by operator direction.
The former internal-flash PFS expansion deferral does not undo the implemented
optional SD volume. Flash bypass remains paused. Language-pack work is closed.
No merge/release readiness is established by the short development captures.


## Additional history from docs/boards/unhygienix/power-todo.md

# Unhygienix power TODO


1 November, 10:49 UTC: bounded AP wake and CP resource/rollback controls pass
on hardware. CP low-voltage residency is still unproved; the full transition on
the same ABI v8 CP is next. No new measured savings. See
[boundary-control evidence](coordinated-sleep.md).

Latest (1 November, 10:28 UTC): coordinated v8 reset timing points toward a
missing AP wake; CP-awake/AP-deep-WFI isolation is implemented and host-tested.
Normal AP `2cc3478b…` / v7 CP `c272678c…` is installed; the bounded control is
pending. Uncommanded pre-slot Bluetooth drops remain separately recorded.
See [current evidence](coordinated-sleep.md). No new measured saving.

Current investigation (1 November): retained pilots now have expiring UTC slots,
exact AP interrupt-route restoration and CP GPIO ownership preservation. AP WFI
returns have been observed, but CP low-voltage residency is still unproved; the
v6 first attempt reset and its trace is preserved. A bounded timeout-separation
probe is next. See [current checkpoint](coordinated-sleep.md). No new measured
savings or hardware qualification; display and overnight soak remain deferred.

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
No new power policy is enabled by default. Bounded USB measurements below are
separate from calibrated or fully hardware-qualified savings.

31 October radio/sleep follow-up: ABI v2 CP also reproduced an operator-confirmed
uncommanded supervision timeout after alarm exercise and warm entry-AP return.
The phone independently reports reason 8 for the watch. Short five-cycle pilots
passed but do not qualify stability. Guarded HCI logical leases, latched quiet
waits and mailbox RX callback ownership are implemented and host-tested;
the former 1,000 B overflow is resolved by the operator-authorized 1,664 KiB
firmware layout. Normal paired transition, complete byte/CRC readback and paired
legacy rollback and final cold-power/PFS/bond restoration checks pass. The linked SRAM stub
audit passes; retained entry remains blocked by radio isolation and physical
wake/clock/retention qualification. See [the latest evidence](coordinated-sleep.md).
No new electrical savings are claimed; display and overnight work remain deferred.

Latest bounded sleep result: corrected pilot `5fa1d854…` / CP `f1aabe49…`
stalled after one reported AP WFI return, with no CP low-voltage residency proven.
The operator confirmed a frozen/disconnected watch. Known AP `2cc3478b…` and CP
`d4d65559…` are restored, with received PFS/encrypted protocol recovery and operator
Nadir/Bluetooth confirmation. Before another pilot, establish an independent
lost-wake safeguard, complete native PSRAM/AHBP ownership and preserve CP/AP stage
markers. The existing post-WFI timeout cannot bound a WFI that never returns.
See the [bounded failure and ordered gates](coordinated-sleep.md). No new power
saving or retained-sleep qualification is claimed.

1 November 09:09 admission repair: AP `ed05ff87…` now uses 20 ms bounded
admission retries, final slot/clock checks, early expired-slot rejection and
watchdog register readback with rollback. Thirty-six tests and ARM checks pass.
Two timed reset/recovery trials are received; reset attribution and a surviving
failure record still gate coordinated sleep (boot cause/SRAM marker remain zero).
No retained pilot or energy measurement was run. The installed probe's slot is
expired; v5 CP is unchanged. See [the current evidence](coordinated-sleep.md).
The checkpoints below are historical.

1 November 08:49 flash checkpoint: v5 fenced CP and expiring-slot guard AP
`1163df17…` are now installed. CP-only/AP-only writes and encrypted protocol
recovery pass; no post-write readback was performed. The probe acquired its CP
lease but cancelled after its two-second UTC slot expired, explicitly **no WFI**.
One commanded post-slot reconnect recovered. Watchdog reset timing/source,
retention and coordinated sleep remain unqualified. The operator confirms the
visible reboot was the flash restart. No new energy measurement, cold-power or
overnight qualification. See [current evidence and next gates](coordinated-sleep.md).
The older checkpoint below is historical.

1 November guard checkpoint: the CP-ready/resource-restore race, full PRIMASK
protection and real memory leases are implemented and host-tested. Independent
guard probes did not qualify reset timing/source or SRAM stop-latch survival;
their old images must not be replayed. New source requires a CP AON-write lease
and an explicit expiring UTC slot, default disabled. No new retained pilot was
run. Exact normal AP/CP are restored, with received PFS/encrypted recovery. See
[the current gates and evidence](coordinated-sleep.md). No new electrical saving;
reconnect/cold-power/soak and final display work remain open.

## 1 Establish the baseline

- [x] Investigate optional, default-off Wi-Fi: [findings and 31 October revalidation](wifi-power.md).
  Native AP has no Wi-Fi stack; verified stock/recorded-installed CP initializes
  Wi-Fi and acquires MAC/PHY/clock/RF votes. Per-block Wi-Fi electrical state is
  unmeasured; bounded system input-power comparisons are recorded below. Current builds and last recorded installed AP/CP are frozen with
  hashes under `artifacts/wifi-optional-20261030/baseline/` in the reference workspace.
- [x] Build isolated board-owned Wi-Fi off/on CP profiles with pinned inputs,
  conditional VNET/lwIP/PHY dependencies, source RF correction, exact image/config
  guards, relocation-verified latency correction and native ABI contract.
  Preserve the readback-verified installed controller flash ACK/deadline contract
  in ITCM; 25 focused host tests pass. See [the latest implementation](wifi-power.md#source-built-offon-profiles--31-october-2026).
- [ ] Integrate a qualified production default-off selection. Experimental
  packaging defaults to off, but normal CP packaging/runner and installed control
  selection remain separate. Final corrected raw/config hashes reproduce across clean source builds;
  device/PFS/service behavior and energy acceptance remain gates. Runtime deinit
  remains unsupported. No fixed-hash/address guard was weakened.
- [ ] **Wi-Fi qualification — bounded measurements and 25 reconnects complete:**
  initial off `e48465c3…` faulted through a NULL calibration callback; its complete
  first failure and exact rollback are retained. Pinned callbacks are now kept
  independently of Wi-Fi, covered by the actual ARM path in 25 host tests.
  Corrected off `d4d65559…` is installed with AP `c656055a…`; actual on/off CP
  readbacks match the guarded raw/protected packages and CRCs. Existing SD PFS
  reopens and bonded/encrypted Bluetooth reconnects.
  Quiet idle off/on is 148.091 / 150.332 mW: 2.240 mW (1.490%) lower in a single
  pair. Earlier notification-contaminated controls are secondary; no fully quiet
  idle ABA is established. Fixed-dim-Nadir on/off/on workloads completed five
  encrypted/protocol recoveries per batch: 9.117362 / 8.999244 / 9.196576 J per
  completion. Off is 1.7225% below the mean flanking controls; on drift is 0.8688%.
  The separate 25-consecutive-cycle run completed exact disconnect/encryption/
  protocol counts and full tail, with post-run dim Nadir/BT confirmation and
  delayed review. Known ATT errors are retained; its energy is excluded because
  notifications occurred during settling. Failed readback handshake, successful
  retry and all excluded measurement attempts remain preserved.
  Pebblecraft `0x6e69616c/0x1b0d` and watchface `0/0xc89` faults reproduce on exact
  original CP `1713dee…`; root cause and application/PFS integrity remain open.
  True all-power-removed cold boot is pending; overnight observation is deferred.
  Production default-off integration remains gated. No calibrated battery-life
  or fully hardware-qualified saving is claimed. See [current status](current-status.md).
- [x] Record firmware/AP and CP identities, effective build configuration,
  fitted cell, supply voltage and measurement setup. Frozen AP `c656055a…`,
  actual on/off CP readbacks and guarded effective configs are retained. Setup:
  USB/no cell, dim Nadir, bonded Bluetooth, authorized USB Android phone with
  scan policy 1; workload mean supply 4.988343–4.997812 V. Numeric panel luminance
  and battery-side current are unmeasured; FNB58 records USB input (0.15 W is
  150 mW, not mV). This records the baseline, not hardware qualification.
- [ ] Measure connected idle, disconnected advertising, scripted interaction,
  app transfer, motion wake, and microphone recording followed by idle. Hold
  display brightness and content fixed throughout the non-display phases.
- [ ] Complete sparse counters for IRQ wakeups, actual CPU clock, PM votes,
  peripheral clock state, sensor ODR/watermark, touch state and negotiated BLE
  interval/latency. Avoid continuous logs that distort the measurement.
  Existing minute timer now has opt-in actual CPU/SD activity/byte/error and PM
  event-loss reports; actual received reports are verified in the release
  control. Other peripheral/sensor/memory/radio state coverage remains open.
- [ ] Preserve current controls: one-second CP heartbeat, ADC cleanup, microphone
  audio shutdown, disabled amplifier/gyro, unused-accelerometer shutdown and
  touch reset on disable.

31 October baseline progress is recorded in [the continuation](non-display-power.md):
AP identity and no-cell USB setup verified, exact off/on images frozen and matched
idle windows recorded with display settings fixed. The later CP readbacks and
counted reconnect workloads are in [current status](current-status.md). Sparse
complete runtime state and the remaining workload matrix are still open.

## 2 Balance non-display peripheral clocks

- [x] Implement default-off local I2C bypass release/acquisition with sanitized
  STOP/fault/cancel/recovery tests and normal image validation.
- [ ] Qualify local automatic gating and a repeatable electrical benefit; system
  source-clock/CP vote release is still separate. The option remains default off.
  Wait for physical STOP/bus-free before gating; retain recovery and
  timeout behaviour on the shared touch/RTC/IMU bus.
- [x] Implement default-off AP UART local gating with last-bit TX drain, RX
  retention, reacquisition before MMIO and sticky timeout fallback. Native
  sanitizer tests pass; no CP source vote is released. See [continuation](power-continuation.md).
- [ ] Qualify physical AP UART gating and CP-owned runtime UART/source release.
  Four-bit SD owns the AP UART pins; no current CP console saving is inferred.
- [x] Implement default-off quiet successful battery sample logging; defaults
  retain existing traffic, profiling retains successes, and failures/service
  behavior are preserved. Hardware benefit remains unqualified.
- [ ] Qualify reduced production serial traffic while retaining bounded fault
  diagnostics; compare current with and without the debug connection.
- [x] Centralize AP GDMA interrupt/channel ownership in the native DMA controller
  (existing migration); preserve CP channels. Host isolation tests pass.
- [x] Implement default-off SD controller-local bypass release after card-ready,
  command completion and drained DMA; reacquire before MMIO/IRQ delivery. Native
  failure/ownership tests and matched image checks pass. See [SD clock experiment](sd-idle-clock.md).
- [x] Correct the foreground SD mask RMW race against the DMA callback; behavioral
  injection and six SD/PFS tests pass. Fixed 240/120 diagnostics each complete five
  counted reconnects with zero SD errors. Original failure causation and full
  storage/Bluetooth qualification remain open; see [SD investigation](sd-write-completion.md).
- [ ] Qualify SD local gating, electrical benefit, durable PFS and physical fault
  recovery. Bounded existing-PFS/bonded startup and fixed-display idle/five-reconnect
  off/on/off windows are complete, without a causal/repeatable saving established.
  Cold-start captures are incomplete; durability, physical faults, 25 cycles and
  deferred overnight qualification remain open. Source/domain/CP votes stay held;
  default off.
- [ ] Define shared backup-domain/GDMA power ownership for microphone, optional SD
  and display. Preserve all active owners; do not gate the whole domain from
  an individual driver. Defer display-specific changes to phase 9.

Native SD already clears its FIFO `SD_CLOCK_ALWAYS_ON` override after CMD12 and
status-ready polling. The default retains controller bypass and the CP-managed
source/domain vote. The opt-in experiment releases only local bypass; continue
qualifying it separately from domain release, including final card busy/drain,
fault recovery and CP ownership; clearing a FIFO override does not establish
safe source-clock/domain release. Preserve storage geometry and the existing PFS.

- [ ] Verify error, cancellation and repeated initialization paths release each
  acquired resource exactly once.

See [current ownership/dependency findings](non-display-power.md#non-display-dependencies-and-concrete-next-actions)
for CP-owned runtime UART, drain/RX semantics, shared-domain arbitration, the
single CP-managed AON RTC and native wake-notification prerequisites. These are
open gates, not implemented suspend behavior.

## 3 Implement the wake timer and tickless idle

- [x] Implement default-off CP-applied, owner/generation-tagged native alarm and
  deferred wake/resume routing. Bounded awake-AP v1 diagnostics passed; CP radio
  failure afterward prevents promotion. Later ABI v4 retained pilots failed; see
  [coordinated sleep](coordinated-sleep.md).
- [x] Host-test retained tick accounting for pending/early wake, wrap, fractions
  and exactly-once catch-up. This is not physical deadline qualification.

- [x] Implement an opt-in ordinary-WFI timer contract using the AP-owned SysTick,
  acknowledged CP veto/vote guards and a conservative 160 MHz transient floor.
  Native sanitizer tests and matched release image checks pass. This is separate
  from CP AON wake routing and retained sleep. See [timer experiment](tickless-wfi.md).
- [ ] Qualify that opt-in contract on hardware: steady AON/tick accounting,
  deadlines and heartbeat, connected idle/workload energy, external wake, transfers,
  faults, reconnect/cold-power and overnight soak. Keep default off.

- [x] Implement opt-in independent bounded scalar wake notification routing,
  preserving synchronous PM replies and sticky event-loss evidence. Native
  interleaving/overflow tests pass; this does not arm an alarm or qualify resume.
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

Target policy: keep the normal 240 MHz active vote; switch to the SoC low-power
source and disable unused PLLs only inside CP-coordinated retained sleep, restoring
the saved clocks before work resumes. Follow PR2's suspend/resume structure with
T5AI-specific ownership and sequencing. See [clock policy](retained-clock-policy.md).

- [x] Implement default-off nested/epoch-checked blockers for mailbox, flash/PFS,
  I2C, UART RX/TX and shared GDMA owners. Check physical ACKs and logical IPC
  buffer/response leases separately. Host race/uncertain-drain tests pass.
- [x] Implement default-off CP readiness/AP publication and SRAM retained-entry
  candidate with CP-owned clock restoration and a PSRAM canary.
- [x] Fit the candidate in the authorized 1,664 KiB AP partition and audit the
  final linked SRAM path, including the SRAM reset helper.
- [ ] Qualify all driver lifetimes, native PSRAM/AHBP retention and independent
  recovery from a lost wake. The corrected bounded pilot froze; keep default off.
- [ ] Isolate the alarm-only CP's uncommanded Bluetooth failures before further
  retained-sleep hardware trials; first trace and exact rollback are preserved.
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

- [x] Receive sparse hardware identity/revision reports `05/7c` in the SD
  diagnostic images; record the register observation separately from WOM/INT2
  qualification. See [SD follow-up](sd-write-completion.md).
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

- [x] Remove the failed fixed 120 MHz startup selector and bounded shared-frequency
  probe, including their Kconfig entries and callbacks. Keep the normal active
  vote at 240 MHz and read-only elected-clock reporting. Clock addresses, masks,
  source selections and rates now use named constants; 17 relevant host tests pass.
- [ ] Resolve shared-frequency/radio behavior before accepting any fixed-frequency
  energy claim. The original 120 MHz SD/PFS failure excluded its whole window;
  the SD setup race fix has bounded five-cycle evidence at both clocks. Bluetooth
  qualification remains open, including possible CP state carried across warm
  AP restore. See [clock policy](retained-clock-policy.md) and
  [SD investigation](sd-write-completion.md).
- [ ] Qualify full-speed active / coordinated retained-sleep clock transitions.
  Compare energy per completed workload and idle current, recording actual CP0
  and AP clocks and voltage selectors. The failed fixed 120 MHz selector is removed;
  focus implementation on wake/alarm ownership and CP-coordinated sleep/resume.
- [ ] Verify accepted BLE interval/latency and time in fast/idle states before
  changing the existing idle-latency policy. Check CP radio/module votes rather
  than inferring Wi-Fi or RF power from AP inactivity.

## 8 Qualify non-display savings

New power policies remain default-off pending coordinated sleep and regression
qualification. The SD setup-race correction has bounded functional evidence;
the failed fixed-frequency energy window remains excluded. Uncommanded Bluetooth
failures and delayed reconnects remain part of qualification. Current firmware,
received recovery and active diagnostics are recorded in the dated entries at
the top of this file and in current-status.md; the historical entry-AP rollback
in power-continuation.md is no longer the active firmware state. No new saving
is accepted from a failed or reset interval.

The operator deferred the overnight soak to a later overnight window. It remains
an open qualification gate; no soak is running or recorded as passed.

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

- [x] Add separate default-off raw-zero brightness and explicit display-disable
  sleep implementations. Preserve nonzero dim floor, serialize transitions with
  transfers and defer Display On until a full post-reset redraw succeeds. Native
  failure/cache/redraw tests pass; see [continuation](power-continuation.md).
- [ ] Qualify active/dim/zero/asleep policy and physical Sleep In/wake timing;
  raw zero is distinct from panel sleep and source-clock release.
- [ ] Qualify dim brightness, refresh policy and startup brightness on hardware.
- [ ] Hardware-qualify the opt-in Display Off/Sleep In and wake sequence,
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


## Additional history from boards/unhygienix/tuya/REMAINING_TASKS.md

# Unhygienix validation and remaining work

The operator has deferred CP deep sleep. Preserve all coordinated-sleep findings
and first-failure evidence; do not continue the planned analog/HF CP transition.
The active implementation and qualification sequence is [AP-only sleep](../../../docs/boards/unhygienix/ap-only-sleep.md).
`WAKE_ONLY` now takes operation-selection precedence and its cleanup rejects any
CP low-voltage, resource, clock or mask counter increment after each AP return.
The CP APP veto remains held; shared clocks/voltage/memory policy stays active.

AP `ed05ff87…` is installed with unchanged v5 CP `1ed83dfb…`; its arming slot is
expired and retained sleep remains off. Admission now retries every 20 ms within
the two-second slot, records blockers, rejects expired startup before the CP
lease and checks final time plus accepted watchdog register readback. Thirty-six
host tests and ARM/CRC/map/SRAM checks pass. Two timed reset/recovery observations
replace the former no-WFI cancellation; reset source/period attribution and
reset-surviving evidence remain open because boot reason and SRAM marker are
still zero. See [current evidence and next gates](../../../docs/boards/unhygienix/coordinated-sleep.md).
No new measured savings, cold-power or overnight qualification; display is last.

Next: qualify the leased, expiring-slot guard and reset-surviving evidence before
another retained pilot, then verify clocks, retention, timers, reconnect/cold-power
and the deferred soak. Known-CP rejected-WFI control radio failures also need
operator/context correlation. No new measured savings; display remains last.
See [coordinated sleep](../../../docs/boards/unhygienix/coordinated-sleep.md) for implementation, evidence and the ordered gates.

Before another pilot: establish an independent safeguard for WFI that never
returns, complete native PSRAM/AHBP ownership, and preserve final CP/AP stage
markers. The current 700 ms timeout executes only after WFI; the CP memory-ready
bit checks policy rather than actual owner retention. Neither is sufficient
physical proof. See [coordinated sleep](../../../docs/boards/unhygienix/coordinated-sleep.md) for the full failure sequence, ownership findings,
artifacts and ordered gates. No new measured savings; display and overnight
work remain deferred.

## Earlier coordinated-sleep checkpoint

Known CP `d4d65559…` was restored without changing the AP. Its six-minute connected
idle control received zero disconnects/errors and the same 60 ms/latency-4
parameters. The separate six-minute AP-only warm-return phase also completed
with no received disconnects and one known ATT subscription error. The operator
confirmed dim Nadir stayed selected and Bluetooth connected. Both UART and phone
captures stopped at 21:27:49 UTC; host receiver processes were verified absent.
No new electrical saving is claimed. Latest retained AP sources compile but
exceed the unchanged partition by 1,000 B, blocking final linked SRAM audit and
physical retained entry. Normal default-off release `54845a3a…` passes image/map,
CRC, resource, log and syscall validation with 2,056 B headroom; it is unflashed.
Installed AP remains `86d1dcba…`, with known CP `d4d65559…` and 240 MHz active.
Overnight qualification remains deferred. See
[coordinated sleep evidence and gates](../../../docs/boards/unhygienix/coordinated-sleep.md).

Earlier checkpoints below are historical.

**Latest clock policy:** keep 240 MHz active and follow PR2's coordinated
suspend/resume structure using T5AI's CP-owned 26 MHz/DPLL sequence. Fixed shared
120 MHz startup/probe code and Kconfig are removed. Their archived measurements
show the vote also lowered CP0/Bluetooth and the voltage selector.
A bounded single-boot 240→120→240 probe had no received disconnect/SD failure,
which does not disprove intermittent behavior. Warm AP restore leaves CP state
alive. Exact entry AP is restored with received PFS/240 MHz/protocol recovery;
CP unchanged, captures stopped, no new energy claim. Next: applied alarm
owner/generation, wake accounting, then retained entry/resume. See
[clock policy](../../../docs/boards/unhygienix/retained-clock-policy.md) and
[current status](../../../docs/boards/unhygienix/current-status.md).

**SD follow-up:** the foreground mask/DMA callback race is corrected and behaviorally
reproduced in the host test. Six SD/PFS tests pass. Fixed 240/120 diagnostics each
complete five counted reconnects with zero SD errors; Bluetooth discovery and
parameter errors remain. Original SD failure causation is unproven. Exact entry
AP is restored, CP unchanged, captures stopped. Normal fixed source `4708694e…`
is built but uninstalled. See [SD investigation](../../../docs/boards/unhygienix/sd-write-completion.md)
for first failures, exclusions, later entry-AP terminations and remaining gates.

**31 October final power checkpoint:** exact entry AP `86d1dcba…` is restored;
CP was not written this increment. Expected AP/PFS/240 MHz/protocol startup is
received and the operator confirmed Nadir/Bluetooth. All captures are stopped.
The actual-120-MHz trial hit CMD25 timeout/PFS assertion/AP reset; whole batch is
excluded and frequency causation is unestablished. The return control lost its
host USB hub before the full tail and is also excluded. Unexplained `0x0208`
Bluetooth terminations occurred on a temporary normal image and later on the
restored entry AP; final entry recovery is operator-reported, absent from the
saved trace. The operator confirmed the phone was untouched during the entry-AP
timeout and Bluetooth recovered afterward. Resolve storage and Bluetooth before
broader power rollout.

New default-off UART/event/frequency/diagnostic/quiet-log and separate final-display
implementations pass 50 relevant host tests. Final normal source `816dfa48…`
builds with 328 B FLASH headroom but is not installed. No new comparative saving
is accepted. See [current status](../../../docs/boards/unhygienix/current-status.md)
and [continuation](../../../docs/boards/unhygienix/power-continuation.md) for exact
hashes, first failures, baseline energy and remaining ownership/wake contracts.
Cold/soak and broader workload qualification remain open; overnight is deferred.
Older current-image claims below describe historical checkpoints.

Corrected Wi-Fi-off CP **`d4d65559…` is currently installed**, with unchanged AP
`c656055a…`, for consecutive reconnect qualification. Its expected AP startup is
received. The first pre-qualification readback handshake timed out after
commanded resets; its complete trace is preserved, with no received CP crash and
normal existing-PFS/bonded-session recovery. A fresh-name retry read all 1,114,112
bytes successfully: active CRCs and exact corrected-off package bytes match, and
the full physical SHA is `4f2fd6448da1a78d9c918676faccfb4c103dc08df0cdb6fbfa62a829fc0ebd6c`.
`adb-off-25-A` completed at 15:15:17 UTC after all **25 consecutive cycles**
and the full recovery tail: exactly 25 disconnects, bonded/encrypted recoveries
and protocol sessions, no extra termination or new error class detected by the
harness. The logger closed 0 at 15:15:20. Four notifications and one known ATT
subscription error were recorded during settling; this functional run's energy
is excluded from fixed-display comparison. The operator confirmed Bluetooth
connected and dim Nadir restored/unchanged. Delayed review extends through device
time 15:15:56, after the 15:15:17 batch end, with no new error class, fault/reset,
app transfer or extra termination. The counted functional gate is complete;
full hardware acceptance remains open. The physical cold-power check is armed
and requested in `cold-off-A`; no fresh boot or operator power-removal result
has been recorded yet. Overnight observation has not started and is deferred by the operator until
a later overnight window; it is not a passed or waived qualification gate.
No experimental image is promoted; production defaults, AP/display policy,
common source and pinned vendor trees remain unchanged.

[Source-built guarded Wi-Fi off/on profiles](../../../docs/boards/unhygienix/wifi-power.md)
now build reproducibly with pinned inputs and preserve RF/latency corrections,
native ABI and the installed controller flash deadline/ACK contract. An initial
off calibration NULL-callback boot fault was preserved and rolled back; the pinned
callbacks are now retained and covered by the actual ARM path. Twenty-five focused
host checks pass. Vendor/common source and AP/display configuration are unchanged.

Corrected off `d4d65559…` boots AP `c656055a…`, reopens existing PFS and reconnects
bonded/encrypted BT. A user app-transfer/fault-contaminated window is excluded
whole. CRC-valid device timestamps also downgrade both earlier on controls for
notifications during settling. The fully quiet off/on pair is 148.091 / 150.332 mW,
a 2.240 mW (1.490%) difference; there is no fully quiet ABA or qualified saving.
Phone transfer requires app launch and changes display content, so workload
energy is excluded. On control also faults/relaunches watchface UUID `16183a4f…`
at PC `0`, LR `0xc89`. Both that and the earlier Pebblecraft fault reproduce with
the exact original CP (`1713dee…`), restored at that baseline stage, with operator-confirmed recovery
and CRC-valid existing-PFS/bonded-session evidence. Root cause remains open; do
not attribute either signature to Wi-Fi removal. Preserve all first traces.

The first fixed 240 s original-CP reconnect batch has five encrypted/protocol
completions plus a sixth disconnect before its end (that session opens afterward).
It is excluded from controlled workload-energy acceptance; the operator confirms five manual cycles with dim Nadir
unchanged. Earlier original-CP disconnects were operator-confirmed
Bluetooth/range changes, not spontaneous faults. The later automated checkpoint
records 25 completed candidate reconnects. PFS/application integrity, true cold
power and deferred overnight interaction remain open.
Normal CP defaults/runner are unchanged; neither experimental CP is qualified.
The timestamped receiver has received data but logs can be buffered for minutes;
use CRC-valid device timestamps rather than host arrival to classify activity.
The experiment stream is not an uninterrupted soak. Display remains last.

## SD clock implementation — 2026-10-31

[SD local-clock qualification](../../../docs/boards/unhygienix/sd-idle-clock.md)
is implemented as `CONFIG_T5AI_SD_IDLE_GATING`, default off. Sixteen relevant
host tests and both normal image checks pass; candidate FLASH headroom is 8 B.
Card-ready, command and DMA guards preserve fault quarantine and other owners.
CP SDIO/source and GDMA/BAKP votes stay held. Both APs boot existing SD/PFS and
bonded protocol; the fixed-display idle/five-reconnect off/on/off comparison is
complete. A single nominal USB ABA does not establish causal/repeatable savings.
The identical off control `86d1dcba…` is restored; CP and display are unchanged.
Two operator-reported cold recoveries have UART startup gaps, so full captured
cold qualification stays open. Durable storage, physical mixed-DMA/fault tests,
a 25-cycle run for this AP and the deferred overnight soak remain open. All SD
UART/power captures are stopped. See current status for measured values/limits.

## Current power continuation — 2026-10-31

See [the baseline, I2C candidate and ordered dependency record](../../../docs/boards/unhygienix/non-display-power.md).
The first continuation started at `2eacc9eaa`; A5 is committed (`add66a5e5`) and the later native
migration supersedes the historical transport/installed-image statements below.
Entry AP `6b314eb5…` was verified by UART; CP readback remains unverified and CP
was not written. USB-only/no-cell, dimmed Nadir and Bluetooth were operator confirmed.
Local I2C bypass gating is implemented and host/image tested as a default-off
qualification option. Startup/SD/bonded reconnection and an off/on/off measurement
sequence do not close fault, counted-workload or long-term qualification.
The current controller centralizes AP DMA IRQ ownership; shared-domain/CP power
arbitration remains open. Display source/policy/configuration are held fixed.
Wake/tickless, retained sleep, sensors, memory and CPU/radio work remain in the
ordered power TODO; the sleep veto and SysTick are retained. No measured saving
or full qualification is claimed. See current status for the final installed ID.

The next increment starts from committed I2C HEAD `198385cb0`. A default-off
AP-local ordinary-WFI timer extension and sparse qualification counters are
implemented and host/release-image tested. It retains the veto/active vote,
uses the audited 160 MHz transition floor and avoids CP RTC compare ownership.
Bounded device accounting passes; about 6% fewer tick IRQs shows no demonstrated
electrical saving (30.070 mA on / 30.030 mA off), so the option stays off.
Counted-workload energy, full wake deadlines, reconnect/cold-power and soak gates
remain open. See the
[timer experiment and final installed disposition](../../../docs/boards/unhygienix/tickless-wfi.md).

## Historical correctness packages — 2026-10-30

- [x] **A1 / F01:** committed in `f3ebbee4c`, complete and explicitly operator
  accepted. The installed SD image reopened the existing volume without
  provisioning. [Final acceptance record](/home/baz/Documents/ChatGPT/pebble/artifacts/a1-sd-reflash-20261030/RESULTS.md).
- [x] **A2 / F02 and F06:** committed in `f7f7cbd84`; bounded deferred HCI pressure
  diagnostics, early mailbox-owner logging/blocking guards and under-lock PM
  quarantine. Host validation: 9 focused tests; 231 port tests plus 75 subtests;
  28 shared tests plus 119 subtests. Fresh normal/release packaging and map checks
  pass. [Validation record](/home/baz/Documents/ChatGPT/pebble/artifacts/a2-mailbox-20261030/RESULTS.md).
  No standalone A2 flash occurred; its corrections are included in the A3, A4 and A5 AP images.
- [x] **A3 / F03 implementation and host/image validation:** bounded receive
  retention reuses the original 1,285-byte IPC staging budget, with nonblocking
  retries and explicit recovery on reliable exhaustion/deadline. Normal adds no
  SRAM over A2 (20,192-byte linked heap arena); release adds 32 bytes. All 233
  port tests plus 81 subtests and 28 shared tests plus 119 subtests pass, with
  the same four optional execution checks unconfigured. Fresh normal/release
  images pass protected packaging/maps/config checks. Committed as `06079008a`.
  [Implementation record](/home/baz/Documents/ChatGPT/pebble/artifacts/a3-hci-pressure-20261030/RESULTS.md).
- [x] **A3 AP flash and startup/reconnect check:** compact normal build
  `4245c7ab3e16d7c5449b864b355ea1ba22550993` was installed; A5 now supersedes it. UART confirms four-bit
  SD, the existing 32 MiB volume (`new=0`), 49 apps and encrypted bonded PPoGATT.
  CP, resources, persistent volumes and bonds were excluded from flashing.
  [Flash record](/home/baz/Documents/ChatGPT/pebble/artifacts/a3-flash-20261030/RESULTS.md).
- [ ] **A3 device qualification:** pressure during phone/app transfers, repeated
  credit/connection recovery, heap largest-block/fragmentation and worker stack
  high-water; watchface install/launch, 25 reconnect cycles, cold boot/bond
  restoration and overnight connected idle. Startup verification is not operator
  acceptance or resolution of historical app/radio faults.
- [x] **A4 / F04 and F05 implementation and host/image validation:** restart after
  failed stopped-calendar writes, preserve the first error and gate persistence
  health on readback. Retained word 22 preserves the external fault signature.
  Saved-mask tuple accesses and ordered cache-durable commits preserve the
  136-byte retained layout/checksum. Separate F04/F05 diffs and regressions are
  reviewable; committed as `bf10c6435`. Validation: 5 focused tests
  plus 74 subtests; 231 port tests plus 147 subtests; 28 shared tests plus 119
  subtests. Four existing optional execution checks remain unconfigured. Fresh
  normal/release builds pass packaging/maps. [Implementation record](/home/baz/Documents/ChatGPT/pebble/artifacts/a4-rtc-20261030/RESULTS.md).
- [x] **A4 AP flash and bounded hardware checks:** rebuilt normal image
  `943caf619e5bb3e49f6e852bfe97efe0eb8323f0` was installed; A5 now supersedes it. Initial boot and one
  controlled DTR/RTS reset confirm current boot time before phone reconnection,
  existing four-bit SD/PFS with 49 apps, and encrypted bonded PPoGATT. CP,
  resources, volumes and bonds were not written or provisioned. Captures stopped.
  [Flash/test record](/home/baz/Documents/ChatGPT/pebble/artifacts/a4-flash-20261030/RESULTS.md).
- [ ] **A4 device qualification:** actual STOP/calendar/restart/readback faults,
  direct persistence-health observation, reset at retained/cache-write boundaries,
  wrap/IRQ-mask latency, battery-loss/cold boot, oscillator/calendar drift and
  longer board checks. Console time writes did not execute because T5AI debug
  UART input is stubbed even in normal builds; do not count them as passing.
- [x] **A5 / F07–F09 implementation and host/image validation:** bounded eFuse
  polling requires completion before valid data; the common idle gate suppresses
  WFI; local GDMA claims preserve display/MIC/SD and inherited routing across
  microphone lifecycle changes. CP ownership, acknowledgements, display deadlines,
  polling and experimental defaults remain unchanged. Separate F08/F09/F07 diffs
  pass independently. 10 focused tests plus 8 subtests; 235 port tests plus 155
  subtests; 28 shared tests plus 119 subtests. Four existing optional execution
  checks remain unconfigured. Nine fresh images cover normal/release and all
  eight DMA combinations, with packaging/maps passing. A5 remains uncommitted.
  [Implementation record](/home/baz/Documents/ChatGPT/pebble/artifacts/a5-local-20261030/RESULTS.md).
- [x] **A5 AP flash and startup/reconnection:** normal image
  `901c6c58f0d520c5388b15a0865d4b21c055efb3` is installed. Exact UART ID,
  existing four-bit SD/PFS with 49 apps and encrypted bonded PPoGATT verified.
  CP, resources, PFS and bonds preserved. Capture stopped; no fatal fault or
  additional uncommanded boot observed. `Animation 3 does not exist` was logged
  and remains unassigned. [Flash record](/home/baz/Documents/ChatGPT/pebble/artifacts/a5-flash-20261030/RESULTS.md).
- [ ] **A5 device qualification:** physical eFuse timing/faults and factory ID;
  runtime idle disable/enable and latency; microphone/display/SD transfer,
  init-order/teardown/fault stress and CP coexistence; audio/storage integrity;
  then standing watchface, 25 reconnects, cold boot and overnight checks.
- [ ] **T5AI console input integration:** runtime output uses the CP-owned UART;
  input callbacks are unconditional no-ops. Other SoCs use shared RX in normal/MFG
  builds and stub it in non-MFG release. This predates A4 and remains separate
  work; do not reclaim AP UART0 pins from four-bit SD to enable console input.

Preserve default SD PFS, four-bit mode, the existing-volume guard, CP wire ABI,
A1 cache maintenance, A2 owner guards/quarantine and the one-second heartbeat.
The latest verified AP is A5 `901c6c58`; all bounded captures are stopped.
A1's first-launch app fault and longer device checks remain open. Implementation,
host/image validation and device qualification remain separate dispositions.

## Highest priority — power optimization — 2026-10-30

Wi-Fi investigation is complete; default-off implementation remains blocked on
CP Bluetooth-only build/packaging integration, and device/current qualification
remains pending. [Findings and exact next steps](../../../docs/boards/unhygienix/wifi-power.md).
Current builds and recorded installed images are preserved for later current
benchmarking; no Wi-Fi-off firmware was flashed and no savings are claimed.

- [ ] Complete the [power TODO](../../../docs/boards/unhygienix/power-todo.md)
  as the highest-priority workstream. Work through the non-display phases first;
  leave AMOLED power states, display QSPI gating and display DMA changes until
  the final phase. Track implementation and measured device qualification
  separately. Existing stability, data-integrity and release checks remain required.

This power sequence follows the local correctness prerequisites above and
supersedes the power ordering in older dated dispositions below.

## Historical disposition — 2026-10-22

Use the [current status](../../../docs/boards/unhygienix/current-status.md) for
latest firmware, measured results and remaining priorities. AP `c0e283f0` was the
verified image at the 22 October refresh: four-bit DMA SD PFS, dedicated 8 KiB PSRAM metadata cache,
about 12 KiB observed free kernel heap and normal notification display. The
25.56-minute resumed capture had no reboot; seven recovered BLE timeouts and
one nonfatal oversized-image drop remain open. Capture stopped.

**Language-pack work is complete and closed by operator direction.** Remove
language/font install, replacement/removal and glyph qualification from active
work. No new tests are claimed. Historical language-switch watchdog evidence
remains under general stability investigation, not a language-pack blocker.
Full bank CRC/table validation and explicit no-update-bank handling are implemented.

The October 19 display implementation/measurement pass is complete; polling is
retained after IRQ measured slower. The October 21 CP latency-transition fix is
installed, with recorder/zero-latency workaround removed. Remaining priorities:
Bluetooth transfer/reconnect/soak, general watchdog/full-sync stalls, SD performance
and interruption recovery, peak heap/stack and image allocation, low power/wake,
fitted-cell battery/RTC checks, upstream cleanup and safe OTA/recovery design.
A whole-heap PSRAM move still needs benchmarks and is not implemented.

The dated sections below are historical progress records. This disposition
supersedes their older open/installed-image claims; retain original failed traces.


The [operator handoff](../../../docs/boards/unhygienix/index.md) records current hardware
support and implementation gaps. The [tooling guide](../../../docs/boards/unhygienix/tooling.md)
records dependency, build, packaging and flash procedures.

## Tuya private workers — 2026-10-18

Heartbeat and touch consolidation were explicitly resumed on 2026-10-18. Battery
subsystem review and software consolidation are complete; device qualification
remains open. Native object ownership does not establish that every dedicated worker or its
current stack capacity is necessary.

- **Touch — implemented and flashed; broader qualification pending:** CST92xx now follows stock CST816's
  coalesced raised-priority KernelBG callback and synchronized native input reports.
  The private thread, semaphore and 3,072-byte stack are removed, along with direct
  app/worker/kernel queue inspection and the driver-specific 33 ms motion throttle.
  A native mutex serializes controller reads/reset with enable/disable; input
  listeners run after that lock is released. Generation checks reject stale work.
  CST9217-specific report decoding, mirrored coordinates, reset timing and bounded
  recovery remain local: a one-shot timer schedules the 20 ms contact/asserted-INT
  fallback and 80/1,000 ms read/reset retries. A shared one-second regular callback
  rescues dropped work or a missed asserted edge; idle does no I2C. Timers only
  enqueue work. Read failures report liftoff through the stock input path.
  Candidate `69549c57d6ef588a96516b36640d53b18e705d20` builds/packages.
  The 287 tooling tests pass with two skips, including five full-driver cases;
  four rebuilt native touch/input suites pass (57 cases). Static RAM falls by 3,168 B
  to 101,728 B; linker heap becomes 25,248 B. Flashed AP-only with boot ID verified;
  encrypted bond/phone session restored and discovery completed in 3,759 ms.
  The operator confirms taps, drags/swipes, clean finger lifts and sleep/wake touch
  work normally, and reconfirms touch after a KernelBG watchdog warning/recovery.
  The new image logged a 5,952 ms BG feed delay and App throttling for FdF Time,
  then recovery; no later AP boot was decoded. A later channel-mask warning lacks
  matching feed-age/recovery records before capture ends. Cause remains unresolved and the
  combined run is not clean qualification. First failure and full trace preserved;
  capture stopped. A pre-flash recovery record is kept separate from new-image
  findings. Queue saturation, measured latency/stack/heap, injected I2C recovery,
  cold power-cycle and sustained watchdog checks remain open.
  Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/cst92xx-kernelbg-20261018/flash/report.txt`.
- **Battery — repair/consolidation flashed; ADC and controls pass, watchdog open:** the private
  worker and its 2,048-byte stack are removed. The existing voltage service drives
  sampling; getters submit a nonblocking request and report unavailable until it
  completes. The existing mailbox owner advances bounded asynchronous MB-IPC/
  SARADC phases with a 5-second whole-acquisition deadline (500 ms connect,
  existing 2-second CP conversion timeout). No ADC waits, sleeps or extra thread
  run on KernelBG or NewTimer. The raw-read error is preserved; successful raw
  replies always complete READ_RAW_DONE before cleanup, including rejected
  CRC/size data. Cleanup only acts on successfully acquired resources. Ambiguous
  transport failures and failed cleanup retain/poison CP-visible storage until
  reset. Synchronous flash waits for a shared physical slot instead of failing
  busy; asynchronous callers cannot overtake it. Wiring/calibration are board
  data, and BATTERY_T5AI selects the driver. A native both-edge charge IRQ plus
  shared regular timer provides debounced stock connection events, missed-edge
  reconciliation and dropped-callback recovery. CHRG remains only a charging
  proxy; full/plugged versus unplugged is not independently observable.
  The previous validity fix remains: no 3,700 mV default, failure/expiry returns
  unavailable, and the stock voltage service skips invalid readings. All 299
  tooling entries pass with two skips; all 15 native battery-monitor cases pass.
  Static RAM falls by 1,760 B to 99,968 B; linker heap becomes 27,008 B. These
  are linker capacities, not measured peak headroom. Final build
  `779f3e5a1bcaeed66d06043ead303ab3c91254ed` is flashed AP-only; boot ID verified.
  Four ADC samples succeeded in 6–7 ms at 4,011–4,025 mV; zero ADC errors and
  no additional AP boots in the completed 300.478-second capture. The operator
  confirms watchface, phone, controls and sleep/wake work normally. KernelBG
  watchdog stalls/recoveries persist and remain unresolved. Hardware testing
  exposed mailbox-owner synchronous logging and an inherited vendor CRC bit
  error; both are corrected with regression coverage. Failed candidates and the
  recovery flash are preserved. No cell is fitted; unloaded charger voltage does
  not qualify battery calibration or percentage accuracy. Cold-cycle, sustained
  watchdog, read-done timing under load, stack/peak heap and fitted-cell checks
  remain open. [Subsystem review](../../../docs/boards/unhygienix/battery-review.md).
  Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/battery-consolidation-20261018/vendor-crc/flash/report.txt`.
- **CP heartbeat — implemented and flashed; broader qualification pending:**
  This consolidation was not required for correctness, but removes one dedicated
  thread and its 1,024-byte stack, saving 1,152 B of static RAM overall. The existing
  mailbox worker now services the one-second heartbeat deadline. Runtime readiness
  explicitly wakes it; idle waits are bounded by the next heartbeat or physical
  fault check, and busy-loop iterations also check the deadline. Submission and
  physical/logical completion share the mailbox context. The private thread and
  1,024-byte stack are removed; no timer-service dependency or blocking heartbeat
  operation was added. Late execution skips missed intervals without catch-up
  bursts. Firmware builds/packages; 284 tooling entries pass with two skips.
  Static RAM falls by 1,152 B, restoring linker heap capacity to 22,080 B. This is
  not measured peak heap headroom. Flashed AP-only; boot ID
  `c42b19caafdddfeef102c7d1eec3182da1fc0fcb` verified. Phone protocol
  reconnection/discovery and operator face/control checks pass. Capture is stopped;
  warnings/errors remain recorded. Heartbeat timing, mailbox stack high-water,
  cold power-cycle and sustained watchdog behavior still need device checks.
  Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/heartbeat-mailbox-20261018/flash/report.txt`.

Before heartbeat consolidation these workers reserved **6,144 B of internal stack
storage**. Heartbeat, touch and battery are consolidated and flashed. Battery
removes the remaining **2,048 B**. None of those three private stacks remain
in the final image. Together the consolidations recover 6,080 B of static
RAM after accounting for replacement state, bringing linker heap from 20,928 B
at the native-object baseline to 27,008 B. Startup/peak heap and mailbox stack
high-water still need measurement. The original native-object conversion itself
moved allocations from heap to static storage; its +8,768 B static increase
replaced 9,044 B of former heap requests and was not an equivalent steady-state
live-RAM increase. HCI and mailbox owner threads remain necessary to the current
transport design. Broader combined device qualification remains open.
See the [stock comparison and accounting](/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/stock-comparison.md).

## Current disposition — 2026-10-18

The [2026-10-18 stock convergence review](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-18-stock-convergence/review.md)
is the active cleanup checklist and supersedes earlier audit dispositions.
Reassess retained divergences against current source and stock behavior; historical
justifications do not establish a continuing requirement. Completed work must not
be repeated merely because it appears in older progress records.
Current committed source reviewed is `ecf82bce8`, compared with the already merged
local `origin/main` at `b3a2dcb88`; the review did not fetch upstream.

- Completed: QEMU metadata/unused override cleanup, diagnostic flag exclusions,
  tool regression repair/reference staging, alternate syscall-header removal,
  shared resource-bank condition restoration, duplicate image removal, and
  shared GATT discovery consolidation. Recorded GATT validation: 280 tool entries
  passed with one skip, four native suites passed and firmware ELF linked.
- Hardware smoke-tested: the consolidated GATT image restored an encrypted bond,
  opened the protocol session and discovered services in 6499 ms. The operator
  confirmed responsiveness; the 141-second post-flash interval contained no
  later boot or watchdog expiry. This does not close install, cold-boot,
  reconnect-cycle or soak qualification. Exact evidence is linked below.
- Completed and committed: normal T5AI firmware selects the existing normal
  Bluetooth backend; recovery keeps PRF. The SoC selection exception and temporary
  evaluation switch are removed. Three native persistence suites pass (17 cases).
  The normal-backend evaluation image built/packaged successfully; its initial
  Python/protoc mismatch was corrected. The operator accepts re-pairing if needed;
  old-bond preservation and the reproduced PRF-deletion re-export behavior do not
  block this cleanup. A bounded flash/cold-cycle check now passes with a capture
  gap recorded below; broader hardware qualification remains open. Evidence:
  `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/persistence/validation.txt`.
- Open: remaining audit verdicts and justified exceptions, separate upstream
  correctness patches, SDK compatibility-cache removal, display completion IRQ
  investigation, exploratory accelerometer CPU measurement, and full hardware
  qualification. Watchdog/full-sync failures, resource/app-entry warnings and
  transient I2C/touch errors remain unresolved. Flash bypass remains paused.

Latest recorded installed image and configuration are summarized in the
[operator handoff](../../../docs/boards/unhygienix/current-status.md).
No capture remains from the latest GATT test. Merge readiness remains unqualified.

<a id="next-priorities--2026-10-14"></a>

## Historical priorities — 2026-10-18

1. **Top priority: stock convergence and readiness to merge to main.**
   Follow the [2026-10-18 stock convergence review](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-18-stock-convergence/review.md)
   for current decisions, implementation order and validation requirements. It
   reviews `ecf82bce8` against local `origin/main` at `b3a2dcb88`. Preserve or attach
   this local report with the merge review so reviewers can access its evidence.
   The [October 14 audit](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-14-pebbleos/audit.md)
   and its file inventory are historical evidence only, not the active checklist
   or authority for retaining exceptions.

   - Pixel fast-path removal is deferred by the operator. The Getafix full-frame
     compositor override and unrelated utility/cosmetic changes are removed.
     Separate independently useful improvements from the port.
   - SDK compatibility-cache benchmarking is deferred by the operator as of
     2026-10-18. The T5AI GATT discovery lease override is removed; stock zero
     is inherited without a build override. Keep its broader device qualification
     and later real connection-parameter negotiation checks separate.
   - Full-pack CRC/table validation and explicit no-update-bank handling are
     implemented. PFS system-file routing and watch/unwatch are restored;
     language-pack work is closed by operator direction on 2026-10-22.
     Shared-bank backend consolidation remains separate cleanup.
   - Test stock reconnect advertising transitions, then stock connection-parameter
     negotiation, independently on the RF-corrected CP. Record actual parameters
     and update completions. Retain only reproduced controller limitations at a
     narrow boundary; replace the upper layer's unconditional local grants.
   - Share ordinary boot-bit and reboot-record logic through small retained-storage
     and reset hooks. Convert local driver allocations to native kernel objects,
     make allocation/capability contracts explicit, and revalidate current RAM
     budgets, compiler tuning and watchdog timing.
   - Review general correctness changes independently and prepare separate
     upstream patches for GATT lifetimes, PULSE framing/bounds/transport lifetime,
     logging progress, pairing context lifetime, watchdog runnable-waiter
     accounting, KernelBG recovery, serial teardown and MPU correctness. Do not
     resubmit fixes already present in stock or assume each current fix is complete.
   - Resolve every remaining finding with current evidence, then run relevant
     tests and stock-board/QEMU checks. Qualify the combined watch firmware with
     install/launch, 25 reconnect cycles, cold-boot bond/face restoration and
     connected-idle soak before marking it ready for merge review. Preserve the
     first complete failure trace.

   Prefer stock interfaces and narrow board-/SoC-/driver-local implementations.
   CP ownership, actual hardware interfaces, necessary PSRAM placement and
   unsupported update rejection remain constraints; historical failures do not
   justify keeping whole replacements when a smaller stock integration works.
   The dated progress records below are history, not outstanding instructions;
   the current review identifies work already completed. Merge readiness remains
   unqualified.

   Device update — 2026-10-18 (splash RAM reclamation): AP-only flash
   passed in 31.2 seconds; boot ID matches
   `8f99cca0b42e77cf3b9a92edb10da9ea6cfd0991`. Launcher, bond/pairing and
   phone protocol session pass; operator confirms normal splash/watchface,
   phone connection, controls and sleep/wake. The 131.413-second capture
   (including flash/pre-boot) received 16,651 UART bytes and is stopped.
   Six post-boot KernelBG not-fed records and five recoveries remain with
   FdF Time; no additional AP boot/assertion is decoded. Broader stability
   and physical stack high-water qualification remain open. See the
   RAM-reclamation report below for full evidence and limitations.

   Progress — 2026-10-18 (splash RAM reclamation): splash stack and thread
   object now share a temporary kernel-heap allocation, freed by the stopping
   caller after the worker reaches DEAD. Failure paths release owned memory.
   Thirty-eight focused checks and ARM build pass. Static internal RAM falls
   from 101,888 B to 99,744 B (2,144 B recovered); flash rises 64 B and PSRAM
   is unchanged. T5AI requests 2,195 B plus allocator overhead while the splash
   runs. This follow-up is flashed as recorded above; broader stability/stack
   qualification remains open. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/splash-reclaim-20261018/report.md`.

   Device update — 2026-10-18 (stock splash candidate): operator authorized
   AP-only flash of frozen build `d722d45420f10eb5efd6c8a15422d4118e2f15f1`.
   Flash passed in 31.0 seconds; exact physical range `0x132000..0x2d1414`
   preserves CP, resources and PFS. Boot identity matches; pairing completed and
   the phone protocol session opened. The 180.381-second capture received 21,119
   UART bytes and is stopped. It contains seven KernelBG not-fed records and
   seven recovery records during FdF Time, plus filesystem/app-event/pindb and
   advertising-start warnings. This previously recorded symptom remains unresolved;
   no additional AP boot is decoded. This is not clean stability qualification.
   Splash/colors/control/sleep-wake operator confirmation is pending. Full raw,
   CRC-validated replay and warnings are retained in the proposal's `flash/` evidence.

   Proposal — 2026-10-18 (stock boot splash, audit item 18): implemented in
   the working tree for operator review; subsequently flashed as recorded above. The stock
   rendering/animation service uses a small deferred-start/buffer-provider seam.
   The T5AI adapter borrows the existing compositor PSRAM pixel array after board
   validation, releases it through the final frame before compositor_init(), and
   cancels requests stopped before readiness. Main/compositor are unchanged.
   The local boot-frame transfer now locks the panel and accepts ARGB2222 without
   inversion. A shared failure-path fix releases the buffer if thread creation
   fails. Thirty-eight focused checks pass; ARM firmware builds/packages. Flash
   increases by 608 B and static internal RAM by 1,920 B to 101,888 B; PSRAM remains
   118,272 B. The stock task reserves a 2,048-byte stack. Physical startup/visual,
   stack/headroom and scheduling checks remain open. Preserve the prior static
   implementation as the committed baseline until the operator reviews the diff.
   Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/stock-splash-proposal-20261018/report.md`.

   Progress — 2026-10-18 (unsupported update capabilities): shared firmware,
   PRF and PutBytes update guards now use explicit firmware/recovery/system-resource
   update capabilities instead of SoC identity. T5AI defaults all three off while
   retaining protocol endpoints and existing rejection behavior. Other targets
   retain enabled defaults. Stale PRF requests are consumed without a flash copy.
   Thirty-five focused checks pass, covering all eight capability combinations
   in normal/recovery mode, firmware-start rejection before side effects, PRF
   stale-bit handling, hidden-option overrides and stock-board defaults. Supported
   shared paths preprocess identically to the pre-change source in normal/recovery
   builds. All three native PutBytes suites pass (52 cases per platform) with their
   supported capabilities explicitly configured. Normal ARM firmware builds/packages
   with unchanged memory budgets.
   No firmware was flashed; device/OTA qualification remains separate. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/update-capabilities-20261018/report.md`.

   Operator disposition — 2026-10-18 (app-cache reserve/PFS expansion):
   deferred at the operator's request; the storage gain does not justify pursuing
   this item now. Keep the current 300 KiB cleanup target and flash/PFS layout.
   No reserve configuration, admission-policy or PFS expansion changes were made.
   Investigation evidence remains available at
   `/home/baz/Documents/ChatGPT/pebble/artifacts/app-cache-reserve-20261018/report.md`.

   Progress — 2026-10-18 (voice/Speex allocator contract): implemented in
   `b694ad747`. Explicit codec allocator calls replace CMake renaming of kernel
   allocation/free functions. The default backend delegates to the kernel heap;
   T5AI retains its local mutex-protected 16 KiB PSRAM arena. Codec state and voice
   frame buffers use the same selected backend. Forty-one focused checks pass,
   including real voice/Speex initialization, encoding and teardown (20 cycles per
   backend), bit-buffer growth, failed-realloc preservation and T5AI fatal
   allocation/foreign-free behavior. The existing timing harness still checks
   mutex scope. Normal firmware builds/packages with unchanged flash/static RAM
   and PSRAM budgets. Shared voice call sites and the Speex adapter use the new
   interface; stock kernel/heap implementation and vendored codec sources are
   unchanged. No device flash was performed; microphone/phone dictation and peak
   memory qualification remain open. Extra bit-buffer regression coverage and
   this checklist update remain uncommitted. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/speex-memory-20261018/report.md`.

   Progress — 2026-10-18 (PutBytes buffer contract): implemented. The service
   and T5AI pool share the job count and backend capacity; a compile-time check
   rejects requests larger than that capacity. The existing 2,053-byte request,
   three 2,080-byte PSRAM slots and 32-byte alignment remain unchanged. Thirty-four
   focused checks and the three native PutBytes suites (52 cases per platform)
   pass. ARM compilation accepts the current capacity and rejects an undersized
   backend at the intended assertion. Firmware builds/packages; the final ELF
   places the 6,240-byte pool at `0x600195a0`. Internal static RAM remains 99,968 B.
   This is a source/build check; the image was not flashed. Peak memory and device
   transfer qualification remain separate. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/putbytes-contract-20261018/report.md`.

   Progress — 2026-10-18 (retained boot/reboot consolidation, audit items 14/15):
   implemented and AP-only boot smoke-tested. The common boot-bit API and common
   four-word record/first-reason/restarted-safe/last-app operations now serve T5AI
   through a small retained-storage binding. The local boot-bit API copy is gone;
   reset snapshots, AP marker mirroring/clear and optional trace/freeze remain
   local hooks. Store format/checksum and NOLOAD address/size are unchanged:
   `0x280100a8`, 136 B. Thirty-five focused tests pass, including actual-store
   corruption/warm-state cases, fault-context first-reason preservation and eight
   stock-backend generated-code comparisons. Normal firmware builds/packages;
   static RAM remains 99,968 B. Flashed build
   `256a67bee969fb0415e52acf82b36f0757f7c25c` boots with matching identity,
   reports AP/CP boot sources and restores encrypted bonding, the phone session
   and GATT discovery (3,058 ms). Physical fault/cold-cycle/soak qualification,
   and unrelated boot warnings remain open. Operator confirms normal watchface,
   controls and sleep/wake; the 180.361-second capture received 23,729 bytes and
   is stopped, with no additional boot/watchdog/assertion decoded. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/retained-consolidation-20261018/report.md`.

   Progress — 2026-10-18 (connection negotiation, audit items 8/9): restored
   the shared upper state machine and stock NimBLE update adapter; removed both
   T5AI replacement files and build selectors. Both shared C files and both
   selection CMake files match local `origin/main`. Eight new native policy cases,
   four connection-manager cases and 29 board checks pass. Diagnostic firmware
   applied 15 ms/latency 0 and 30 ms/latency 30 with 6-second timeout via actual
   controller completions. Both requested reconnects restored sessions/discovery
   and fast updates, followed by idle-update events; repeated parameter-detail
   records are incomplete. No port-specific forced L2CAP, timeout inflation or
   nonzero-latency rejection remains. NimBLE retains its ordinary transport
   choice/fallback. Final normal-logging image
   `6974866918986203ee3d1796be87690aa9b012bd` is flashed AP-only and boot identity
   verified, encrypted phone session restored and discovery completed in 2,691 ms.
   Final normal-build actual-parameter records are absent; diagnostic completions
   establish the applied tuples. Both captures are stopped. Operator responsiveness
   confirmation, broad qualification and unrelated warnings remain open. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/stock-conn-params-20261018/report.md`.

   Progress — 2026-10-18 (stock reconnect advertising, audit item 10): implemented
   and device smoke-tested. `gap_le_slave_reconnect.c` matches local `origin/main`
   byte for byte: stock 30-second short/indefinite long terms and churn handling
   are restored. Upper/lower parameter overrides remain unchanged. Normal/debug
   builds package; 13 rebuilt advertising scheduler cases and 29 board tests pass.
   Three captured cycles request 32 then 1635 HCI units and restore encrypted
   bonding, protocol sessions and discovery (5,451/3,880/4,449 ms). Operator confirms
   all three reconnects and responsiveness. No additional boot/watchdog expiry
   is decoded in the diagnostic capture; unrelated warnings remain preserved.
   The debug build override is removed. Final normal-logging firmware
   `7c5d20f0aebae01dee706eb5640340c5d3381a30` is flashed AP-only; boot identity,
   phone session and discovery (3,914 ms) verified. Both captures are stopped.
   Churn-threshold, HRM, 25-cycle, cold-cycle and soak qualification remain open.
   Items 8/9 were subsequently restored as recorded above.
   Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/stock-advertising-20261018/report.md`.

   Progress — 2026-10-18 (Getafix compositor override, audit item 3): removed
   the full-frame dirty-region override, its Kconfig option and obsolete board-test
   expectations. The shared compositor now matches local `origin/main` and the
   pinned stock baseline exactly. All 29 Getafix/Unhygienix board checks pass.
   No hardware flash or Getafix display qualification was performed.

   Progress — 2026-10-18 (GATT discovery lease, audit item 11): removed the
   four-line T5AI 120-second default; stock zero now applies naturally. No
   shared discovery implementation changed. SDK compatibility caching remains
   enabled; display profiling is off and pixel-path removal is deferred.
   The actual-source discovery harness passes 21 scenarios under each lease
   setting (42 runs), including a new 24-service/96-characteristic/288-descriptor
   case with incremental release and final allocation accounting. QEMU Gabbro,
   Getafix DVT2 and Unhygienix configuration checks all select stock zero.
   Firmware builds/packages with the default inherited, without an explicit
   lease override. The final AP-only image's boot ID
   `a7dde5d4e97d85395949fe84d93f7d09c493f85c` is verified; initial discovery
   took 4,144 ms and two operator-confirmed Bluetooth off/on reconnects completed
   discovery in 4,992/4,908 ms, restoring encrypted bonds and protocol sessions.
   The earlier explicit-zero trial took 4,001 ms and reproduced existing
   KernelBG/FdF Time watchdog stalls/recovery. The final image logged a watchface
   app fault/relaunch during the operator's switch/upload test; its cause remains
   unresolved. Neither is clean stability qualification. Evidence and limits:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/gatt-lease-zero-20261018/report.md`.
   Large-database hardware, cold power-cycle, sustained soak and a repeat after
   restoring actual controller negotiation remain open.

   Progress — 2026-10-18 (native kernel objects, audit item 7): removed the
   allocating T5AI kernel adapter. Mailbox, heartbeat, HCI, battery and touch
   threads now own native thread objects; stacks retain their byte sizes and
   priorities. The HCI stack stays in board-cache PSRAM; the other four stacks
   are explicitly aligned internal SRAM. Nine semaphores and eight mutexes are
   embedded/static, including the microphone's device-owned mutex. This removes
   26 boot-lifetime heap allocations (9,044 requested bytes before allocator
   overhead), rather than introducing a replacement OS abstraction. Retry tests
   cover mailbox/heartbeat/battery creation, CP/MB-IPC registration and HCI worker
   reuse with quarantined descriptors retained. The broad tooling suite passes
   283 entries with two skips; the existing native kernel suite passes.
   Firmware packaging succeeds with existing LTO warnings. Static RAM rises
   8,768 B to 106,048 B; the linker heap spans 20,928 B instead of 29,696 B,
   while the removed allocations no longer consume that heap. Peak heap and
   cold-boot/reconnect/touch/battery/microphone behavior still need hardware
   qualification. Hardware update — 2026-10-18: flashed AP-only; the booted
   build ID `e570b7959ef3ff01ffe5b51e68c35460478146fc` matches the saved candidate.
   The encrypted bond and protocol session restored; discovery completed in
   4,275 ms. The operator confirms connection, selected face and working controls.
   One KernelBG feed-delay warning (5,984 ms) triggered app throttling; a watchdog
   recovery record followed. This is an unresolved finding, not clean watchdog
   qualification or evidence of causation by this change. Capture is stopped.
   Flash report:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/flash/report.md`.
   Candidate, exact build ID and validation:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/validation.txt`.
   Static-RAM follow-up: stock HCI also uses static native objects/stacks, while
   stock touch and battery reuse system-task callbacks. Native ownership does
   not justify the existing T5AI worker count. Reassess the touch/battery/
   heartbeat workers (6,144 B of internal stacks) and the extra CP HCI worker;
   preserve IPC ownership/deadlines and measure boot/peak heap when comparing.
   Detailed comparison:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/native-kernel-objects-20261018/stock-comparison.md`.

   Progress — 2026-10-18 (unrelated utility cleanup, audit item 6): restored
   the activity attachment download directory, BSS reporting script and
   orientation service's final-newline state to `origin/main` at `b3a2dcb88`.
   All three files now match that baseline byte for byte. Existing upstream
   UTC aliases, include cleanup and native accel/magnetometer APIs are retained;
   local `main` at `a2120e2ae` predates those merged changes. Python syntax checks
   pass. No firmware behavior changes, rebuild or flash; the BSS tool again
   expects `pebbleos.elf` in the current directory and uses stock shell/temp-file
   behavior. The removed rewrite remains in Git and the saved patch at
   `/home/baz/Documents/ChatGPT/pebble/artifacts/utility-cleanup-20261018/before.patch`.

   Progress — 2026-10-18 (PFS system-resource restoration): restored stock
   `resource_impl.def` ordering and `resource_storage.c` unwatch behavior; both
   files now match `origin/main` at `b3a2dcb88` exactly. The existing file backend,
   PFS and file-transfer path are reused without a migration or seeded language
   pack. Current resource IDs place optional translation/extension resources
   outside the bank; English strings and base fonts already have stock fallback
   behavior. This supersedes the audit's concern that registering absent files
   would displace working bank fonts/strings. Eight new integration cases link
   real PFS, the T5AI bank and shared language/font code; six failures were
   reproduced before restoration, and all eight cases now pass. Together with
   existing language/font suites, 42 native cases pass; 23 resource/image tooling
   tests pass. Normal firmware builds/packages with existing LTO warnings:
   flash 1,596,832 B (+608 B), static RAM 97,280 B and display RAM 118,272 B
   (unchanged). The system resource pack is byte-identical to the prior candidate.
   Build ID: `50d14c63d1336e77edb840d723bfefc230982883`; source is uncommitted.
   Flashed AP-only on 2026-10-18; the booted build ID matches. The trace records
   valid system resources, restored bond registration and an open reversed
   PPoGATT session. CP, resource bank and persistent storage were not selected
   for programming. Capture is stopped; existing PFS/app warnings remain.
   Language/font follow-ups are closed by operator direction on 2026-10-22;
   these original capture limits remain historical evidence. Flash evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-system-resources-20261018/flash/report.md`.
   Build evidence and candidate details:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-system-resources-20261018/validation.txt`.
   Full-pack integrity validation was subsequently implemented; see current status.

   Follow-up — 2026-10-18 (marker warning): the startup layout probe temporarily
   restores a zero-page PFS, triggering the stock marker warning without reading
   any page. A native preserved-flash restart finds marker page 6 before/after,
   warns only at zero pages, and passes the language/font checks. Deliberately
   retiring that marker in fake NOR produces additional nonempty-scan warnings.
   Exact candidate disassembly confirms the zero-page path; the watch warning
   alone is not evidence of marker loss. No production fix, hardware read/reset
   or storage repair was performed. A generic zero-page scan guard is the narrow
   potential correction; keep nonempty-filesystem warnings. Evidence and limits:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/pfs-marker-investigation-20261018/report.md`.

   Progress — 2026-10-18 (legacy accelerometer retirement): removed the periodic
   QMI8658 driver, its Kconfig selector, conditional driver-state layout and
   legacy-only tests. The interrupt/FIFO driver is now selected directly;
   acquisition logic and normal sensor settings are unchanged. Shared test stubs
   and disabled-mode coverage are retained in the interrupt/FIFO suite. Git
   history and saved images retain historical rollback material. All 63 focused
   driver/I2C/memory/board/kernel checks pass. Normal firmware builds/packages
   successfully: flash 1,596,224 B, static RAM 97,280 B and display RAM 118,272 B,
   unchanged from the prior shell-init candidate. Existing LTO warnings remain.
   Candidate build ID: `6dc9f1c7e63c3eeb3fa5d926f35782f8f1126442`; source is
   uncommitted and no image has been flashed. Evidence and matching images:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/retire-legacy-accel-20261018/`.
   On-device animation, shake/wrist-to-wake and touch smoke checks remain open.

   Progress — 2026-10-17 (first cleanup batch, based on `75cdedff7`):
   restored `tools/boards.py` exactly to local `main`, including QEMU metadata
   loading, and removed unused revision-specific runner/defconfig overrides.
   Added board-local Kconfig exclusions for conflicting mutex and timer
   wrappers, including App tracing's implicit BG tracing selection. Reminder
   profiling and watchdog recovery tracing are also mutually exclusive in
   Kconfig; their existing CMake rejection remains as a defensive check.
   Nine focused board/parser/configuration tests pass. The broad tool suite
   ran 269 entries: 10 failures, 12 errors and one skip, with leak detection
   disabled for the sandbox. Remaining failures include retired FreeRTOS and
   old CMake/source-path expectations, obsolete reference staging, and an
   unresolved `pebble.loghashing` import. Full output is preserved at
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/tool-tests.log`.
   Regression-suite repair, reference-build disposition and the other audit
   findings remain open. No firmware build, QEMU boot, flash or physical
   qualification was performed for this batch; merge readiness is not established.

   Progress — 2026-10-17 (regression and reference-staging batch):
   the broad tool suite is now green: 274 entries, one ARM SDK/QEMU fault-entry
   test skipped because its tools were unavailable. This supersedes the test
   failures recorded above. Retired FreeRTOS/startup assumptions and oversized
   source-snippet checks were replaced with current native integration checks;
   heartbeat and mailbox behavioral harnesses were retained and updated.
   Added executable fault-priority, PSRAM corruption/bounds and panel reset/
   command-order/write-failure coverage. The trace import resolves with the
   repository package roots on `PYTHONPATH`, without production changes.
   Archived the unused reference mutex adapter under `tuya/history/`, removed
   its self-test/build glue, and repaired repeatable reference staging using
   only the required button/touch event type headers. The reference runtime
   harness compiles and passes against the staged sources and headers alone.
   Full logs and the exact validation command are in
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/validation-after-repair.txt`.
   Leak detection remains disabled for the sandbox. Full firmware/vendor SDK
   builds, stock QEMU boot and hardware qualification have not been performed.
   GATT consolidation, persistence reassessment, the remaining audit verdicts
   and merge readiness remain open.

   Progress — 2026-10-17 (unused code and duplicate assets, based on
   `a53522389` after upstream merge): removed the unused alternate syscall ABI
   header and restored the stock resource-bank condition. The shared
   `fw/resource/resource_storage_flash.c` now matches `origin/main` at `b3a2dcb88`
   exactly. The Unhygienix map references the eight existing Getafix multiwatch
   PNGs directly; byte-identical local copies were removed. The real resource
   generator produced identical affected PNG payloads and an identical declared
   resource ID header before/after; all 607 resource definitions and 31
   declarations retain their metadata/order apart from the eight source paths.
   Fifty-five focused resource/pack/font/board tests pass. Evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/leftovers/validation.txt`.
   This is not a full resource-pack/firmware rebuild or hardware qualification.
   GATT discovery consolidation is the next substantive cleanup; persistence
   reassessment and the remaining audit dispositions stay open.

   Progress — 2026-10-17 (GATT discovery consolidation, based on `441534a95`):
   removed the alternate T5AI engine and its CMake selection. The shared
   NimBLE engine now emits/frees completed services incrementally, retains
   Service Changed handles before freeing nodes, and frees subscription
   contexts on immediate write failure. It preserves the newer stock
   connection-validity, queue-completion and immediate-procedure-error paths;
   failed-start completion now uses the same guarded notification helper.
   The descriptor walk handles an empty first service without skipping later
   nonempty services. A narrow generic response-time lease override defaults
   to zero on stock boards and 120 seconds on T5AI, retaining its prior policy.
   The common 10-second success tail is unchanged.
   The new full-source ASan/UBSan harness passes 20 scripted scenarios under
   both lease settings; four old-stock failure cases were reproduced first.
   All four rebuilt native GATT suites pass; the broad tool suite passes 280
   entries with one skipped ARM SDK/QEMU fault-entry test. Host allocation
   accounting confirms early node release and no outstanding allocations at
   terminal paths. The normal Unhygienix firmware ELF also built and linked
   successfully; its log retains LTO warnings outside discovery. No firmware
   was flashed, and physical qualification remains outstanding. Validation
   artifacts are under
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/gatt/`.
   Persistence reassessment and the remaining audit dispositions stay open.

   Hardware update — 2026-10-17: the consolidated GATT candidate was flashed
   AP-only after regenerating and validating the binary/protected image and
   matching dictionary. The booted build ID matches the saved ELF. In a bounded
   141-second post-flash capture, the phone restored its encrypted bond,
   opened a protocol session and completed GATT discovery in 6499 ms. The
   operator confirmed reconnect and responsiveness. One commanded flash boot
   was seen; no later reset/watchdog expiry was observed. Battery ADC, transient
   I2C/touch, resource-check and app-entry errors were retained and remain open.
   This is a smoke test, not 25-cycle/cold-boot/soak or install qualification.
   Capture is stopped; raw bytes, dictionary, exact images and report are at
   `/home/baz/coredevices/pebbleos/build/traces/gatt-consolidation-20261017-145916/analysis.md`.

   Progress — 2026-10-17 (Bluetooth persistence cleanup, based on `ab3616d5e`):
   restored stock normal/recovery backend selection; the Bluetooth service CMake
   file matches `origin/main` exactly. Normal T5AI firmware uses the existing
   normal backend and its PRF import/synchronization path. Removed the temporary
   evaluation switch and T5AI-specific airplane-mode stubs from PRF. The normal
   backend, shared PRF implementation, flash ownership and geometry are unchanged.
   PFS/settings writes already fall within the T5AI flash allowlist.
   Three native suites pass (17 cases), including five migration cases linking
   real shared PRF, settings and PFS over fake flash. They cover key import and
   repeated initialization, CCCD/gateway/settings/capability restoration, unchanged
   PRF bytes, stale-bond replacement and PRF-deletion re-export. The final 56
   focused tooling checks pass. Default firmware builds/packages successfully;
   existing LTO warnings remain in the saved log.
   Forgetting a phone in PRF can leave an older normal bond that normal boot
   exports back into PRF. The operator explicitly accepts re-pairing as recovery
   (2026-10-17), so this behavior does not block cleanup. No candidate was flashed;
   physical CCCD/gateway/capability behavior, CP flash latency, heap low water,
   cold-boot reconnect, installation and soak remain unqualified. The installed
   watch still runs the earlier GATT candidate with PRF storage.
   Default candidate: `build/traces/persistence-default-20261017/`; evidence:
   `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/persistence/validation.txt`.
   Source selection is resolved; physical qualification, development-only product
   policy and the remaining audit findings remain open.

   Hardware update — 2026-10-17: default normal-persistence candidate flashed
   AP-only, matching build ID `28416c29102c08546e1b83d6c23826ea3c476133`.
   Trace shows restored encrypted bonding, reversed PPoGATT session and discovery
   in 6890 ms; operator confirms connected/responsive before and after a requested
   USB cold power cycle, with the same face selected. Two expected matching boot
   banners were captured. A host port-busy error caused a 56.218-second gap after
   the cold-boot banner; that reconnection is operator-confirmed, not independently
   traced. No fatal/watchdog or gap-bonding update error was decoded in captured
   portions. PFS startup/resource/app/animation/weather warnings remain. No
   battery ADC or I2C error was decoded in this run; earlier issues stay open.
   Capture stopped; installed image now uses normal persistence. Direct CCCD/
   gateway/capability/airplane-mode qualification, latency/heap measurements,
   repeated reconnects, controlled installs and soak remain open. Evidence:
   `build/traces/persistence-test-20261017-152214/analysis.md`.

   Progress — 2026-10-17 (stock battery UI restoration, based on `c06ee2a46`):
   at the operator's request, removed the T5AI battery-event UI bypass entirely.
   Charging/full-charge and low/critical battery events now use the stock handler;
   no development flag or replacement UI was added. Battery driver/calibration
   and charge controls are unchanged. Both existing native battery UI suites
   pass (14 cases), and the firmware builds/packages with existing LTO warnings.
   Candidate saved at `build/traces/stock-battery-ui-candidate-20261017/`;
   not flashed. The installed persistence-test image still has the bypass.
   First-use/onboarding and settings-migration exceptions are the next remaining
   product-policy review. Battery behavior with a fitted cell stays unqualified.
   Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/stock-battery-ui/validation.txt`.

   Flash update — 2026-10-17: the stock battery-UI candidate is now installed,
   AP-only, build ID `36ce226a40bbc90127f02ba1505313be86977367` confirmed at boot.
   Capture stopped. Normal Bluetooth persistence is retained. No operator visual
   charging-modal check was collected in this flash-only run; fitted-cell battery
   validation and broader hardware qualification stay open. Exact flash/boot
   evidence: `build/traces/stock-battery-ui-flash-20261017-153945/analysis.md`.

   Progress — 2026-10-17 (stock shell initialization, based on `30ff66be8`):
   removed all remaining T5AI guards from `normal/shell_event_loop.c`; the file
   now matches `origin/main` exactly. Existing first-use handling records setup
   complete and requests phone sync once when needed; it does not launch an
   onboarding app or require recovery graphics. The existing one-time KernelBG
   callback compacts eligible DBs and records its preference marker. No migration
   fork, special board flag, flash-layout or permission change was added.
   Four new T5AI-enabled native init cases pass alongside the settings-file,
   shared-PRF, prefs and app-state suites. Firmware builds/packages with existing
   LTO warnings; flash +128 B, static RAM unchanged. Candidate not flashed:
   `build/traces/stock-shell-init-candidate-20261017/`.
   On first hardware boot, trace all per-DB compaction results and completion,
   first-use sync, app/face retention and repeat boot. Stock marks the attempt
   done even if a DB compaction returns an error, so check results explicitly.
   Source exceptions are removed; physical migration qualification remains open.
   Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/upstream-cleanup-20261017/shell-init/validation.txt`.

   Hardware update — 2026-10-17: stock shell-init candidate flashed AP-only,
   build ID `6e39f48175667f6dce5d6e419b8581a805ebed62` confirmed on both boots.
   Compaction completion and seven DB results (all zero) were captured; start,
   reminder and health results are absent. Operator confirms connected/responsive
   with the selected face working. A commanded non-writing reset reconnects;
   no repeat compaction was observed. Startup logged a delayed force-quit/back
   event after compaction; PFS/resource/battery/app warnings remain. No fatal or
   watchdog event was decoded, but missing log records limit absence claims.
   Fresh first-use state was not forced, and the two missing DB results remain
   unverified. Capture stopped; installed image now has the complete stock shell
   path. Evidence: `build/traces/stock-shell-init-flash-20261017-155019/analysis.md`.

2. **Display implementation pass complete — 2026-10-19.** Wide copy/fill,
   native compositor, two-row DMA, timestamped touch coalescing, rotated DMA and
   bounded recovery are implemented. IRQ completion was implemented and measured
   slower; polling remains selected and the worker stays off. Do not repeat the
   original IRQ investigation. Longer stability qualification, rendering costs,
   unqualified scalar fallback and deferred DMA2D remain documented in the
   [display handoff](/home/baz/Documents/ChatGPT/pebble/artifacts/t5-display-implementation-20261019/HANDOFF.md).
3. **Investigate further accelerometer CPU-cost reduction — exploratory (2026-10-14).**
   Measure how much CPU the accelerometer pipeline costs while the watch is idle
   and while an accelerometer-using watchface is active, then determine whether
   a worthwhile further reduction is practical. A reduction is not guaranteed;
   reporting the measured cost and an unsuccessful optimisation is a valid result.
   Establish matched baselines with the same firmware, clock, tracing and sensor
   settings; distinguish idle with shake/wrist-to-wake enabled, a live subscriber,
   and true sensor standby. Record actual sample rate and shake/batching state.
   Use worker CPU ms/s and ms/sample as the primary metrics, and account for
   I2C ISR and shared service/shake processing separately without double-counting.
   Preserve input latency, sample delivery, shake/wake and touch behavior. Keep
   changes driver-/SoC-local and preserve stock shared code and task priorities.
   Historical active FdF captures measured 9.21–9.74% worker CPU with legacy
   polling and 2.17–2.42% with interrupt-driven direct acquisition. The later
   controller-FIFO comparison measured 2.16–2.29% in byte controls, 2.42% with
   twelve-byte bursts and 2.57% with four-byte bursts. These are instrumented
   active measurements, not idle baselines or exclusive whole-pipeline CPU.
   A qualified matched idle baseline was not found in the reviewed historical
   tracing; obtain it before claiming an idle/active saving.

   The previous acquisition implementation iteration remains complete for now,
   closed at the operator’s request on 2026-10-14. Implemented driver-local interrupt-driven
   acquisition, bounded sensor FIFO batching and combined register reads.
   The periodic comparison implementation and its legacy compile-time flag were
   retired on 2026-10-18; historical measurements remain below.
   The I2C controller receive FIFO defaults on with a twelve-byte maximum;
   the four-byte cap and acquisition profiling default off. Shared Pebble code,
   task priorities and the 100 kHz bus are preserved.
   Focused tests and matched FdF Time watch captures are recorded in the
   [acquisition results](../../../docs/boards/unhygienix/qmi8658_acquisition.md).
   Accelerometer-thread CPU is the primary metric: controller FIFO reduced IRQ
   entries by about 69%, but measured worker CPU was 2.42% versus 2.29%/2.16%
   in byte-mode controls, so no CPU reduction is claimed for that experiment.
   Animation, shake/wrist-to-wake and touch were operator-confirmed on the last
   restored normal image. The October 17 GATT image includes the FIFO default
   change, but its bounded smoke test did not repeat matched sensor measurements.
   Sensor FIFO throughput/timestamp limitations, DMA support not established,
   unqualified low-power transitions and the earlier unresolved freeze/reboot
   remain documented limitations; completion of the previous iteration does not qualify them.

Previous notes: [QMI8658 polling isolation and accelerometer-free comparison](../../../docs/boards/unhygienix/tooling.md),
[shake/display wake behaviour](../../../docs/boards/unhygienix/index.md), and
[input alignment, App render tracing and accelerometer-disabled/restored runs](../../../docs/boards/unhygienix/refactor_handoff.md).
The 2026-10-10 FdF comparison reduced median render/upkeep wall time from 448 ms
with the sensor enabled to 362 ms disabled, but KernelBG still starved; sensor
load is measurable and is not required for that starvation. The sensor was
subsequently restored. Preserve stock shared code and standard task priorities;
keep these optimisations board-, SoC- or driver-local wherever possible.

## Existing validation priorities

1. Verify watchface install/launch and selection persistence.
2. Run 25 disconnect/reconnect cycles, retaining the first complete failure trace.
3. Cold boot and verify bond restoration, protocol reconnection and selected face.
4. Run overnight connected-idle with periodic UI interaction.
5. Resolve App privileged-code stalls and resource/app-entry warnings.
6. Complete hardware wake/alarm routing and low-power operation.
7. Validate battery/charging with a fitted cell, RTC drift, privilege isolation,
   storage interruption and corruption recovery.
8. Define rollback and recovery before enabling firmware updates or OTA.

Keep captures and matching firmware/log dictionaries under `build/traces/`.
Host builds do not establish physical qualification. Confirm actual received
bytes before reporting a capture or soak as active.

## Deferred KernelBG stall investigation — 2026-10-09

Deferred at the operator's request. MU-TH-UR T loading still triggers a
KernelBG watchdog stall after the synchronous-erase delay fix. In the latest
flashed candidate, the task was unfed for 5435/6442 ms, then recovered after app
throttling without a reboot during the bounded observation. App-driven CPU
starvation is a hypothesis, not an established root cause; communications or
lock waits remain possible.

When resumed, correlate KernelBG/App CPU-time deltas and ready/blocked state
with the active communications endpoint, callback elapsed time, settings
filename/progress and send-buffer wait duration. Use the optional
`CONFIG_T5AI_STATE_TRACE` recorder if useful; it remains disabled by default.
Do not add stack/register tracing or change shared scheduling policy merely
from sampled PCs. Preserve exact firmware, dictionary and first-failure logs.
Evidence: `build/traces/erase-delay-fix-20261009/flash-results.json` and
`build/traces/erase-delay-fix-flash-20261009.log`.

## TODO: investigate and remove the SDK compatibility cache — 2026-10-10

Treat `CONFIG_T5AI_SDK_COMPAT_CACHE` / `soc/t5ai/sdk_compat.c` as a temporary
workaround and code smell, not the final draw-performance solution. The operator
notes that other SoCs do not need this cache. Although it reduced observed FdF
render/upkeep spans from a 448 ms median to 185 ms, it adds eight bytes of cached
App/Worker flags in read-only-to-app RAM and does not resolve KernelBG starvation.

- Establish why T5 pays such a large cost for the stock SDK compatibility queries
  and drawing path compared with other SoCs. Audit syscall/context-switch costs,
  memory/cache configuration, compiler settings and render handoff; do not assume
  a SoC-specific cache is inherently necessary.
- Fix the underlying cause and remeasure with the SDK cache disabled. Preserve
  stock SDK semantics, privilege isolation and process lifecycle behavior.
- Remove the cache and its linker/build glue once the underlying fix preserves
  the performance gain. Avoid replacing it with further RAM-backed workarounds,
  draw sleeps or unexplained priority changes.

Measured evidence and limitations:
`build/traces/sdk-compat-draw-20261010/analysis.txt`.
The currently installed lighter firmware retains the cache; this TODO does not
change firmware or disable it.

## Resolved for now: KernelBG watchdog starvation recovery — 2026-10-11

The lightweight selector trace reproduced FdF KernelBG READY starvation while
PendSV continued choosing higher-priority work. The existing starvation recovery
is bypassed by a generic watchdog regression: commit `fb1f13e12` marks KernelBG
waiting before its poll wait, and `864cd70aa` exempts that flag unconditionally.
After wakeup, a starved READY thread cannot run to clear the flag, so its expired
channel never invokes the existing recovery callback. An actual-source host
reproduction confirms that READY and RUNNING channels remain exempt.

The generic watchdog now exempts genuinely blocked idle waits and starts a fresh
timeout at the first observed runnable state, without requiring the starved
thread to clear its waiting flag. No T5 sleep or priority workaround was added.
The upstream grace-period fix `a8772bc52` is also imported. Four additional native
scheduler tests cover READY starvation, recovery, long-idle wakeup and a busy
thread retaining its waiting flag; the 17-test watchdog suite passes.

The corrected image is flashed. The bounded hardware run recorded four KernelBG
expiries, each followed by recovery about one second later, with KernelBG
progress and no reset/OOM. The former idle-wait exemption is no longer preventing
recovery. This restores recovery, not the underlying expensive draws.

Peak observed READY age still reached 64.699 seconds during a communication
callback with a 65189 ms pindb mutex hold. Settings compaction can globally suspend
the watchdog for 60 seconds; this is a candidate explanation for the long gap,
not established for that episode. The 2026-10-11 SoC-only caller trace captured
two settings-compaction 60-second suspension requests, each explicitly resumed
after about 0.62 seconds, before FdF. No suspension overlapped the captured FdF
intervals; seven recovery reports and a 5.564-second peak READY age were recorded.
The operator accepts this as fixed enough for now: no recurrence of the prolonged
stall occurred during the latest trace. Further suspension investigation is
deferred unless the symptom returns; the earlier episode remains unattributed.
The operator attributed the intervening face exit to syncing another watchface.
Evidence: `build/traces/wdt-cause-20261011/analysis.md`. Continue draw/syscall
optimization independently. The bounded run is not long-term
stability qualification. Both lightweight diagnostic flags are now disabled in
the built and flashed firmware (fresh boot reached communication readiness); their opt-in, default-off implementations remain available.
The SDK compatibility cache TODO and draw optimization work remain separate.
Evidence:
`build/traces/wdt-recovery-effectiveness-20261010/analysis.txt`;
baseline: `build/traces/bg-pick-light-20261010/`.

## Candidate fix: end BG recovery on progress, not a fixed timer — 2026-10-11

The operator reported a reboot during the diagnostic-free validation. A
raw/decoded UART capture started before a requested hardware reset,
using the same flashed image with diagnostic flags off. Treat the reset boot as
commanded. Reproduction showed that the earlier accepted recovery was not reliable. Evidence: `build/traces/reboot-repro-trace-off-20261011/`.

## Remaining stock-code refactor work

See the [2026-10-09 detailed handoff](../../../docs/boards/unhygienix/refactor_handoff.md)
for scope, file ownership, known failed approaches, acceptance checks and the
last-flashed candidate.

The historical [shared-code audit](../../../docs/boards/unhygienix/history/shared_code_cleanup.md)
contains superseded startup/kernel work. The remaining candidates are:

- Reassess resource-policy findings against the current source: as of
  `441534a95`, shared `resource_storage_flash.c` matches the compared upstream
  `b3a2dcb88`. The earlier extraction proposal is superseded; preserve the stock
  implementation and review only deviations still present.
- Stock file-store routing and resource-watch ownership were restored on
  2026-10-18 and verified with real PFS integration cases. Complete the device
  checks listed above; language-pack work is closed and full-pack validation
  is implemented. Shared bank-backend consolidation remains open.
- Select unavailable update, recovery, coredump and reset implementations or
  explicit capabilities at build time, reducing shared T5 conditionals without
  enabling unsupported operations.
- Finish compositor framebuffer-placement ownership. PSRAM validation and power
  setup have moved to the board; the shared framebuffer section annotation
  remains. Earlier provider extraction faulted and needs a sound new design.
- Audit remaining shared identity/profiler exceptions and CMake selection glue;
  refresh stale board tests after the startup/service extractions. Separate
  independently justified generic fixes from platform policy.

Stock `main.c`, native kernel wrappers/startup, SoC-local delay selection and
most common service restoration are already completed; do not repeat those
extractions. Hardware qualification and remaining peripheral implementations
are separate work from this refactor list.

2026-10-11 reproduction update: three spontaneous reboots were captured after
one commanded reset with diagnostics disabled. Each follows unrecovered KernelBG
watchdog expiry and reports AP=112 (0x60 + Watchdog=16), CP=16. Recovery is not
reliable under this reproduction; the earlier accepted disposition is superseded
by this evidence. Root cause of failed recovery is still open. Frozen evidence:
`build/traces/reboot-repro-trace-off-20261011/three-reboots/analysis.txt`.

2026-10-11 candidate update: expiry-only priority/CPU probes reproduced reset
with App priority successfully lowered 2/2 → 0/0 and BG READY at 1/1. BG received
240 then 237 ms CPU between checks, but fixed 300 ms App unthrottling interrupted
recovery before the communication callback fed. Recovery now keeps the affected
App throttled until a successful BG feed; the watchdog's two-second grace remains
unchanged. Timer expiry checks and priority restoration are atomic, and an old
recovery cannot change a replacement App. A separate T5 flash priority scope race
now preserves concurrent throttle/priority changes instead of restoring a stale
base priority. Both regression cases fail before their fixes and pass afterwards.
Twelve focused host tests plus native kernel/watchdog suites pass. Initial combined
candidate bounded capture contains five recovery reports and seven expiry reports,
with no subsequent boot in that interval;
this is not long-term qualification or an isolated comparison of the two fixes.
Evidence: `build/traces/wdt-recovery-cpu-20261011/` and
`build/traces/wdt-progress-recovery-20261011/`. Draw cost remains open.


2026-10-11 final-candidate validation **failed**: build
`a6a8697cb47428676579b5930585b9fd24401a5d` rebooted with AP=112/CP=16.
App remained at priority 0 throughout the final grace checks. BG CPU advanced
356 → 1027 → 1688 ticks while executing the default communication receiver;
the sampled PC/LR includes PFS/FTL flash reads. This proves the remaining failure
can occur despite sustained BG scheduling. Do not mark the reboot issue fixed.
The progress-driven recovery and flash priority corrections are candidates,
not sufficient fixes. An expanded expiry-only SoC probe records existing flash
operation counts/timings and a settings full-sync marker to distinguish slow
transactions from long filesystem work. Evidence:
`build/traces/wdt-progress-recovery-final-20261011/analysis.md`.


2026-10-11 targeted probe captured two further watchdog resets after the
commanded reset. Both have settings mark-all-dirty active for over 7.5 seconds,
App at 0/0, advancing BG CPU and over 100 completed small writes in the final
roughly two seconds. Completed read maxima are 8/9 ms; writes 22/25 ms, with no
erases completed in those intervals. Investigate synchronous settings rewrite
and T5 per-write overhead/durability-safe batching, rather than assuming a
scheduler failure or one stuck flash request. CP reference code toggles flash
protection around each write; its contribution needs measurement. Frozen report:
`build/traces/wdt-flash-recovery-20261011/two-reproductions/analysis.md`.


Historical October 11 capture finished at operator request (175,988 raw bytes flushed). See the
[settings full-sync watchdog investigation](../../../docs/boards/unhygienix/settings_sync_watchdog.md) for the two
reproductions, verified CP binary findings, limitations and staged debugging
plan. That capture has stopped. The October 17 GATT image supersedes the
installed diagnostic firmware; the investigation findings remain open.


## Flash optimisation and rewrite robustness — 2026-10-11

See the [audited avenues and implementation sequence](../../../docs/boards/unhygienix/flash_optimisation.md).
The reset remains open; documentation of a candidate is not qualification.

- Split request-send timing into socket lock, physical handshake and logical
  response; measure CP protection, notifications, programming and service wait.
- Add record/file progress only after verified success, using the existing
  current-thread watchdog feed without prematurely releasing App throttling.
- Correct dirty-all allocation/write/open error propagation and preserve old
  settings on abort; resolve READ_DONE cleanup on failed read validation.
- Evaluate sequential dirty-all copy using existing temporary-file semantics
  to remove setter searches and combine key/value I/O.
- Keep global deadlines, CP ownership and write durability intact. Do not add
  caches or blindly retry mutations. SDK compatibility cache removal stays
  tracked separately above.
