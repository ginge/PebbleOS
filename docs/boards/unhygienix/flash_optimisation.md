# T5 flash optimisation and robustness audit — 2026-10-11

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](current-status.md). Dated results below retain their original evidence limits.

Status: the 2026-10-11 SPI correctness review below supersedes the earlier
bypass safety assumptions. The controller-based diagnostic CP baseline has been
restored and verified against its sealed hash. Experimental SPI bypass packaging
is blocked until a verified RAM-resident AP ownership handoff exists. The
capture is closed. Protection-status work dominates the measured writes.
Performance optimisation, fault-injection qualification and the observed
post-sync BG starvation remain open. See the hardware measurements below.
The original audit used a corrected 277311-byte phase capture; those historical
findings are retained separately from this new 59705-byte run.

## Findings and priorities

There are two independent targets: reduce each small flash transaction, and
reduce the number of transactions needed to rewrite settings. Neither alone
resolves App starving KernelBG between transactions. Robustness must recognise
bounded operation progress without treating every successful low-level I/O as
proof that a whole event handler is healthy.

The corrected trace measured 23,232 send ticks out of 23,689 whole-WRITE ticks
across 1,405 transactions on its second boot; whole-write peak was 21 ticks.
The first boot peaked at 25 ticks. These are measured distributions/maxima, not
an FPS result or a throughput estimate for every workload. The first boot reset
while full sync was active; the second boot completed shell/notification rewrites
in 1,916/1,161 ticks. An earlier shell rewrite took 7,695 ticks, including a
5,966-tick gap before its first record write. See the
[watchdog evidence](settings_sync_watchdog.md) for attribution limits.

Recommended order:

1. Split the dominant request-send interval and the initial rewrite preparation.
   This determines whether the driver fix belongs in AP transport or CP flash.
2. Add bounded, meaningful rewrite progress and correct failure propagation.
   Keep App recovery tied to BG event-loop progress.
3. Optimise the measured CP overhead, with protection changes the leading
   code-based candidate, not yet a measured conclusion.
4. Replace ordinary setter calls during dirty-all copying with a sequential
   temporary-file rewrite. This reduces real I/O, independently of CP latency.
5. Revisit read caching, erase behavior and larger transfers only against their
   measured workloads. Keep SDK compatibility cache removal on its separate TODO.

## Driver and transport avenues

| Avenue | Evidence and expected scope | Decision / next measurement |
| --- | --- | --- |
| AP request handshake | 98–99% of cumulative WRITE transaction ticks are inside `t5ai_mb_ipc_send`. | First priority: separate socket-lock wait, physical-send wait and logical-response wait. |
| Physical mailbox queue/fairness | AP permits one active physical command across logical channels, matching vendor buffer ownership; ACKs bypass this gate. RX drains eight frames per pass and busy retry sleeps one tick. | Measure enqueue-to-publish, publish-to-ACK, pending ACK age, RX backlog and busy-retry counts. Correlate HCI activity. Preserve the gate until CP ownership is changed too. |
| CP service scheduling | Flash server uses the vendor worker priority. CP returns the request logical response through an asynchronous router before executing the flash handler. | Time request arrival, server dispatch and actual logical-response publication; AP send time can overlap CP work. Raising AP priority cannot speed a starved CP server. |
| CP protection changes | Every API write saves protection, requests NONE, programs, then restores protection. Actual WRSR occurs only if bits differ. Pinned CP disables volatile status writes. | Count actual old/new bits and time unprotect/program/reprotect. If dominant, evaluate protection covering writable partitions or a bounded CP-owned write batch; preserve protected firmware/resources and restore on every exit. |
| Volatile status writes | Vendor has a conditional hook, but the BK7258 `flash_ll.h` inspected here has no implementation of `flash_ll_set_volatile_status_write`. | This is not a safe one-line configuration change. Identify the actual chip and command support, implement/verify the complete controller sequence, and check reset behavior first. |
| CP flash notifications | Each write suspends the CP scheduler and sends START/END notifications. Each notification busy-waits for an ACK, with a 5,000-us timeout and 2,000-iteration limit. AP handles notification ACKs in the mailbox worker. | Time each notification and record result; `flash_lock/unlock` currently ignore those results. Two possible 5-ms waits are a hypothesis, not measured fixed overhead. Do not delete cross-core coordination. |
| CP interrupt-off intervals | Status writes and each 32-byte program command busy-wait in a critical section. CP scheduler suspension spans the operation. | Record longest IRQ-off span and AON wall time. Examine whether queued mailbox responses are delayed until programming ends and whether AP XIP is stalled. Do not enable IRQs through flash mutation without an execution/cache safety design. |
| Program granularity/alignment | CP programs 32-byte controller chunks, padding unused bytes with FF. AP page writes stop at 256-byte boundaries. | Compare 1/8/18/25/32/256-byte aligned and straddling writes. Determine fixed overhead versus per-chunk cost before changing geometry. |
| Larger IPC payloads | AP write helper accepts up to UINT16_MAX, but ordinary page writes are at most 256 bytes. CP uses a fixed 512-byte buffer and chunks larger writes. | Larger requests help bulk operations, not existing tiny records automatically. CP still invokes the protected write API for each buffer chunk. Preserve range checks and source-buffer lifetime. |
| Asynchronous begin/status | T5 `write_page_begin` and erase begin complete synchronously through CP; status then reports completion. IPC semaphore waits already yield the AP CPU. | A true asynchronous backend can reduce caller lock occupancy, but will not remove physical flash latency. Requires owned command/buffer storage, timeout quarantine and completion ordering. Defer until contention is measured. |
| Shared flash/PFS locks | Measured T5-client write mutex wait totals only seven ticks. Shared `s_flash_lock`, PFS and preferences locks are outside that measurement. Shared flash lock remains held across all pages of one write. | Wrap shared read/write entry and PFS/open boundaries diagnostically; measure wait/hold separately. Do not conclude there is no lock contention from the T5 mutex alone. |
| CRC and cache maintenance | AP CRC32 is table-based; MIPC CRC8 covers a 16-byte command. CP copies and CRC-checks the payload. Shared buffer cache clean/invalidate is required. | Time separately if the dominant interval split leaves a gap. Low priority for 10–25-ms tiny writes; do not remove coherency or validation. |
| Read round trips | CP READ requires READ_DONE before its buffer can be reused. General cache always allocates four 512-byte lines; immutable resources already use mapped reads. | Count requested bytes, filled bytes, hits, misses, invalidations and READ/READ_DONE time. Tiny sparse reads currently fetch 512 bytes; compare uncached short reads versus existing lines before choosing policy. |
| Cache invalidation | Every mutation invalidates overlapping general read lines, including failed mutations. Optional PFS header cache updates use NOR bitwise-AND semantics. | Investigate repeated read-after-write refills. Driver-local successful write-through could avoid a refill but must preserve NOR semantics and retire data on ambiguous failure. No additional RAM cache proposed. |
| PFS header cache | Optional 256 x 32-byte header cache is disabled in board defaults; existing general cache remains enabled. | Do not re-enable 8 KiB as the default remedy. Measure header miss/scan costs and fix transaction overhead first. |
| Direct mutable reads | Only the protected immutable resource bank bypasses CP today. Mutable PFS offsets are physical, while protected XIP has 32:34 geometry. | No naive pointer cast for PFS. A direct path needs a verified raw aperture, mutation/cache coordination and exact mapping. AP direct mutation requires ownership of the shared controller, not merely an address conversion. |
| Flash clocks / quad mode | CP configuration enables quad support, disables the listed 60/120-MHz options, and writes a default controller clock setting. Writes explicitly select two-line mode and restore the prior mode. | Read live controller/source/divider and JEDEC ID; timing is not inferable from a Kconfig name. Clock or line-mode changes need chip/board qualification. Higher bus speed may not shorten internal status/program busy time. |
| Erase / blank checks | T5 sector erase loops over 4-KiB CP erases; blank checking reads every byte through the cache. Shared completion needs the existing nonzero poll delay. | Measure pre-erase scans, actual erase, metadata writes and completion scheduling separately. CP FAST_ERASE exists but its geometry/semantics must be verified before use. The final failure intervals had no erases; that does not exclude earlier preparation/GC. |
| Erase suspend / bulk lock time | T5 erase is synchronous, so its suspend/resume methods cannot interrupt an in-flight CP erase. | A true suspend/resume implementation requires CP support and a safely multiplexed controller. Do not claim the no-op methods provide read latency bounds. Retain the proven completion-delay fix. |
| Retry and quarantine | Reads retry twice; ambiguous transaction failure poisons the socket. Writes are deliberately never blindly retried. | Preserve mutation quarantine. Avoid repeating a read after the channel is known poisoned; fix valid read-lease cleanup before expanding retry behavior. |
| Priority scope | Current temporary priority boost preserves an external App throttle, and blocking IPC yields normally. | Retain its race regression. Audit measured base/effective priority and CP scheduling; permanent priority increases do not explain or eliminate hardware waits. |

