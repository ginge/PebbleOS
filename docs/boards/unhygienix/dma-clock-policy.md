# DMA clock-policy inheritance — 14 November 2026

DMA clock-policy inheritance is enabled by default on Unhygienix native DMA,
at the operator's explicit request after the bounded trials. It withdraws AP's
forced bypass while leaving the global clock policy established by CP untouched.
Optional QSPI/DMA profiling and both measurement workloads remain default-off.
Historical trial sections below describe their then-default-off configurations.
It does **not** implement AP-controlled global gating, reset or domain shutdown.
The operator explicitly deferred startup and Bluetooth investigation for this
work. Preserve those failures and distinguish their state in measurements;
do not weaken sleep guards, erase bonds or change CP to make a trial pass.

## Implementation and ownership boundary

`CONFIG_T5AI_DMA_INHERIT_CLOCK_POLICY=y` requires Unhygienix and native DMA;
it no longer requires QSPI/DMA profiling. This is the board default. Explicitly
set it to `n` to restore the original
`global |= RESET_RELEASE | CLOCK_GATE_BYPASS` operation. Unprofiled builds retain
the same reset-release guard and no-write policy; only optional event recording
is compiled out. Existing explicit cache/menuconfig overrides still take priority.

The experiment reads global control at 0x45020008 after the existing power/memory
setup and settling. If reset-release bit 0 is clear, it records
`T5QD_DMA_INIT_FAILED` and returns `-EAGAIN` before access attributes, AP channel
setup or IRQ enable. Otherwise it never writes the global word. Inherited
bypass bit 1, arbitration and unrelated bits remain entirely under their
existing owner, including CP changes after the guard. There is no RMW that
could overwrite a concurrent CP request. Inherited value 1 permits existing
hardware automatic gating; inherited value 3 keeps bypass on and offers no
saving from this option. No code forcibly clears bypass to manufacture a result.

AP channels 0–3 retain their existing access/security/IRQ setup and lifecycle.
Cyclic periods keep DMA ownership; stop/error/cancel release only their owner.
Controller power and memory votes, peripheral completion boundaries, QMI WOM,
normal dim, CP frequency/firmware/identity and retained blockers are unchanged.
No mailbox call is added to an IRQ or completion path. No timer is added.
There is no off/on clock restoration sequence because AP never changes that
clock policy. An explicit shared-gate implementation still needs CP ownership
and all-channel wake/restore proof; this option is not such permission.

`T5QD_DMA_INIT_FAILED` is appended as event 23, preserving prior event numbers.
Its amount is the rejected global word. It uses the existing first-failure
bootstrap recorder before PSRAM activation. Decode snapshots against the exact
built header: EVENT_COUNT is now 24. Maximum profile report length becomes 53
rows. The profile is 844 bytes, aligned in AP-only PSRAM; no new normal SRAM
allocation is introduced by the option. The qualification pair retains a
28,832-byte kernel heap and uses 128,544 / 131,072 display/cache bytes.

## Source and binary evidence

Pinned platform remains 13379b63e07e78770fb4d0bffe36db2754658132. CP
`middleware/soc/bk7258/hal/dma_ll.h` initializes global to zero and releases
reset in the secure path; its already-initialized helper returns without reset.
The AP equivalent also initializes with bypass clear in its non-secure branch.
Common HAL configures single/repeat mode and request/width/loop fields afterward.
No late explicit bypass writer was found in the pinned DMA source. CP driver
initialization/deinitialization and reset-held reinitialization are additional
global/domain owners; none is copied into the live AP driver.

The exact installed CP97 ELF remains missing after a host search of 647 ELF
files / 22 CP candidates, including ignored paths. However, two exact function
bodies from vendor-linked templates match the frozen installed CP97 payload:

| Template | CP raw offset | XIP address | Bytes |
| --- | --- | --- | --- |
| dma_hal_init | 0x12ef0 | 0x02032ef0 | 82 |
| dma_hal_init_without_channels | 0x12f42 | 0x02032f42 | 40 |

Disassembly shows these bodies derive the DMA unit base, initialize global
with bypass clear, and avoid reinitialization when reset is already released.
They contain no external calls in those ranges. The matched init loops eight
channel groups; twelve attribute bits do not establish twelve populated groups.
This is not a complete linked call graph or inventory of proprietary code and
permits no new CP channel-register reads. Other template functions did not
match; do not treat a nearby stock ELF as the installed ELF.

Artifact `cp-function-byte-audit.json`, `cp-dma-hal.asm` and
`cp-audit-conclusion.json` preserve exact inputs, matches and limitations.
`dma-init.asm` in each frozen build is obtained through the actual T5AI_DMA
init function pointer, not a guessed symbol. Experiment assembly has a global
load and reset test followed by AP-only setup, with no global store.

## Validation and bounded hardware method

69 focused actual-source tests pass, including an mmap write-protected global
register through single/cyclic/error/cancel paths, held-reset rejection before
any AP/CP channel access, a CP bypass update after the guard, and four simultaneous
AP owners. Pending/duplicate completion, bus errors, retained blockers, profile
bootstrap, display and mailbox regressions remain covered. These models do not
simulate physical clock-stop/wake behavior.

Normal, diagnostic control and diagnostic experiment release builds pass CRC,
AP/map/memory, effective configuration, unchanged resources and actual linked
SRAM-WFI checks. Normal runtime matches installed faae34b9 except its build ID.
Control and experiment differ in exactly this option. Use the frozen source,
ELF/map/config/dictionary and hashes under reference
`artifacts/dma-auto-gating-20261114/`.

