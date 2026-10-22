# Flash SPI rewrite: implementation and qualification record

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](current-status.md). Dated results below retain their original evidence limits.

Date: 2026-10-11. **Paused by operator; incomplete qualification; controller baseline retained.**

See [pause decision, branch locations and tradeoffs](flash_spi_bypass_paused.md).
Implementation references describe `codex/unhygienix-flash-bypass-paused`,
not the active `tuya` source tree.

The versioned AP/CP dry ownership path and a replacement unified RAM TX/RX
engine are implemented. Hardware exercised dry ownership only. SPI mode is
rejected by both production peers and packaging. The rewrite is not qualified
for flash routing, volatile status changes, or production use.

The remaining prerequisite is a verified exclusion/drain contract for CPU2,
both GDMA units, other autonomous memory readers, and competing SPI users. IRQ
masking and parking AP execution do not establish that contract. Consequently
no read-only SPI hardware gate, latch/status gate, or matched benchmark was run.
There is no performance result or evidence that AP/XIP interference caused the
previous command failures.

## Source and evidence

Implementation started after the operator committed the existing work at
`c0327d426`. All changes are under the Unhygienix board, T5AI driver/include,
board tools, tests, and board documentation. Vendor and shared Pebble sources
were preserved.

Evidence is under production `build/traces/flash-spi-rewrite-20261011/`:

- `starting-*`: original diff, source snapshots and hashes before the operator's
  commit. `rollback-ap/` and `rollback-cp/`: sealed original images.
- `reproduced-controller/` and `controller-final/`: controller CP reproduced
  before and after implementation. Both raw images match the sealed baseline.
- `ap-owner-dry/`, `cp-owner-dry/`, `cp-owner-hold/`: exact hardware candidates,
  dictionaries, manifests, ELF, linker inputs, and source hashes.
- `ap-owner-final/`, `cp-owner-final/`: final source build, sealed separately
  from hardware candidates. Formatting and validation updates are not labelled
  as another hardware qualification run.
- `engine-final-offline/`: final ARM ELF, linker script, shims, RAM disassembly
  and section sizes. `stock-cp-symbols.txt`, `stock-bypass-callers.dis`: pinned
  caller audit. `bus-master-inputs.json`: inventory source hashes.
- `*-uart.bin`, `*-index.jsonl`, `*-trace.log`: received bytes, host UTC byte
  offset index, and decoded AP trace. CP `T5OWNER`/`T5FLASH` messages are unframed
  ASCII in the raw bytes; they are not fully represented by the AP decoder.
- `capture-results.json`, `analyse.py`: parsed counters with raw offsets and
  host receive bounds; full raw captures remain authoritative. Serial reopen
  intervals limit claims about events not observed.
- `flash-*.log`, `restore-*.log`: exact image hashes, physical ranges and flasher
  completion. `software-tests-final.log`, `ruff-final.log`, build logs,
  `final-hashes.json`, `source-final/`: validation and reproducibility inputs.

The original controller CP raw SHA256 is
`63e6a7f37acfa2cfd9ad827e0fee61199c54c611538f6e4f12e60ef16c6b7e04`;
protected SHA256 is
`951f1284435cbe48fbe4900c1311d42d1d6311332c562201a626f90136afa200`.
The restored AP raw SHA256 is
`80db6fab2f56feb2a7e7f4110e9c7dcea715ae3a34c140092188c38ff6a6156f`;
protected SHA256 is
`721bcef53be849f77e149674983be46a855a4c5c2264542b3161bf92f784fac4`.

## Ownership protocol v1

`CONFIG_T5AI_FLASH_OWNERSHIP` defaults off. Enabling it requires the pinned stock
CP ELF for CP packaging. `--ownership-ap` verifies the exact linked AP ELF/raw/
protected/map/dictionary pair and RAM reservation before producing a dry CP.
The manifest records both image hashes, source hashes, protocol version,
address and dry-only capability. Changing a manifest does not enable SPI.

The AP owns a 512-aligned 1024-byte kernel SRAM reservation, exported as
`t5ai_flash_owner`. The AP writes bytes 0–31; CP writes bytes 32–63. Separate
32-byte cache lines prevent one CPU's clean from overwriting the other's state.
Bytes 64–511 are CP-reserved; the later dry bus diagnostic uses 432 bytes there. Bytes 512–1023 reserve a future CP RAM vector table;
it is unused in dry mode. AP has a separate 512-aligned 80-word RAM exception
table. The measured AP uses owner address `0x28012200`, AP vectors `0x28012000`,
park code `0x28010100`, and fail-stop code `0x280100a8`. Addresses are checked
against each linked image, not assumed by CP packaging.

CP local serialization, generation, nonce and failure count occupy exactly the
final 16 bytes (`0x280673f0..0x28067400`) of the existing zeroed 160-byte IRAM
tail. The existing diagnostic state retains its original 144 bytes. Neither
reservation, neighboring code nor startup RAM limit is extended.

Channel `0x4b` uses CAPS `0x464f0101` and REQUEST `0x464f0102`, with a fresh
nonzero nonce and exact owner address. Legacy REQ/ACK remains an ACK-only path.
CP rejects legacy/incompatible peers without writing their RAM. In the dry
qualification CP, that failure also refuses the pending controller write;
this build must be deployed with its paired AP. The default controller CP
retains its original write behavior. The versioned
magic is `0x464f5701`; only mode 1 (dry) is accepted. Mode 2 (SPI) is rejected.
CP rejects ISR, PRIMASK/BASEPRI-masked and nested calls. Generations increase
without wrapping; saturation refuses further ownership.

Ordering:

1. CP serializes, checks AP capability, and publishes its REQUESTED generation,
   nonce/token, mode and AON request time with a cache clean and barriers.
2. AP's callback validates it and returns. The physical ACK is published before
   `t5ai_flash_owner_after_ack` enters the RAM routine. Deferred busy ACKs use the
   same post-publication hook. Synchronous channel requests cannot park AP.
3. AP masks IRQs, installs RAM exception vectors, then publishes PARKED for the
   generation. CP's RAM dry routine observes PARKED through cache invalidation.
4. CP leaves routing unchanged, optionally holds, then publishes RESTORED.
   Release uses shared RAM and requires no mailbox interrupt on parked AP.
