# QSPI and DMA power savings scope — 14 November 2026

The diagnostic recorder/workloads are now removed from production, with exact
[archived recovery](diagnostics-archive.md). Default-on DMA inheritance and real
failure/owner guards remain. Dated measurements and trials below retain their
original evidence limits; see current status for build/installed state.

## Latest outcome — 14 November

The default-off [fixed dim-display energy harness](display-energy.md) now provides
matched shallow-WFI control/trial/control measurements with phone Bluetooth off.
DMA globals were 3/1/3 and all 48 redraws completed per run. Trial USB input was
about 1.005 mW lower idle and 0.869 mW lower during redraws than the controls'
mean. This is a small observed reduction in a bounded pre-UI workload; normal
connected retained savings and isolated panel/transfer energy remain unqualified.
Initial delayed-marker rejection and corrected captures are preserved. Normal
faae34b9 was restored with reset 0/0 and 32 clean AP returns;
see [installed checkpoint](current-status.md).
The operator subsequently enabled inheritance by default, independently of
profiling. The installed normal-use trial 4865f074 has
inheritance enabled and both test workloads off. Its AP=0x4f boot guard currently
blocks retained entry; see current status. Startup/BT diagnosis remains deferred.

## Objective and baseline

Reduce idle controller/interface activity and energy per completed display update
while the AMOLED remains on at its current dim brightness. Preserve visible
content, redraw cadence, wake latency policy, QMI WOM, touch/buttons, heartbeat,
storage completion, resources and bonds. The original scope introduced no runtime change. Package 1 was subsequently
implemented, and its unsafe startup accesses were corrected after failed trials.
Normal `faae34b9` is restored after a bounded mixed display/microphone/SD-RX
qualification and four disable/enable/rotation cycles; see [current status](current-status.md). No saving was measured.
Actual globals show QSPI bypass already clear and DMA bypass still set; a new
QSPI bypass policy is unnecessary on this observed startup.

Current implementation baseline is production HEAD `21b1cabcd`, which commits
the preceding standby/panel increment; the recorded installed normal AP is
`b40a6cb1070c5440f74c1c758b2c0eff8a38290a`. This scope does not re-observe the
physical device. The final reinstall required one commanded reset after an
AP=0x4f boot, then received 32 clean retained returns. Startup qualification is
still open. See [current status](current-status.md) and
[standby/display review](standby-display-review.md).

No CP frequency, firmware, closed settings, calibration or identity changes.
Keep normal 240 MHz active AP policy and the existing 80 MHz QSPI setting.
Preserve stock shared SPI/DMA classes. Production work belongs in native T5AI
SPI/DMA, T5AI mailbox/SoC ownership and board-local configuration/test tooling.
No global controller/domain, flash or PSRAM gating without ownership and restore
proof. No overnight soak is authorized.

## Source findings and ownership

| Resource | Current implementation | Consequence |
| --- | --- | --- |
| QSPI peripheral clock bypass | `drivers/spi/t5ai.c` sets reset-release with `REG(8) |= 1`. It preserves inherited bit 1. Pinned `qspi_struct.h` identifies bit 1 as `bps_clkgate`. | Source does not guarantee bypass is cleared. Read actual state before proposing a saving; an already-clear bit has nothing to release. |
| QSPI source and memory | Init requests CP QSPI power/clock, clears QSPI memory shutdown/deep-sleep bits, sets SYS byte offset 0x30 bit 20. Package 1 now rejects failed CP requests before source/controller writes. | Source clock stays enabled; controller-local CS/stream cleanup is not source-clock release. Acquisition failure handling is a prerequisite, not a power saving. |
| QSPI transfer lifetime | Start/command polling, pending software QSPI IRQ, DMA chunks, final wire drain, bus completion. Disable clears stream/memory-aperture/CS controls. | A gate must cover commands and drain/cleanup as well as DMA activity. No redundant release of these existing controls is justified. |
| GDMA0 global bypass | `drivers/dma/t5ai.c` explicitly ORs global reset-release and clock-gate bypass into 0x45020008. | Source forces bypass on. Removing this forced bypass is a concrete research candidate, but changes a whole shared controller. |
| AP DMA channels | Native controller owns 0 display, 1 cyclic microphone, 2 SD TX, 3 SD RX; IRQ only services AP 0–3. | AP last-owner accounting belongs in the native controller, not display. Zero AP owners cannot prove CP inactivity. |
| CP DMA channels | AP driver leaves the other channels untouched; linked/current CP users and their bypass expectations are not inventoried by this scope. | No AP-only global-off, reset or domain shutdown decision is safe from the AP bitmap. |
| Domains | QSPI is an AHBP submodule; DMA0/SD use BAKP. The pinned PM implementation groups QSPI with PSRAM/USB and DMA with other backup-domain users. | Keep domain and memory votes. A display idle boundary does not establish permission to remove them. |
| Peripheral completion | QSPI waits for wire idle after DMA; SD waits for controller/card completion; microphone cyclic half/finish events are continuing activity. | DMA finish alone is not an idle boundary for those peripherals. |

