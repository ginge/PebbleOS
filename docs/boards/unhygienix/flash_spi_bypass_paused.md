# Flash bypass paused: controller baseline retained

Cleanup update — 2026-10-18: the production tree now retains only the Bluetooth
CP hook. The old CP flash guard and SPI/volatile sources, packaging support and
tests were removed. This document and linked evidence retain the historical
experiment; source is available in Git and the preserved experiment branch.
Current CP packaging rejects diagnostic manifests; rebuild from the pinned
stock input for a Bluetooth-only image. This cleanup did not flash the watch.

Date: 2026-10-11. Operator decision: pause bypass work, preserve it on a branch,
and return to `tuya` on the current controller baseline. This decision overrides
the next-step instructions in earlier experimental handoffs.

Current-status update — 2026-10-17: the pause decision still applies. The
`tuya` production branch has advanced through the upstream merge, driver API
adaptation and shared GATT consolidation (`ab3616d5e`). References to
`c0327d426` below describe the shelving checkpoint, not the current production
HEAD or installed AP. The latest recorded flash is the controller-path GATT
candidate; see the [operator handoff](index.md#latest-implementation-status--2026-10-17).
No qualified bypass speed or stability result has been added.

## Branches and scope

- `codex/unhygienix-flash-bypass-paused` preserves all accumulated AP/CP ownership,
  RAM SPI/fault/bus diagnostics, packaging checks, tests and qualification records.
  It starts at `c0327d426`, the operator's last commit before this rewrite.
- `tuya` retains `c0327d426` production sources and existing committed recovery
  fixes, with this documentation and the full results/evidence index added.
  It does not receive the new ownership or bypass rewrite implementation.
- Existing committed experimental SPI code at `c0327d426` is retained as history;
  controller operation and the existing volatile-packaging rejection remain in
  force. Returning to `tuya` does not enable that older implementation.
- Vendor sources and shared/common Pebble code were preserved. No remote push,
  merge, hardware reflash or new experiment is part of this shelving operation.

The local experiment branch is a research snapshot, not release-qualified
firmware. Do not cherry-pick it wholesale to address baseline watchdog stalls.

## What the experiment could achieve

Stock controller writes repeatedly change protection status. In one preserved
256-write diagnostic window, the status phase accumulated 122,940 AON ticks
within 126,716 total write ticks (about 97%, with nested measurements). This is
an observed opportunity, not a benchmark or a promise of the same percentage
speedup: not all status work would disappear, and ownership, transfer, verification
and restoration add overhead. At 32 kHz those totals are about 3.842 and 3.960
seconds. See the raw CP phase counters in `flash-fault-probe-20261011/fault-live`.

The proposed bypass changes volatile protection using SPI and retains the stock
controller for programming. If qualified, it might lower small-write latency,
IRQ-off time and settings-sync duration. There is no qualified rewritten-bypass
performance result, matched full-sync comparison, or demonstrated user benefit.
It is not established that AP/XIP interference caused earlier command failures.

## Why pause: explicit tradeoffs

| Choice | Potential value | Cost or limitation |
| --- | --- | --- |
| Continue binary-patched SPI bypass | Reduce expensive protection changes | Requires private hardware knowledge, strict pinned-image contracts, ownership across autonomous masters, RAM-only exceptions and tiny fixed code reservations; substantial regression and maintenance burden |
| Keep controller writes | Preserve the vendor-supported programming path and a simpler operating baseline | Retains measured write overhead and does not by itself cure sync stalls |
| Investigate baseline stalls first | Work on failures visible without bypass and establish the actual cause | May still expose flash latency as a contributor; storage/scheduling changes need their own scoped evidence |
| Seek supported vendor primitives later | Could replace hard-coded patch sites and undocumented routing assumptions | Availability and suitability are unknown; not implemented or promised |

The complexity now exceeds the demonstrated benefit. The immediate engineering
priority should be the controller-baseline watchdog/database behavior, not more
bypass qualification. That is a recommendation, not authorization to change
shared settings/scheduler code. Do not extend watchdog grace or feed it
unconditionally to hide stalls.

## What is verified and what remains incomplete

The [full chronological results](flash_spi_rewrite_results.md),
[original audit](flash_optimisation.md) and
[staged plan](flash_spi_rewrite_handoff.md) remain available on both branches.
They preserve successful and failed runs, withdrawn explanations, exact image
identities, operator observations and the remaining gates.

Verified evidence includes versioned dry AP/CP handoffs under FdF/phone activity,
bounded 0–30 ms parks, controller compatibility with one capability probe per
CP boot, offline unified TX/RX engine tests and linked RAM branch/literal audits.
A deliberate RAM UDF produced one complete checksum-valid record at PC
`0x08066646`, HardFault 3, CFSR `0x10000`, HFSR `0x40000000`, VTOR
`0x28012400`, EXC_RETURN `0xffffffed`. Twenty focused tests pass without skips.
The later stricter frame-span check was tested/built but not hardware-flashed;
its SPI-support allocation is 628/632 bytes, only four bytes of headroom.

Remaining gates: proven radio/DMA/other-master exclusion and drain; 10,000
read-only SPI transactions; 1,000 WREN/WRDI pairs; 32 volatile BP restore cycles;
qualified failure handling; and at least three matched full phone-sync pairs.
No production SPI admission or volatile packaging gate was opened. The earlier
combined radio MMIO diagnostic rebooted repeatedly; its exact failing read is
unknown. It must not be reused as a trusted probe. Compatibility failures and
reboot loops are preserved, not replaced by later successful observations.

## Exact controller baseline left on the watch

Authoritative files: `build/traces/flash-owner-compat-fix-20261011/controller-baseline/`
and its manifest, also copied into `flash-fault-probe-20261011/controller-baseline/`.

| Artifact | SHA256 |
| --- | --- |
| AP raw `pebbleos.bin` | `7f5c9058ed4b2e33ea10434cc37649063f432c9004be9f1ab3a53f9431a20eba` |
| AP protected `pebbleos.t5ai-crc.bin` | `9425dd3aaefc1c70d295f57974d11e97f3dfad9ffc68d04b2a953d4eb1924d32` |
| CP raw `pebbleos.t5ai-cp.bin` | `63e6a7f37acfa2cfd9ad827e0fee61199c54c611538f6e4f12e60ef16c6b7e04` |
| CP protected `pebbleos.t5ai-cp-crc.bin` | `951f1284435cbe48fbe4900c1311d42d1d6311332c562201a626f90136afa200` |

AP build ID: `54b161d1d9c03dfc6fce75dd1bf8e129813728ed`. Ownership, SPI bypass,
flash profiling and display profiling are disabled. Controller CP is unchanged
from the original sealed baseline; this AP includes the committed recovery
fixes. The older AP raw `80db6fab...` independently reproduced a watchdog reset
and is failure evidence, not the current rollback choice. Use the exact sealed
images for rollback: later documentation commits change build identity, so a
newly compiled AP must not be assumed byte-identical to this installed image.

After the deliberate fault test the exact controller pair was restored, UART
confirmed its build and phone reconnection, and the operator confirmed usability.
The final approximately 100-second capture received 4,326 bytes, with two BG
stalls and two recoveries, no new boot banners/assertions, and a reported
65,221 ms pin-database mutex hold. This is not release qualification. Battery
ADC failures were also observed. All captures and test runners are stopped.

## Evidence preservation and recovery

Raw images/captures/disassembly and source snapshots remain under
`build/traces/flash-*` across branch switches. They are ignored build artifacts,
not Git objects. A separate local compressed archive preserves every such
folder as it existed at the pause:

`/home/baz/Documents/ChatGPT/pebble/artifacts/unhygienix-flash-bypass-paused-20261011.tar.gz`

The tracked [inventory](flash_spi_pause_evidence/inventory.json) records its
SHA256, size and directory/file counts. The tracked
[per-file hashes](flash_spi_pause_evidence/SHA256SUMS) permit independent checking
of every original artifact. This is local preservation, not an off-host backup.
Extract into a separate directory to inspect; do not overwrite current build
outputs. The archive's `build/traces` paths are relative to the production repo.

Before any future resumption, read this decision and the full failure record,
verify archive/inputs and the current controller pair, re-enumerate UART hardware
and prove actual incoming bytes. Resume only with explicit operator direction
and a concrete plan for reducing or justifying the maintenance burden. Keep the
controller default until all qualification gates and matched benchmarks pass.