The programmer reports flash ID `0x001765c8`; the CP build configuration names
`gd25q127c` for its Tuya QSPI setting. Do not substitute that configured name for
an identification of the actual system NOR. No part-specific speed or volatile
status capability is assumed by this audit.

## Reducing settings write amplification

`prv_mark_all_dirty_rewrite_cb()` reads key/value separately, allocates/frees a
value buffer per record, then calls `settings_file_set_with_timestamp()` for an
empty temporary file. The setter searches for existing keys and the end of the
file, writes a new header, key, value, then committed header. Repeated searches
can grow with record count even though every key is new in the replacement.

`settings_file_rewrite_filtered()` already demonstrates a sequential copy with
one reusable key/value buffer, direct iterator writes and combined key/value
I/O. Reuse that *approach*, not its global 60-second watchdog suspension. A
narrow dirty-all transform can preserve timestamps, reconstruct valid flags,
clear the logical synced state, append each live record once, and publish the
replacement only after the whole file is complete.

This reduces the nominal settings writes from four to two per nonempty record
before page splitting and PFS metadata. It is not a prediction of 2x total
speedup. If the existing temporary-file recovery contract is verified for this
transform, committed record headers can be copied as compaction already does;
visibility is controlled by the temporary file commit. Do not collapse the
commit sequence of ordinary in-place setters or turn the driver into an
unacknowledged write-back buffer.

This work belongs in the existing settings rewrite implementation. There is no
sound T5 driver-local substitute: the driver cannot infer logical record
boundaries or safely delay acknowledged writes. Keep the shared change narrowly
scoped, reuse existing iterator/temporary-file semantics, and qualify partial
records, overwritten/deleted records, timestamp preservation, callbacks, reboot
cleanup and failure abort before adopting it.

Also measure temporary-file creation, PFS free-page search/GC, preferences-lock
wait, old-file close, replacement commit and reopen. The observed initial
5,966-tick gap contains all preparation before the first record and scheduling;
it has not been attributed to a particular one of those stages.

## Robustness fix design

### Recognise durable progress without releasing App too early

The current recovery code restores App when `system_task_watchdog_feed()`
advances its generation. Calling that function after every record would permit
App restoration before the handler returns.

The existing `pbl_task_wdt_feed_self()` instead feeds the current thread's
watchdog channel directly, without changing that generation. It can report
bounded rewrite progress while App remains throttled until the BG loop's normal
feed. PFS already uses it after meaningful erase preparation.

The minimal candidate is a feed after each successfully copied record and after
a successfully closed/committed file, on the owning thread. Do not feed for a
raw flash chunk, a retry attempt, a timer firing, an unverified callback return,
or another task's activity. A stalled record, lock or commit must still expire.
Do not change the global timeout/grace, suspend all watchdogs, mark runnable
work as waiting, or add draw sleeps. This is robustness, not an I/O speedup.

A file-boundary-only feed is a smaller experiment for the captured failure
(shell completed before notif started), but does not bound larger single-file
rewrites. Per-record progress requires trustworthy error propagation first.
Measure preparation and final commit too: feeding records cannot protect an
unbounded pre-record phase or make BG schedule sooner.

If a single healthy record/commit exceeds the remaining grace even with App
throttled, the work must be bounded further or the operation-specific watchdog
contract revisited. If BG still monopolises the event loop, use continuations
that release locks between bounded units and retain owned session/token state;
send one final protocol response only after both files finish. This is a larger
change and should follow the smaller candidate, not precede measurement.

### Error handling uncovered by the audit

- The dirty-all callback currently returns silently on allocation failure and
  ignores `settings_file_set_with_timestamp()` status. The void rewrite callback
  always continues; the replacement can therefore be committed without every
  source record. Use an error-aware rewrite/abort path that preserves the old
  file, reports failure and does not feed progress for a skipped record.
- `settings_blob_db_mark_all_dirty()` propagates shell-open failure but omits
  the analogous notification-open failure. Preserve the first failure across
  both files and return a truthful protocol result.
