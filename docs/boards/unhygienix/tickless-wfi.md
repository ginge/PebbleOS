# Ordinary-WFI timer experiment — 31 October 2026

This is a default-off, AP-local experiment built on production `198385cb0`.
It does not establish retained sleep, an AON alarm implementation, electrical
savings or full hardware qualification. Display configuration/resources and CP
are unchanged. The earlier I2C option remains off.

## Contract and implementation

`T5AI_TICKLESS_WFI` extends the existing AP-owned SysTick only in ordinary WFI,
after the existing idle gate and scheduler confirmation with interrupts masked.
The application sleep veto and >=240 MHz active CPU vote remain. A separate
`T5AI_TICK_PROFILE` option reports once per minute through existing regular timers
and a droppable KernelBG callback; it creates no worker and does not log in the
tick ISR or block the timer service. Enable it identically in control/candidate.

The native PM client tracks successful application-veto and CPU1-vote replies,
invalidating each guard under the transaction mutex before publishing a change.
Failures, PM quarantine and discarded responses fail closed. Idle performs no
mailbox transaction. It rejects unknown/DCO or insufficient clock rates,
SLEEPDEEP, pending interrupts, nonstandard SysTick configuration and short gaps.
It neither overwrites CP RTC compare nor interprets asynchronous CP wake events.

The pinned CP PM frequency arbiter takes the largest module vote, but steady
frequency is insufficient to bound SysTick: `sys_hal_core_bus_clock_ctrl()` changes
source/divider in separate writes. The 240/320 MHz transition passes through
320 MHz / 2 = **160 MHz**. The experiment uses that transient lower bound while
retaining the >=240 MHz vote and veto. At the usual 240 MHz clock the 24-bit
counter bounds a single extension to approximately 69.9 ms; short deadlines wake
earlier because of the conservative clock bound. This is not a frequency policy
change. Further CP variants/direct clock paths must re-establish this contract.

The target is at least one kernel tick before the next scheduler timeout.
Elapsed AON fractions remain in the existing accounting epoch, including wrap.
An underflow racing timer stop is preserved; slow setup aborts. The computed
period must exceed the running periodic reload. If AON accounting is due before
extension starts, leave the already-running periodic deadline alone: a software
pend there can be followed immediately by a duplicate hardware interrupt. Every wake
restores the actual-clock periodic reload and pends the existing SysTick ISR if
AON time is due. Timer expiration stays in its existing ISR context; coalesced
elapsed ticks are accounted exactly once. This ordinary-WFI suspend/wake contract
must not be reused for retained sleep where the core clock may stop.

## Validation and limits

The native sanitizer harness executes production timer/idle/power code with
modeled register semantics, pending underflow/external wake, repeated fractional
wakes, AON wrap, a 24-bit deadline, elected/transient clocks, failed/in-flight PM
votes and quarantine. A separate production profile test checks dropped work,
interval/reset and callback context. The final current non-display suite passes
**36 tests**, including the three new production timer/PM/profile tests and the fractional/periodic-phase regressions.
A broader 23-test selection passes 20 and errors in three legacy display DMA
tests that extract removed functions; preserve those failures and defer their
retirement/migration to the final display phase. No display source was changed.
These model results do not establish physical interrupt latency.

Matched release builds use source-built picolibc; the config snapshots differ
only in `T5AI_TICKLESS_WFI`. Resources are byte-identical. The normal partition
has insufficient established headroom for larger experiments; its geometry was
not changed. Control FLASH is 1,629,416 B, candidate 1,629,896 B (+480 B), with
88,800 B static RAM in both.

- Control ELF ID: `976f838adecfd377372aa74fef95206387fb7f51`.
- Initial candidate ELF ID: `379c377b129fe9a9f3e9f3f79e5a21a8367ef6bc`.

The control boot verified 240 MHz, four-bit SD, the existing PFS (`new=0`), 49 apps
and stored bond restoration. Its first profile accounted 59,019 ticks in
1,888,616 AON counts, then 60,001 ticks in 1,920,035 AON counts, with max delta 1.
The initial runtime logs showed advertising rather than an encrypted session;
connected Nadir state was requested from the operator. No power comparison may
be labeled connected-idle until that state is verified.

## Candidate iterations and hardware observations

The first candidate (`379c377b…`) accounted time but increased steady SysTick
interrupts: 64,141 IRQs for 59,997 ticks, then 64,243 IRQs for 60,000 ticks.
The first complete raw trace is retained. The second candidate (`2d9e4f44…`)
rejected periods shorter than 1 ms but still showed excess IRQs because of the
synthetic pending path ahead of the running periodic tick. These are efficiency
regressions, not established kernel time loss or crashes. Neither is promoted.

Final candidate `f2f070e40a9a7b02d20cd94135f8c35914b21a6e` retains both corrections.
Its steady intervals account AON time and show about 56,000–56,400 interrupts
per 60,000 elapsed ticks; max catch-up is 3 ticks in these intervals (70 during
startup). This is roughly 6–7% fewer tick interrupts. It is not a total IRQ/wake
count, an energy result or a complete deadline/fault qualification.

A control reconnect at 10:58:33 failed with supervision status `0x208` following
a parameter timeout; encrypted PPoGATT recovered at 10:58:40. The control has no
timer extension. Preserve `control-first-disconnect.rx.bin`; do not attribute
that baseline failure to the candidate or count startup reconnects as the
25-cycle suite. Existing baseline radio stability remains an open gate.