Earlier notes that QSPI bypass was "already clear" are historical observations,
not a source guarantee. Native init preserves incoming bit 1. The next diagnostic
must distinguish inherited configuration from what AP actually programs.

Pinned reference baseline: TuyaOpen
`e82840dc9b629cb77fa922fb04e3f820dc2e78b4` and T5AI platform
`13379b63e07e78770fb4d0bffe36db2754658132`. Relevant platform sources are
`middleware/soc/bk7258/{soc/{dma_struct.h,qspi_struct.h,sys_struct.h},hal/dma_ll.h}`,
`components/bk_pm/pm.c` and `middleware/driver/pwr_clk/{low_pwr_core.c,pwr_clk.c}`
under `third_party/hal_tuya/TuyaOpen-T5AI/t5_os/cp/`.
Vendor DMA initialization that zeroes a control word is not permission to copy
its global reset/channel initialization into the live AP driver.

## Ranked work packages

### 1. Diagnostics and acquisition correctness — first deliverable

Add default-off, bounded T5AI diagnostics; no busy periodic polling or per-transfer
logging. Capture startup and selected transition snapshots of QSPI bypass/source,
DMA global bypass and read-only channel-enable/status state. Read AP channel registers only after controller reset/security/privilege setup.
Do not read CP channel registers without a proven access contract; obtain matching
linked CP evidence first. Read-only intent does not establish access permission. Count transfers,
bytes, DMA/command completions, drain timeouts, busy/drain ticks and AP owner
lifetimes. Separate snapshots from evidence of actual clock gating/residency.
Do not read peripheral registers after a proposed source gate unless the BSP
proves access remains safe; retain needed counters in AP-owned memory.

Implemented and host/build validated; see [exact evidence and limits](qspi-dma-diagnostics.md).
The installed CP linked-use inventory remains incomplete; no gating package is enabled.

Handle `t5ai_cp_mailbox_enable_qspi0()` failure before native QSPI register use.
Inventory partial initialization: power acquired but clock failed, pinctrl/DMA
configuration failed, mailbox timeout/poisoned state. Do not blindly undo an
uncertain shared acquisition or retry a poisoned PM session. Fail device init
conservatively and preserve diagnostics. This correction must be separately
reviewable from gating changes and must be tested with failed acquisitions.

Deliver ownership/register map, current CP linked-use inventory, counters and
actual-source fault tests. A bounded diagnostic AP build must preserve normal
policy and pass image/map/CRC/SRAM/config/resource checks before hardware use.
If measurements show QSPI already auto-gates, record that result rather than
add a redundant policy.

### 2. QSPI local automatic gating — conditional small experiment

Establish whether QSPI 0x46040008 bit 1 is set on this actual startup path and
whether automatic gating covers FIFO/command/memory mode safely. If clear and
stable across transactions, this work package ends with no policy change.
If set, qualify clearing only that peripheral bypass while leaving reset-release,
SYS source, domains and memories intact. Do not copy the SD bypass policy:
QSPI may support automatic gating throughout active transfers, or may need bypass
through commands/drain. Derive that from pinned hardware semantics and tests.

