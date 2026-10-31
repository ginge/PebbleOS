# Unhygienix current status — 2026-10-31

This page supersedes installed-image claims and task dispositions in older dated
handoffs. Dates refer to Europe/London. Historical captures and failed candidates
remain evidence; completion by operator decision does not imply new measurements.

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