- `t5ai_flash_ipc_read()` skips READ_DONE after a payload CRC failure and leaves
  the channel available on ordinary E_ERROR. The CP can still be awaiting that
  lease release when the outer driver retries READ. For a known valid lease,
  safely release it while retaining the original read error; if state is
  ambiguous, quarantine instead. Exercise late completion and timeout cases.
- CP flash busy loops have no local timeout and notification failures are
  ignored. Add bounded AON deadlines and a safe whole-chip recovery path where
  execution/storage state cannot be recovered. A loop cannot report a clean
  mutation failure if programming may still be active.
- `flash_impl_init(coredump_mode)` ignores the mode and requires IPC/OS services.
  The low-level API documents that coredump mode cannot rely on them. Keep the
  board's unsupported coredump/update paths disabled until an explicit safe
  implementation exists; do not expand flash support incidentally.

These are source-level failure paths, not explanations proven for the captured
watchdog resets. No read CRC fault or OOM is required by the reproduced failure.

## Implementation and validation sequence

1. Extend default-off AP diagnostics with socket/physical/logical timing and
   shared flash/PFS preparation boundaries. Use a hardware/AON clock for CP and
   IRQ-off timings; compare it with AP uptime to detect time lost during stalls.
   Buffer counters and histograms, including maxima, without logging in critical
   sections. Record operation size and owning task so all-caller totals are not
   confused with settings-only time.
2. Diagnose CP with the exact hash-verified stock ELF and verified hook targets.
   Split service queue, protection, mode switching, notifications, programming
   and response delivery. Do not patch a different CP or assume the AP send
   interval is pure transport time.
3. Implement error-aware bounded rewrite progress separately from driver speed
   changes. Test App remains throttled until the BG-loop feed; replacement App
   IDs are protected; a failed/stalled record still expires; old settings survive
   abort/reset and protocol errors propagate.
4. Apply one measured driver optimisation at a time, then the sequential dirty
   copy. Compare transaction counts, phase maxima/distributions, IRQ-off maxima,
   preparation/commit peaks and end-to-end sync completion. Test quiet and heavy
   phone-sync/FdF workloads with the same payload and storage occupancy.
5. Qualify reset during initial header, key/value, record commit, file commit,
   protection restoration and an ambiguous IPC response. Verify settings and
   protected regions after reboot. Run existing flash/priority/watchdog tests,
   native build/layout checks, then repeat with diagnostics disabled.

Use a verified isolated writable test region for synthetic programming; do not
benchmark by erasing live PFS or firmware. Retain byte-for-byte image, CP hash,
configuration, raw UART and decoded dictionary for every compared candidate.
Flash optimisation may reduce rendering interference during sync, but peak draw
FPS and remaining idle-animation BG starvation need separate measurements.

## Driver-local candidate, 2026-10-11

This candidate implements READ lease cleanup, preparation/send attribution and
bounded CP fault recovery. It does **not** yet implement a measured performance
optimisation: the existing measurements identify request-send as the dominant
interval but do not establish which CP phase to change. Protection policy,
program granularity and shared Pebble source remain unchanged. No hardware
qualification or speedup is claimed for this candidate.

### AP correctness and measurement

A timely, validated READ response owns a buffer lease. CRC failure now sends
READ_DONE while preserving the original error and leaving the caller's output
untouched. Invalid responses, expired leases, failed completion acknowledgements
and failed READ_DONE quarantine the channel; the driver stops retrying an
unavailable channel. Lease age uses the independent AON clock, including counter
wrap, against the pinned CP's 50 ms lease. Ambiguous mutations remain unretried.

`CONFIG_T5AI_WDT_RECOVERY_TRACE=y` adds socket lock, physical send and logical
reply timing plus owner-filtered PFS open/close, preferences/alerts lock, generic
mutex and flash read/write/erase preparation measurements. Both AP uptime and
AON totals/maxima are recorded; longest mutex attribution includes its owner.
Counters are dumped at file/rewrite boundaries, with no per-operation logging or
allocation. Wrapper boundaries are kept out of LTO only in the diagnostic build.
This configuration conflicts with `CONFIG_T5AI_REMINDER_PROFILE`; disable that
profile. Use `CONFIG_T5AI_FLASH_PROFILE=y` for operation diagnostics too.

### Optional CP guard and phase measurements

Normal CP packaging remains unchanged. Enable the candidate explicitly with
`T5AI_CP_FLASH_GUARD_ELF=/absolute/path/to/the/verified/stock/app.elf`, or pass
`--flash-guard-elf` to `python3 -m tools.unhygienix_cp`. Packaging verifies the
pinned stock image, exact ELF hash and extracted load bytes, every replaced
instruction, partition capacity and RAM reservations. The manifest records
source hashes and original/replacement bytes. It uses source-built board hooks
on the pinned binary ABI; it does not rebuild or edit vendor firmware source.

The original ITCM and IRAM load images are relocated intact; startup copy-table
entries extend their unused tails for the guard and 144-byte telemetry state.
Seventeen busy loops enter ITCM directly, without a flash literal. Nine command
launch sequences enter RAM before starting the controller, so timeout recovery
can execute while XIP is blocked. Busy waits expire after 160000 AON ticks
(5 seconds at 32 kHz) and request whole-chip watchdog reset with reason 0x7e.
START/END notification hooks check both return status and actual acknowledgement,
including the stock loop-cap false-success case; unsafe continuation resets.
These bounds/recovery paths require on-device fault-injection qualification.

CP telemetry phases are WRITE, UNPROTECT, REPROTECT, STATUS, MODE, START, END,
PROGRAM and IRQ, numbered 0 through 8. Counts, total/max AON ticks, bytes,
cached/commanded status changes and before/after requested status values are recorded (not an
independent physical status readback). PROGRAM
covers the original 32-byte critical section; IRQ measures nested outer spans.
These phases overlap: do not add their totals as independent components. Dumps
occur every 128 completed server writes after protection/interrupt restoration;
logging is excluded from WRITE samples but affects end-to-end runtime. Queue and
response delivery are not separately resolved by these CP hooks; correlate them
with AP physical/logical stages. Convert AON ticks using 32000 ticks/second.

### Qualification gate

Host regressions cover lease faults/deadlines, wrapper attribution, notification
faults/nesting and real hash-pinned CP binary relocation/patch round trips. AP
image/resource layout (diagnostics enabled and disabled) and native shared flash
API/erase suites also pass. AP sizes are 1611480 bytes with diagnostics and
1607232 bytes without; these are build sizes, not performance measurements. These
checks establish build and simulated fault behaviour, not physical flash safety.
The existing `test_tuya_kernel` suite has baseline failures against the current
FreeRTOS/CMake tree; those are reproduced without this candidate.