Any dynamic acquire/release must serialize with SPI bus ownership and command IRQs,
recheck pending DMA and final wire state, preserve unrelated bits and retain the
safe clock state on uncertain drain. No sleep/wake timer or panel command changes.
Use a separate default-off board option until repeated mixed-owner hardware
checks pass. Expected saving is unknown and may be zero if already auto-gated.

### 3. GDMA hardware automatic gating — largest concrete bypass candidate

Investigate removing the forced global bypass while retaining controller power,
reset-release, channel state/security/privilege and IRQ allocation. Hardware
automatic gating is distinct from switching the whole controller clock/domain off.
It could avoid continuous controller clocking without software gating each
transfer, but the bit name alone does not prove all-channel wake behavior.

Before changing the global bit, prove its semantics cover pending requests,
all CP/AP channels, cyclic gaps, burst/loop transfers, bus errors and register
access. Inventory who else writes the global word and why. An AP IRQ lock does
not serialize CP writes; AP last-owner count is insufficient. Keep this package
blocked if CP ownership/bypass requirements cannot be established within the
unchanged-CP constraint. Do not solve that by silently changing CP firmware.

If justified, isolate the experiment in the native DMA controller behind a
separate default-off board control. No global reset, mass channel reinitialization,
CP IRQ acknowledgement or BAKP/memory power-down. Preserve current DMA sleep
blockers and the full peripheral completion blockers. Compare idle and cyclic/
mixed-transfer energy, not just idle screenshots.

### 4. QSPI source clock lifetime — later, more complex experiment

The existing CP clock operation targets a module clock, not a new per-client
reference-counted ownership service. Pinned `bk_pm_clock_ctrl()` directly applies
the gate and returns success; the worker sends its response afterward. This
supports command ordering, but does not prove exclusive ownership or independently
verify a live source state. Inventory CP QSPI0 users and shared register writers,
then prove an exclusive or explicitly coordinated lifetime using the installed
contract. Keep the AHBP/QSPI power and memory votes; source-clock release is the
only candidate at this stage. No direct AP SYS RMW as a shortcut around CP.

The shared SPI API calls configure in thread context but disable from completion,
which can be inside the GDMA/QSPI IRQ. `prv_pm_exchange()` takes a mutex and waits
for replies; it must not be called from that disable/IRQ path. Enable has no error
return, so a failed resume must be handled before it or in an appropriate native
configure/start state, while preserving the shared API.

Design an explicit `ACTIVE -> DRAINING -> IDLE_PENDING -> CLOCK_OFF` lifecycle,
with fault/unknown state retaining clocks where possible. Deferred release needs
an owner/generation, recheck under the bus ownership protocol and cancellation
when new work arrives. Acquisition, delayed release and reacquisition must not
race mailbox reply, next command, timeout, callback or borrowed buffer lifetime.
Uncertain off/on replies must prevent further transfer/retained admission until
safe recovery; do not claim clocks remain on after an ambiguous off reply.

Use an amortized burst/idle boundary, not two CP exchanges around every row or
brightness command. Derive the delay/break-even interval from measurements;
mailbox/worker activity can cost more energy than the gate saves. No guessed
fixed delay becomes the default. Preserve command timing, batching and wake
behavior during comparisons; any measurable added first-update latency must be
presented explicitly before default enablement.

### Excluded work

Global GDMA off/reset, BAKP/AHBP domain removal, flash/PSRAM power changes, CP
sleep/frequency/firmware changes, panel Sleep In/raw-zero, watchface rate limits
and new default dim behavior. Changing the historical polling/IRQ tradeoff,
QSPI frequency, batching or conversion at the same time would confound attribution.
Those can be separate scopes after ownership/completion qualification.

## Required state and failure tests

Extend actual-source native SPI and GDMA harnesses with modeled registers and
endpoints. Keep the shared SPI/DMA implementation unchanged; exercise integration
against their real callback/lock ordering where the native-driver-only harness
currently substitutes it.