5. AP observes the matching generation and restored route/controller readiness,
   updates counters, publishes COMPLETE, restores vectors and IRQ state, and
   resumes XIP. CP observes COMPLETE before unlocking/reusing state.

The AP and protocol waits have a 3200-AON-tick (100 ms at the existing 32 kHz
conversion) deadline, using unsigned differences across wrap. A changed
in-flight generation, unexpected state, timeout, still-routed SPI or busy flash
controller enters RAM fail-stop, never an unconditional return to XIP. RAM
NMI/fault handlers request whole-chip watchdog reset with cause `0x7d`. CP uses
the existing ITCM whole-chip reset path (`0x7e`) if an accepted request cannot
complete. A request send failure publishes CANCELLED and resets instead of
recycling a generation whose AP park could still be pending. A pre-request
capability failure returns unsupported while routing is intact. Reset/fault
transitions were injected in host tests, not deliberately induced on hardware.

Cache clean/invalidate primitives use DCCMVAC/DCIMVAC with DSB/ISB. AP discharges
startup-zeroed CP/reserved lines before CP becomes their writer. Logs occur only
after COMPLETE. AP RAM source is built without LTO, floating-point registers,
or stack protector; final linked branches and literals are audited. The
protocol does not claim to stop DMA or CPU2 in its current dry-only form.

## Unified engine contract and RAM budget

`PebbleSPIFrame` describes TX bytes, explicit `ff` dummy bytes, and `ff` clocks
for RX. TX/dummy response bytes are discarded, but every clocked byte is drained
once. Each frame has 1–4095 total bytes; an interval accepts up to two frames.
Buffers, descriptors, results and shared state must be internal SRAM or DTCM.
Empty, oversized and invalid buffers are rejected before changing hardware.

The engine preloads up to 16 bytes before enabling equal full-duplex TX/RX
counts. It distinguishes queued bytes, received bytes, completed frames, FIFO
errors and peripheral finish flags. Premature completion, excess RX, missing
completion and deadline expiration return errors after cleanup. A 1600-tick
(50 ms) transfer deadline leaves margin before AP's fail-stop deadline. The
existing controller-ready ITCM wait can also fail-stop; AP never resumes simply
because CP returned an error.

Each frame disables both enables and CS on exit. FIFO reset, inter-frame gap
and final-bit hold each use a conservative two-tick delay. These are programmed
bounds, **not measured pin timing**. CS/SCLK/MOSI/MISO were not captured. CTRL
uses vendor mode 0, divider 7 and sample-edge bit 30; estimated SCLK is
26 MHz/(2*7), approximately 1.857 MHz. Actual frequency remains unmeasured.
Configuration/clock/mux bits are saved and restored; DATA is never restored.
Routing is restored and the controller is ready before RESTORED is published.
The engine initializes every CP RAM exception entry to the ITCM fail-stop
handler before changing VTOR. Exclusion of other bus masters/users, SPI-mode
ownership admission, and integration of a qualified engine caller remain required before enabling it.
The later offline volatile adapter replaces legacy bypass use, but its owned
transport admission remains an explicit rejection and is not qualified.

| Final offline section | Address | Bytes | Available |
| --- | --- | ---: | ---: |
| Existing ITCM guard and wait/reset helpers | `0x00003ea0` | 332 | 352 |
| Unified frame, RAM-range and validation helpers | `0x080665a0` | 484 | 568 |
| Engine/configuration/cleanup support | `0x080667e0` | 624 | 632 |

The linked engine uses 64 stack bytes itself, with a maximum additional
32-byte frame helper (96 bytes for that nested path, excluding its future
caller and exception stacking). AP park uses 24 bytes. The pinned flash-server
thread allocates 1536 bytes; its eventual SPI stack high-water remains
unmeasured. Final qualification AP RAM use is 98240/126976 bytes, with kernel
heap bounds `0x28027fc0..0x2802f000`; default-off RAM use is 96704 bytes.

The engine fits inside the two existing stock bypass function allocations;
neighboring veneers beginning `0x08066a58` are preserved. The unified engine support
section remains offline-only. The later dry diagnostic installs its read-only bus
sampler in that allocation alongside the RAM release routine, and redirects both legacy bypass entries
to explicit unsupported (`-95`). No original OTP/security ABI is silently
converted to a two-byte status writer. Default controller packaging preserves
both stock entries unchanged.

Pinned source callers include the status reader/writer and OTP read, program,
erase/lock operations in `flash_bypass.c`, reached through the flash-server OTP
IPC operation. Deep-sleep writer calls in `flash_driver.c` are conditional on
BK7236 and must not be assumed live in this BK7258 image. The binary caller
record includes veneers and distinguishes source possibilities from linked
call sites. The stock reader is not used as a correctness oracle.

## Hardware observations and gate decisions

Both host CH342 UARTs were re-enumerated. Actual received bytes were checked
before claims of a live capture. AP boot/dictionary identity and phone session
establishment were observed. The operator confirmed FdF animation and watch
responsiveness during both dry runs.

| Gate/run | Result |
| --- | --- |
| 1: baseline reproducibility | Exact sealed controller CP reproduced before and after changes; rollback pair sealed |
| 2: immediate dry release | At least 1360 COMPLETE generations, zero reported CP ownership failures; maximum AP park 13 ticks (0.40625 ms) |
| 2: bounded dry release | First 32 generations exercise 0/10/20/30 ms holds; at least 1216 COMPLETE generations, zero reported failures; maximum AP park 961 ticks (30.03125 ms) |
| Request to CP observation of PARKED | Maximum 11 ticks (0.34375 ms) in the bounded run; includes mailbox/scheduling/observation latency |
| 2: exceptions, stale/busy/incompatible/reset cases | Host fault tests pass; reset behavior not deliberately hardware-injected; exclusive bus-master ownership remains unproved |
| 3: 10,000 read-only SPI transactions | Not run; routing prerequisite incomplete |
| 4: 1000 WREN/WRDI pairs, 32 volatile BP cycles | Not run; protection policy unchanged |
| 5: three paired full sync benchmarks | Not run; no speed or acceptance claim |

The 0–30 ms holds occur in the first 32 generations during boot activity;
phone sync/FdF observations later in that run cover immediate releases. Longer
parks specifically during an already active phone/FdF workload remain untested.

