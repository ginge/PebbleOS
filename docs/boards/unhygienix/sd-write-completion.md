# SD write-completion investigation — 31 October 2026

Status: a concrete setup interrupt race is fixed and host-tested. Attribution of
that race to the original 120 MHz failure remains unproven. Bounded diagnostic
runs are complete; no CPU policy or power saving is accepted.

Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/power-all-20261031`.
The original `cpu120-B` raw/config/ELF/phone/power evidence remains immutable.

## What the original trace establishes

At device 17:46:46.667 UTC, actual-120-MHz AP `4b5d813b…` reports CMD25,
one sector `0x9b4b`, `-116` (`ETIMEDOUT`), SD `00a64480`, DMA `00040000`.
The existing SD-PFS assertion then resets AP. The write had not reached CMD12;
DMA completion was recorded, but final TX-empty completion was not established.
Pre-failure AON/tick reports agree and show no extended-WFI operation. SD local
clock gating was off, and display/CP selection were unchanged.

Pinned BK7258 register decoding of the raw status gives TX FIFO needs data,
command CRC OK, response index 25, write status 2 and data-busy. No final FIFO
empty, write-end, data-timeout, overflow or data-CRC-fail bit remains in that
snapshot. Earlier ISR acknowledgement can clear flags; absence does not prove
an event never occurred. DMA finish alone does not prove card completion.

Only the AP frequency selector differed between original images. The CP policy
can change shared bus/voltage as well as CPU frequency; this was not a calibrated
CPU-only electrical experiment. The failed trace lacks clock/rail snapshots.

## Concrete setup race and scoped fix

The DMA callback enables TX-empty interrupt delivery by updating
`SD_INTERRUPT_MASK`. Foreground write setup separately executes a volatile
read/modify/write to enable FIFO clock gating. A callback between that load and
store can have its TX-empty enable erased by the stale foreground store. Equal
SD/GDMA interrupt priorities do not protect foreground code from either ISR.
For example, foreground reads `0x013f`; the DMA callback stores `0x033f`
(TX-empty enabled), then foreground stores its stale value OR `0x2000`, producing
`0x213f` and losing TX-empty. Excluding the callback until after the foreground
store produces `0x233f`, preserving both bits.

A short AP interrupt lock now protects that mask update and the write-engine
start. DMA starts in the same order as before. The fix does not change timeout,
retry, command sequence, transfer length, SD divisor, PFS geometry or fault
quarantine. It does not touch shared DMA dispatch/channel ownership or vendor code.

The native regression instruments only the mask RMW to schedule DMA completion
between its load and store. Without the production interrupt lock, the callback's
TX-empty enable is actually lost and the test fails on that missing bit. With
the lock, delivery waits until the first legal boundary and both enables survive.
One- and four-bit cases pass. This reproduces the software race, not the original
physical failure. Six relevant
SD/PFS tests pass, including both completion orders, intermediate FIFO gaps,
read/write roundtrips, errors/timeouts, quarantine and local clock lifetimes.

Profile-only failure diagnostics now retain setup masks before/after, callback
SD status/mask, last serviced SD status/mask, CPU mode/CPU1 and SD clock registers.
They supplement command/data/controller/FIFO/DMA-residue snapshots taken before
abort. No per-transfer serial logging is added. All logs meet the seven-argument
limit. Snapshot fields do not reconstruct every earlier interrupt.

## Diagnostic hardware protocol

Frozen APs `57aab799…` (240 MHz) and `f2cebc84…` (120 MHz) differ only in
`T5AI_POWER_CPU_120`. Both include the fix and failure diagnostics, release/PULSE,
four-bit existing PFS, unchanged resource pack and disabled display/gating/tickless
experiments. CP is not written. The 240 MHz diagnostic uses 1,637,392 B FLASH and
97,952 B RAM; image/CRC/map/resource/log/syscall checks pass.

Use the same USB/no-cell, dim Nadir, bonded phone and quiet-notification setup.
Run five automated 5-second-disabled/40-second-enabled reconnect cycles followed
by a 60-second tail. Preserve first SD/firmware failure and stop further cycles;
record additional link terminations and host outages separately. These are
functional checks, with no meter recording or energy claim. Review buffered
CRC-valid device-time logs and completed SD byte/error counters after the tail.
A bounded successful run does not prove causation or long-term storage integrity.
Restore the exact entry AP after the diagnostic sequence. Keep 120 MHz and all
power experiments default off pending broader qualification.

## Bounded hardware results and final state

Both fixed diagnostic images completed exactly five scripted disconnects,
bonded/encrypted recoveries and protocol sessions plus the full 60-second tail.
Actual clock reports remained 240/120 MHz respectively, with zero SD errors and
no received SD assertion/AP reset. Device-time review extends past both ends:
18:37:15 for `sd-race-240-A`, 18:48:17 for `sd-race-120-C`. Sampled completed-write
counters increase by 129,024 and 121,344 B over the corresponding reviewed
intervals. These counters are not a physical data-integrity/readback test.

The initial 120 MHz attempt `sd-race-120-B` is excluded: a pre-run disconnect at
18:39:46 (`0x0208`) left the protocol closed, so the first scripted cycle recovered
without a matching new disconnect. Full trace and failed count are retained.
The retry adds a received-watch-protocol preflight, rather than relying on phone
Bluetooth being enabled or operator connection status alone.

Retain Bluetooth errors: the 240 run has the known ATT subscription error; the
120 retry has descriptor/service discovery errors, a parameter-request timeout,
`ble_gap_update_params` error 2 and update status `0x022a`. The descriptor failure
is adjacent to a counted commanded disconnect (`0x0215`); timing offsets are
uncalibrated. Do not call either stack error-free or call five counted sessions
full Bluetooth qualification. The 120 selection remains default off.

The software bug is established and corrected. **Its causation of the original
SD timeout remains unproven**: that trace lacks the mask/latched-data snapshots,
and these new images also contain additional diagnostics/source changes. In
particular, their PM wake-event option is off whereas the original matched pair
had it on. No claim of a hardware fix-only ABA is made. No new energy measurement
or saving is accepted.
The diagnostic images also provide received QMI identity/revision values `05/7c`
through the sparse snapshot, unlike the earlier model-only evidence. This is a
hardware register observation, not WOM/apply/INT2 or gesture-latency qualification.

The normal default-policy build `4708694e…` passes ARM/image/log checks and fits
1,638,072 B FLASH (328 B free), 97,824 B RAM; it is not installed. Exact entry AP
`86d1dcba…` is restored with expected build, existing four-bit PFS, 240 MHz and
protocol recovery. CP was not written. This restore is commanded warm startup,
not cold qualification. It subsequently records link terminations `0x0228` at
18:50:55, `0x0208` at 18:51:58 and `0x023e` at 18:52:08, followed by protocol
recovery at 18:52:40. The operator confirmed both devices were untouched; these are uncommanded
terminations. They do not establish independence from the shared-frequency
transition: warm AP restoration leaves CP powered. See the later
[clock-policy review](retained-clock-policy.md) and current status. Final visible-state
confirmation was pending at this earlier checkpoint.

The receive-only capture is intentionally stopped; phone Bluetooth remains
enabled. No power recording or overnight soak is running. All changes remain
unstaged. First failures, excluded attempts, test negative controls and frozen
images are preserved under the evidence root.

Next: review/commit the scoped race correction separately from power policy;
qualify a longer reconnect run and controlled storage write/readback workload,
then complete captured cold-power/PFS and deferred overnight gates. Investigate
Bluetooth discovery/parameter negotiation; fixed 120 MHz is diagnostic. The
intended policy now keeps 240 MHz active and reduces clocks during coordinated sleep.
If SD fails again, use the saved setup/IRQ/clock snapshots to distinguish lost
completion, controller/card state and partial-write failure without timeout
extensions, formatting or retries that hide the first failure.