- Reject failed clock/power acquisition before register access; cover partial init
  and mailbox failure/timeout without unsafe rollback or unbounded retries.
- Cover command-only brightness, window setup, command-to-quad transition, DMA
  submission failure, multi-chunk transfer and successful final wire drain.
- Inject final-drain failure, lost IRQ, DMA bus error, timeout/terminate, duplicate
  completion and stale timeout/IRQ after a subsequent transfer. Check exactly-once
  callback, safe CS/aperture cleanup and no invalid framebuffer reuse.
- Acquire new work before, during and after deferred release; exercise failed
  release enqueue, cancelled worker, delayed PM reply and rapid off/on requests.
- Run AP channels 0–3 together with cyclic microphone half/finish events. Preserve
  seeded CP channel state/IRQs and unrelated global bits; one owner's completion
  must not release another's resource. Treat the AP bitmap as AP-only evidence.
- Keep SD DMA finish separate from FIFO/card-ready/durable completion. No partial
  write retry, storage format or durability weakening.
- Verify last owner/blocker release only after safe cleanup, including failures;
  preserve CP-awake/SRAM gate/timer accounting and first-update after AP idle.

Current harness entry points include `test_t5ai_spi`, `test_t5ai_gdma`,
`test_t5ai_mic_dma`, `test_t5ai_sd_irq_race`, `test_t5ai_sd_idle_clock`,
`test_t5ai_sd_pfs`, `test_unhygienix_cp_mailbox_lifetime`,
`test_t5ai_retained_sleep`, `test_t5ai_sleep_guard` and display lifecycle/rotation/
power tests. The three old `test_t5ai_dma_{irq,deadline,fault_injection}` extractors
still target removed code; migrate them as a separate test-maintenance change,
or explicitly retain their known failures. Do not count them as passing or
silently skip them to present a clean suite.

## Hardware and electrical acceptance

Prepare separate normal/control and single-option experimental APs. Validate
layout, protected CRCs, linked SRAM, heap/PSRAM limits, unchanged resources and
normal dim/WOM configuration. Preserve the installed image and prior known-good
rollback, source snapshots, ELF/map/dictionary/config and first-failure bytes.
Recheck host UART enumeration and actual fresh received bytes; preserve CH342
errors and request reconnection if necessary. No unattended/overnight run.

First establish usable startup on the control. Keep the AP=0x4f admission failure
and earlier splash report as separate known blockers; no failing startup window
is an accepted energy comparison. A commanded recovery must be labelled and
matched across arms. Do not weaken guards to admit a trial.

Use bounded, completed workloads: static dim idle, identical partial redraws and
full redraws, repeated display enable/disable with sleep option still off, first
update after retained idle, alternating orientation, QMI motion wake, touch/buttons,
SD/PFS transfers and concurrent microphone/display/SD while CP services Bluetooth.
Capture exact identity, 32 clean AP returns, encrypted phone recovery and every
first error. Qualify storage completion under overlap and fault/cancel paths.
Do not describe independent host tests as physical concurrency qualification.

Only then compare matched A/B/A USB idle and energy per completed redraw with
FNB58, fixed supply/battery/charging/debug power paths, brightness/content/radio
conditions and counted completions. Record latency, elapsed time, bytes, retries,
clock/residency counters, sample timestamps and USB gaps. Change one gate at a
time. Reject incomplete or reset/disconnected windows. Require a repeatable
energy reduction beyond control drift with no correctness or latency regression.
The measurement is whole-device USB input; do not report it as panel-only or
battery current. No numerical savings estimate is supported yet.

## Recommended next action

Implement work package 1 only: bounded ownership/clock diagnostics and safe QSPI
acquisition failure handling, with native fault tests. Use its findings to choose
between a no-op QSPI conclusion, a local-QSPI experiment and a separately proven
GDMA automatic-gating experiment. Keep shared QSPI source release as a later
package. This yields a reviewable first change without committing to an unsafe
shared-clock policy or an unmeasured power claim.