The last counters are sampled periodic reports, not an exact count of every
operation at capture end. All captured reports have zero CP ownership failures.
No additional AP boot record was observed within either post-flash dry window.
The captures include commanded programming resets and serial reopen intervals.
These observations validate exercised RAM park/release behavior only.

The 45-second pre-change baseline capture received 6868 bytes and contained no
BG stall warning; it was not a matched sync. The immediate capture received
44609 bytes and the bounded capture 40836 bytes. BG watchdog warnings/recoveries
occurred in both dry captures. The bounded capture also contains warnings from
the preceding image before its commanded flash. The restored sealed controller
recorded BG stalls during its phone activity too. This confirms the stall can
occur without rewritten ownership or SPI. It does not establish equal stall
rates or exonerate any new code from all scheduling effects. The raw and decoded
first-failure evidence is retained. No scheduler/shared-code change was made.

The next routing prerequisite needs a fail-closed, race-free way to prevent new
flash reads and drain existing ones from all enabled masters. Pinned register
sources identify CPU2 controls at SYS `0x44010018`, GDMA units at `0x45020000`
and `0x45030000`, plus DMA2D, display, JPEG, H264, scalers and USB. Their address
capability and actual enabled configuration must be audited; this inventory
alone does not assert that each currently reads flash. Sampling DMA enable bits
or merely checking that CPU2 looks idle would not prevent later submissions.
No undocumented whole-system clock gate was introduced to bypass this proof.

## Watch left on the baseline

The exact sealed controller CP was flashed first, then the sealed diagnostic
AP. Flasher logs confirm success and partition bounds. Restored UART contains
AP boot reset sources `AP=0 CP=0`, phone reconnection and a settings sync start,
followed by continued received data. The baseline AP/CP hashes above are the
images left on the watch. Capture completion and final received-byte count are
sealed in `restore-live-capture-finished.json`; no monitoring process is left
running. SPI remains default-off and packaging remains blocked.

Validation: 19 ownership, FIFO/cleanup, ARM reachability, binary packaging,
volatile-policy and integration tests passed, including real paired-AP fixture
checks. Separate ownership-on and default-off AP builds and image map checks
passed. Ruff passed. Installed clang-format 21 was used with unknown-key
warnings: the repository's two newer braced-list options are unsupported by
that version; the warnings are preserved in `format-c.log`. No repository style
configuration or vendor source was changed.


## Extended dry ownership and bus audit — 2026-10-11

Evidence: `build/traces/flash-spi-bus-audit-20261011/`. The AP is the exact
previously sealed `ap-owner-final/` image. New dry CP raw SHA256 is
`cb86a297b0deab73a3dc95e14902da50773de0eefa3bd29fbb9f2ac13bdfb1ef`;
protected SHA256 is
`a659c81d7520fb9bc126c119c42d62a9278d3b9f89413ac37182d5b7d26bcfe9`.
Both old bypass entry points still return unsupported and no SPI routing or
status commands were enabled.

CP now masks its own IRQs after observing PARKED. A bounded 32-handoff sequence
starts 960000 AON ticks after the first handoff, rotating 0/320/640/960-tick
holds. This moves long parks out of early boot. A read-only RAM sampler records
CPU2 controls/status, clocks, power, memory-domain state, SPI0 CFG and all 16
GDMA channel control/source/destination/loop/request registers. Powered-down
backup/DMA domains are skipped. It uses 432 bytes of the existing CP reservation,
without moving either AP state or CP vectors. It never grants routing permission.
Snapshots and logs occur on opposite sides of COMPLETE; no logging runs in the
critical interval. Dry RAM allocations are 232 bytes at `0x080665a0` and 240 bytes
at `0x080667e0`; ITCM remains 332 bytes. Actual packaged disassembly is retained.

The capture received 59442 bytes over approximately 352 seconds, including the
commanded AP/CP programming intervals. Last sampled report: 1712 COMPLETE
handoffs, failures 0, maximum AP park 960 ticks (30 ms), and maximum request to
CP observation of PARKED 16 ticks (0.5 ms). All 32 delayed holds completed. UART
receive timestamps place the nonzero holds around 17:45:17–17:45:19 UTC, alongside
phone encryption and session setup. Receive bounds are not wire timestamps and
AP logs can buffer. The operator reported FdF animating, phone syncing and watch
responsive; the reply arrived after CP rollback had begun and does not itself
timestamp an individual long park.

All 38 bus snapshots report CPU2 powered down (`status=0x00007420`,
`CPU2 control=0x0000001a`) and SPI0 CFG zero. No sampled enabled GDMA channel was
reported. Primary clocks are `0x87188491` or `0x871884b1`; secondary clocks zero;
power `0x200000c0` or `0x204000c0`. BTDM, XVR and MAC clocks remain enabled;
video power-down is asserted. These samples narrow the audit but are not an
exclusion/drain contract. General DMA inactivity at sampled instants does not
prove that no access was in flight between samples.