Before selecting item 3's optimisation, capture matched baseline/candidate UART
runs with identical settings payload, occupancy and phone-sync workload. Compare
phase totals/maxima, IRQ-off spans, preparation peaks and end-to-end completion.
Only change a phase demonstrated to dominate; measure the resulting A/B gain and
verify protection restoration and reset during programming/commit. Repeat with
AP/CP diagnostics disabled before adopting any performance result. No shared
source change is justified by the evidence currently available.


## Hardware measurements — 2026-10-11

The sealed AP/CP diagnostic candidate was flashed successfully, CP first then
AP, preserving PFS, resources, bonds and calibration. AP UART confirmed build
`c926ecda17551618718e72d596fa470511666451` and Ready for communication. Tyutool
reported flash ID `0x001765c8`. Raw/decoded UART capture received 59705 bytes,
including pre-flash traffic; analysis starts at the matching AP build-ID marker
(raw offset 12962). The operator ran phone full sync with FdF and confirmed it
finished. Capture is closed; the diagnostic AP/CP remain flashed.

Evidence: `build/traces/flash-guard-20261011/hardware/`, including `stats.json`,
`candidate-decoded.log`, raw UART/index, exact flash logs, analysis script and
checksums. Exact images/ELFs/configuration/dictionary/source remain in the parent
candidate directory. The CP guard's reset and fault-injection paths were not
exercised by this normal workload.

| Measured boundary | Result |
| --- | --- |
| Shell settings rewrite, 19 records / 363 bytes | 1949 ms, status 0 |
| Notification rewrite, 10 records / 217 bytes | 1250 ms, status 0 |
| Whole dirty-all call | 3261 uptime ms; 3261.0625 AON ms |
| CP WRITE, cumulative 256 writes / 2115 bytes | 3920.90625 ms total; 15.31604 ms mean; 19.5625 ms max |
| CP UNPROTECT, 256 calls | 3770.1875 ms total; 14.72729 ms mean; **96.156% of WRITE total** |
| CP REPROTECT, 256 calls | 43.0625 ms total; 0.16821 ms mean |
| CP 32-byte PROGRAM sections, 320 calls | 17.375 ms total; 0.05430 ms mean; 0.125 ms max |
| CP IRQ-off maximum | 18.25 ms |
| Latest AP write total, 329 calls | 5302 ms; physical send 5108 ms (96.34%); logical reply 27 ms |
| AP write-lock / socket-lock totals, same 329 calls | 3 / 4 ms |

The two CP dumps cover the first 128 and 256 server writes after boot, including
startup/connection and part of sync; they are not a settings-only window. The
first dump independently assigned 97.093% of WRITE time to UNPROTECT. CP phases
overlap, so STATUS/IRQ must not be added to WRITE/protection totals. No erase was
observed. Status counters reported 512 cached/commanded changes for 256 writes,
with last transition 0x0a00 to 0x0a38. These are not independent physical status
readback or reset-persistence verification.

Preparation measurements: two overwrite opens totalled 334.6875 AON ms, flash
reads 466 ms and flash writes 2398.9375 ms across the dirty-all operation; these
are nested boundaries. Preferences/alerts outer lock samples were one AON tick
each. The longest sampled generic mutex wait was 2.09375 ms, owner 7, address
0x2801f294 (`s_log_state_mutex` in the exact ELF). Generic mutex totals also
include the profiler's own boundary-dump logging and must not be interpreted as
pure PFS preparation. Dumping contributes to measured wall time. No per-stage
histogram exists for these new preparation/CP counters; only counts/totals/maxima.

### Correctness and stability observations

Both settings rewrites returned success. No additional AP boot, flash IPC error,
assertion or hard fault was captured after the verified candidate boot. There
was a Resource 614 check failure for App 0 during startup; it is retained in the
trace and has not been attributed to this change. No physical protection
restoration audit, reset-during-write test or diagnostics-disabled hardware A/B
was performed.

There were **8 watchdog stall reports and 7 recovery messages**. In one interval,
BG CPU remained 3499 ticks at uptime 138480 and 198550: **60.070 seconds with no
recorded BG CPU progress**. Both snapshots show BG READY at priority 1, App
priority 2, no active flash transaction and no active dirty-all call; flash
counters were unchanged. Recovery was later reported. This reproduces prolonged
starvation after settings completion and prevents stability qualification. These
samples do not establish why the throttle/recovery failed to maintain progress.

### Decision from the measurements

Protection-status work, specifically UNPROTECT, is now the measured CP target.
Transport gate changes, priority increases and page-program tuning are lower
priority: physical-send contains the CP work, locks/replies are small, and
programming itself is a small fraction of WRITE. Investigate a board-local
protection strategy or validated volatile-status sequence next, retaining
protected firmware/calibration boundaries and reset safety; do not simply leave
flash unprotected or skip restoration. The current hooks preserve protection
policy and establish no optimisation speedup.

The earlier completed 19-record shell rewrite was 1916 ms versus 1949 ms here;
notification was 1161 ms versus 1250 ms. Different instrumentation/conditions make
these contextual comparisons, not a controlled A/B result. No shared Pebble
source change is justified by this run. Keep the separate post-sync BG starvation
issue open rather than attributing the 60-second episode to flash latency.

## Volatile protection implementation and hardware rejection — 2026-10-11

Implemented an opt-in board-local CP hook (`T5AI_CP_VOLATILE_STATUS`, default
OFF; tool flag `--volatile-status`). It is **experimental and rejected for use**
after physical readback failures. No stock Pebble shared/common source or vendor
source was changed. The unchanged diagnostic AP build was
`c926ecda17551618718e72d596fa470511666451` throughout all trials.

