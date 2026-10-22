# Unhygienix flash SPI rewrite handoff

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](current-status.md). Dated results below retain their original evidence limits.

Date: 2026-10-11  
Status: the unsupported-owner diagnostic now probes at most once per CP boot;
its finite hardware test completed 1920 controller writes with no unexpected reset
and operator-reported stability. The old sealed controller AP independently
reproduced a BG watchdog reset and predates committed recovery fixes. The updated
controller-only rollback pair uses the unchanged sealed CP plus AP raw 7f5c9058...
(build 54b161d1...), saved in `build/traces/flash-owner-compat-fix-20261011/controller-baseline/`.
BG stalls remain unresolved. A controlled RAM UDF now produced a complete,
checksum-valid hardware exception record with the exact expected PC and status;
see `build/traces/flash-fault-probe-20261011/`. The controller pair was restored
and the operator confirmed usability. Exclusive-master routing remains unqualified.
The subsequent stricter frame-span check is host-tested and built, but unflashed. SPI admission and volatile packaging stay blocked. See
[implementation and results](flash_spi_rewrite_results.md).

## Paused by operator — 2026-10-11

Bypass development and hardware qualification are paused. Preserve the complete
experiment on `codex/unhygienix-flash-bypass-paused`; active development returns
to `tuya` at controller source base `c0327d426` plus documentation only.
Do not resume tests or deploy diagnostic candidates without a new instruction.
Read [the pause decision and tradeoffs](flash_spi_bypass_paused.md) first.
The implementation sequence below is retained as a historical/resumption plan,
not an instruction to continue. On `tuya`, implementation file references refer
to the experiment branch; the historical audit predating that branch is retained.

## Objective

Replace the experimental flash SPI bypass with a board-local implementation
whose AP/CP ownership, wire transactions, readback and failure handling can be
verified. Then determine whether volatile protection improves real settings
writes against the controller baseline. Preserve shared/common Pebble source.

The user permits a ground-up driver rewrite. This plan scopes that rewrite to
the flash bypass and the T5AI ownership support it requires. Normal programming
and erasing should continue through the stock flash controller initially.

## Starting state (historical)

- Production repository: `/home/baz/coredevices/pebbleos`.
- Reference workspace: `/home/baz/Documents/ChatGPT/pebble`. Do not edit its
  sources. The pinned vendor submodule in production is also reference material.
- The watch was restored to the controller-based diagnostic CP baseline.
  Raw SHA256:
  `63e6a7f37acfa2cfd9ad827e0fee61199c54c611538f6e4f12e60ef16c6b7e04`.
- Sealed rollback artifacts, relative to production:
  `build/traces/flash-guard-20261011/cp-diagnostic/pebbleos.t5ai-cp.bin` and
  `pebbleos.t5ai-cp-crc.bin` in that directory. Protected SHA256:
  `951f1284435cbe48fbe4900c1311d42d1d6311332c562201a626f90136afa200`.
- `install_flash_guard(..., volatile_status=True)` currently rejects packaging
  because AP ownership is missing. Keep this restriction until the replacement
  protocol and its image compatibility checks are implemented and verified.
- Six CP tests and eight flash/mailbox integration tests passed. These establish
  software contracts, not reliable SPI signalling.
- No serial capture remains running. The latest baseline health capture received
  11,404 bytes over 90 seconds and included a BG watchdog stall and recovery.
  It was not a matched full-sync benchmark.
- Preserve existing uncommitted work. Inspect status/diffs before changing files;
  do not reset the repository or silently overwrite earlier diagnostics.

Audit and preserved evidence:

- [SPI correctness audit](flash_optimisation.md#spi-correctness-review-and-evidence-correction--2026-10-11).
- `build/traces/flash-spi-audit-20261011/`: results JSON, source snapshots, UART
  bytes/index, decoded trace, tests, rollback log and offline disassembly.
- `build/traces/flash-spi-recovery-final-20261011/`: earlier experimental run.
  Its timings are historical observations, not a qualified performance result.

## Facts to retain

1. AP notification channel `0x4b` currently only turns REQ into ACK. It does not
   park the AP. AP application code executes from flash. CP-local IRQ masking
   and RAM code do not prove exclusive flash ownership.
2. AP/XIP interference remains a hypothesis. The audit did not prove how the
   silicon handles AP cache misses during SPI routing, or identify one measured
   cause of every ignored command. The earlier premature-resume explanation
   was withdrawn because the AP handler does not pause/resume execution.
3. Stock Tuya normal writes use the dedicated flash controller. Its exceptional
   SPI bypass is not an equivalent failure-rate baseline for our settings writes.
4. The stock bypass has faulty TX-enable cleanup, saves/restores the DATA FIFO
   port, and can read RX twice after one readiness check. Replace the diagnostic
   reader too; do not use it as the independent correctness oracle.
5. Divider 7 implies approximately 1.857 MHz under the pinned vendor HAL's
   active formula, not 3.25 MHz. Wire frequency remains unmeasured. CTRL bit 30
   is used by the vendor RX sample-edge helper despite its reserved field name.
6. BG starvation/stalls occur on the controller baseline too. Track them during
   qualification, but do not attribute all such events to SPI.

## Scope and implementation locations

| Area | Existing location / intended change |
| --- | --- |
| CP transaction engine | Rewrite `boards/unhygienix/tuya/cp/flash_spi.c`; use named registers and one TX/RX engine |
| CP integration | Review `flash_guard.c`, `flash_guard.S`, `flash_volatile.c` in the same directory; separate ownership from timing hooks |
| AP ownership | T5AI-local code beside `fw/drivers/t5ai/cp_mailbox.c`, with required RAM placement in `soc/t5ai/` |
| Image contract | `tools/unhygienix_cp_flash.py`, `tools/unhygienix_cp.py`, `boards/unhygienix/firmware.cmake` |
| Tests | `tools/tests/test_unhygienix_cp_{spi,flash,volatile}.py`, `test_tuya_flash.py`, plus ownership protocol tests |

Do not rewrite the generic vendor SPI/DMA subsystem, unrelated board drivers,
or display QSPI. The display block at `0x46040000` is separate from flash bypass
SPI0 at `0x44870000`. Do not change shared scheduling, settings storage or common
flash APIs to make this experiment work. If a shared change becomes unavoidable,
record why a scoped solution fails and supply measured outcomes before adopting it.

## Required design contracts

### AP/CP ownership

Use a versioned protocol with a transaction generation and explicit states such
as IDLE, REQUESTED, PARKED, RESTORED and COMPLETE. This is a design requirement,
not a fixed wire layout; choose the layout after inspecting mailbox ACK ordering,
cache policy and available shared RAM.

The critical ordering must be:

1. CP serializes flash/SPI access and requests ownership while normal flash
   routing is intact.
2. AP excludes flash-dependent execution and readers, enters a RAM-only wait,
   then publishes PARKED for this generation. CP must not switch the mux before
   observing this state with the required barriers/cache maintenance.
3. CP disables relevant local interference and runs the complete transfer from
   RAM, including code, literals, buffers and failure cleanup.
4. CP ends CS, restores flash routing and controller readiness, then publishes
   RESTORED. AP observes that state before returning to XIP or restoring its
   normal interrupt state.
5. Both sides complete the generation before permitting another transaction.

The existing compatibility ACK is not PARKED. In particular, inspect whether
physical mailbox ACK is sent only after the receive callback returns: a callback
that spins before the ACK is emitted would deadlock. The parked AP must not need
another mailbox interrupt to receive release. Do not implement the handshake by
blocking in the existing callback without resolving this ordering.

Use an explicitly reserved, appropriately aligned shared RAM area with documented
ownership and cache rules. Do not assume `volatile` makes shared state coherent.
Do not casually reuse the existing trace reservation; its space is tightly bounded.
Reject stale generations and unsupported peers. Legacy AP/CP image combinations
must keep bypass disabled rather than interpret an ACK as exclusive ownership.

Specify every timeout and failure transition. An AP timeout must not simply return
to flash while CP might still own the pins. Recovery must establish restored routing
or enter a RAM-safe reset/fail-stop path. Account for NMI/fault handlers, watchdogs,
DMA and other bus masters that can access flash; IRQ masking alone is insufficient.
Allow nested callers only through a defined owner/token policy; do not send nested
legacy START/END notifications as an ownership mechanism.

### Transaction engine

- One bounded RAM-resident engine supports explicit frames and TX/RX byte counts.
  Document dummy bytes and discarded command-response bytes for reads.
- Define CS assertion/release and inter-frame timing explicitly. Preload TX before
  enabling transfer. Never restore DATA as configuration or pop RX twice for one
  available byte. Reject unsupported lengths and stage or validate RAM-only buffers.
- Distinguish bytes queued, bytes received, peripheral completion and device
  acceptance. Use FIFO errors, exact accounting and an AON deadline; do not infer
  pin idle solely from TX_FINISH without evidence.
- Establish exclusive SPI ownership before resetting/changing configuration.
  Preserve unrelated clock/register bits and define cleanup for every error exit.
- Restore routing before any XIP access. Keep logs outside the parked/mux interval;
  collect bounded counters and emit them after release.
- Treat the original ABI as a compatibility requirement: enumerate all live callers
  before replacing entry points. Reject unsupported uses explicitly or retain them
  on a safe path. Do not silently narrow an OTP/security API to a two-byte writer.

### Volatile status integration

Gate on verified GD25WQ64E JEDEC `c8 65 17` and BP-only changes. Preserve QE and
all other status fields. Keep `50h` and the following WRSR frames in one ownership
interval with no intervening flash command. Verify physical status after restoring
routing and before permitting normal programming; publish the cached status only
once physical verification succeeds. Require expected BP, WIP clear and WEL clear.

Initially preserve stock controller programming and protection restoration.
Never hide acceptance failures with repeated SPI retries. If retaining the existing
bounded stock-controller fallback, count every use as a failed bypass acceptance
for qualification. Reject partial/unexpected status changes and failed restoration.
Consult the [manufacturer datasheet](https://download.gigadevice.com/Datasheet/DS-00476-GD25WQ64E-Rev1.2.pdf)
for command framing, voltage-dependent timing and status semantics.

## Implementation sequence and gates

### 1. Establish reproducible inputs and memory budget

Record repository diff, source hashes, exact AP/CP images, dictionaries and toolchain.
Reproduce the sealed controller CP hash before changing transport behavior. Inspect
pinned source and disassembly; enumerate bypass callers and every RAM dependency.

Current patching overwrites only the writer allocation beginning `0x08066594`,
with a RAM body at `0x080665a0` limited to `0x238` bytes. Do not assume the new
TX/RX engine fits there. Design and prove a new RAM placement if necessary;
preserve neighboring code, startup copy semantics and stack headroom. Maintain
stock image/ELF hash gates, expected instruction bytes and protected CRC checks.

**Exit:** reproducible baseline, documented memory/caller map, and rollback artifacts.

### 2. Implement and qualify ownership without routing changes

Build AP/CP protocol state machines and a RAM-only AP park/release path. Exercise
normal, duplicate, stale, busy, timeout, reset and incompatible-peer cases. Measure
park latency/duration. Do not issue SPI commands yet.

Inspect the final linked images for XIP calls, veneers, literals, logging, stack
helpers and exception paths during the critical interval. Exercise under FdF and
phone activity. Prove the actual PARKED/release ordering, not just ACK counters.

**Exit:** bounded ownership works without deadlock; incompatible peers cannot
enter bypass; failure paths cannot resume AP into disconnected flash.

### 3. Implement engine and qualify read-only operations

Replace TX and RX together. Test empty/small/FIFO-boundary/maximum lengths,
AON wrap, FIFO faults, delayed RX, premature completion, timeout and cleanup.
Compile with the real ARM toolchain and inspect linked RAM reachability.

On hardware, start with JEDEC and SR1/SR2 reads, checking exact counts and comparing
with controller reads after routing restoration. Use varied bit patterns where a
safe immutable read permits them. If accessible, capture CS/SCLK/MOSI/MISO and
measure frequency, edge count, final-bit hold and CS gaps. If pins are unavailable,
record that limitation and do not label timing as measured.

**Exit:** at least 10,000 read-only transactions with zero discrepancies, FIFO errors,
timeouts or resets, and documented wire-timing evidence or its explicit limitation.
This finite test is a qualification gate, not proof of zero future failures.

### 4. Qualify latch commands and volatile status

Run at least 1,000 WREN/WRDI pairs with controller-verified WEL transitions; leave
WEL clear. Then run a small bounded set of volatile BP transitions (initial target:
32 complete unprotect/restore cycles), verifying untouched status bits and final
protection each time. Avoid turning this into an unnecessary nonvolatile endurance
test. Confirm controller program/erase behavior on a designated disposable test
region only after identifying and authorizing that region; never use live settings
or application data as an arbitrary pattern-test area.

**Exit:** zero unaccepted commands, unexpected status changes, fallbacks or resets;
protection restored after every successful cycle; fault-injected failure paths pass.

### 5. Integrate and compare the real workload

Use the same diagnostic AP configuration for controller and rewritten-bypass runs.
Record the ownership capability/version in both image manifests. Only now replace
the unconditional packaging rejection with strict paired-image/capability checks;
retain default-off operation until qualification completes. Provide a controlled
qualification build path after stages 2–3, not a blanket bypass of the safety gate.

Run at least three paired controller/bypass full phone syncs with FdF animating.
Match starting settings/data and logging levels as closely as possible; record
unavoidable differences and report per-operation cost as well as workload totals.
Ask the operator to confirm sync start/finish only after boot and live capture are
verified. Preserve raw bytes, timing index, decoded logs and exact build artifacts.

Report counts/bytes, full-sync and settings-rewrite duration, write phase means and
maxima (percentiles only if individual samples exist), IRQ-off and AP-park maxima,
command acceptance failures, fallback count, resets and BG watchdog events. Use
AON units and documented conversion consistently. Do not compare unlike windows
or count only successful transactions. A performance win must exceed observed run
variation without a correctness regression; otherwise keep the controller default.

**Exit:** a measured comparison and an explicit enable/keep-disabled decision.
Any failure returns to the sealed controller baseline with evidence preserved.

## Hardware and rollback procedure

The current controller rollback pair is
`build/traces/flash-owner-compat-fix-20261011/controller-baseline/manifest.json`:
unchanged sealed CP raw `63e6a7f...`, current controller-only AP raw `7f5c9058...`.
The original AP raw `80db6fab...` is retained as failure evidence: it independently
reproduced a BG watchdog reset and predates committed recovery fixes. Use the
current pair for subsequent recovery; preserve both generations and their logs.


Use host hardware access when sandbox device nodes are absent. Re-enumerate both
UARTs each session; previously verified identifiers are:

- Programming: `/dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00`.
- Logging: `/dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if02`, 460800 baud.

Device presence is not proof of live capture: verify received bytes and their
continued arrival. Use the CP-only flasher `tools/flash_unhygienix_cp_firmware.py`
with the sealed raw/protected pair and the verified programming port. Preserve
its hash/range report; AP begins at physical `0x132000`. An ownership-capable AP
will also need a separately validated AP image/update; CP-only flashing cannot
introduce the missing AP behavior. Verify its compatibility with rollback CP.

Stop exact capture processes when finished, state which images remain flashed,
and leave the watch on the controller baseline if qualification is incomplete.

## Deliverables for the next handoff

1. Scoped source changes and a short ownership protocol specification, including
   timeout/reset behavior, compatibility, RAM/cache map and measured park bounds.
2. TX/RX API contract, final ARM disassembly/RAM dependency checks and meaningful
   protocol, FIFO, status and binary-packaging tests.
3. Sealed controller and candidate images/manifests, firmware dictionaries, source
   hashes, raw captures and any logic-analyzer traces.
4. Results for each stage, matched benchmark table, unresolved limits and the
   reason for enabling the rewrite or retaining the controller path.
5. Updated [flash audit](flash_optimisation.md) and a concise record of the exact
   AP/CP images left on the watch. No safety or speed claim based on host mocks alone.
