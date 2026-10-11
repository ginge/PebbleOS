# Settings full-sync watchdog resets — 2026-10-11

Status: reproduced twice on the targeted diagnostic candidate; reboot issue open.
Hardware tracing has finished at the operator's request. No firmware was changed
or flashed while documenting these findings. The diagnostic image remains on the
watch, with expiry tracing and flash profiling enabled.

## Evidence and reproducible trigger

Animate FdF Time and reconnect/sync the phone. An operator-requested hardware
reset at 10:47:54 was followed by spontaneous watchdog boots at device times
10:48:47 and 10:49:24. Both report AP=112 (0x60 + watchdog 16), CP=16.
Do not count the commanded reset as a reproduction. Phone sync changes wall
clock time; use uptime ticks for durations.

Exact AP build: `3b0287cb96ae1f1a9c9bbb698dc9196744b89020`.
The finished capture contains 175,988 raw UART bytes, receive-offset timestamps,
decoded logs, image/ELF/map, source copies and SHA256 manifests in
`build/traces/wdt-flash-recovery-20261011/`. Frozen failure intervals and the
comparison report are in its `two-reproductions/` directory. Build artifacts are
local evidence; this document records findings independently of ignored files.

| Final recovery interval | First reproduction | Second reproduction |
| --- | ---: | ---: |
| Settings operation start tick | 49,477 | 28,602 |
| Last expiry sample tick | 57,281 | 36,133 |
| Operation still active after | 7,804 ms | 7,531 ms |
| Three BG CPU counter samples | 278 / 638 / 1,070 | 397 / 1,051 / 1,609 |
| Span between first/last samples | 2,066 ms | 2,071 ms |
| Completed flash reads in span | 169 | 171 |
| Completed flash writes in span | 107 | 106 |
| Completed erases in span | 0 | 0 |
| Cumulative maximum completed read | 8 ms | 9 ms |
| Cumulative maximum completed write | 22 ms | 25 ms |

App base/effective priority remains 0/0 during the final grace checks. KernelBG
runs and completes I/O; its CPU counter increases by 792/1,212 ms respectively.
The `settings=1` marker identifies `settings_blob_db_mark_all_dirty()` as active
through both intervals. BG's temporary priority 3 is the existing flash-client
handshake boost, not evidence of a stuck inherited App priority.

Counters include all callers, so they do not assign every transaction to BG.
They are cumulative maxima and counts, not summed I/O time. A write observed
active at a snapshot is not itself evidence of a timeout. No multi-second
completed operation or erase burst explains these final intervals.

## What is established

The immediate reset trigger is a BG watchdog deadline exceeded while the
synchronous settings full-sync operation remains in progress. The initial
five-second starvation and the subsequent two-second recovery are distinct:
App can prevent BG from starting useful work, then the rewrite still takes too
long to finish and return to the BG event loop after App is throttled. The
7.5–7.8 second wall durations are not measurements of pure flash service time.

The sampled failures persist with effective App throttling and sustained BG
scheduling. This rules out complete lack of task switching as their sufficient
explanation. It does not resolve expensive watchface drawing or establish an
end-to-end frame rate. SDK cache removal remains a separate TODO.

Progress-driven App recovery fixes premature restoration after 300 ms. The
T5 priority scope also preserves an external throttle applied mid-transaction.
Both corrections are present but insufficient to prevent these reproductions;
there has been no isolated hardware A/B qualification of either correction.

## Source and binary investigation

The call path is:

`BlobDB endpoint2 mark-all-dirty → settings_blob_db_mark_all_dirty →
settings_file_mark_all_dirty → settings_file_rewrite →
prv_mark_all_dirty_rewrite_cb → settings_file_set_with_timestamp →
settings_raw_iter writes → pfs_write → flash_write_bytes → T5 flash IPC →
CP flash_write_handler → bk_flash_write_bytes → flash_write_no_lock`.

The shell and notification preferences are rewritten into temporary files.
Each record is written as header, key, value, then committed header. PFS adds
metadata writes. The new record writer also searches for existing records,
so read/seek work may amplify with record count. Measure that scaling before
assuming fixed write overhead explains all time. Existing close/rename logic
keeps the old file recoverable until the temporary replacement is committed;
post-reset logs show temporary-file cleanup. That is recovery behavior, not
proof of corruption or a reason to weaken commit ordering.

CP reference source removes protection, programs data, then restores the old
protection and line mode for each API write. `flash_set_protect_type()` writes
the status register only when protection/CMP bits change. The low-level status
write waits on the controller while interrupts are disabled.

This behavior was checked against the exact pinned stock CP ELF:
extracting its binary yields SHA256
`0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856`,
matching the pinned stock input. Disassembly confirms both protection calls in
`flash_write_no_lock` (0x0207ef30), conditional status writes in
`flash_set_protect_type` (0x0207ed34), and the status writer at 0x0207ec24.
Its configuration disables `FLASH_WRITE_STATUS_VOLATILE`. The existing BLE-init
hook is separate from these flash functions. Evidence:
`cp-binary-verification.json` and `cp-write-disassembly.txt` in the capture.

Protection changes are a concrete hypothesis for the measured small-write
latency, not a measured timing breakdown. Whether the bits actually change
on each live request still needs observation. Mailbox scheduling, CP locking,
programming, and response delivery remain alternative contributors.

## Next debugging steps

1. Measure the fixed cost of representative 8/18/25-byte writes and larger
   aligned writes in an isolated test partition, with readback and unchanged
   durability semantics. Compare quiet and phone-connected workloads. Record
   maxima and per-operation distributions, not only averages.
2. Split AP timing into flash-mutex wait, mailbox send/completion, and response
   acknowledgement. Current flash-profile timing excludes some outer lock
   acquisition and cannot identify CP-internal work.
3. In a separately verified CP diagnostic build/hook, count actual status-register
   changes and time unprotect, program, reprotect, plus IPC service wait. Keep
   results buffered and report after the transaction; serial logging inside
   critical sections would contaminate timing. Preserve the stock ABI and verify
   exact hook targets. Do not blindly patch a different CP image.
4. Measure rewrite record count, bytes, read/write counts and record-stage times
   for shell and notification files separately. This determines whether per-write
   cost, record search scaling, or both dominate, and exposes a useful progress
   boundary without adding a periodic diagnostic task.
5. Choose the smallest fix supported by those results. Prefer T5-local flash/CP
   improvements. Safe batching must preserve write-complete ordering, synchronous
   visibility, source-buffer lifetime, error propagation and reset recovery.
   A generic rewrite change requires evidence that no scoped fix is sufficient.

Do not use a larger global grace period or unconditional flash-level watchdog
feeding as a performance fix. If bounded per-record progress feeding is required
for a legitimate long operation, justify it separately: a stuck record must
still expire, and successful partial I/O is not proof a whole handler is healthy.
No new cache is proposed.

Acceptance requires the same animated FdF + phone-sync trigger completing with
bounded rewrite time, no watchdog resets, verified saved settings after reset,
and no loss of atomic replacement behavior. Then revalidate with diagnostics
disabled. The current host regressions and native kernel/watchdog tests pass;
that validates the candidate logic, not hardware stability.