The pinned PPC inventory explicitly includes `BTDM_M` and `MAC_M`, in addition
to GDMA and the other multimedia/USB masters. The linked radio initialization
sets a BLE exchange-memory base of `0x2807f480` at `0x4a01001c`, and inspected BLE
link fields address internal SRAM. This is useful evidence, but does not yet
prove every radio master path remains confined to that SRAM or is drained before
mux changes. The stock flash notification only coordinates CPU1. The vendor BLE
sleep example is scheduling advice, not an atomic master-exclusion protocol; it
even permits an erase after a timeout. PPC/MPC security attribution is not a
proven drain operation and was not changed. The public Beken
[security overview](https://docs.riselink.ai/arminodoc/bk_idk/bk7258/en/v2.0.1/developer-guide/security/bk_security_overview.html)
also describes separate AHB masters and security filtering, without establishing
this missing handoff contract.

**Remaining blocker:** establish a race-free, RAM-safe admission check or scoped
quiesce/restore protocol for all flash-capable masters, including the autonomous
radio paths. It must prevent new requests and drain outstanding requests, or
prove their full address ranges cannot reach flash. CPU2 power-down and AP/CP
IRQ masking alone do not establish this. No new assumption that the radio does
access flash is being made. Read-only SPI, latch/status and matched benchmark
gates remain unrun; this dry run provides no bypass acceptance or speed result.

The trace contains three KernelBG watchdog warning/recovery episodes. The first
reported PC resolves to `pbl_irq_unlock` and LR to `sched_block`; that sample is
not a root-cause diagnosis. An AppFetch put-bytes sequencing error is retained,
followed by successful fetch cleanup. These observations remain separate from
SPI, which was never enabled. No additional AP reset record was observed after
the commanded candidate CP programming reset, subject to serial reopen gaps.

All 22 software tests pass, including actual dry-state/fault injection, AON wrap,
bounded hold count, powered-down register avoidance, exact DMA channel addressing,
RAM branch/literal audit and real binary packaging that preserves every neighboring
IRAM byte. Ruff passes. Default CP packaging still reproduces the sealed baseline
raw/protected hashes exactly. `bus-results.json` records raw offsets and receive
bounds; candidate ELF, dictionaries, source hashes, raw bytes, disassembly and
input hashes are preserved. The initial empty rollback capture failed because of
a dictionary filename mismatch and is retained separately; `restored-v2-*` uses
the correct baseline dictionary. The exact sealed controller CP and AP are
restored before ending this run.

Rollback UART received 34287 bytes and contains the final baseline AP boot,
phone session reopening and settings sync start. KernelBG watchdog warnings and
recoveries also recur after the baseline AP restore; they are retained in
`restored-results.json`. All exact capture processes were stopped at run end.


## Radio audit and failed diagnostic — 2026-10-11

Evidence: `build/traces/flash-spi-radio-audit-20261011/`. Full DWARF from the
hash-pinned stock CP ELF exposes radio register helper names and linked ranges
although the private radio headers are absent. Saved disassembly shows BLE
initialization writing exchange-memory base `0x2807f480`, and AES copying input
to SRAM `0x2807f5b0` before programming exchange-memory offset `0x4c`. Inspected
BLE descriptor fields use offsets. This narrows possible address confinement;
it does not prove every radio DMA path is confined or that MAC requests drain.
MAC current/next state and DMA-status reads were also located in the binary.
The public Beken SDK reference examined was release/v2.0.1 commit
`650e754e12fe1e43c37ce2316a973668b033fd48`; it did not supply the missing complete
radio access/quiesce contract. No vendor source was modified.

A dry diagnostic added four reads: BLE exchange-memory base `0x4a01001c`, MAC
state `0x49100038`, and MAC DMA status `0x49108188`/`0x4910818c`, guarded by the
sampled power-down and clock bits. The reserved snapshot increased from 432 to
448 bytes, retaining its boundary and 14 cache cleans. Host domain tests and
ARM reachability passed, but these guards were **not sufficient hardware
qualification for safe register access**. RAM sections were 232-byte dry release,
332-byte sampler and 332-byte ITCM guard. SPI remained disabled throughout.

Failed CP raw SHA256:
`479a975d7bea547d455266c2632c6267146b0cb08c2623beeab00c40befe5d69`;
protected SHA256:
`07919e77155ad1d5dac4f2c9e2138b525347f5a9447c6801b46abad6633d5d4d`.
Its AP was the sealed ownership AP identified earlier. After the commanded CP
programming boot at watch time 18:59:39, there were 22 unexpected boot records
from 18:59:48 through 19:02:46, before rollback started at 19:02:49. No
`T5OWNER`, `T5BUS`, or `T5RADIO` report was received. The capture received 68654
bytes including programming and CP rollback. All captured decoded reset-source
reports were `AP=0 CP=0`. The raw stream contains unknown-dangerous-reboot
warnings, some mixed with unframed CP text and absent from the live AP decoder.
An initial commentary misidentified raw `0x7d` as the RAM fail-stop reset cause;
it is the log source line number 125. That diagnosis is withdrawn. Neither an
AP fail-stop trigger nor the exact failing radio read is established.

This diagnostic fails the ownership hardware gate. Radio MMIO stalls/faults or
other diagnostic effects remain hypotheses; no SPI command, flash mux change,
or volatile status write was enabled. The four changed source/test files were
restored byte-for-byte to their pre-probe snapshots. `failed-source/`, the exact
candidate images/manifests/ELF, RAM disassembly, DWARF extraction, raw UART,
receive index and flasher logs preserve the failure. Do not redeploy this failed
probe or infer a safe radio access condition from its host tests.

The immediate blocker is safe, diagnosable access to the radio state (or another
verified way to prove address confinement/quiescence). Before another hardware
probe, establish the complete clock/reset/power/access prerequisites and arrange
RAM-safe stage/fault evidence that survives reset. Then finish the race-free
master admission/exclusion contract. Merely extending timeouts or accepting idle
samples would not satisfy it. Read-only SPI qualification, latch/status cycles,
volatile integration and matched benchmarks remain unrun.

The sealed controller CP and AP were restored, with successful exact-range
flasher logs and hashes. A separate 150-second recovery capture received 18213
bytes, one baseline AP boot, encrypted bond restoration, protocol session open
and full settings sync start. It also recorded three BG watchdog warnings and
two recoveries; the capture ends without establishing the final warning's
outcome. These are separate baseline observations, not a SPI diagnosis.
All capture processes were stopped. Both the failed-probe tests and the restored
source tests passed 22 cases; software tests do not establish MMIO accessibility.
SPI packaging remains blocked and the controller remains the deployed default.


## Offline volatile adapter rewrite — 2026-10-11

Evidence: `build/traces/flash-spi-adapter-20261011/`. No hardware image was
flashed during this step; the exact controller baseline remains on the watch.
The preceding radio failure is retained and has not been requalified.

The legacy adapter's stock bypass call and retry/fallback policy were removed.
The replacement builds two SRAM descriptors for `50h` and `01h/SR1` in one
owned transfer, checks two completed frames and exactly three queued/received
bytes, then verifies SR1/SR2 using the controller after routing restoration.
Only the pinned JEDEC identity, controller base and BP-only unprotect changes
are eligible. SRP, QE and all other status fields are preserved. No cached status
is published until physical readback matches and WIP/WEL are clear. Ignored
commands, unexpected bits, transport errors, incomplete accounting and busy
timeout all fail-stop, without retrying or falling back after an attempted
transfer. Stock controller re-protection and non-BP operations remain stock;
physical re-protection qualification is still required.

Controller preflight/readback uses the existing verified ITCM launch/wait helper.
The ownership request boundary is outside the CP IRQ-masked controller section;
a future qualified transport must serialize, await PARKED, enter the unified RAM
engine, and finish RESTORED/COMPLETE before returning to the adapter. The physical
notification ACK is not used as ownership. `pebble_spi_transport_ready()` currently
returns false and `pebble_spi_owned_transfer()` returns unsupported without
changing hardware. These are deliberate admission blocks, **not an implemented
or qualified SPI-mode ownership wrapper**. AP/CP still reject mode 2 and image
packaging still rejects volatile SPI. Completing that wrapper requires the
unresolved master exclusion/drain contract and read-only hardware gate.

The host adapter tests inject a transport contract and exercise 15872 combinations
of all nonzero BP values, SRP values and SR2 bytes. Ten attempted-transfer fault
cases cover negative returns (including unsupported after admission), result
errors, wrong byte/frame counts, unchanged protection, unexpected SR2, WEL and
persistent WIP. Delayed preflight readiness, AON wrap, wrong identity/controller,
non-BP changes, unsupported admission and stock re-protection dispatch also pass.
The actual engine host test verifies that the production transport stub refuses
without touching registers or the result. These tests establish software policy;
they do not demonstrate an end-to-end owned transfer or device acceptance.

All 22 established ownership/engine/packaging/volatile tests pass. The initial
ARM link exposed a compiler-generated `memset` for result initialization; explicit
volatile stores remove that new libc dependency. Both the failed initial link
and the passing final run are preserved. Ruff and diff checks pass. The sealed
controller CP raw and protected hashes reproduce byte-for-byte after the rewrite.
The obsolete stock-bypass symbol binding was removed from the patch linker.

Offline linked RAM sizes remain 332-byte ITCM guard, 484-byte frame/validation,
and 624-byte engine support. ARM stack-usage reports give 96 bytes for the adapter,
32 for its controller-ready helper, 8 for its fault helper, and 64 plus 32 for the
engine/frame path. The future ownership wrapper, stock helpers and exception
stacking are excluded from these individual figures; runtime stack high-water
is still unmeasured. The ELF, linker inputs, disassembly, stack reports and exact
source hashes are saved under `offline-engine/`.

Further binary review found that the MAC getter at `0x0203e0dc` checks SRAM byte
`0x28078245` before its register access, and the ITCM accessor at `0x2bd4` reads
that byte as the vendor's doze state. The failed probe omitted this check.
`rwnxl_sleep` sets the doze byte and bit 13 of `0x49000004`; `rwnxl_wakeup` clears
that bit before MAC register operations. Saved disassembly captures that ordering.
This shows that SYS clock/power bits alone omit vendor access prerequisites. It
still does not identify the failing read, prove the reset cause, establish safe
BLE register access, or prove that MAC DMA cannot wake/access flash autonomously.
No radio MMIO was reintroduced and no speculative clock/reset change was made.


## Master address-map audit — 2026-10-11

Evidence: `build/traces/flash-spi-master-map-20261011-resolved/`, with the
expanded catalog under `complete-bluetooth/`. No hardware was accessed or image
deployed in this step; the preceding restored controller pair remains the last
verified deployment. No live monitoring or new hardware qualification is claimed.

The revised DWARF extraction resolves noncontiguous inline ranges and restricts
instruction extraction to their containing executable sections. It catalogs 468
matching records and 207 unique concrete pointer/address ranges. Unlinked
zero-address debug ranges are excluded; the generic `ip_exmbaseaddr_set` is one
such range. The actual Beken setter at `0x020c0a0e` writes the SRAM EM base
`0x2807f480` to `0x4a01001c`. The previous prefix scan omitted that setter and
`em_csptr_setf`, whose scheduler path stores a halfword at `0x020be60e`.
Reviewed BLE TX/RX/data and antenna-switch pointer setters also use halfword
fields. Hardware conversion, all address paths and autonomous operation still
need proof before these findings establish flash exclusion. The catalog is
name-based and does not claim coverage of every raw register store.

The linked logic-analyzer test code programs `0x4507001c` with a heap buffer,
starts acquisition through bit 0 at `0x4507000c`, polls that bit, and clears
bit 0 at `0x45070008` before freeing its buffer. CLI/ATE are enabled in the
pinned image. This corrects the earlier assumption that the linked image had
only an LA interrupt handler. Full-address destination programming is visible;
LA bus direction, accessibility and drain semantics remain unverified. Do not
infer that LA is inactive merely because no initialization function was found.

DWARF and pinned adapter source identify MAC sleep callback offsets `0x160`
and `0x178` as power-vote and RF-vote operations. Their ordering does not supply
a verified DMA-drain primitive. `ip_address_set` is an Internet configuration
routine, not a radio DMA setter. Exact ELF identity, the extraction script,
resolved instruction ranges, adapter members and audit assertions are preserved.
The initial extraction's empty inline disassemblies are retained as a superseded
attempt and explicitly identified in the evidence README.

The failed radio probe remains a release blocker. Safe radio access with reset-
surviving fault/stage evidence and complete, race-free master confinement or
exclusion/drain are still required. SPI-mode ownership integration, read-only
SPI qualification, volatile status cycles and matched benchmarks remain unrun.


## Bounded captured-MAC TX audit — 2026-10-11

Evidence: `build/traces/flash-spi-dma-contract-20261011/`. The new board-local
`tools/unhygienix_flash_dma.py` audits an immutable RAM capture and up to six
supplied MAC TX header roots. It uses an explicit bounded traversal, follows
frame/MPDU/PBD links and validates inclusive header/PBD payload spans. It refuses
cycles, overlapping or misaligned descriptors, XIP/external-memory descriptors
or payloads, reversed spans, missing captured bytes and exhausted node budgets.
The CLI requires the exact pinned stock CP ELF hash. Its output always reports
`routing_qualified: false`; supplied roots and a static capture do not establish
hardware-root completeness, atomic capture, policy-table/RX reads, autonomous
activity, prefetched/in-flight accesses or other masters. It is an offline audit
component, not a RAM-resident admission implementation or permission to route.

The private linked `txdesc` is 124 bytes with `lmac` at offset 100 and hardware
pointer at offset 108; the public facade exposes only 72 bytes. The ITCM
initializer at `0x836` confirms the private offsets. It copies supplied segment
addresses directly into 20-byte PBDs and computes inclusive end addresses,
without checking SRAM ranges. Descriptor allocation in SRAM alone therefore
cannot establish payload confinement. The pinned public scatter/gather source
also takes addresses from pbuf payloads. This shows address capability, not an
observation of flash-backed MAC traffic during any previous failure.

`dma_push` at `0x02055478` walks descriptors through CPU `memcpy`; its name must
not be used as evidence of an additional autonomous hardware DMA engine. The
separate MAC packet engine and its hardware header registers still need audit.
Per-CU DWARF layouts preserve the public/private distinction, and exact
initializer, submission, header-programming and software-copy disassemblies are
saved with their input hashes.

All 26 ownership, engine, ARM, packaging, volatile-policy and DMA-audit tests
pass. The new tests include malformed graphs and capture bounds; an initial
truncation fixture mistakenly placed a complete 20-byte descriptor within the
capture, and was corrected to cross the boundary. Its failed test log is retained.
Ruff and format checks pass. A CLI run using synthetic RAM and the real pinned
ELF produces four descriptors/two spans and explicitly refuses qualification.
No real RAM capture, radio MMIO read, firmware deployment or new hardware gate
was performed. The restored controller pair remains the last verified deployed
pair, and SPI admission/packaging remain blocked.

Offline usage (the heads must come from separately verified capture evidence):

```sh
python3 -m tools.unhygienix_flash_dma ram.bin --base 0x28000000 \
  --stock-elf /path/to/pinned/app.elf --tx-head 0x28001234
```


## TX policy-window audit — 2026-10-11

Evidence: `build/traces/flash-spi-policy-contract-20261011/`. The captured-MAC
TX audit now checks header policy pointers as well as descriptor/payload links.
Per-CU DWARF confirms a 52-byte full table and an 8-byte compressed table. The
audit conservatively requires a captured, aligned 52-byte internal-RAM window
for every supplied non-null header's policy pointer, and refuses null, XIP,
truncated, descriptor-overlapping or partially overlapping policy windows.
Identical shared windows are accepted. This can conservatively refuse a valid
8-byte-only capture. It does not interpret compressed/full policy control bits
or establish all hardware reads; routing qualification remains false.

The pinned `sys_hal_mac_bus_clk_ctrl()` and MAC subsystem-reset HAL functions
are empty TODO implementations. MAC power and functional-clock setters change
SYS bits without a documented drain handshake. These functions cannot be used
as evidence that existing MAC reads finished and future requests were excluded.
No speculative power/clock/reset writes or radio-register reads were introduced.

All 27 established tests pass, including the new policy faults and exact-end
capture boundary. Ruff and formatting checks pass; a synthetic CLI capture with
the real hash-pinned ELF reports one shared policy window and refuses routing
qualification. Before/after tool sources, type layouts, binary policy-copy
code, vendor control excerpts, hashes and logs are preserved.

There was no hardware access or image deployment. The restored controller pair
remains the last verified deployment. Safe radio access with reset-surviving
fault evidence and a complete master confinement or exclusion/drain contract
remain the blockers to SPI-mode integration and hardware qualification.


## RAM UART breadcrumb diagnostic — 2026-10-11

Evidence: `build/traces/flash-spi-next-gate-20261011/`. Dry ownership now emits
checksummed, generation-tagged UART packets immediately before and after the
existing RAM bus snapshot. AP is PARKED and CP IRQs are masked. The packet path
is RAM-resident with bounded polling; it does not enable SPI or read radio MMIO.

The first candidate incorrectly selected UART0 from the AP board declaration.
It completed at least 1008 dry handoffs, failures=0, but reported stages=0 and
received no marker. That UART gate failed and the exact controller pair was
restored. The failed image/source/raw trace remain preserved. AP runtime
`dbgserial_t5ai.c` actually logs through the CP mailbox. Pinned CP ELF `.data`
initializes `s_print_port` at `0x28067446` to UART1; its getter and polling TX
instructions are preserved in the evidence.

The corrected candidate uses UART1 at `0x45830000`, checking backup power,
UART1 memory shutdown/deep-sleep, functional clock, runtime port selector,
reset release and existing baud/frame config before peripheral access. It never
wakes or reconfigures the UART. A 16-byte T5ST packet contains generation, stage
and XOR check. Each transmit has a 64-AON-tick deadline, FIFO-empty check and
2-tick final shift margin; these are software bounds, not measured wire timing.
Linked disassembly uses 40 bytes of helper stack plus the 24-byte dry-release
frame, excluding outer callers and exception frames.

The corrected capture received valid paired breadcrumbs, reached all 32 delayed
holds and reported zero ownership failures. The operator answered “looks good”
for the FdF/phone-sync observation. Logs show bond restoration, session opening
and full-sync start, not a verified matched full-sync completion. A task watchdog
warning and FdF throttling also occurred; these remain separate from SPI, which
was disabled. One received stages=3 bus report (generation 316) has no matching
raw breadcrumb. Automatic serial reopening is a possible cause, not proven.
A separate 120-second continuous session disabled idle reopen and received
7247 bytes, two complete marker pairs, no unpaired/invalid packets and no bus
reports missing their pair. Ownership reached 1200 completions, failures=0,
max park 985 AON ticks (about 30.8 ms including marker overhead), max observed
request-to-park latency 10 ticks. The operator replied “synced app”; AP app-fetch
cleanup reported result 0. The two-pair sample is limited evidence and does not
explain the earlier lost packet or prove capture reliability during a reset.
Two BG stall warnings and three recovery records were received in that window.
Their sampled PC/LR resolve to `pbl_irq_unlock` / `sched_block` in the exact AP
ELF; this does not identify the cause of BG starvation.

Twenty targeted ownership, ARM, image-compatibility, unified-engine, volatile
adapter and DMA tests pass without skips. New host tests cover wrong UART config,
inactive backup/clock/memory domains, runtime selector mismatch, FIFO timeouts,
partial packets and AON wrap; dry ownership still completes if marker output is
refused. The first missing host mock failure is preserved. Default controller
CP reproduces the sealed baseline byte for byte. Vendor/shared sources remain
unchanged; actual SPI admission and volatile packaging remain blocked.

This establishes a useful host-retained stage signal in observed samples, not
reliable reset-surviving fault diagnosis. Before another radio probe, capture
must be dependable and a RAM fault path must retain fault PC/status/address.
Complete autonomous-master confinement or verified exclusion/drain is still
required before read-only SPI, status-cycle qualification and matched benchmarks.

The exact sealed controller CP/AP pair was restored again at run end. Both
flashers reported Flash OK with the sealed hashes. A 180-second raw capture
(including programming) received 24483 bytes, confirmed the baseline build
ID and phone session opening, and received 3 BG stall warnings /
0 recoveries. The baseline BG PC/LR also resolve to
`pbl_irq_unlock` / `sched_block` in its matching ELF. No firmware qualification
or benchmark is claimed from this short recovery window. All exact capture
processes are stopped. The remaining gate is reliable fault/receipt evidence
plus a complete master confinement or verified exclusion/drain contract.


## RAM fault context and failed legacy-pair qualification (2026-10-11)

Evidence: `build/traces/flash-spi-fault-context-20261011/`. The RAM fault entry
and reporter are implemented with a bounded emergency stack, validated core
frame access, SCB context, ordered checksummed UART packets and a decoder that
never grants routing qualification. CP vectors are prepared before REQUEST;
normal dry completion restores VTOR before IRQs. No intentional exception,
radio-register access or SPI bypass was attempted. Actual fault delivery,
stacking behavior and complete host receipt remain unqualified.

The final `cp-final` paired with the ownership AP completed336 handoffs with
zero reported failures, all32 delayed holds, max985ticks (30.78125ms), and
latency13ticks (0.40625ms). 54 valid stage packets form27 pairs, with no captured
bus row missing a pair. Operator reported “looks good.” This qualifies only
observed normal dry vector installation/restoration. Five BG warnings and five
recoveries occurred across the paired trace; BG stalls remain a separate issue.

An intermediate diagnostic-CP/baseline-AP pairing produced five unexpected
reset-reason10 boots. The preserved raw trace includes AP flash IPC command5
status-1, addr02580000 length12 write failure and the assertion
`flash_impl_write_page_begin failed: -1`. Dry unsupported capability refusal
prevented the normal controller write. The board-local wrapper was corrected
to permit the normal controller write on pre-REQUEST -95 only; uncertain
in-flight requests still fail-stop. Default controller output remains byte
identical to the sealed baseline.

That correction is **not hardware-qualified**. `cp-compatible` with the exact
baseline AP completed controller writes, then the operator reported reboot
looping. The continuous23277-byte capture contains one unexpected CP startup
reset10 after the repeated commanded candidate flash, three BG watchdog
warnings, then an AP boot reporting AP=112 / CP=16. The earlier flash-write
assertion was not observed in this captured window. This does not prove a
root cause; repeated capability-probe delay is a hypothesis. Both candidate
flash logs, the failed capture startup, retry raw bytes/index, decoded logs,
source snapshots and exact images are preserved. Do not deploy this candidate.

A subsequent offline-only correction latches unsupported capability probing
for the remainder of the CP boot, avoiding repeated probes on each write.
The CP owner tests now exercise1000 repeated refusals without another send,
while REQUEST uncertainty remains fatal. This follow-up is built as
`cp-negative-cache-unflashed`; it has **not** been flashed or hardware-qualified.
18 follow-up tests pass without skips and Ruff passes (the preceding compatibility
revision passed23 targeted tests). These results establish
software behavior only, not that the watchdog/reset cause has been fixed.

The sealed controller CP was immediately restored after the operator report;
the AP had already been restored and remained untouched during compatibility
testing. Flasher raw/protected hashes match63e6a7f... /951f1284... for CP and
80db6fab... /721bcef5... for AP. The same continuous capture confirms the
commanded baseline reset0, AP=0 /CP=0, bond restoration, phone session opening,
full-sync start and controller counts reaching384. The operator confirmed the
loop stopped and the watch was responsive. This short recovery window is not
a soak qualification. Capture processes are stopped at run end.

Remaining gates: resolve and requalify legacy image compatibility, verify a
controlled RAM exception with independently known PC/status and complete
receipt, establish safe radio access and exclude/drain or confine every
potential XIP master, then run read-only SPI, latch/status qualification and
matched benchmarks. Actual SPI admission and volatile packaging remain blocked.


## Fixing rollback and unsupported-owner compatibility (2026-10-11)

Evidence: `build/traces/flash-owner-compat-fix-20261011/`. The previous
compatibility candidate remains failed; this run adds new evidence rather than
reclassifying its reset as harmless.

Before any flash in this run, the exact original controller CP/AP reproduced
an unexpected BG watchdog reset (AP=112 / CP=16). The 17597-byte capture retains
9 BG warnings, 6 recoveries and one unexpected boot. The old AP is
v4.39.0-157-gef484e54f-dirty, build3a61b846c6a0ee93d7bc8758610f5dda4c5b808d.
Its disassembly omits the flash-client priority protection and progress-driven
App-throttle corrections already committed in 0e63d88c1. Thus “baseline restored”
never established that this old AP was a reliable long-term rollback target.
The independent baseline reproduction also prevents attributing every earlier
watchdog reset to ownership probes or SPI.

A current controller-only AP was built with those existing committed fixes;
no new common Pebble or vendor edits were made. Ownership and SPI bypass are
disabled. Watchdog timeout/grace remain 5000/2000ms. AP raw SHA256:
`7f5c9058ed4b2e33ea10434cc37649063f432c9004be9f1ab3a53f9431a20eba`;
protected:`9425dd3aaefc1c70d295f57974d11e97f3dfad9ffc68d04b2a953d4eb1924d32`;
build:`54b161d1d9c03dfc6fce75dd1bf8e129813728ed`.
Its 300-second capture including programming received 25679bytes; after the new
boot there was one commanded boot, no unexpected reset/assertion, a full-sync
start and successful app-fetch cleanup, with 4 BG warnings/3 recoveries. A final
recovery arrived in the next capture before CP programming. FdF activity is
present in the logs. This is finite hardware evidence, not an overnight soak.

The board-local ownership correction now remembers a pre-REQUEST unsupported
peer for the remainder of the CP boot. Ordinary controller writes continue on
-95; busy/other errors refuse writes and uncertain in-flight ownership still
fails closed. It reports the one rejected probe's duration. The 200.56-second
candidate capture received 20210bytes, exactly one 3200AONtick (100ms) refusal,
and controller counters reached 1920 writes/137592 bytes without another probe.
It contains one commanded boot/AP=0 CP=0, a full-sync start, 3 BG warnings and
3 recoveries, no assertion or unexpected reset. Operator: “looks stable.”
This directly verifies the bounded probe in observed writes. It does not prove
that probe delay caused every prior reset, or qualify actual SPI ownership.
Candidate CP raw:`a23d02cf743fedc2d6acb657a4d4ebf3c990ad25a5a0aa5c308c25e98360e9bc`;
protected:`9c6b60aa0956c34b1776d940ab11d0e3d8e9cab5f15d32d0c03490a322dfb6e7`.
18 ownership/fault/SPI tests and 4 existing priority/recovery tests pass without
skips; Ruff passes. The default controller CP still reproduces the original
sealed raw/protected bytes exactly. Capture tooling, source snapshots, source
hashes, disassembly and failed build attempts are retained.

The final controller rollback pair is now preserved in
`build/traces/flash-owner-compat-fix-20261011/controller-baseline/`: the unchanged
original controller CP (`63e6a7f...`/`951f1284...`) and the current controller-only
AP (`7f5c9058...`/`9425dd3a...`) above. The original AP is preserved as historical
failure evidence; do not automatically reinstall it as a healthy baseline.
The experimental CP was removed after the successful finite compatibility run.
BG stalls remain an independent unresolved performance issue. Full solid-firmware
qualification still requires repeat syncs, reconnect/cold-boot/data-persistence
checks and a longer soak. RAM exception delivery, all-master exclusion/drain,
SPI latch/status qualification and matched bypass benchmarks remain incomplete;
SPI transport admission and volatile packaging remain blocked.


Final controller recovery capture received 21537 bytes over the 180-second
window including CP programming. It confirmed one commanded AP=0 /CP=0 boot,
current AP build 54b161d1..., bond/session restoration and another settings-sync
start, with 1 BG warnings /0 recoveries and no unexpected reset or
assertion. The experimental CP is removed. The unchanged controller CP and
updated controller-only AP remain installed; raw/protected artifacts are sealed
in the current controller-baseline directory. All captures are stopped.


## Controlled RAM exception delivery — 2026-10-11

Evidence: `build/traces/flash-fault-probe-20261011/`. This test changed no SPI
routing and issued no bypass command. The separate `--ownership-fault-test`
build requires the verified ownership AP and stock ELF; normal builds omit its
UDF instruction. It deliberately faults at the first dry handoff after 960,000
AON ticks from the first handoff (30 seconds at 32 kHz). It re-arms after each
boot, so the finite runner restores the controller CP immediately after a fault
record begins, or after a 150-second post-flash deadline, then restores the AP.
Do not deploy this diagnostic as ordinary firmware.

The tested CP raw SHA256 is
`6b4c9ae1d4032281b53a0c0f352f49484955b4ce887dabcfa93454ed5fe40744`,
protected SHA256
`1e7edc98cc4831e6fbe72548539d184dd1fcbf7a5b1d31a21be19c61d9c57034`.
It was paired with ownership AP raw `b72fe84b...`, build `e5ca2f25...`.
Disassembly establishes the deliberately executed `UDF #0x42` at `0x08066646`.
The capture contains exactly one complete fatal record, generation 324, with:

| Field | Received | Expected interpretation |
| --- | --- | --- |
| PC | `0x08066646` | Exact deliberate instruction |
| Exception | 3 | HardFault, because PRIMASK masks configurable faults |
| CFSR | `0x00010000` | Undefined instruction |
| HFSR | `0x40000000` | Forced escalation |
| EXC_RETURN | `0xffffffed` | Thread PSP, FP-extended frame |
| Frame readable | 1 | Core registers recovered from original SP |
| VTOR | `0x28012400` | Reserved RAM CP vectors |
| AFSR | 0 | No auxiliary status |

All 16 per-packet checksums and the whole-record checksum pass; there are no
rejected fatal records. MMFAR/BFAR valid flags are clear, so their raw values are
not fault addresses. The emergency RAM stack, real extended-frame core-word
layout, UART delivery and exact fault PC/status have hardware evidence for this
image and exception. This does not qualify other exception types, every invalid
stack case, radio accesses, bus-master exclusion or SPI routing.

Linked sizes remain `.guard` 332/352, `.spi_guard` 536/568 and `.spi_support`
624/632 bytes. Both ordinary and fault diagnostic builds pass RAM branch/literal
reachability checks; the ordinary build contains no diagnostic UDF symbol.
Twenty focused tests pass without skips. Afterwards, frame validation was
narrowed to reject any 32-byte core frame overlapping the owner reservation,
including a frame starting just before it. The boundary regression tests and
ARM build pass. This refinement is preserved separately in
`cp-frame-bounds-unflashed`; it was not substituted for the tested hardware image.

The initial 30-second controller capture received 2,552 bytes, with one BG
watchdog warning. The continuous diagnostic/restoration capture received 24,875
bytes, with five boot banners: AP installation, CP installation, deliberate
exception reset, CP restoration and AP restoration. It includes one BG warning
and its task detail before the intentional reset; this is not a clean matched
benchmark. The operator started a phone sync and confirmed usability after
restoration. Final controller observation details are saved alongside the raw
capture. Battery ADC errors and BG warnings remain separate controller issues.

The exact controller CP raw `63e6a7f...` and AP raw `7f5c9058...`, build
`54b161d1...`, were restored using their sealed protected images. The next gate
is controlled, individually bracketed radio/master inspection with the now
verified fault recorder, followed by an actual exclusion/drain argument. The
previous combined radio MMIO experiment remains failed evidence; do not repeat
it wholesale or treat a powered clock or idle snapshot as proof of exclusion.
Read-only SPI, WREN/WRDI, volatile BP cycles and matched benchmarks remain blocked.

Final finite controller capture: 4,326 bytes over approximately 100 seconds,
two BG warnings and two recoveries, zero boot banners/assertions. One BlobDB
message reports a 65,221 ms pin-database mutex hold. This is an observed baseline
problem, not a SPI result. The stricter unflashed frame bounds build uses
`.spi_support` 628/632 bytes (four bytes of headroom); the tested hardware build
uses 624/632. Controller CP raw/protected images reproduce byte-for-byte after
these source edits. All capture and runner processes have stopped.