The one-shot mixed-I/O qualification is the same in both arms: 40 redraws,
16 read-only SD sectors, four display disable/enable/rotation cycles and
microphone callback delivery. It checks driver completion rather than a separate
SD payload hash or audio-fidelity reference. SD TX concurrency and cancellation
inside a live display batch remain host-tested rather than physically shown.

Keep operator-confirmed battery-absent, FNB58-only watch USB input and no UART
power lead. Recheck serial by-id mapping: this session if02 enumerated ttyACM2;
do not hardcode historical ttyACM1. Fresh received bytes are required for a live
capture claim. Preserve capture/protocol failures. Restore exact normal faae34b9
after the bounded trial. No overnight or shutdown test is included.

Power comparisons are accepted only with matched AP sleep admission and radio
state. A closed-admission pair measures shallow-idle input, not qualified retained
idle. Do not change guards to force a result or claim panel current from USB
input. Completed-workload energy needs time boundaries tied to actual completion;
uncalibrated UART receive timestamps alone do not establish it. See the artifact
RESULTS.md for installed state, observed results and electrical limits.

## DMA clock-policy trial — latest checkpoint, 14 November

The separate default-off DMA inheritance trial `2921571f` passed: DMA global
was inherited as 1 and remained 1 through the recorded phases, whereas control
`99a8a768` forced it to 3. The experiment never writes that shared word; it
rejects reset-held hardware before channel/IRQ setup. This permits inherited
automatic gating but does not prove physical clock residency or authorize shared
controller/domain shutdown. Normal builds retain their existing bypass policy.
See [implementation and evidence](dma-clock-policy.md).

Both diagnostic arms completed 40 redraws, 16 SD-RX reads, four off/on/rotation
cycles, 168 microphone frames / 53,760 samples and 16 concurrent owner starts,
with no owners left, restore errors or microphone faults. The trial recorded
32 clean AP retained returns (all CP deltas zero), reset sources 0/0 and encrypted
bonded phone recovery. The operator confirmed the requested display/dim, motion,
touch/button checks: "done. fine". These are bounded observations; SD-TX and
in-flight display cancellation remain host-tested. 69 actual-source tests pass.

The exact prior normal AP `faae34b9e9037bec3cce3bb118f898beceae0bc3` was restored
and its exact build banner, reset sources 0/0 and 32 clean AP returns
(all CP deltas zero) received; the capture under
`artifacts/dma-auto-gating-20261114/restore/` records the final installed state.
No CP, resource, PFS or bond partition is programmed. Panel sleep and raw-zero
brightness remain off; normal dim policy is unchanged.

FNB58 whole-watch USB windows were 137.609 mW / 27.546 mA (control, 5,976
samples) and 137.369 mW / 27.497 mA (trial, 5,980 samples), near 4.996 V.
Control had retained-sleep admission closed (AP=0x4f, reason 4); trial admitted
retained sleep. Radio/input activity also differed. The approximately 0.240 mW
difference is **not demonstrated savings**. No panel-only current or energy per
completed redraw workload was measured. Battery remains absent, USB supplied
through FNB58, no UART power lead; signal back-power is not electrically excluded.

Startup and Bluetooth diagnosis are explicitly deferred by the operator for
this work; prior failures remain preserved and guards unchanged. Next power
work is a timed fixed-workload comparison with matched AP and radio states,
including completed-workload energy, before proposing default enablement.
No overnight or shutdown test was performed.


## Subsequent matched energy workload

The [fixed dim-display test](display-energy.md) completed a control/trial/control
sequence with zero retained entries and globals 3/1/3. It observed approximately
1.005 mW lower idle USB input and 0.869 mW lower during the scheduled workload
than the controls' mean; gross energy per completed redraw was 0.231 mJ lower.
These limited pre-UI/phone-off observations supersede the earlier unmatched
windows only for this workload. They do not qualify normal connected use or
isolate panel/DMA energy. Inheritance remains default-off. See reference
`artifacts/dma-energy-20261114/` for timing rejection, corrected raw data,
analysis, frozen builds and final normal restoration.

## Readability cleanup and normal-use trial

The later cleanup names the private failure/transaction/channel/latest snapshot
slots and checks their separation at compile time. It derives bootstrap capacity
from the same layout, removes a redundant AP-channel bound test inside an already
bounded loop, and names workload timing/partial-row constants. Event numbers,
snapshot slots, profile layout, report limits and acquisition/completion behavior
are preserved. Sanitized actual-source tests cover the same transitions and errors.

The operator explicitly requested inheritance enabled for the next normal-use
flash. That candidate sets `T5AI_DMA_INHERIT_CLOCK_POLICY=y` and
`T5AI_QSPI_DMA_PROFILE=y`, with `T5AI_IO_QUALIFICATION=n` and
`T5AI_DISPLAY_ENERGY_TEST=n`. Profiling emits only the existing boot and 32-return
reports. It introduces no recurring work or test pattern. Source defaults remain
off. See current status for the actual installed ID and observed global value;
normal connected savings remain unqualified. Startup/BT diagnosis stays deferred.

Installed normal-use trial: 4865f0740c250426618095f1e3fd6cf6e6262bd9. UART
received inherited/ready DMA globals 1/1 and exact build. This programming boot
hit the existing AP=0x4f / CP=0 / retained rejection 4; no automatic reset or
guard change followed. Retained sleep is blocked, physical UI acceptance remains
separate, and no normal-use electrical saving is claimed. This explicitly enabled
trial supersedes prior faae34b9 restoration as the installed image; source defaults
remain off. Evidence: `artifacts/power-cleanup-20261114/`.
