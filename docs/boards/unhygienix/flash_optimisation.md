# T5 flash optimisation and robustness audit — 2026-10-11

Status: source audit and captured-timing analysis complete; implementation and
hardware qualification remain. This audit changes documentation only. The
existing diagnostic AP remains flashed; CP is unchanged. The corrected phase
capture finished at its configured limit with 277,311 received UART bytes.

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