The first power-on window used a different screen according to the operator and
is excluded from matched Nadir comparison. The first confirmed Nadir control
window includes a notification at 11:00:45.477 and a pronounced current transient,
with `Animation 10 does not exist` errors also retained. Its full mean is
31.568 mA / 157.329 mW; it is excluded as connected-idle evidence rather than
silently trimmed. The later candidate window began after an explicit dim-Nadir/
Bluetooth confirmation and 90-second settling period: 11,968 samples, mean
30.070 mA / 149.888 mW at 4.984623 V, nominal energy 4.98293 mWh. The subsequent off window began after a second operator confirmation and the same
90-second settling period. No notification or reconnect is present during either
clean window in the bounded trace.

| Timer extension | Samples | Mean current | Mean USB input power | Nominal energy |
| --- | ---: | ---: | ---: | ---: |
| On, final candidate | 11,968 | 30.070 mA | 149.888 mW | 4.98293 mWh |
| Off, matched control | 11,980 | 30.030 mA | 149.690 mW | 4.98136 mWh |

On minus off is **+0.040 mA / +0.197 mW**, with one-second mean standard deviations
0.316/0.236 mA. Both loggers stopped cleanly (0); CRC-enabled raw samples and
USB diagnostics are retained. Nominal durations/sample counts differ slightly,
so raw integrated energies are not equal-duration task-energy comparisons.
This pair does not establish a causal increase or any saving. The rejected
notification window prevents a clean off/on/off sequence. **Keep default off.**
No counted workload, frequency/radio policy or battery-side saving was qualified.

Final normal default-off image `c656055a2c73c30defa3c8939c00b67af2771c95` passes
all map/protected/resource/log-hash/syscall checks: FLASH 1,638,072 B (328 B free),
static RAM 97,824 B. Profiling, timer extension and I2C gating are all disabled.
It is frozen for restoration after the comparison. UART verifies that exact restored ID, four-bit SD, the existing 32 MiB PFS,
49 apps and encrypted bonded PPoGATT. The commanded warm-flash reboot is not a
cold-power result.

## Continuing baseline qualification

The bounded comparison UART capture is stopped: 29,018 B over 1,886.282 seconds.
All bounded meter captures are stopped. The normal-image warm startup verifies
its exact build ID, four-bit SD/PFS with 49 apps and encrypted bonded PPoGATT.

The first operator power attempt left a cable attached and is not qualified as
cold power. After the requested redo, a fresh AP startup at host 11:22:48 UTC
confirms `c656055a…` and 240 MHz; the operator reports Nadir running and Bluetooth
connected. The cold trace stops after reset-source logs; full storage/reconnect
logs were not captured. Do not label that output gap a firmware hang or full
cold-start storage qualification. `cold-redo-validation.json` records the limits.

The first receive-only baseline capture (PID 739131) stopped after 302.426 seconds,
6,437 bytes and three attachments. It records serial re-enumeration and a
nonblocking-read retry incorrectly treated as a gap. The second (PID 749158)
stopped after 355.824 seconds, 1,078 bytes and two attachments; its fresh cold-boot
ID and sparse/corrupted later bytes are retained. The port reported exclusive
ownership even though that receiver did not request it; a separate baud read
was unavailable. Do not assert which host service caused the condition.

Current receive-only baseline capture is **`baseline-soak-v3/`**, PID **760489**,
started 11:27:06 UTC for up to 25 hours. It takes exclusive UART ownership,
verifies/restores 460800 baud and preserves separate immutable attachment files
and explicit gaps. At 11:27:48 it received 159 B yielding one valid AP battery-log
frame and no invalid frame; reception is verified, rather than inferred from a
process/device node. Check its JSON `last_received_utc` and growing raw files for
continued freshness. Embedded log times and host receive times are separate.

Live evidence: `artifacts/tickless-wfi-20261031/baseline-soak-v3/` and
`baseline-soak-v3.log` in the reference workspace. Create `baseline-soak-v3/stop`
to end it cleanly. Earlier captures are stopped and retained. This recording can
cover the coming overnight interval while power/connection remain available;
**no completed overnight qualification is claimed**. The 25-cycle suite,
complete cold-start trace/storage checks and periodic-UI overnight confirmation
remain open. This is baseline qualification, not an overnight timer-candidate
result.

Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/tickless-wfi-20261031/`.
Keep raw UART, first failures, frozen configs/images/dictionaries and AP-only flash
logs. Release output is textual hashed `NL:` logging, unlike the older framed
normal output; `decode_text.py` annotates templates and preserves hexadecimal raw
arguments rather than claiming fully formatted values.

## Remaining gates

Verify timer/AON ratios and interrupt reduction on hardware, then repeat matched
connected-idle off/on/off with dim Nadir, no cell and USB power. Complete counted
identical workloads and compare their total input energy. Check buttons, sensor
latency, heartbeat/supervision, sustained transfers, PFS/flash/DMA concurrency and
faults. Complete 25 reconnects, a true USB cold-power boot with bond/watchface
restoration, and an overnight connected soak with periodic UI interaction.
A warm flasher reboot is neither a cold-power test nor a reconnect-cycle suite.

CP AON alarm ownership and asynchronous wake routing remain blockers for retained
sleep; shared peripheral/domain ownership and the other non-display phases remain
open in [the power TODO](power-todo.md). Do not remove the veto or begin display
optimization on the strength of this bounded timer experiment.