The programmer identified system NOR as `0x001765c8`; CP reported JEDEC
`0xc86517`. The [GD25WQ64E manufacturer datasheet, rev 1.2, sections 7.4–7.5](https://www.mouser.com/datasheet/2/870/gd25wq64e_v1_2_20200415-1825549.pdf)
describes `50h` followed immediately by WRSR as a volatile status update. This
motivated a chip-gated implementation, but does **not** prove the tested SPI
bypass issued an electrically correct sequence.

The hook uses the pinned RAM-resident SPI bypass, pauses AP, issues `50h` and
`01h/SR1`, changes only BP bits, leaves SR2 untouched, and reads actual SR1/SR2
through the controller before and after the operation. It verifies the prior
physical value against the cache and bounds WIP polling to 100 AON ms. Volatile
updates publish the cache only after successful readback. The v3 stock restore
function writes its cache internally, so its return/cache are explicitly not
treated as proof: physical readback is still required before releasing AP. A mismatch triggers the existing
whole-chip guard reset (`AP=126`, `CP=16`, guard reason `0x7e`). No success is
inferred from the cached status or the SPI function's return value.

### Hardware outcomes

UART reception was verified before each CP-only flash. Every flash and rollback
reported `Flash OK`; AP/PFS/resource partitions were outside the programmed
range. Three distinct captures, images, exact source hashes and fault evidence
are retained under `build/traces/`:

| Trial | Artifact directory | Observed result |
| --- | --- | --- |
| Initial volatile unprotect/restore | `flash-volatile-20261011` | Startup guard resets; no completed candidate timing window. |
| Same sequence with fault diagnostics | `flash-volatile-v2-20261011` | Restore requested `0x0a38`, read `0x0a00`; another boot also failed unprotect (`0x0a00` requested, `0x0a38` read). |
| Volatile unprotect with stock restore | `flash-volatile-v3-20261011` | Unprotect repeatedly requested `0x0a00` but read `0x0a38`, before reaching stock restore. |

All trials failed startup qualification, before requesting the operator's
full-sync workload. There is **no valid candidate performance benchmark or
speedup**. Both readback directions failed across the runs: the evidence does
not support blaming only restore, increasing a delay without evidence, disabling
readback, or declaring volatile updates unsupported by the chip. The fault is
not yet isolated between command transport, timing, controller state and the
physical flash response. Host mocks passed while hardware failed; mocks do not
model the SPI electrical transaction.

The final rollback restored baseline CP raw SHA-256
`63e6a7f37acfa2cfd9ad827e0fee61199c54c611538f6e4f12e60ef16c6b7e04`
and protected SHA-256
`951f1284435cbe48fbe4900c1311d42d1d6311332c562201a626f90136afa200`.
The watch booted at 13:36:53 local time with AP/CP reset sources `0/0` and
subsequently produced a cumulative 384-write window (26552 bytes):

| Post-rollback CP metric | Measured value |
| --- | --- |
| WRITE | 6315.15625 ms total; 16.44572 ms mean; 19.53125 ms max |
| UNPROTECT | 6130.40625 ms total; 15.96460 ms mean; 97.0745% of WRITE |
| REPROTECT | 60.21875 ms total; 0.15682 ms mean |
| IRQ-off maximum | 18.28125 ms |

These are baseline recovery measurements with a different byte count/workload,
not a matched comparison against the earlier full sync. Baseline protection
counters remain cached/commanded values, not a new physical protection audit.
Capture was closed cleanly. Each directory has `stats.json`, raw UART with
receive timestamps, decoded trace, flash/rollback logs, firmware and
`SHA256SUMS`. The v2/v3 archived C/assembly source hashes match their exact build
manifests; later experimental warnings in the working source are not represented
as the tested image.

Seven focused host tests passed, including actual C hook execution with injected
SPI/readback/WIP/deadline/restore faults, existing notification/IRQ timing checks,
real pinned CP packaging, read-lease handling and AP timing hooks. Packaging with
the optimisation disabled also passed. Existing unrelated Ruff `TRY004` in
`tools/unhygienix_cp.py` remains; new hook/tool/test files pass lint.

**Decision:** keep volatile status OFF and retain the measured baseline on the
watch. Do not ship or enable this experimental bypass. Further implementation
requires establishing the physical `50h`/WRSR transaction (for example with a
logic analyser), then repeating verified unprotect/restore/reset qualification
before any full-sync speed comparison. No shared Pebble source change is
justified by these outcomes. The previously observed BG starvation also remains
unresolved and independent of claiming flash optimisation success.

## Boot-loop diagnosis and isolated SPI tests — 2026-10-11

The immediate boot-loop cause is confirmed in both source and captured logs.
At startup's first CP server write, the experimental status hook reads an
unexpected physical protection value. `pebble_flash_volatile_status()` calls
`fail(4)`; that deliberately calls `pebble_flash_fatal()`, writes guard reason
`0x7e` and resets the whole chip. The AP then reports `AP=126 CP=16`. There is no
persistent suppression of the failed experiment, so the next boot retries the
same startup write and resets again. Restoring the baseline CP removes this path.
This is a deliberate guard reset, not evidence of a hard fault or task-watchdog
expiry. The relevant readback values in the original failed trials were
`0x0a38 -> 0x0a00` requested with `0x0a38` still read, and the reverse restoration
request with `0x0a00` still read.

Further hardware tests used isolated CP probes, the same AP, and the original
stock protection/data-write path. The probes never programmed or erased flash
data. The isolated status probe temporarily requested volatile unprotect, then
restored **and physically verified** stock protection before normal API writes
could proceed. Write-enable latch tests ended with independent controller WRDI.

Results:

| Experiment | Observation |
| --- | --- |
| Read-only SPI bypass | JEDEC `c8 65 17` twice; SR1 `38`, SR2 `0a`; controller agreed before/after. Watch reached Ready. |
| Separate latch commands | Both SPI transmit paths can set and clear WEL. An additional run returned success for WRDI while WEL stayed set. |
| Stock repeated latch checks | 3 mismatches across 96 command checks: 1 prefix-path enable and 2 payload-path enables. |
| Explicit FIFO clear before transmit | 7 mismatches across 96 checks; did not solve it. |
| Change the final payload "release CS" mask to clear TX enable | 4 mismatches across 96 checks; did not solve it. This was an isolated four-byte CP RAM-image patch, never a vendor source change. |
| Paired immediate / delayed WEL reads | 2 enable mismatches remained after at least two AON ticks, in combined-command mode; no mismatch in the other modes in this sample. |
| Isolated volatile status update | Read `0x0a00` at approximately 62 us, 1 ms and 10 ms, then stock restoration physically read `0x0a38`. |

Each repeated test covered 16 enable/disable pairs per mode, three modes. Mode 0
uses the opcode argument alone; mode 1 uses the payload alone; mode 2 sends an
opposite opcode prefix followed by the intended payload command. The command
checks above exclude preliminary latch operations. One preliminary SPI ID read
in the latch-only run returned zero bytes despite a successful byte-count return;
a later ID read returned the correct ID. Preserve this anomaly too.

These tests narrow the problem to the SPI bypass/completion contract: its
successful return does not reliably establish the requested device state, and
the failure is not specific to `50h` or proof of unsupported volatile writes.
A simple fixed settling-delay explanation is insufficient for the paired test,
but these samples do not establish the full timing envelope. FIFO cleanup and
transmit-disable alone are rejected fixes. The precise remaining fault—on-wire
command/CS timing, peripheral state, or completion/readback behavior—has **not**
been isolated. A logic-analyser capture of the failed `50h -> 01h` transaction
and following `05h`/`35h` reads is the next discriminating measurement. Do not
replace this uncertainty with an assertion that the chip rejected the command.

All temporary probes and exact source hashes are archived under
`build/traces/flash-spi-*-20261011/`. The combined stress/FIFO/CS/settling raw
capture is in `flash-spi-stress-20261011`; individual firmware and build scripts
are in their named directories. `flash-spi-cause-20261011/diagnosis.json` maps
firmware hashes, captures and outcomes. These diagnostic images are not release
artifacts; their reused experimental build flag does not mean the production
optimisation was enabled or qualified. The measured baseline CP was restored,
with a separate live rollback capture in `flash-spi-rollback-20261011`.

Rollback verification: flashing the sealed baseline CP completed successfully.
The final 90-second capture started after its boot header, received 21160 bytes,
observed a phone session opening and a cumulative 512-write CP window, then
closed normally. It confirms live baseline operation rather than a newly
captured reset-reason audit. Existing watchdog/starvation reports remain; this
is not a stability qualification. All diagnostic capture processes are closed.


## SPI recovery implementation and re-benchmark, 2026-10-11

The requested SPI repair is **not established at the signalling level**. A
board-local replacement still occasionally returns success without the NOR
accepting a command. The usable change is a physically verified recovery path;
volatile-status remains opt-in and disabled in the default build. No shared
Pebble code or vendor source was changed for this work.

### Rejected SPI replacements

All probes checked physical SR1/SR2, restored protection with the stock
controller and retained stock data programming. Except for the longer probe,
each short test exercised eight enable/disable pairs in each of three calling
modes (48 command checks). The earlier prefill and comparison probes used 16
pairs (96 checks).

| Candidate | Failed latch checks | Result |
| --- | ---: | --- |
| FIFO preload, clean state, explicit CS release | 2/96 | Rejected |
| Same writer, independent SPI/controller comparison | 5/96 | Both reads confirmed a failed WRDI |
| Continuous-read reset immediately before RAM SPI ownership | 21/48 | Rejected; isolated volatile write also timed out |
| FIFO-clear settling | 3/48 | Rejected |
| Idle-high mode 3 at slower clock | 2/48 | Rejected; isolated volatile write left BP unchanged |
| Delayed controller readback | 12/48 | Rejected |
| Vendor dual-line preparation | 4/48 | Rejected |
| Disable before clock/configuration changes | 4/48 | Rejected as a complete fix |
| Transmit-only framing with FIFO preload | 6/48 | Rejected as a complete fix |

The 128-pair timing probe and controller-rearm probes reset with AP=80/CP=16
before returning their results. The large comparison-buffer probe reset with
AP=0/CP=20; stack pressure was suspected but not proved. Those candidates were
rolled back, and their first-failure traces were retained. These resets are not
successful qualification of the replacement. There was no hardware run of the
prepared two-wire-override variant.

### Guarded recovery

`boards/unhygienix/tuya/cp/flash_spi.c` is compiled only for the experimental
volatile-status build. It disables SPI before configuration changes, preloads
the TX FIFO, sends transmit-only frames in RAM, explicitly releases CS, bounds
completion with the independent AON clock, restores configuration without
replaying the FIFO data port, and returns to XIP only after restoring NOR routing.
The binary patch is restricted to the verified stock bypass-write allocation;
the stock bypass-read routine and the remainder of the relocated RAM image are
preserved. Exact stock image and ELF hashes remain mandatory.

`flash_volatile.c` now checks the device status after the SPI operation even if
SPI reports an error. An idle status matching the requested protection is
accepted. If the device is still in its exact original protected state, the
hook uses the stock controller status write and physically verifies the result.
It never retries SPI. Partial/unexpected status changes, stuck busy, failed
controller fallback, and failed protection restoration still cause fail-stop
before data programming can proceed. Recovery is logged only after AP resume
and IRQ restoration. Stock re-protection is retained.

This specifically removes the boot-loop trigger caused by treating an unchanged
SPI unprotect result as unrecoverable. It does not prove that the underlying SPI
fault has been repaired. A waveform measurement remains necessary to establish
why completed SPI frames are sometimes ignored.

### Initial normal-write measurements

The running raw CP image is
`c3df1e5277a92b201bc5b4974b82f8d9714b35d64acba6ab57860fa650e3d28c`.
The AP diagnostic build is unchanged:
`c926ecda17551618718e72d596fa470511666451`.
Sealed source, CP images and machine-readable measurements are under
`build/traces/flash-spi-recovery-final-20261011/`. Combined UART evidence is in
`build/traces/flash-spi-rearm-v2-20261011/`; the running recovery candidate begins
at raw byte 84830, with its AP boot at 15:33:03 BST (AP=0/CP=0).

| CP write snapshot | Bytes | Mean write, ms | Maximum, ms | Verified stock fallbacks |
| ---: | ---: | ---: | ---: | ---: |
| 128 | 11,012 | 5.6921 | 19.9063 | 34 |
| 256 | 25,522 | 6.0087 | 20.8125 | 71 |
| 384 | 26,551 | 6.9448 | 20.8125 | 122 |
| 512 | 27,627 | 8.8256 | 21.8125 | 221 |

These are cumulative CP AON timings (32 ticks/ms). The previously recorded
baseline mean was 15.3160 ms at 256 writes/2,115 bytes. Different byte totals and
workload phases prevent claiming a matched 42% speedup or any end-to-end sync
speedup. Worst IRQ-disabled time in the recovery sample was 18.8125 ms, versus
18.25 ms in that baseline: the slow fallback path remains. Initial settings
rewrites both returned status=0. User-triggered full-sync qualification and
post-reset results are recorded below when available.

Five focused tests pass, including actual replacement C execution, FIFO
preloading, disabling before reconfiguration, injected errors, deadline wrap,
physical recovery and rejection of unsafe/failed recovery, and real pinned CP
packaging with optimisation both enabled and disabled. New Python files pass
Ruff. Formatting the source produced a byte-for-byte identical running CP image.

The default flag remains OFF. Neither this short run nor the recovered failures
justify changing common Pebble source, declaring the SPI transport reliable, or
claiming that the separately observed BG starvation is resolved.


### Operator full-sync result

The operator reported the sync in progress and then Finished. The sealed
candidate capture contains 1,467 AP flash writes, 1,084 logged recoveries,
zero `T5VOL fault` reports and one AP boot header (AP=0/CP=0); no uncommanded
reset occurred in that capture. The initial two successful settings rewrites
preceded the manually triggered sync; no additional settings-rewrite completion
was logged during that sync. Do not attribute the earlier completions to the
manual workload.

The final complete CP snapshot was 1,408 writes / 72,511 bytes:
mean **13.3191 ms**, maximum **23.375 ms**, IRQ-disabled maximum **18.84375 ms**.
This supersedes the early 512-write mean as the more representative sample.
The recovery rate grew markedly during the run; the early mean must not be
used as a sustained improvement claim. Full snapshots are saved in
`full-sync-phase-results.json`; `full-sync-results.json` records capture hashes,
operator completion and trace limits. BG CPU accounting again stopped advancing
for approximately one minute while app CPU accounting advanced; that independent
starvation issue persists.

Reflashing the byte-identical candidate for a controlled reset returned
AP=0/CP=0 at 15:40:11 BST and valid system resources. Post-reset flash results
are preserved in `reboot-rx.bin` and `reboot-trace.log`.


The final power-down-inhibit/RAM-barrier probe also failed: 4/48 latch checks,
and isolated volatile unprotect left status at `a38`. That variant was rejected;
the exact sealed recovery source and CP image were restored. The first controlled
recovery reset completed both settings rewrites with status=0 at 15:40:34 and
15:40:36 BST. `reboot-rx.bin` also contains the subsequent power-down probe;
its separate commanded boot begins at 15:42:27 BST. Final restoration is logged
separately in `final-flash-cp.log`, `final-rx.bin` and `final-trace.log`.

**Outcome:** a tested fallback for the unchanged-status boot-loop case is
implemented and the guarded candidate is flashed. The physical SPI defect is
still unresolved, so this is a workaround and the build default remains OFF.
The completed sync and controlled-reset checks are limited qualification of
that workaround, not a release soak or proof of reliable SPI signalling.

Final restoration booted at 15:43:56 BST with AP=0/CP=0, validated system
resources, and completed both settings rewrites with status=0 at 15:44:18 and
15:44:19. No guard fault or uncommanded reset was observed in this final capture.
The capture was closed after verification; the tested guarded firmware remains
running on the watch. See `final-results.json` for sealed trace identity.


## SPI correctness review and evidence correction — 2026-10-11

This review covers the flash SPI replacement, volatile-status integration,
notification and IRQ hooks, pinned binary patch and relocation, stock Tuya
bypass writer/reader, and the relevant BK7258 SPI HAL/register definitions.
The display uses QSPI0 at `0x46040000`, whereas this bypass uses SPI0 at
`0x44870000`; they are separate peripheral register blocks. This is not a
qualification of every generic SPI/DMA API in the vendor SDK or other boards.

### Most important correction: notification ACK is not AP ownership

`fw/drivers/t5ai/cp_mailbox.c:prv_receive` handles channel `0x4b` by changing
parameter1 from REQ (1) to ACK (2), then returning. Neither START nor END
parks AP execution, masks its interrupts, or prevents flash reads. The AP
linker places application code in flash at `0x02120000..0x022b0000`.
Disabling interrupts on CP and executing the CP transfer loop from IRAM does
not establish exclusive ownership against AP XIP fetches.

The earlier wording that `pebble_flash_prepare` "pauses AP" was incorrect.
The volatile helper also sends nested START/END notifications inside the
stock write's existing pair. Those calls are redundant, but the proposed
"premature AP resume" explanation is **withdrawn**: this AP handler does not
pause or resume anything. A notification nesting counter would not fix the
missing ownership guarantee.

AP/XIP interference is a plausible hypothesis requiring a controlled test.
The audit has not established what the silicon does to an AP cache miss while
`flash_sel` routes pins to SPI, or proved that such a miss corrupts a frame.
A cache hit can hide the issue. No claim that this is the measured cause of
all ignored commands is warranted. Previous completion counts and timings
remain observations, not evidence that the bypass is safe or qualified.

### Why stock Tuya is not a comparable SPI failure baseline

Stock `bk_flash_write_bytes` uses `flash_lock`, `flash_write_no_lock`, and the
dedicated flash controller. Its status-register writes use
`flash_hal_write_status_reg`. The pinned SDK has
`CONFIG_FLASH_WRITE_STATUS_VOLATILE` disabled. SPI bypass is used for special
operations such as security/OTP access, rather than every settings write.
Thus 1,084 recovery messages from the earlier full-sync capture are not a
comparison against 1,084 equivalent stock SPI operations. They count guarded
recovery conditions; a complete SPI frame is not proof of device acceptance.

### Concrete stock bypass defects

These findings concern unchanged reference/vendor code, not changes to it:

| Finding | Source and binary evidence | Consequence |
| --- | --- | --- |
| Writer's payload CS cleanup retains TX enable | `flash_bypass_op_write` step 7.4 uses `reg &= (RX_EN | TX_EN | TX_FIN_INT_EN)`, rather than clearing these bits; pinned disassembly also retains them | Its "release CS" step does not actually clear TX_EN before mux restoration |
| DATA FIFO port is saved and restored as configuration | Both stock read/write paths read DATA during save and write the saved value during restore | Reads can consume FIFO content; restore can inject stale transmit data |
| Reader can pop two bytes for one readiness observation | Final RX catch-up loop discards a byte when `tx_cnt < tx_len`, then unconditionally reads DATA again; pinned instructions at `0x080669b8` and `0x080669ba` confirm both reads | A one-byte FIFO can be read empty; the returned byte count does not prove valid contents |
| Completion and delays do not establish device acceptance | Polls TX_FINISH with CPU-loop timeout/delays, then returns success or received count | Clock changes affect delay duration; a successful return can accompany an ignored command |

Consequently the earlier "independent SPI/controller agreement" is weaker
than stated: its SPI reader contains a correctness defect. Controller readback
still recorded the status, but the extra SPI result is not an independently
qualified oracle. These source defects do not prove which one caused every
previous observed failure.

### Replacement and integration audit

The replacement already avoids the stock DATA restore, preloads TX before
TX_EN, explicitly clears CFG before releasing the mux, clears FIFO/W1C bits,
uses an independent AON timeout, and restores saved clock/configuration state.
The current CP RAM section and its literal pool fit within the original
writer allocation, and disassembly shows no XIP call inside mux ownership.
The stock reader and neighboring IRAM code are preserved by the previous
binary patch contract. The real pinned ELF/image hashes, relocation contents,
and protected CRC are checked by host tests.

Those checks do not establish pin-level completion or AP ownership. Additional
limitations remain: the bypass ABI accepts pointers that are dereferenced
while flash is disconnected without enforcing RAM-only buffers; the current
volatile caller uses stack bytes, but a general replacement must validate or
stage all buffers. CFG TX/RX idle bits alone also do not prove that another
owner cannot schedule SPI/DMA activity. A rewrite needs an explicit ownership
contract, not just more delay loops.

The register definitions initially label CTRL bits 30..31 reserved, but the
BK7258 `spi_ll_set_rx_sample_edge` deliberately uses that field and the stock
HAL initializes it to 1. Therefore bit 30 in `0x41c00700` is supported by the
vendor HAL and is not classified as an accidental reserved-bit write.

Clock reporting was wrong: with the exact SDK's `CONFIG_SYSTEM_CTRL=1` and
26 MHz XTAL, the active HAL computes divider as `src/(2*baud)`. Divider 7
therefore implies approximately **1.857 MHz**, not the previously stated
3.25 MHz. This is a HAL-derived estimate, not a measured wire frequency;
metadata now labels it accordingly. Historical divider-25 "1 MHz" claims
likewise cannot be treated as measured frequency (the HAL implies 520 kHz).

The GD25WQ64E command sequence itself is supported: `50h` must be immediately
followed by WRSR without another command, `01h` writes SR1 with CS rising after
its data byte, and `50h` does not set WEL. Its status-write maximum is 30 ms,
below the existing 100 ms status deadline. See the manufacturer's
[datasheet, sections 7.4/7.5 and AC timing tables](https://download.gigadevice.com/Datasheet/DS-00476-GD25WQ64E-Rev1.2.pdf).
The hook's BP-only gate preserves other status bits. Postcondition checks now
also reject WEL remaining set after either SPI or stock recovery; previously
masking WIP/WEL could accept that unexpected state.

### Changes and validation from this review

- Experimental `volatile_status=True` packaging now raises an explicit error
  before constructing a deployable image. The flag remains disabled by default;
  explicitly enabling it cannot bypass the missing AP ownership prerequisite.
- AP handler and volatile-helper comments now state the actual ACK-only
  semantics. No shared/common Pebble source or Tuya reference source was changed.
- WEL postcondition checks and fault-injection cases were added. Clock metadata
  now reports an estimate with its vendor HAL basis.
- Six host tests passed, including real ARM compilation/relocation and CRC
  checks, transport faults, unexpected status/WEL, and rejection of unsafe
  packaging without requiring a toolchain. Eight existing flash/mailbox
  integration tests also passed (14 tests total). Offline SPI compilation remains
  available for source/disassembly audit, not flashing qualification.
- The controller-based CP baseline was flashed successfully in 20.3 seconds.
  Rebuilding the current default path reproduced the sealed raw SHA256
  `63e6a7f37acfa2cfd9ad827e0fee61199c54c611538f6e4f12e60ef16c6b7e04`.
  A subsequent 90-second UART capture received 11,404 bytes and showed live AP
  output, including a background-task watchdog stall and recovery. That symptom
  is therefore also present on the controller baseline. This was a health
  capture, not a new matched full-sync benchmark.

Evidence is under `build/traces/flash-spi-audit-20261011/`: rollback flasher log,
raw UART bytes/index, decoded trace, host test log, audited CP source snapshots,
rebuilt baseline, offline RAM disassembly, and `audit-results.json`.

### Recommended rewrite boundary and acceptance criteria

A ground-up **board-local flash bypass** replacement is reasonable; rewriting
the unrelated generic SPI subsystem would broaden scope without resolving
these findings. First implement a real AP/CP handoff: AP must acknowledge
only after entering a RAM-only wait with flash-dependent interrupts and other
flash readers excluded, and must leave only after CP restores normal routing.
The release/deadline/failure path must itself work without XIP and must not
rely on a second notification interrupt while AP is parked.

Then replace both bypass TX and diagnostic RX with one bounded RAM-resident
transaction engine: named registers, explicit CS boundaries, RAM-only buffers,
exact transmitted/received byte accounting, FIFO errors, wrap-safe deadlines,
and deterministic cleanup. Qualify read-only JEDEC/status first; then WREN/
WRDI latch tests and volatile writes with controller verification after mux
restoration. Capture CS/SCLK/MOSI/MISO if possible to distinguish ignored
commands from status-oracle errors. Check byte counts and actual clock/CS
timing rather than inferring them from a completion flag.

Only after those checks pass should the same full phone sync with FdF be
repeated against the sealed baseline. Record acceptance failures, fallbacks,
resets, IRQ-off maxima and flash phase times. The present evidence justifies
SoC/board-local ownership and driver work; it does not justify modifying shared
Pebble source or claiming a performance improvement.


## RAM ownership rewrite checkpoint — 2026-10-11

The [rewrite implementation and qualification record](flash_spi_rewrite_results.md)
documents the versioned post-ACK RAM park/release path, unified offline TX/RX
engine, paired-image checks, RAM allocations and preserved evidence. Two dry
hardware runs reached at least 1360 and 1216 completed generations with zero
reported ownership failures; the bounded run exercised parks up to 30 ms. The
operator confirmed FdF animation and watch responsiveness. This is evidence of
the exercised dry protocol, not exclusive flash-bus ownership or SPI command
acceptance.

SPI routing remains blocked pending verified exclusion/draining of CPU2, DMA
and other enabled masters, followed by the read-only hardware gate. Latch/BP
qualification and matched benchmarks were not run. No safety or speed claim is
made for the rewritten transport. Both exact sealed controller baseline images
were restored. BG stalls were observed on the restored baseline as well and
remain a separate issue.


### Extended ownership and bus audit — 2026-10-11

The subsequent dry-only run completed at least 1712 AP/CP handoffs with no reported
ownership failures, including 32 delayed 0–30 ms holds. CPU2 was powered down and
no enabled general DMA channel appeared in 38 samples; BTDM/MAC clocks remained
enabled. Autonomous master exclusion/drain remains unproved, so SPI routing,
volatile status and performance gates remain blocked. The controller baseline
was restored. See the [extended results](flash_spi_rewrite_results.md#extended-dry-ownership-and-bus-audit--2026-10-11)
for exact images, raw capture evidence, test results and limitations.
