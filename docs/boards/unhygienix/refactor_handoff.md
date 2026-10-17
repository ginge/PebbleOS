# Unhygienix stock-code refactor handoff — 2026-10-09

Current-status update — 2026-10-17: this is a historical investigation and
refactor record. Use the [operator handoff](index.md#latest-implementation-status--2026-10-17)
for the latest recorded flash/configuration and the
[remaining-work list](../../../boards/unhygienix/tuya/REMAINING_TASKS.md#current-disposition--2026-10-17)
for current scope. The upstream merge/driver API adaptation, restored stock
resource-bank condition and shared GATT engine supersede the corresponding
older extraction plans and source paths below. The October 17 GATT smoke test
does not resolve the recorded watchdog, resource lifetime or persistence failures.
Installed-image and capture claims below describe their dated sessions.

This was the October 9 refactor handoff. It supersedes the paused September
startup/FreeRTOS cleanup plan in the [historical audit](history/shared_code_cleanup.md).
The operator resumed targeted KernelBG stall tracing on 2026-10-10; see the
latest evidence below. Retain the remaining stock-code refactor list. This document records work; it
does not authorize enabling unsupported update/recovery operations.

## Accelerometer restored — 2026-10-10

Operator requested accelerometer re-enable. Configured
`CONFIG_T5AI_ACCEL_DISABLED=n`, rebuilt and AP-only flashed successfully in
31.3 seconds. QMI8658 driver/service are enabled; App/BG tracing remains enabled.
Newly added SDK-cache source was preserved and `CONFIG_T5AI_SDK_COMPAT_CACHE=n`
keeps this restoration consistent with the previous tested trace configuration.
Six targeted tests plus image layout/CRC/loghash checks passed. Live UART
verification showed repeated QMI8658 task CPU activity, confirming polling resumed.
Evidence: `build/traces/app-bg-accel-restored-20261010/`. Verification capture ended;
no monitor remains. This supersedes the installed-state notes below.

## Accelerometer-disabled retrace — 2026-10-10

Operator requested a build without accelerometer and a retrace. Added default-off
`CONFIG_T5AI_ACCEL_DISABLED` in the board Kconfig and QMI driver-local init gate.
It writes sensor CTRL7=0 and skips worker/timer creation. Standard service/API
code and task priorities are unchanged. Normal and disabled driver tests pass;
subscriptions/peeks cannot restart acquisition when uninitialized. Six targeted
actual-source/decoder tests pass, plus firmware layout/CRC/loghash checks.

Current installed AP: accelerometer acquisition disabled, App/BG tracing enabled.
Candidate/evidence: `build/traces/app-bg-noaccel-20261010/`, especially `analysis.txt`,
`comparison.json`, `per-instance.json` and `hardware-fdf/`. AP-only flash passed in
31.4 seconds, physical `0x132000..0x2d334a`. Only autoconf delta from the previous
App trace candidate is the disable flag; App/BG source is byte-identical.

Initial capture ran Launcher/TicToc. Operator selected FdF; fresh 180-second
capture received 161870 bytes. A brief button/Launcher interval was excluded by
App ID. FdF redraw/upkeep: 469 long spans, median 362 ms wall / 336 scheduled ticks,
versus 448 ms / 366 ticks with sensor enabled. App scheduled share rose from
80.9% to 91.5%, and BG still got zero ticks in steady READY windows. One completed
continuous BG READY segment lasted 107375 ms, ending when FdF exited to Launcher;
relaunching FdF reproduced starvation. Compatibility-stub samples remained about
27.5%, versus 28.3% before. No QMI worker symbol or runtime CPU row remains.
No OOM/assertion/reset/watchdog/throttle log observed during the bounded FdF run.

Conclusion: sensor load affects wall time, but acquisition/delivery is not needed
for starvation. App fills freed capacity with the same rendering/SDK-query loop.
Exact drawing caller and exclusive function CPU cost remain unproven. Sampling,
UART and identity-transition limitations are recorded in the artifact report.
No capture or monitor remains running. Disable this diagnostic flag in the next
normal build to restore the accelerometer.

## App render-path trace — 2026-10-10

Operator clarified that App's large CPU share is expected and requested tracing
what prevents BG progress. Added default-off `CONFIG_T5AI_APP_TRACE`: board-local
SysTick PC/LR sampling plus a wrapper on the exact App event queue. Common source,
standard priorities and runtime behavior are unchanged. Diagnostic tables use
8192 bytes of the existing reserved PSRAM aperture; initialization waits for App
creation. Actual-source/decoder/QMI tests: five passed. Image/layout/CRC/loghash
checks passed; disabled generated commands omit App source and queue wrapper.

The first candidate used internal RAM for the tables and caused startup OOM;
reject it for starvation analysis. Evidence and explicit invalid-run note are in
`build/traces/app-bg-trace-20261010/`. Corrected installed candidate/evidence:
`build/traces/app-bg-trace-20261010-psram/`, with exact ELF, dictionary, config,
source snapshots, raw UART/index, reports, manifests and `analysis.txt`.

Corrected 180-second capture: 136242 bytes; no OOM/reset/throttle warning observed.
FdF Time thread 18 had 299 long render-finished/next-event spans, median 448 ms
wall / 366 scheduled App ticks (429–547 ms wall range). This includes redraw
upkeep after dispatch; render-finished's trivial handler alone is not blamed.
Stable event receives were immediate/sub-millisecond, typically two events still
queued. KernelBG remained continuously READY for at least 111692 ms, with zero
CPU in the stable READY windows. App median 832 ticks per roughly 1026 ms window.

11012 App samples, 22 collision rejects. About 28% hit the SDK-compatibility
syscall-stub bucket, which spans legacy2 and legacy3 stubs. Other busy addresses
resolve syscall/task/context bookkeeping and software pixel row lookup/blending.
Inference: software redraw work amplified by repeated SDK queries keeps App
runnable; exact loaded-face drawing caller and inclusive per-function cost remain
unproven. Do not simply lower App priority. No scheduling/graphics fix made.
See `analysis.txt` for measured counts, source paths and sampling limits.

No capture or monitor remains running. Installed AP has corrected App/BG tracing.

## KernelBG blocking trace — 2026-10-10

Operator requested definitive BG blocking attribution. Added default-off
`CONFIG_T5AI_BG_TRACE`, implemented entirely in T5 source/build options.
Common source and standard priorities are unchanged. Compiler hooks observe
four scheduler boundaries including timeout wakeups; linker wrappers record
BG wait objects and mutex lock/unlock caller/owner. Hooks only update a bounded
64-record ring; a priority-3 worker prints snapshots outside locks. Scheduled
running time includes interrupts. No stack/register walks were added.

Candidate and all evidence: `build/traces/bg-block-trace-20261010/`.
BUILD `96b51260007fb8bc1adea8fe1f264927699acad4`; AP-only flash passed in
31.5 seconds, physical `0x132000..0x2d35d0`. CP/resources/PFS not selected.
Candidate manifest verified before/after hardware capture. Firmware layout,
CRC protection and loghash checks passed; three actual-source/decoder/QMI tests
passed. Disabled CMake commands omit trace source, hooks, wrappers and no-LTO
options. Existing dirty display work was preserved; display tracing disabled
for this capture. Source, ELF, dictionary, raw bytes, index and reports retained.

The completed 180-second host capture received 118726 bytes. **No capture or
monitor remains running.** The installed AP still has BG tracing enabled.
Actual runtime bytes verified before and during capture. First throttle/reboot
raw and decoded evidence frozen independently under `hardware/first-*`.

Definite observations:

- One completed **163520 ms continuously READY** interval for KernelBG,
  originally waking from poll, before it could resume. In 98 stable windows
  BG CPU delta was zero. Median per ~1026 ms window: FdF Time 832 scheduler
  ticks, QMI8658 115, trace worker 22. This proves starvation for these
  intervals, rather than lock blocking. Frame frequency does not measure
  this app-thread CPU occupancy; app instruction-level attribution is absent.
- At 16:42:07.423 London, app throttling followed another **5413 ms READY**
  interval. By 16:42:08.488 BG was blocked in receiver callback
  `prv_default_kernel_receiver_bg_cb` (`0x02146d61`), through `prv_put`, on
  waitq `0x28011268`: the putters of **s_system_task_queue** at `0x28011250`.
  The actual msgq path blocks there only when the queue is full. Thus the
  queue consumer was trying to enqueue into its own full queue. Exact enqueue
  caller inside receiver/endpoint processing is not captured. The receiver's
  rescheduling paths call system_task_add_callback; that non-app path permits
  a 3000ms wait, longer than the watchdog's 2000ms expiry grace.
  A full boot followed at 16:42:11. The sequence is consistent with task-WDT
  expiry but lacks an independent final reset-path marker; do not generalize
  it as the cause of earlier resets.
- After reboot: **797 ms BLOCKED** in pfs_read on **s_pfs_mutex**, owner ID 1
  (**KernelMain**), during timeline initialization. Separate READY spans across
  pfs_read unlock were 1012/848/227 ms. A Bluetooth unlock earlier spanned
  2933 ms READY. These distinguish mutex contention from post-release CPU delay.

No device-ring overwrites reported. Three emitted segment pairs lack a half
in decoded output, so UART evidence is incomplete. Instrumentation adds load
(~22 ticks per typical 1026ms window); it is diagnostic, not a behavior fix.
Next correction needs app/background progress and safe receiver requeueing under
full-queue conditions, preserving stock priorities and scoped source rules.

## Investigation tracing disabled and flashed — 2026-10-10

Operator requested all investigation tracing disabled and build-gated, then flash.
All nine T5 flags are n: STATE_TRACE, REMINDER_PROFILE, REBOOT_TRACE,
INTERRUPT_PROFILE, HEAP_TRACE, FAULT_TRACE, FLASH_PROFILE, DISPLAY_PROFILE,
HCI_PROFILE. All default off. Added the last three gates for previously
unconditional flash transaction sampling, display frame/DMA-failure counters,
and HCI counters/periodic heartbeat/disconnect/task snapshots. Disabled source
hooks are no-ops; diagnostic translation units, wrappers, no-LTO overrides and
fatal vector interception are excluded. Stock operational logging and upstream
FIRM-1649 mutex warnings remain unchanged. CP supervision heartbeat, actual
transport/flash behavior, reset-reason preservation and MPU fix remain active.
No task priority changes. Preserve all prior dirty work.

14 scoped tests pass (MPU restore, fault capture/entry, reset and flash).
Ten actual production compiler combinations pass for flash/display/HCI flags,
including enabled and disabled and independent flash/HCI combinations. Firmware,
image-map, protected image and loghash checks pass. Diagnostic symbols/counters
absent from linked ELF; both MPU restore callers retain disable-before-replace
sequence. Legacy board/kernel static suite:54 tests,10 failures/9 errors; identical
failures/errors with pre-slice sources substituted. Obsolete Waf/FreeRTOS/source
assumptions were not repaired in this slice. Existing BLE LTO warnings remain.

BUILD6242298ee15c33b52d86a204e2c9b240139d0d13; FLASH1606112/RAM96672/display118272.
Kernel heap30304, +6528 versus MPU diagnostic. Candidate exact artifacts/source/
config/dictionary/tests/compile combinations sealed BEFORE hardware validation:
build/traces/tracing-disabled-20261010/,117-file manifest verified before/after
AP-only flash. Programming31.2s,physical0x132000..0x2d29fe end exclusive;
CP/resources/PFS excluded. Exact BUILD booted15:31:24 London (programming reboot).
Brief raw/index/decoded boot verification:build/traces/tracing-disabled-boot-20261010/.
Capture stopped after verification; no long trace remains. Previous diagnostic
capture closed at236698 bytes,10-file final manifest preserved. A stock filesystem
warning "Couldn't resolve last written pg" appeared at15:31:25; persistence and
stability are not qualified. Separate watchdog/OOM causes remain unresolved.

## ARMv8 task MPU restore fix flashed — 2026-10-10

Operator requested flash. Sealed80-file candidate manifest verified before and
again after successful AP-only programming (31.5s). Physical range
0x132000..0x2d4ca8 end exclusive; CP/resources/PFS excluded. Exact
BUILD cf92f3362f6bdccffcd9fb24173623e4d38fdc3b booted15:18:25 London.
This boot was caused by programming, not a spontaneous reset.
Previous capture stopped cleanly at517877 bytes;40-file final manifest saved.
New four-hour raw/index/decoded trace uses the sealed candidate dictionary:
build/traces/mpu-task-restore-long-20261010/. Incoming UART bytes and BUILD
verified; session.json records capture PID and start. Pre-flash bytes in this
capture belong to the previous firmware. No additional agent reset.
Startup does not qualify stability or prove the MPU fix on physical fault paths.
Separate watchdog/OOM causes remain unresolved. Prepared section below records
immutable pre-flash validation; its NOT FLASHED status is historical.

## Prepared ARMv8 task MPU restore fix — 2026-10-10

Operator requested a fix focused on MPU. Changed only native ARMv8-M RESTORE_MPU
in kernel/arch/arm/arch.c: savePRIMASK/cpsid, DSB, disable all FOUR old task RLARs
before changing ANY bases, DSB/ISB, install new RBAR/RLAR pairs, DSB/ISB, restore
originalPRIMASK. Both PendSV and first-thread restore use this macro. r0 nextSP
preserved, r1 advances as before; r12 used for saved mask then overwritten by
normal task-register restore (including PSPLIM). No task/global priority change,
MPU global disable, allocation, stack push, diagnostic or static-region change.
The short mask window has fixed instruction count; physical latency unmeasured.

Shared architecture change is necessary: native context-switch assembly owns the
unsafe alias update; a T5 driver cannot make its intermediate descriptors safe.
ARMv8 conditional covers its shared architectural requirement; ARMv7 fallback
expands identically to preserved pre-fix source. Existing dirty changes preserved.

13 scoped tests pass. New actual-macro Cortex-M33 regression runs32 combinations:
region group4/8, enabled->disabled, disabled->enabled, swapped/overlapping old/new
ranges and mixed enabled/disabled slots, originalPRIMASK0/1, unmodified macro or
instrumented descriptor-store boundaries. IRQ injection checks only complete
old/new maps are observed; NMI injection checks intermediate enabled regions do
not overlap flash/each other, with selector restoration. Split STM stores expose
each word boundary; that variant is equivalent instrumentation, NOT execution
of the production STM's internal interruption mechanism. Removing disable stores
fails the regression. Static mappings and nextSP/descriptor pointer/mask checked.
Linked ELF verifies disable/reload/barrier/mask ordering in BOTH actual callers.
Existing fault-stack emulator regression remains passing. Firmware/image-map/
protected-image/loghash checks pass, existing BLE LTO warnings remain. No other
board firmware built; region8 behavior tested in the emulator.

BUILD cf92f3362f6bdccffcd9fb24173623e4d38fdc3b; FLASH1614448/RAM103200/display118272.
FLASH+64, RAM/heap unchanged versus currently flashed MPU diagnostic (heap23776).
CP/resources/config/fault tracing unchanged. Exact firmware/ELF/map/config/
dictionary/source/diff/tests/logs/disassembly sealed BEFORE hardware validation:
build/traces/mpu-task-restore-fix-20261010/. NOT FLASHED. Current long trace still
runs old BUILD6129e282...; no reset/flash during this fix slice. On-device task
switch/IRQ behavior and stability remain unvalidated. Separate watchdog/OOM
categories remain outside this fix; do not claim all reboot causes resolved.

## MPU failure located: transient task-region overlap — 2026-10-10

Two identical complete16-region reports at14:54:32 and15:00:10 London. Region5
RBAR0/RLAR28031fe5 is ENABLED00000000..28031fff, overlapping executable static
flash02120000..0249ffff. Old limit matches workerRAM end; region4 alreadydisabled,
selector4 and BASEPRI/PRIMASK0. Recovered PC0213aef0 is I2C IRQ6 entry;
xPSR16hex identifiesIRQ6. HardFaultPC0220e2a8 is MemManage entry, CFSR1/HFSR40000000.
SHCSR00070405 includes PendSV-active. Linked PendSV unmasks before alias STM at
021f060e, updating bases before limits. Empty slots use0/0, so zeroing region5
base while old limit remains enabled produces EXACTLY this transient overlap.
This strongly identifies an MPU-update interrupt window; actual PendSV PC/beat
was not directly captured. Armv8 forbids region overlap, including STAR cores.

Isolated Cortex-M33 QEMU test reproduces IRQ-entry instruction violation then
MemManage-entry escalation with equivalent transient map; disabling RLAR before
RBAR allows IRQ to run. Both pass expected outcomes. Harness uses RAM update
code and injectedIRQ, not production STM or physical T5 scheduling. Evidence,
source/ELFs/logs/raw snapshots and verified manifest:
build/traces/mpu-fault-long-20261010/mpu-update-review/.

Next narrow fix: ARMv8 task-region restore with saved-PRIMASK critical section;
disable ALL four task RLARs before changing bases, barriers, load complete new
regions and barriers before unmask. Cover first-thread restore too. Preserve
ARMv7 path/static regions/priorities. No fix or flash during this log review.
Separate14:57:08 Watchdog-category reboot (AP70/CP10), no fault marker; not proven
same mechanism. Capture continues. Do not claim all reboots explained.

## MPU snapshot candidate flashed — 2026-10-10

Operator requested flash. Sealed80-file candidate manifest reverified before and
after AP-only programming. Flash succeeded31.7s, physical0x132000..0x2d4c64 end
exclusive; CP/resources/PFS excluded. Exact BUILD6129e282a8350be7e673e31cca2b280684ea68a1
booted14:53:16 London. This is the programming reboot, not a spontaneous fault.
Previous capture stopped cleanly at683348 bytes;53-file final manifest verified.
New raw/index/decoded capture: build/traces/mpu-fault-long-20261010/,
PID2045408/session6329; incoming bytes and matching dictionary/BUILD verified.
Four-hour capture started14:52:30 London, expected stop18:52:30.
Initial evidence manifest verified separately. No additional agent reset.
MPU report remains unvalidated on physical fatal path until fault captured;
startup is not stability qualification. The candidate's sealed results.json
records historical preflash status; live flash-results.json records success.

## Prepared MPU fault snapshot — 2026-10-10

Operator requested focus on MPU tracing; OOM investigation deferred. Added only
T5-local fatal-path diagnostics under existing default-off T5AI_FAULT_TRACE.
Snapshot reads MPU TYPE/CTRL/RNR/MAIR0/MAIR1 and up to16 RBAR/RLAR pairs with
interrupts disabled, restores RNR, never changes region contents or MPU policy.
Includes CONTROL/BASEPRI, original PRIMASK/MSPLIM, PSPLIM, SHCSR/CCR. T5ORIGIN
attempts one original exception PC/LR/xPSR only when the nested HardFault frame
identifies MemManage at its FIRST instruction, with known EXC_RETURN encoding,
bounded AP-RAM frame, stacking/lazy-FP/overflow faults rejected and STKALIGN/FP
size accounted for. Unknown contexts emitX. No general stack/register dump.
Bounds do not establish MPU/security readability; this remains a limitation.

Output reuses160-byte text and32-byte message buffers. Added static128-byte
region snapshot; no heap/task/lock. Whole report shares100ms AON deadline plus
finite polling; abort on first unacknowledged publication, never overwrite its
buffer. Physical delivery remains best effort and bypasses normal mailbox gate.
Existing first T5FAULT record retained; new raw lines T5MPU/T5CPU/T5ORIGIN/
T5REGION may be absent in decoded PULSE log, so preserve raw UART. Snapshot and
origin describe exception-time state, not a sampled earlier MPU transition.

12 scoped tests pass, including indexed MPU snapshots/selector restoration,
16-region cap, original-frame guard/PSP/MSP/alignment/FP cases, unknown security
encoding rejection, shared deadline and ACK failure. Actual Cortex-M33 entry
emulation checks eight stack/frame/mask combinations and original MSPLIM/PRIMASK
arguments; removing limit writes still reproduces Lockup. Compile stack-usage:
capture80 bytes, end/send24, field12; entry saves24, existing512-byte stack kept.
Firmware/image-map/protected-image/loghash checks pass, existing BLE LTO warnings
remain. An overlapping initial build attempt failed objcopy on an empty ELF;
only the completed serialized final build is sealed/eligible for validation.

BUILD 6129e282a8350be7e673e31cca2b280684ea68a1; FLASH1614384/RAM103200/display118272.
StaticRAM+128 versus currently flashed candidate, expected kernel heap23776
(previous23904). CP/resources/config identical. Exact images/ELF/map/config/
dictionary/source/tests/logs/disassembly sealed at
build/traces/mpu-fault-snapshot-20261010/ BEFORE hardware validation. NOT FLASHED.
Current long capture continues using previous BUILD78782b8c...; it cannot contain
these new MPU records until firmware is flashed. No reset during this slice.

## Reboot review: repeated MPU escalation and separate OOM — 2026-10-10

No reset or flash during review; long capture remains running. Four raw faults
received14:31:11,14:34:16,14:35:53,14:36:43 London have identical PC0220dfd0
(MemManage entry), LR/EXC_RETURNfffffff1, xPSR21000004, SP28027240,
CFSR1/HFSR40000000. Completed boots14:31:13,14:34:18,14:35:53,14:36:46
saveCP10/AP73. Instruction-access violation escalates in MemManage context;
original access/task and runtime MPU descriptors remain unknown. Static flash
region is executable and covers handler. Do not interpret MMFAR0 as NULL access.
Last pre-fourth-fault heapfree5524/largest3516; flash report idle, heartbeat ACKs
continue. Cumulative mask52.31ms/tickgap53.56ms are not fault-time proof.

A DIFFERENT reboot occurred14:39:34: explicit OOM at14:39:32.591, failed352-byte
allocation at LR021873bb, exact ELF prv_draw_text_node_tree/text_node.c:541
(task_zalloc_check draw contexts). Fatal heapused23252/free652/largest260 of23904,
ringseq6508 confirms NULL. New boot reports OOM. No ownership/leak attribution
from16-entry ring. Fault diagnostic costs704 bytes versus disabled control, so
measurement overhead can contribute; adding704 alone is not a proven fix.
Notification deserialization and resource618/619 validation failures precede it;
these do not establish bad flash CRC/DB corruption. Flash maxima read4/write21/
erase55ms and idle at last report; no >100ms mutex warning in14:39 interval.

Immutable snapshot, raw/index/decoded, fault times, linked disassembly and full
limitations: build/traces/hardfault-msplim-long-20261010/reboot-review-complete/,
manifest verified. Next investigation should capture bounded active MPU map and
original exception PC/LR, and separately identify retained kernel heap owners
around notifications/rendering. No broad priority/watchdog changes supported.

## MSPLIM fault-entry repair candidate — 2026-10-10

Operator requested fix and flash. T5-local HardFault entry now saves original
MSPLIM without touching the interrupted stack, installs private-stack base as
MSPLIM BEFORE switching MSP/pushing, then restores original MSP BEFORE original
MSPLIM/PRIMASK and hands off with original EXC_RETURN. Saved frame selection and
callee-saved registers preserved; stack protection remains enabled. Only fault
entry changed; mailbox/C formatter, watchdog policy, task priorities, shared
handler and flash layout unchanged. Fault tracing re-enabled in this candidate.

12 scoped tests pass. New emulator regression executes production naked-entry
instructions on Cortex-M33 with CCR.STKOFHFNMIGN CLEAR: all eight combinations
of MSP/PSP, basic/FP frames and original PRIMASK pass capture/handoff checks,
including caller-register clobbering. Removing the MSPLIM writes reproduces QEMU
CPU Lockup. This closes the C-only host-test gap and supports the identified
secondary-fault mechanism; emulator stubs do NOT validate BK7258 CP transport,
physical UART delivery, the original fault or runtime CCR on the frozen watch.
Firmware build/image map/protected-image/loghash checks pass; existing BLE LTO
warnings remain. Final ELF verifies switch/restore ordering and vector linkage.

BUILD 78782b8cb6617c9cdef0e86c82c6bb99d8d95aa2; prepared exact candidate:
build/traces/hardfault-msplim-fix-20261010/. Images, ELF/map/dictionary/config,
source/diff, emulator and regression tests, build logs and disassembly sealed
before hardware validation. CP/resources/config match failing candidate; this
repairs the enabled diagnostic, not the prepared disabled control. Static RAM
103072 bytes, FLASH1613520, display118272.
Sealed before flashing, then AP-only flash succeeded in31.6s, physical range
0x132000..0x2d48ce end exclusive. CP/resources/PFS excluded. Exact repaired BUILD
booted14:30:48 London. New raw/index/decoded trace is under
build/traces/hardfault-msplim-long-20261010/; capture PID2001250, first-reset
watcher PID2002068. Actual received logs verified. Four-hour capture expected
stop18:30:02 London. Previous frozen-candidate capture stopped
cleanly and50-file final manifest verified; repaired candidate78-file manifest
verified after flash. Initial new-boot evidence preserved separately.
Original random reboots remain unresolved; CP emergency delivery/transaction
contention and frame-readability limits remain. Startup is not qualification.

First repaired-candidate fault was captured in RAW UART before a full reboot
at14:31:13 London (no agent reset after programming). T5FAULT PC0220dfd0,
LRfffffff1, xPSR21000004, EXC_RETURNfffffff1, SP28027240, CFSR00000001,
HFSR40000000, MMFAR/BFAR0, frameV. Exact ELF resolves PC to MemManage_Handler
entry. xPSR IPSR4 is MemManage; LR/EXC_RETURN are Handler/MSP exception tokens,
not caller PCs. IACCVIOL plus FORCED indicates an instruction-access violation
escalating in MemManage context. Initial offending access and active MPU regions
are not captured; do not infer an app/task, heap error or flash CRC cause yet.
Saved CP0x10/AP0x73 at next boot confirms AP HardFault reset category.
Physical fault message delivery and stock reset path now have evidence on this
event. Original reset problem is still present. First-fault raw/index/decoded
snapshot and analysis preserved in first-fault-evidence/ with verified manifest;
first-reset watcher independently preserved reboot evidence. Capture continues.


## Fault-hook review: concrete MSPLIM defect — 2026-10-10

Review of the FLASHED diagnostic ELF (not the disabled control) found an unsafe
exception-stack switch. Reset_Handler programs MSPLIM=__isr_stack_start__=
0x28026ea0. New HardFault entry loads MSP=0x2801d5d0 (private stack top), then
pushes16 bytes to0x2801d5c0 WITHOUT changing MSPLIM. The new stack top is
0x98d0/39120 bytes below the active boot-configured limit. Both values, the
literal relocation and absence of an MSPLIM write were verified in linked
assembly. Native code contains no subsequent MSPLIM write or STKOFHFNMIGN set.

With CCR.STKOFHFNMIGN clear (architectural reset default), that push violates
the hardware stack limit while HardFault is active, which can lock up before
C capture, T5FAULT output, stock reboot-reason recording or force-reset. This
fits both silent UI freezes and missing fault message. It can turn an original
HardFault/reboot into a freeze; it does not identify the original fault or
explain its startup timing. Runtime CCR/lockup/fault PC were NOT captured, so
the observed execution at this exact instruction remains unproven. No broad
priority/clock conclusion follows. AP hardware watchdog is unstarted.

C-only host tests deliberately compile out the naked entry; therefore the
11 passing tests did not check this hardware stack-limit condition. Review
artifacts and deterministic linked-image assertions are preserved under
build/traces/minimal-hardfault-long-20261010/hook-review/. These checks establish
the invalid stack/limit pairing, not an on-device fault reproduction.

Narrow repair design: save MSP AND MSPLIM without stack access, set MSPLIM to
the private stack base before switching MSP/pushing, and restore original MSP
before restoring original MSPLIM and PRIMASK on the stock-handler handoff.
Keep stack protection enabled. Test the actual M33 exception entry including
stack limits and basic/FP frames, not just the C formatter. Record fault reason
before optional transport so a transport failure cannot mask that evidence.
No source fix or new firmware flash performed during this review.

Secondary review risks: emergency send checks only FIFO capacity and bypasses
the normal one-active-mailbox-transaction gate; CP congestion/delivery is not
proven safe. Frame bounds alone are not a complete MPU/security readability
check. Neither is established as the observed freeze cause. Avoid attributing
this to an unbounded poll: both transport loops have finite iteration counts.
Linked dcache_flush/invalidate are no-ops for this target; they do not explain
a new cache-maintenance loop in the fault hook.

Architecture reference: Armv8-M exception model guide, stack-limit checking and
CCR.STKOFHFNMIGN: https://support.arm.com/documentation/107706/0100/System-exceptions/Fault-exceptions-and-their-causes/Using-FAULTMASK-in-a-configurable-fault-handler

## Repeat freeze and fault-trace-disabled control — 2026-10-10

Operator confirmed the watch froze again after the requested hardware reset.
Normal logs end at 14:14:57.386, about 34 seconds after the 14:14:23 boot;
first run ended about 36 seconds after boot. Both freezes are close in startup
age. No T5FAULT raw marker or spontaneous boot observed in either interval.
Second snapshot: build/traces/minimal-hardfault-long-20261010/freeze-repeat-141550/,
checksum manifest preserved. Capture continues. Temporal association makes the
last diagnostic change suspect; it does not establish which code path caused
freezing. The hook changes fatal exception-stack and mailbox handling; host
unit tests did not execute those paths on physical hardware. No proven specific
hook defect or fault site has been recovered.

Prepared CONTROL build with only CONFIG_T5AI_FAULT_TRACE disabled; parsed Kconfig
comparison verifies that is the sole option difference. Earlier reset reason,
heap/DB/IRQ/reboot diagnostics, priorities, CP, resources and PFS layout retained.
No source changes reverted; the optional fault implementation remains default off.
Final ELF contains stock HardFault_Handler and no diagnostic capture/private
stack/renamed-handler symbols. Vector entry points to the stock handler.
11 scoped tests and firmware/image-map/protected-image/loghash checks pass;
existing BLE LTO warnings remain. FLASH/RAM/display1612816/102368/118272 bytes,
kernel heap24608 (704 bytes restored versus failing diagnostic candidate).

Control BUILD e10561e6439b4c995385165bba26f3aff83a9210, exact images/ELF/dictionary/config/source and validation
sealed at build/traces/fault-trace-disabled-control-20261010/. NOT flashed.
Do not interpret a prepared control as evidence the freeze or original random
reboots are fixed. To distinguish repeat freezes, compare otherwise similar
startup/workload after disabling the hook; earlier firmware also had spontaneous
reboots. No rollback, extra reset or flash issued during this review.

## Freeze after minimal HardFault candidate flash — 2026-10-10

Operator confirmed Select, other buttons and touch are all unresponsive.
Candidate boot was 14:08:21 London; normal
firmware logs stop at 14:08:57.946, about 36 seconds later. Raw UART continues
receiving CRC-valid PULSE/PPP control traffic while the log subscriber times out
and reconnects. This does not prove the AP scheduler or UI is running. No raw
T5FAULT marker and no subsequent candidate boot observed at initial review.
No terminal decoded OOM, flash CRC error or assertion recovered. Last heap at
AON 36545 ms: used18408/free5496/largest3316; does not exclude later corruption.
Final IRQ maxima gap/mask/body are 1913/1849/1702 AON ticks (~59.8/57.8/53.2ms),
with no new clock/reload change recorded immediately before silence.

Preserved freeze snapshot and verified hashes under
build/traces/minimal-hardfault-long-20261010/freeze-141056/;
freeze-review.json records findings. Live capture and first-reset watcher
continue. No reset/rollback/new flash issued in response to this report.
Fault/lockup, scheduling deadlock and logging failure remain unresolved; the new
fatal hook's contribution cannot be excluded. This candidate is not qualified,
and emergency fault delivery remains unvalidated on hardware.

Operator subsequently requested hardware reset. Issued tyutool reset on if00
at 14:14:22 London; new boot at 14:14:23 verified the same exact BUILD. This
boot is commanded, including the first-reset watcher's saved event; do not
count it as another spontaneous reboot. Saved CP/AP reasons were both zero.
Capture continues; reset restores execution but does not establish a fix.
See operator-reset.json and operator-reset.log in the live trace directory.

## Minimal HardFault capture candidate — 2026-10-10

Implemented at operator request to locate AP HardFaults (saved AP reason 0x73).
CONFIG_T5AI_FAULT_TRACE defaults off; enabled in this diagnostic candidate.
T5-local entry on a private 512-byte MSP stack captures PC/LR, stacked xPSR,
EXC_RETURN, frame SP, CFSR/HFSR and valid MMFAR/BFAR fields. V/X marks frame
accepted/rejected. No stack walk, general-register dump, allocation, task mutex,
priority, flash-layout or reset-policy changes. Original MSP/PRIMASK/EXC_RETURN
are restored before entering the unchanged shared handler. Build selection
renames only the shared handler symbol; final vector override is verified in ELF.
A linker --wrap prototype was discarded because vector-local weak definitions
bypass it; no hardware validation used that prototype.

The normal serial path deliberately drops exception output. One independent,
cache-line-isolated assert-out CP mailbox frame now sends a plain T5FAULT line
before the stock handler. No RX draining/ACK stealing, runtime staging-buffer
reuse, UART remux or CP modification. Total AON deadline is 50 ms with finite
poll counts if AON stops. Delivery is BEST EFFORT: CP/FIFO congestion, existing
transactions, CP failure or immediate hardware failure may still lose it. No
new persistent-storage claim or retention dependency. HardFault VECTOR only:
MemManage/BusFault/UsageFault handling and software-generated HardFault reason
paths are not intercepted; absence of T5FAULT does not exclude those paths.

Frame validation rejects stacking/lazy-FP errors, STKOF, bad alignment, invalid
EXC_RETURN prefix and addresses outside internal AP RAM before reading PC/LR.
Both basic and extended FP frames use core offsets 5/6/7; FP context follows
core context (pinned Tuya CM33 trap and Armv8-M exception model). This is not a
complete MPU/security-state validator; severely corrupted exception state or
faults inside the diagnostic itself can still lock up. Cannot guarantee PC
attribution for imprecise bus faults. Private stack is not a recovery mechanism.

Validation: 11 actual-source tests pass (fault/reset/reboot/timing/heap/DB).
New coverage: basic and extended frames, forbidden unmapped frames, limits,
valid address bits, duplicate entry, full FIFO, stopped/wrapping AON, CP release
and CP nonresponse. Firmware build/image map/protected-image/loghash checks
pass; existing BLE LTO warnings remain. Final vector 0x021ed03d targets the
new entry; disassembly verifies capture then stock-handler branch.
FLASH/RAM/display = 1613488/103072/118272 bytes; static RAM +704 bytes versus
running image, kernel heap reduced from 24608 to 23904 bytes. Heap pressure
remains an investigation concern; this diagnostic is temporary.

BUILD e9c59ed7675a879f54cb432ae026e138baf79108; sealed exact candidate at
build/traces/minimal-hardfault-20261010/ (images, ELF/map, dictionary, config,
source snapshots/diff, tests/build logs and vector/disassembly checks).
Flashed AP-only on operator request: programmer success in 31.5 s; physical
0x132000..0x2d48ac end exclusive. CP/resources/PFS excluded. Candidate boot at
14:08:21 London verified BUILD e9c59ed7675a879f54cb432ae026e138baf79108.
New raw/index/decoded trace: build/traces/minimal-hardfault-long-20261010/;
capture PID 1944118, first-reset file watcher PID 1945019. Actual received bytes
verified after candidate boot. Four-hour capture expected to stop around
18:07:33 London. Previous trace stopped cleanly and final 36-file manifest
verified; exact candidate 81-file manifest reverified after flash. Initial new
boot evidence is frozen with its own manifest. No subsequent agent reset issued.
Emergency fault delivery remains untested on hardware; startup is not stability
validation. This locates evidence and does not fix the originating HardFault.

## Reset reason now identifies AP HardFault — 2026-10-10

Operator reported reboot; full boot at 13:49:56 London, about 4 min 51 s after
programming. Analysis also observed another full boot at 13:50:29; the operator
has not separately confirmed whether that second boot was uncommanded.
Both new SoC/board snapshots show saved CP=0x10/AP=0x73. 0x73 is Pebble base 0x60
plus RebootReasonCode_HardFault=19. Normal boot-source log also reports
AP=115/CP=16, validating recovery from CP-saved words. The preservation fix has
hardware evidence of exposing a specific AP reason previously overwritten by
Unknown. CP NMI watchdog remains the reset mechanism; AP fault is the useful
initiating category. It does not prove a CP-only watchdog failure.

First-reset watcher evidence verified. Frozen raw/index/decoded snapshot:
build/traces/reset-reason-preserve-long-20261010/analysis-135044/, manifest
verified. hardfault-review.json in live directory records interpretation.
Capture remains receiving; no firmware edit, reset or flash issued in review.
Previous ring is unavailable after reset; no fault PC/LR, access address or
specific instruction has been recovered. Privileged thread versus IRQ context
is unknown. Do not infer null pointer, use-after-free or task identity yet.

No decoded OOM, flash CRC/IPC failure or terminal force-reset marker in the
completed boots. First completed boot last heap at AON 278711ms had
used=19188/free=5420/largest=4016; second last heap at AON 29801ms had
used=10852/free=13756/largest=13732. These samples do not rule out later memory
corruption or allocation pressure. First boot lost 419/overwrote 21 diagnostic
records; second lost 22/overwrote 2. Missing terminal messages are inconclusive.
Task watchdog marker 16 at AON 278673ms was followed by reported recovery; final
saved fault is HardFault, so do not label this a simple task-timeout reset.

New DB profiling worked: pin flush 564 ms, reminder 530 ms, both successful and
no erases within either flush. Pin create 415 ms includes 299 ms write/109ms read;
commit 110 ms. Reminder create 403 ms. Categories overlap; these are complete API
wall times. Pin next_item max 1420 ms is outside flush profiling. Flushes completed
around 13:46:13..14, minutes before the reset: no causal link to terminal fault
established. First-boot AON/kernel heartbeat interval differences<=1ms; remaining
mask/gap/body maxima 56.56/57.31/52.78ms, not fault-site attribution.

Next focused investigation is locating the AP hard-fault instruction/context,
using minimal fault evidence rather than further timeout or priority changes.
Existing no-general-stack/register-tracing constraint remains; new diagnostic
scope must stay specific to this fault. AP hardware watchdog remains deliberately
unstarted. Configured period fields in inherited analysis metadata are not
measurements of an active AP watchdog.

## Reset-reason candidate flashed and trace receiving — 2026-10-10

Operator requested flash and trace. All 72 sealed files verified before/after
programming. Guarded AP-only flash succeeded in 31.6 s, physical range
0x132000..0x2d45e2 end exclusive; CP/resources/PFS excluded. Exact BUILD
e10561e6439b4c995385165bba26f3aff83a9210 booted at 13:45:05 London. Sealed prepared-only
results remain immutable; flash outcome and boot evidence saved separately.

Previous DB/heap capture stopped cleanly at 311639 raw bytes; its 34-file final
manifest verified, including first-reboot evidence. New raw/index/decoded capture:
build/traces/reset-reason-preserve-long-20261010/, PID 1888421, session 70487,
460800 baud and exact sealed dictionary. Host UARTs and increasing received bytes
verified. Four-hour listener started 13:44:18 London;
expected stop 17:44:18. First-reset file watcher session 4179
preserves failure/context without another UART reader. Pre-programming bytes
belong to previous firmware; no agent reset after programmer reboot. Initial
saved CP/AP words are 0 at both boot stages. Specific reason persistence through
an actual fault reset remains unvalidated; no stability claim. Recheck process
and actual received bytes on resumption; no unattended analysis is promised.

## Reset-reason preservation fix prepared — 2026-10-10

Operator requested implementing proposal 1 only. T5-local driver now snapshots
CP-saved AP/CP words at 0x2809f71c/0x2809f718 once before native cache setup.
Boot getters return that immutable snapshot instead of live/cleared R7A. Existing
watchdog flags, reboot-reason fallback and boot-source logging therefore use the
CP copies in normal builds, independently of optional diagnostics. CP cause is
kept distinct from AP Pebble fault codes. No CP writes or new cache operations.

Generic force reset (requested AP 0x60) preserves an existing mailbox tag
0x50..0x5f or specific Pebble tag above0x60 through the final defined reason.
Zero, raw hardware watchdog arm marker2, genericUnknown and out-of-range tags
fall back to0x60. Explicit caller-supplied reasons take precedence. Selection
and write run with IRQs disabled; ForceReset event/freeze use the selected
reason. Reset keys, periods, clock/reset mask sequence and watchdog ownership
remain unchanged. This fixes reason destruction, not the initiating fault or
reboot stability. Reset can still occur before a reason is recorded, and RAM
retention remains unproven; CP-saved words depend on the pinned boot chain.

No shared source edits for this slice. Ten focused actual-source tests pass,
including diagnostics-on/off, cached CP-word recovery with misleading PMU R7A=3,
snapshot immutability, mailbox/assert/watchdog/OOM tags, generic fallbacks,
explicit reason precedence, non-AP field preservation and force-reset periods.
T5 firmware build and image/map/loghash checks pass; existing BLE LTO warnings
remain. FLASH/RAM/display =1612816/102368/118272 bytes; kernel heap remains24608.
Candidate BUILD e10561e6439b4c995385165bba26f3aff83a9210; exact artifacts/dictionary/source/test/build evidence
sealed at build/traces/reset-reason-preserve-20261010/ before hardware validation.
CP/resources match the diagnostic candidate. Not flashed. Existing capture
continues with the old sealed dictionary; no reset issued. This change does not
start the deliberately disabled AP hardware watchdog or modify task priorities.

## First diagnostic-candidate reboot: saved NMI reason — 2026-10-10

Operator confirmed first uncommanded reboot. Full candidate boot at 13:32:15
London, about 89 s after programming boot. First-reset watcher manifest verified;
new immutable raw/index/decoded snapshot analysis-133414 under
build/traces/db-heap-investigation-long-20261010/ is SHA-256 verified.
first-reboot-review.json records findings. Capture remains receiving; no new
flash or reset issued.

New evidence: both SoC and board entry saved CP=0x10, AP=0x60, while live PMU
R0/R7A=3 and previous ring unavailable. Pinned CP include/components/system.h
names 0x10 RESET_SOURCE_NMI_WDT; startup CPU0 NMI handler writes that reason.
AP 0x60 matches T5AI_RESET_REASON_PEBBLE_BASE | Unknown used by
fw/kernel/reset.c:system_hard_reset -> t5ai_force_whole_chip_reset. That path
programs NMI/AON force-reset periods and overwrites any earlier specific AP
reason. Thus saved words support an NMI watchdog reset mechanism and suggest
the generic AP hard-reset route, not a proven CP-only spontaneous failure.
Original fault/reset initiator remains unknown; missing retained markers cannot
exclude an AP reset path. Do not call this a proven watchdog-feed starvation reset.

Another diagnostic finding: AP watchdog running=0/feeds=0 throughout observed
first boot. Board calls watchdog_init only; no production T5 watchdog_start
call site is present. The corrected 30000 ms start/feed period is not proof
that this driver is actually running on the watch, nor evidence of a 30 s
starvation interval. CP NMI watchdog and Pebble task-watchdog are distinct.
Do not blindly start/change watchdogs before reviewing ownership with CP.

No OOM/assert/Resetting, flash CRC/IPC failure, reason-set/force-reset marker
or T5DB explicit-flush block in the completed first boot. Largest pin warning
1267 ms is next_item, outside the explicit-flush profiler. Last heap report at
AON 50909 ms used=18404/free=6204/largest=4356 (24608 total), about 39 s before
next boot; this does not exclude subsequent exhaustion. Last priority-1 drain
59086 ms, heartbeat ACK through61446 ms, while HCI logs at kernel83467 still
report no active flash/fault. This is a terminal low-priority diagnostic blind
interval, not proof that CP heartbeat stopped or that the CPU stopped entirely.

Kernel/AON heartbeat interval error <=1 ms; cumulative mask/gap/body maxima
56.72/57.47/43.84 ms. Clock remains 240 MHz/reload239999 at last sample.
No timing drift cause established. Trace coverage and reset-reason overwrite
now constrain the next investigation: distinguish AP hard-reset entry from CP
NMI-origin failure and preserve the original reason before the generic fallback,
while extending beyond explicit flushes only if workload evidence justifies it.
No firmware edit made in this review.

## DB/heap investigation candidate flashed and capture receiving — 2026-10-10

Operator requested flash. Verified all 87 sealed candidate files before/after
programming; guarded AP-only flash succeeded in 31.5 s at physical
0x132000..0x2d4516 end exclusive. CP/resources/PFS excluded. Exact ELF BUILD
b2805f816527cc9c21f410f342be1ea95d02f604 booted at 13:30:46 London. Candidate's
prepared-only results and manifest remain immutable. Programmer/tyutool logs,
initial raw/decoded boot evidence and session metadata saved separately.

Previous watchdog-timeout capture stopped cleanly after 1845473 raw bytes;
its 50-file final manifest verified, retaining earlier failure evidence. New
raw/index/decoded capture is build/traces/db-heap-investigation-long-20261010/,
PID 1860431, exec session 97607, exact sealed candidate dictionary, 460800 baud.
Host UART enumeration and increasing received bytes verified. Four-hour capture
starts 13:29:59 London; expected stop 17:29:59.
Listener preceded programming; earlier bytes belong to old firmware. First-reset
file watcher session 48866 preserves context without a second UART reader.
No agent reset follows programming. Saved CP/AP words are zero at both observed
programming-boot stages; this alone does not classify later resets. Diagnostics
are not a stability fix. Recheck received bytes/process when resuming; no
unattended analysis is promised. Repeat normal full resync/app/notification
workload to exercise DB phases and allocation pressure.

## Next DB/heap investigation diagnostics prepared — 2026-10-10

Operator requested implementing the next phase's traces. Candidate is prepared
only, not flashed: `build/traces/db-heap-investigation-20261010/`, ELF BUILD
`b2805f816527cc9c21f410f342be1ea95d02f604`. Current watchdog-timeout capture
continues on its original sealed dictionary; do not switch its decoder to this
candidate. Actual raw receive offset advanced during implementation. No watch
reset, flash, CP change, priority change or allocator policy change was issued.
All existing uncommitted work is preserved; no new shared source edits.

The default-off T5 reminder profiler now covers explicit timeline storage flushes
for both pindb and reminderdb. `T5DB name` begins a summary block. It measures
DB acquisition wait separately from wall hold through immediately before unlock,
then rewrite wall/kernel time, phase CPU/wall, full flash API R/W/E calls and
bytes, all intercepted mutex waits, longest sampled wait/owner, and nested
write-driver/analytics/unlock durations. Full flash API durations include nested
CP transport, READ_DONE, retries and mutex waits, addressing the older IPC-only
maxima's blind spots. Categories overlap and must not be added. All new summary
logging occurs after the selected DB's storage flush has returned and unlocked.
An exclusive static slot avoids interleaved summaries; competing/nested flushes
pass through, with cumulative skipped count. Existing shared FIRM-1649 warnings
are untouched. Automatic compaction, insert/delete and rewrites called internally
within settings_file.c are outside this targeted flush profiler; no completeness
claim for all DB holds. CPU measurement is for the owner task, not other tasks.

New default-off `CONFIG_T5AI_HEAP_TRACE` (requires reboot trace) intercepts
external heap malloc/zalloc/calloc/realloc/free for kernel heap only. Sixteen
32-byte records hold AON time, completion sequence, task ID, operation, requested
bytes, pointer, supplied client PC and wrapper call site. Null allocation
pointers identify failure; free is op 5. No allocation, heap scan or logging
occurs in the record hook; brief IRQ masking protects the fixed ring. Internal
allocator calls are deliberately not double-counted. Realloc's record reports
new pointer/request, not the old pointer or old size. This is recent operation
history, not a live-allocation ledger or leak proof. Records are completed-call
observations and can be ordered differently from operation entry under preemption.

The existing priority-1 reboot worker scans heap totals at most once per ten
AON seconds. It prints recent operations when free <2048 or largest block <512.
The croak_oom wrapper makes a best-effort history dump before the original fatal
handler. A dump guard suppresses recording during printing across tasks;
`omitted_during_dump` exposes cumulative omissions (sampled before that dump).
Nested fatal dumps are suppressed. Ring overwrite is bounded; abrupt resets,
UART backpressure, or another failure during a dump may lose terminal evidence.
No heap history retention is claimed. Heap scans and logging perturb timing.
Only selected diagnostic translation units lose LTO; actual ELF confirms
kernel_malloc_check preserves LR and calls both heap and OOM wrappers, plus
pin_db_flush calls the DB wrapper and storage lock calls mutex wrapper.

Reboot diagnostics additionally sample raw CP-saved CP/AP words at
0x2809f718/0x2809f71c at SoC and board entry, alongside existing PMU words.
Interpret as raw observations of pinned CP storage, not authoritative reset
classification until hardware validated. No cache invalidation or CP writes are
added. Watchdog start/feed/stop update bounded AON feed count, age and maximum
interval counters; start counts as a feed, stopped intervals do not inflate
max gap. Existing drain prints them every two seconds; counters are not retained,
and an unscheduled worker cannot report the final feed age before an abrupt reset.
Watchdog timing, force-reset sequence and task policies remain unchanged.

Validation: 9 focused actual-source tests pass (heap, DB phases/filter/failure/
task isolation/CPU, boot snapshots/feed tracking, disabled headers, watchdog and
T5 timing); native test_kernel and test_kernel_le_client pass. T5 and existing
Asterix firmware builds pass. T5 FLASH/RAM/display = 1612624/102368/118272 bytes;
existing BLE LTO warnings remain. Static RAM grows 640 bytes versus the flashed
watchdog candidate. Kernel heap is now 0x28028fe0..0x2802f000 = 24608 bytes,
640 bytes smaller; account for this when comparing OOM. State trace stays OFF,
other existing bounded diagnostics ON, watchdog timeout 30000 ms. Exact raw/
protected AP, ELF/map, dictionary/config/source snapshots, tests/build logs and
wrapper disassembly are sealed with a verified SHA-256 manifest before hardware
validation; CP and system-resource hashes match the previous candidate.

## Pin-DB hold and explicit OOM reset investigated — 2026-10-10

Operator requested investigating pin-DB holds, T5 contributions, flash operations
and CRC/corruption evidence. New manifest-verified raw/decoded snapshot:
`build/traces/watchdog-timeout-fix-long-20261010/analysis-122759/`.
`pindb-review.json` in the live trace directory records sampled counters/source
findings. Capture continues; no firmware changes or flash issued in this review.

The 17385 ms warning is wall time from after DB mutex acquisition to immediately
before unlock, not the acquisition wait. It includes preemption and nested
PFS/flash waits, not just CPU execution. Warning output happens after unlock and
can be delayed. timeline_item_storage_flush holds this mutex across the whole
settings_file_rewrite_filtered operation: temporary file creation, record scan,
filter/copy, close/commit and reopen. Shared settings code already suspends the
current task's watchdog for up to 60 s during rewrite; this does not suspend
other blocked tasks' watchdogs. No shared-code change is needed to explain the
lock coverage. Pin DB phase/CPU time is unmeasured: the existing T5 wrapper only
profiles files named reminderdb.

Around the hold, flash counters at 12:21:24 versus 12:21:54 increase by 1060
reads and 264 writes. Erase count remains 48, with last erase at kernel tick
11203. Read/write/erase maxima are 4/20/53 ms in that boot. This is a broader
sample window containing other DB work, not a per-pin operation ledger; it
rules out a new multi-second erase in the measured window but does not allocate
17.4 s among pin rewrite phases. T5 PFS reads use a bounded 4x512-byte cache
(header cache disabled), CP READ/CRC/copy/READ_DONE, and synchronous writes.
The CP path contributes latency for many small shared-PFS operations. Caller
base priority is temporarily raised to 3 during handshakes and restored; no
priority leak was established. Mutex wait before a transaction, READ_DONE,
cache/copy/CRC overhead and time after priority restore/before unlock are absent
from the printed read/write duration maxima. Per-call 5 s IPC timeouts are not
an end-to-end database deadline. CP-backed synchronous logging can also block
and diagnostics can compete for scheduling, but this hold has no evidence
attributing its duration to those paths.

No PFS page-CRC-corrupt, flash IPC fault, completion failure, CP flash error,
PFS version-verification failure or settings boot-check failure was found in
the decoded candidate snapshot. Flash read CRC failure would produce a generic
flash IPC fault; there is no such observed report. Startup does report an
unresolved last-written-page tag and one incomplete file creation. The latter
is explicitly a PFS interrupted-create cleanup path, not itself a CRC failure;
repeated resets can cause it. These warnings leave persistence unqualified and
do not establish physical flash corruption. Resource check failures are separate
and are not evidence of a bad flash CRC. Log loss prevents a universal negative.

A newer reset is now explicit: at 12:26:05.936, CROAK OOM failed to allocate
128 bytes, LR=0, used=24852, free=396, largest_free=40; Resetting follows and
full boot at 12:26:07. Exact ELF kernel heap is 0x28028d60..0x2802f000, 25248 bytes,
matching used+free. This strongly identifies kernel-heap exhaustion/fragmentation
as the immediate trigger for this reset; allocation caller and any leak remain
unknown. Notifications were arriving immediately beforehand. This boot's pin
mutex maximum is only 1068 ms and reminder 479 ms, so a 17-second DB hold is not
required for the observed OOM reset. Another full boot at 12:27:18 has no decoded
OOM/assert/reset marker in the snapshot and remains unexplained. Earlier silent
resets must not be relabelled OOM on this evidence.

Next narrow investigation: extend the existing SoC-local rewrite profiler to
pindb with phase/CPU/end-to-end flash and mutex-wait totals, emit summaries after
DB unlock, and add bounded kernel-heap attribution around sync/notification
work. Keep shared code/priorities unchanged. Capture watchdog feed age and CP
saved boot reasons for silent resets. No CRC retry/erase/cache or timeout change
is justified solely by this analysis.

## Reboot persists after watchdog timeout correction — 2026-10-10

Operator reported reboot. Candidate BUILD 79421f2f3de8ef9a6e83d71359f0ed7514b532d6
fully rebooted spontaneously at 12:22:02 London, about 2 min 21 s after programming
boot. Frozen snapshot build/traces/watchdog-timeout-fix-long-20261010/analysis-122353/
includes raw UART/index, decoded trace, per-boot tails/warnings and verified
manifest; first-reset manifest also verified. Correcting the 8 s/30 s mismatch
has not eliminated resets. No additional reset/flash command was issued.

This completed boot has no surviving watchdog reason-set, force-reset, CP reset
request or mailbox-timeout marker, and no watchdog recovery log. Earlier in the
boot, 34 records were lost and 32 overwritten, so this cannot exclude a reset
path. Near the end, event sequence 390..422 is contiguous, successful CP ACKs
continue through AON 139434 ms, and last drain reaches 139934 ms. UART stops at
12:21:59.514 then next boot appears 12:22:02. Latest sampled clock remains
240 MHz/reload 239999 and RTC/kernel heartbeat interval errors remain <=1 ms.
Cumulative mask/gap/body maxima are 48.91/49.91/35.09 ms. No evidence here proves
a 30-second AP watchdog feed starvation interval; last-feed timestamps and CP
saved boot reasons are still absent. Healthy CP ACKs also do not prove a healthy
AP watchdog/feed task or exclude an abrupt CP reset.

Largest new anomaly: pin DB flush held its mutex 17385 ms (printed 12:21:46.516),
followed by AppDB flush and reminder flush at 503 ms. It is the pin DB hold,
not reminder DB. Hold duration alone cannot separate work from preemption or
blocked logging/I/O. HCI remains active/fault=0; flash erase maximum 53 ms was
from startup, no active command at 12:21:54.748. Reset reason remains unknown
with zero retained ring at next boot. The following boot already has watchdog
stall markers/recovery and a 54.56 ms mask by snapshot, showing the stall/mask
problem persists independently of whether a marker survives a particular reset.

Trace remains receiving data with the exact candidate dictionary. Next focused
instrumentation should record watchdog feed age and CP saved startup reason,
and attribute remaining long masks; do not infer reset cause from live reason
fields or change standard task priorities. No firmware changes made in this review.

## Watchdog-timeout candidate flashed and trace restarted — 2026-10-10

Guarded AP-only flash succeeded in 31.6 s, physical range 0x132000..0x2d3efa
(end exclusive). CP/resources/PFS were excluded. All 67 sealed candidate files
verified before and after flashing: build/traces/watchdog-timeout-fix-20261010/.
Exact BUILD 79421f2f3de8ef9a6e83d71359f0ed7514b532d6 booted at 12:19:41 London.
Exact ELF watchdog_feed contains key/period 0x005a7530, then 0x00a57530 (30000 ms).
Firmware build passed, FLASH/RAM/display sizes remain 1611152/101728/118272 bytes.
Existing BLE LTO warnings remain. Candidate pre-flash results remain immutable.

Previous timing/mask capture stopped cleanly and its 52-file final manifest
verified, including earlier frozen failures. Fresh raw UART/index/decoded capture
is build/traces/watchdog-timeout-fix-long-20261010/, PID 1721028, session 76512,
460800 baud, exact sealed dictionary. Host UART enumeration and actual received
bytes verified, including candidate boot/heartbeat records. Four-hour capture
started 12:18:55 London; expected stop 16:18:55.
Listener starts before programming; earlier bytes belong to the previous build.
No agent reset followed programmer boot. First-reset watcher session 99290
preserves failure/context without another UART reader. Programmer/tyutool logs,
session metadata and initial raw/decoded evidence saved. Startup reports PFS
last-written-page/incomplete-file warnings; persistence remains unqualified.
Timeout correction does not establish reboot stability or fix remaining masks.
Recheck process and received bytes at resumption; no unattended analysis is promised.

## Configured T5 watchdog timeout fix — 2026-10-10

Operator requested fixing the watchdog mismatch, flashing and tracing. T5
watchdog_start/feed now program CONFIG_WATCHDOG_TIMEOUT_MS (30000 ms for this
candidate), replacing the hardcoded 8000 ms. Pinned BK7258 BSP AON driver/HAL
passes milliseconds directly through a 16-bit period mask with 0x5a/0xa5 keys.
Compile-time bounds require a nonzero period no greater than UINT16_MAX,
preventing silent truncation or accidental disabling. Stop and force-reset
sequences remain intact. No shared watchdog policy, task priorities or CP image
changes were made. Standard task watchdog detection/recovery still applies.

Seven focused actual-source tests pass (reset/watchdog, reboot trace and timing).
Watchdog start/feed/stop/reset-state tests exercise 1, 1234, 30000 and 65535 ms;
feeding after stop keeps the watchdog disabled. The mismatch is a proven source
issue, but its correction does not establish the reset cause or eliminate long
interrupt masks. Actual expiry duration and stability require hardware evidence.
Exact candidate artifacts will be sealed under
`build/traces/watchdog-timeout-fix-20261010/` before programming. The previous
trace remains on its original dictionary until stopped and preserved; the next
capture must use this candidate's exact dictionary. Last-feed timestamps and
CP saved startup reason remain absent from existing diagnostic evidence.

## Third timing-candidate spontaneous reboot — 2026-10-10

Operator reported another reboot. Exact candidate boot confirmed at 12:12:30
London, after approximately 9 min 31 s of uptime. Frozen manifest-verified
snapshot: `build/traces/timing-mask-fix-long-20261010/analysis-121359/`.
Capture PID 1683658 remains running, with post-reboot UART/log bytes received.
No reset or flash command was issued.

The completed third boot again has RTC/kernel heartbeat interval differences
at most 1 ms; last clock sample is 240 MHz/reload 239999. Cumulative SysTick
gap/mask/body maxima are 57.56/51.75/41.41 ms. Watchdog stall-reason markers
and four recovery logs appear; last recovery printed at 12:11:45.684. Display
updates continue through 12:12:26 and HCI heartbeat at 12:12:17 reports no fault.
No active flash command was reported there; last flash write was around boot
289.9 s, well before this reset. Pin/reminder mutex maxima are 556/117 ms,
so this reset does not require another multi-second database hold.

Diagnostic delivery is severely impaired: 690 records reported lost and 117
overwritten. Last surviving heartbeat ACK event is at AON 286630 ms, whereas
last drain reaches AON 569760 ms immediately before reboot. This gap in retained
event evidence is not proof that CP stopped acknowledging for those 283 s.
The priority-1 drain and logging path cannot currently preserve enough history
to identify the final reset trigger. No explicit force-reset/CP-request marker
or assertion survived. The 8 s hardware watchdog versus 30 s configured timeout
remains a concrete discrepancy; watchdog feed history and CP saved boot reasons
remain the next missing evidence. No source change was made for this review.

## Two timing-candidate spontaneous reboots reviewed — 2026-10-10

Candidate BUILD 9070d487e09b6a43b4d10e6429cda0f8f5aa7cf7 fully booted at
11:59:42 after programming, then spontaneously at 12:01:30 and 12:02:59 London.
No agent reset followed programming. Frozen, manifest-verified evidence:
`build/traces/timing-mask-fix-long-20261010/analysis-120708/` (raw UART,
index, decoded log, per-boot tails/warnings and results). First-reset evidence
also verified. Capture PID 1683658 was still receiving bytes at 12:10:14;
no further full boot appeared by that check. Capture remains running.

| Boot ending in reset | Largest SysTick gap | Largest measured IRQ mask | Largest handler body |
| --- | ---: | ---: | ---: |
| 12:01:30 | 19.28 ms | 18.50 ms | 14.34 ms |
| 12:02:59 | 55.78 ms | 54.41 ms | 48.59 ms |

Heartbeat RTC/kernel interval differences remain at most 1 ms in both completed
boots and the partial third boot. Clock rows now directly show 120 MHz/reload
119999, 60 MHz/59999 and 240 MHz/239999 (the first boot omitted the intermediate
row). Final sampled clocks before both resets remain at 240 MHz/239999.
The earlier sustained time drift appears corrected; delayed interrupts and long
mask spans persist. Maxima are cumulative, without caller attribution, and do
not establish the clock/mask state in the final unobserved interval. Third boot
already reached 57.56 ms gap and 51.75 ms mask by the frozen snapshot.

Both completed boots contain event 7/value 16, meaning the software watchdog
recorded a stall reason, followed by watchdog recovery logs (two in boot 1,
one in boot 2). PMU reason 0x70 appeared then cleared. These are evidence of
stalls, not proof that a watchdog actually executed the reset. No explicit
AP force-reset, CP reset-request or mailbox-timeout marker was captured.
Boot 2 lost seven records and overwrote thirteen; later boot 3 also loses
records. Missing markers cannot exclude those paths. UART print order can lag
embedded AON event times by seconds, so printed line order is not execution order.
Reset retention is again unavailable after full boot; live R0/R7A alone are not
an authoritative reset cause. CP's saved AP/CP startup reason words remain unsampled.

Concrete source discrepancy: fw/drivers/watchdog/t5ai.c hardcodes an 8000 ms
AON watchdog period for start/feed, whereas the sealed configuration requests
CONFIG_WATCHDOG_TIMEOUT_MS=30000. Task watchdog timeout/grace are 5000/2000 ms.
This makes hardware-feed starvation a strong next investigation target, not a
confirmed cause. No last-feed AON timestamp is currently captured. Synchronous
watchdog reporting runs before feeding; kernel log-state acquisition can wait
up to one second, and T5 synchronous CP logging uses a task mutex and mailbox
completion waits. A blocked reporting/feed path is plausible but unproven.
Review the actual hardware period semantics and saved CP reset latches before
claiming that changing the timeout fixes the resets.

Other anomalies: boot 2 held the **pin database** mutex for 8824 ms near the
reset. Its reminder mutex maximum was 551 ms (499 ms wall/500 kernel profile,
zero erases), not an 8.8-second reminder hold. Flash erases maxed at 57 ms and
were recorded around startup, not immediately before reset. Touch I2C startup
failure and a later transfer timeout recur, along with resource-check/app-event
failures and battery ADC -12. These do not independently establish reset cause
or healthy persistence. No assertion/hard-fault evidence was decoded for these
resets. Capture continues; no firmware changes or additional flash were made
for this review. Next focused evidence should cover last watchdog feed, CP's
saved boot reason, and attribution of remaining long masks without changing
standard priorities or enabling general stack/register tracing.

## Timing/mask candidate flashed and trace restarted — 2026-10-10

Operator requested flash and trace. All 75 candidate manifest files verified
before and after programming. Guarded AP-only flash succeeded in 31.5 seconds,
physical range 0x132000..0x2d3efa (end exclusive). CP/resources/PFS excluded.
Sealed candidate results remain the immutable pre-flash snapshot. Previous
interrupt-diagnostic capture stopped cleanly; its 31-file final manifest sealed
before programming. No old evidence was overwritten.

Live trace: build/traces/timing-mask-fix-long-20261010/, PID 1683658,
exec session 35230, original raw UART and decoded logs, exact candidate dictionary.
Host UART enumeration and actual received bytes verified. Four-hour capture
started 11:58:50 London; expected stop about 15:58:50.
Listener started before programming reboot, so pre-candidate bytes may belong
to old firmware. Candidate boot 11:59:42 reports exact BUILD ID
9070d487e09b6a43b4d10e6429cda0f8f5aa7cf7. No subsequent agent reset command.
Programmer and tyutool logs, session metadata and frozen initial evidence saved.
A first-reset watcher starts from the known post-programming boot sequence,
preserving first failure plus 30 seconds of context without opening another UART.

Early summaries show three clock changes and four reload changes including
initial setup, then mode 00100031/reload 239999 at 240 MHz. Intermediate 60 MHz
reload rows were not captured, so their exact values are not established by
this trace. Recorded heartbeat RTC/kernel interval differences are at most
1 ms in the initial review, including startup delays. Initial later SysTick
entry maximum reached 617 AON ticks (19.28 ms), mask maximum 592 ticks (18.5 ms).
These are short uncontrolled samples; mask latency and reboots still need a
longer capture. See initial-timing-review.json in the live trace directory. Startup enumerated one app, next ID 2; persistence
is not qualified. A short successful startup is not proof of reboot stability
or of eliminating all long masks. Recheck capture process and bytes at resumption.

## T5 timing and interrupt-mask fix candidate — 2026-10-10

Operator requested fixes for clock timing and interrupt masking. Implemented:

- T5-owned SysTick follows the live CPU clock tree at each interrupt, updates
  SystemCoreClock and reload, and resets the residual period on a reload change.
  ISR performs no logging, allocation or CP transaction. Unsupported calibrated
  DCO sources retain the known reload.
- Elapsed kernel time comes from the independent 32 kHz AON counter, preserving
  fractional ticks and low-word wrap. Coalesced interrupts advance time through
  existing pbl_idle_slept and deliver one normal kernel tick. No per-missed-tick
  ISR loop or global tick-variable writes. Overdue deadlines expire and the
  final normal tick retains standard round-robin behavior. Masked IRQ delivery
  remains delayed until the mask is released.
- Mailbox completion semaphores and mutexes use embedded static storage,
  initialized before the worker starts. Lazy heap allocation under IRQ lock
  is removed; ordering, descriptors and wire ABI are unchanged.
- The T5 voice arena uses a task mutex, keeping interrupts enabled during arena
  initialization, allocation, zeroing and realloc copying. Current routed
  encoder allocation/free callers run in task context.

The only shared firmware edit extends the existing SoC-owned SysTick exclusion
in kernel/arch/arm/arch.c to T5AI. T5 handler lives in soc/t5ai/tick.c; other boards
retain their handlers. Task/IRQ priorities, flash layout, resource ownership,
CP firmware and watchdog policy are unchanged.

Ten actual-source tests pass: observed 120→60 MHz transition, 240 MHz recovery,
divider/source changes, unsupported source, disabled SysTick, 54 ms coalesced
service, sub-ms fractions, AON wrap, all 16 mailbox channels, stale completion
and busy release without send-time allocation, and voice mutex scope. All 19
native kernel tests pass, including multiple overdue sleepers waking after
one bulk advance. T5 and Asterix firmware builds pass; existing BLE LTO warnings
remain. T5 sizes: FLASH 1611152, RAM 101728, display RAM 118272 bytes.

Exact candidate: `build/traces/timing-mask-fix-20261010/`, containing ELF/map,
raw/protected images, dictionary, config, source snapshots/diff, unchanged
CP/resources and test/build evidence, sealed before hardware validation.
Not flashed. Original interrupt-diagnostic capture continues with its exact
firmware/dictionary. No capture dictionary switch or reset was issued.

Limits: the 54.56 ms mask maximum has no caller attribution. Removing these two
avoidable mask sites does not prove that maximum is eliminated or identify the
reboot cause. Direct PRIMASK and common heap masks remain outside arch-wrapper
measurement. A transition can still cause one stale interrupt period; AON
accounting prevents sustained time drift but cannot dispatch during masking.
Catch-up load from T5's repeating QMI timer remains a separate known issue.
Hardware must verify clock/reload tracking, heartbeat RTC/kernel deltas, mask
maxima, connected-idle reboots, button/accel response and app persistence.
No stability or reboot-resolution claim follows from host tests.

## First interrupt-diagnostic spontaneous reset — 2026-10-10

Operator reported a reboot; trace confirms a full boot at 11:28:13 London.
By the 11:34 review, six spontaneous full boots are captured: 11:28:13,
11:28:37, 11:29:05, 11:29:25, 11:30:21 and 11:33:06. Frozen review snapshot:
`build/traces/interrupt-timing-long-20261010/analysis-113400/`.
No agent reset command followed programming. The capture remains running.
First-failure raw UART, index, decoded detection/context and metadata are frozen
under `build/traces/interrupt-timing-long-20261010/first-reset-*`; the five-file
manifest verified. `first-reset-analysis.json` records per-boot measurements.

A concrete startup clock/reload mismatch is now observed: mode `00100033`,
CPU1 speed bit 1 gives 120 MHz and reload 119999; mode changes to `00100037`
(60 MHz) while reload stays 119999. The early tick gap is approximately 2 ms.
The subsequent 240 MHz vote and reload 239999 restore the nominal 1 ms period.
This establishes stale startup tick calibration, not the reset cause.

Before the reset, the maximum observed SysTick entry gap was 66.5 ms, maximum
outer kernel BASEPRI span 54.56 ms, and maximum tick body elapsed 37.91 ms.
Last recorded heartbeat attempt/send/ACK group completed at hardware 98532 ms,
kernel tick 76010. No timeout, explicit reset-reason, AP-force or CP-reset marker
(events 6–9) appears in the frozen context. Clock/reload were already matched
in the final summaries. New boot again reports R0/R7A=3 and an unavailable
previous ring; definitive reset reason remains uncaptured. CP-copied reset
causes at shared SRAM 0x2809f718/0x2809f71c remain an unsampled evidence gap.

Mask counters are cumulative, quantized and affected by instrumentation; their
totals cannot be directly interpreted as lost kernel time. Caller attribution
is unavailable. The last pre-boot line is a 419 ms pindb mutex warning, which
alone does not establish a causal relationship. No firmware edits, clock fix,
priority changes or additional flash were performed in response to this reboot.

## Interrupt-diagnostic flash and live trace — 2026-10-10

Operator requested flash and trace. All 64 sealed candidate files verified.
Guarded AP-only flash succeeded in 31.4 seconds, range `0x132000..0x2d3dea`.
CP/resources/PFS were not selected. Candidate results remain the immutable
pre-flash snapshot. Previous input-alignment capture stopped cleanly; 48 files
sealed by its final manifest before programming.

New capture `build/traces/interrupt-timing-long-20261010/`, PID 1614116,
matching candidate dictionary, four-hour duration, first received at
11:26:30 London; expected stop approximately 15:26:30.
Runtime if02 enumeration, actual raw bytes and decoded T5IRQ summaries verified.
Listener started before programmer reboot; early PULSE attachment may still
miss boot lines. No agent reboot command followed programming. Initial evidence
is frozen separately with hashes; live capture remains open. A first-reset
watcher starts from the current known boot sequence baseline.

Initial summaries include clock_changes=1, reload=119999 and mode=00100037,
cpu1_speed=1 during startup. Correlate these readings with tick gaps, mask spans
and IRQ activity before selecting a fix. No clock adjustment, priority change
or firmware behavior fix was included. Reboot source and long-term stability
remain unestablished. Recheck live bytes/process before reporting monitoring.

## Interrupt/timebase investigation — 2026-10-10, preparation before flash

The operator requested investigation of the RTC/kernel-tick divergence. Source
review found no demonstrated multi-second masked wait in the T5 input handlers.
Kernel locks use BASEPRI, while direct T5 PRIMASK sites include startup, idle WFI,
eFuse reads, GPIO configuration and individual delay-counter reads. SysTick runs
at lowest kernel IRQ urgency and increments kernel time once per serviced IRQ;
it does not compensate for clock changes or collapsed pending ticks. The T5
SysTick reload uses CPU frequency, refreshed at scheduler/power initialization.
The pinned CP reference can change shared core and CPU1 clocks. Its AP BSP also
explicitly warns that CPU-frequency voting affects processor-clocked SysTick.
Neither a CP clock change nor a long mask/IRQ storm is proved on the running image.

Fifteen captured boot entries were checked during this investigation. The early
heartbeat seq 7→10 interval is approximately 1996–2036 hardware ms / 1000 kernel
ticks in each. This repeatable ratio strengthens the clock/timebase hypothesis;
late intervals up to 7053/1001 still need discrimination. Reboot cause remains
unknown. Existing trace was left running; no watch command or flash was performed.

Prepared optional `CONFIG_T5AI_INTERRUPT_PROFILE`, default off and dependent on
reboot tracing. T5-local linker wrappers measure SysTick entry gaps/body elapsed
time and outermost kernel BASEPRI mask intervals. Short T5-local hooks count and
time I2C/GPIO/mailbox handlers and the PRIMASK-protected idle window. Clock
source/divider fields and SysTick reload changes are sampled at tick entry.
The existing priority-1 reboot worker prints cumulative counters every drain;
there is no new task, ISR logging/allocation, stack/general-register capture,
clock/timer adjustment, priority change or tick compensation. Common kernel
sources are unchanged. Three intercepted kernel translation units disable LTO
only with this option, preserving linker-call boundaries.

Seven actual-source host tests passed (counter/disabled header, reboot tracing,
buttons, QMI and I2C recovery). They cover nested mask duration, 32-bit wrap,
tick gaps/body duration, core-clock vs unrelated field changes, reload changes,
IRQ counters and forwarding to original handlers. Firmware, map/protected-image
checks pass. ELF verification confirms vector slot 15 targets the SysTick wrapper
and lock/unlock call the mask wrappers. Existing BLE LTO warnings remain.

Exact candidate `build/traces/interrupt-timing-20261010/` contains source/diff,
config, ELF/map/dictionary, raw/protected images, unchanged CP/resources, build and
test logs, wrapper disassembly/vector verification and a SHA-256 manifest.

- Raw: `254a810902cf1b3904da99934b6b1b9437bd442e61147c443b53cf1143f7168d`.
- Protected: `9ea68180726cc6e882a6fea0b1e14d16ada657e284a9e5b66e7493eac07cf5fe`.
- ELF: `bd8fa1e9d17fdeac772743835574613698437b87b53018e78ea52f518c87e798`.

**At preparation time, not yet flashed.** The watch then remained on the
input-alignment candidate; see the later flash and reset sections above. The responsiveness/reset issues remain unresolved.
Measure before selecting a fix. Long masks support critical-section latency;
large IRQ totals/rates support interrupt load; clock changes without matching
reload changes support stale tick calibration. These are diagnostic indicators,
not automatically reset causes.

Limits: 31.25 us counter resolution; per-boot cumulative 32-bit counters wrap;
direct PRIMASK spans outside the idle hook are not measured. IRQ and tick body
durations include preemption and are not additive CPU-time totals. Clock changes
between tick samples can be missed; paired clock reads can straddle a transition.
Startup's initial reload setup counts as a reload change. The idle counter covers
the whole masked confirm/WFI window, including attempts that skip WFI. Logging,
extra peripheral reads and disabled LTO add overhead. Statistics live in BSS,
are not retained across resets and can be lost if the drain is delayed. No
firmware behavior fix or stability claim is made from host validation.

## Repeated input-candidate resets — 2026-10-10, review at 11:10:55 London

Eight full reboots captured after the programming boot: 11:07:32, 11:07:53,
11:08:06, 11:08:24, 11:08:43, 11:09:18, 11:09:41, 11:10:40. BUILD ID
`789214b56f4cf2caf9bb367705c5bea53a5df6b2` matches the running candidate.
No captured timeout/reason-set/AP-force/CP-reset marker (events 6–9); boot-stage
reason words are 3 and trace headers zero. Cause remains unknown. Capture remains
running; no watch commands, firmware edits or additional flash were performed.

Frozen raw/decoded/index evidence and analysis: `build/traces/input-alignment-long-20261010/analysis-111055/`.
Its manifest was verified. See `review.txt` and `results.json` for per-boot tails,
complete heartbeat intervals, warnings and limitations. A complete heartbeat
group advances 7053 hardware ms while the kernel advances 1001 ticks; another
advances 6064/1000. This strengthens the timing/interrupt-service investigation,
without identifying the reset source. Mere thread starvation does not account
for lost SysTick progression by itself. Clock/counter assumptions and delayed
interrupt service remain alternatives.

NewTimers catch-up warnings identify QMI's semaphore-only callback at
`0x02136591`. The common repeating-timer branch logs each overdue callback
without the expiry advance described in its comment, potentially amplifying
load. Warnings start after earlier reboots, so this cannot explain every reset.
A driver-local one-shot/rearm design could avoid missed-poll replay; no change
was made. Higher reset frequency may be a candidate regression, but different
startup/sync workloads prevent attribution. The CP's saved reset-cause slots
and clock/SysTick service evidence are the next targeted diagnostic gaps.

## Input-alignment AP flash and restarted trace — 2026-10-10

Operator requested flashing the sealed input-alignment candidate. Verified all
56 candidate files before use. Guarded AP-only flashing succeeded in 31.5 s,
physical range `0x132000..0x2d3a32` (exclusive end). CP/resources/PFS were not
selected. Exact raw/protected hashes remain in the candidate above; its results
file remains the immutable pre-flash preparation snapshot.

The preceding boot-evidence capture was stopped cleanly and all 61 files sealed
in `reboot-boot-evidence-long-20261010/final-manifest.json` before programming.
New raw/decoded capture: `build/traces/input-alignment-long-20261010/`, PID
1570590, four hours from approximately 11:07:02 London, expected end
15:07:02. Runtime if02 enumeration and actual
received bytes were verified; matching sealed dictionary is used. Initial
programming reboot is expected; earliest boot banner/reason snapshot was missed
while attaching. No further reboot command was sent. Programmer logs, session
metadata and first-reset watcher are preserved alongside the live trace.

Received successful heartbeat ACKs and system-resource validation. Early app
resource warnings also occurred; these do not establish healthy app persistence.
Button/accelerometer responsiveness, sustained subscriptions and reboot stability
remain unvalidated pending operator workload and continued capture. Verify the
process and received bytes again before reporting the capture as still live.

## Button and accelerometer alignment — 2026-10-10, prepared only

The operator authorized removal of T5 input-path starvation dependencies after
reporting missed Select and accelerometer input on FdF Time; Select works in
system menus. This is an input-path fix candidate, not a demonstrated fix for
FdF Time or the spontaneous reboot cause. No stack/register tracing was added.

T5 GPIO edges now give a binary semaphore directly from the ISR. A static
1536-byte Buttons worker at priority 4 blocks until an edge, samples every 2 ms
while debounce/reset work remains, then blocks again. This removes KernelBG and
the droppable timer work queue from activation, and retains an edge arriving as
sampling ends. Twenty stable samples, reset combinations, absent-pin handling,
raw event injection and event routing are unchanged. Other platforms use hardware
timer ISRs; T5 uses a bounded sleeping worker at NewTimers urgency because spare
hardware timer ownership across the pinned CP has not been qualified. This does
not make T5 sampling independent of interrupt masking, kernel time or scheduling.

QMI8658 still performs blocking I2C on its static 1536-byte worker, now at priority
3 rather than 2 so continuously runnable App work cannot starve acquisition.
Acquired samples use the standard accel_offload_work callback on NewTimers
(priority 4), with the accel-manager mutex held, matching LSM6DSO/LIS2DW12 manager
entry. KernelBG delivery is removed. Rates, mapping, startup settling, single
pending sample, shake filter and stale-generation checks remain. T5 still polls
one sample rather than using the other sensors' interrupt-driven FIFO batches.
A saturated queue drops that sample and releases pending work for the next tick;
there is no backlog or catch-up burst. NewTimers performs no periodic QMI I2C.
Configuration and uncached peek remain synchronous and can still block callers.

The sole shared seam changes accel_offload_work's return type from void to bool
and forwards the already available timer-queue result. Existing callers may
ignore it and retain behavior. This is required for T5 to handle enqueue failure
without leaving its pending sample stuck, while retaining standard manager
locking; no shared task priorities, scheduler or timer policy changed. CP and
AP flash-client temporary priority boosts are unchanged.

Validation passed: five actual-source host tests covering the button worker,
QMI pipeline and existing state/reboot recorder; rebuilt native accel-manager
suite (four cases, including offload queue-result propagation); Unhygienix and
representative stock Asterix firmware builds. Map and protected/raw image checks
passed. Existing firmware BLE LTO and native test warnings remain.

Exact unflashed candidate: `build/traces/input-alignment-20261010/`, with images,
ELF/map/dictionary, configuration, unchanged CP/resources, all dirty-source
snapshots/diff, tests/build logs and SHA-256 manifest.

- Raw SHA-256: `ed86b65761da8dee96a3a285bba74b6a79559456acac08f4bece6fbe8074d4f3`.
- Protected SHA-256: `29ce6d5bad824834fc59c03eefaa714dd0d158aa4efa1312d885b2a6ece8a145`.
- ELF SHA-256: `bc4e3e369984e01b57fb748f053a1743e07ffb61f6cccd9d9072d170b6e7bc8a`.

The watch continues running the preceding boot-evidence image; its existing
four-hour capture was left running and received data during this work. The new
candidate has **not been flashed or hardware validated**. After AP-only flashing,
check FdF Time Select/accelerometer input, system-menu buttons, short presses and
bounce, sustained sampling/shake subscriptions, app transitions, sync and idle.
Host stubs establish pipeline behavior, not measured latency, CPU headroom or
physical reset timing. App callbacks remain priority 2; app faults and CPU-heavy
app handlers can still impair the watchface. Reset-loop and persistence defects
remain unresolved; no stability claim follows from these tests.

## Working rules and source locations

- Production source: `/home/baz/coredevices/pebbleos`.
- `/home/baz/Documents/ChatGPT/pebble` is Tuya reference material and artifacts;
  do not modify its sources except explicitly requested tests.
- Preserve stock common code. Prefer board-, SoC-, driver-local implementations
  and build-time selection; justify any unavoidable shared seam.
- Preserve pin maps, timing requirements, protected AP layout, PFS, mailbox/HCI
  ABI, pinned CP image, Gabbro app ABI and Round 2 companion identity.
- Do not silently restore an old image or add stack/register tracing. Previous
  requests explicitly canceled that tracing and favored diagnosis over rollback.
- The production checkout has substantial uncommitted work over `423e9523f`.
  Inspect the current diff and preserve it; do not reset unrelated changes.

## Completed: do not repeat these extractions

- Shared `fw/main.c` was restored to stock; T5 boot, power/readiness and transport
  ordering moved to board/SoC/transport ownership.
- Native kernel wrappers and startup replaced the vendor RTOS bridge.
- Common `fw/kernel/util/delay.c` was restored; T5 implementation is selected
  from `soc/t5ai/delay.c`, with SoC CMake wiring.
- Most common service/runlevel restoration is complete.
- PSRAM framebuffer validation and power setup moved to
  `fw/board/boards/board_unhygienix.c`; framebuffer placement is still shared.
- Resource read/CRC paths were restored to stock, with immutable mapped resource
  reads handled in `fw/drivers/flash/t5ai.c`. The bounded system-bank validator
  and single-bank policy now live in `soc/t5ai/resource_storage.c`, selected by
  `CONFIG_SOC_T5AI` through CMake. Shared flash storage again selects two banks.
  Host validation and both firmware builds passed; hardware validation is pending.
- T5 serial logging refuses unsafe ISR/interrupt-locked/scheduler-locked
  runtime contexts, avoiding a mutex/CP wait in failure handling.
- QMI8658 periodic I2C acquisition moved from KernelBG to a dedicated static
  worker. Timer signalling is nonblocking; only one acquisition/delivery can be
  outstanding. Sample/shake callbacks now use standard accel-manager offload on NewTimers. Rates and axis mapping
  are unchanged; configuration and uncached peek remain synchronous.
- Task-state recording is optional via `CONFIG_T5AI_STATE_TRACE`, default off.
  CMake excludes its source and inline start/freeze hooks do nothing when off.
  Enable/disable commands and retention limitations are in [tooling](tooling.md).
- T5 flash erase completion hints now produce a 1 ms initial stock-API poll,
  removing the former 43/700 ms sleeps after synchronous CP erases. Database
  locking and rewrite atomicity were not changed.

## Refactor queue, in suggested order

### 1. T5 system-resource backend — implemented, hardware validation pending

`fw/resource/resource_storage_flash.c` no longer contains T5 conditionals.
CMake excludes it only for `CONFIG_SOC_T5AI`; the SoC library supplies
`soc/t5ai/resource_storage.c` under the same exported interfaces and
`g_system_bank_impl` identity. The generic app-file backend, backend ordering,
resource-watch ownership, mapped flash driver, resource format and partition
geometry are unchanged. Generic metadata/get-resource/check/watch helpers are
reused rather than copied into the SoC backend.

Boot validation retains the original direct mapped manifest/final-entry reads:
12 bytes for an empty or mismatched manifest, at most 28 bytes for a nonempty
pack, no full-pack CRC and no flash mutex/CP transaction. A bank-geometry check
now rejects undersized/reversed extents before accessing metadata. Version
matching still uses the stock CRC identity; timestamp remains informational.
Per-resource CRC checks and copy/privileged-pointer interfaces are retained.
The single-bank unused-bank query returns bank 0 as before; this does not enable
updates or make that bank safe to overwrite.

Earlier generic full-pack CRC validation took 4.754 s per boot versus roughly
1 ms for bounded validation. That path has not been reintroduced. Host tests
prove bounded metadata work, not elapsed hardware startup time. This validator
checks the final entry's identity, offset/length overflow and bank extent; it
does not scan every interior table entry, authenticate the pack or detect a
physically incomplete flash write whose missing bytes happen to pass metadata
checks. Per-resource validation remains the existing generic implementation.

Passed validation:

- Four actual-source host cases compile the SoC backend with shared resource
  routing and real resource-format headers. They cover matching/mismatched and
  empty/oversized manifests, truncated manifest/table bank geometry, maximum
  resource count, final-entry ID mismatch, overflow/out-of-bank/exact-end
  entries, failed-init system rejection with app access retained, backend
  precedence, copy and privileged mapped-pointer paths, and resource CRC
  delegation/failure. Flash/boot and app-file access use controlled stubs.
- 33 tool tests: `test_t5ai_resources`, `test_t5ai_flash_completion`,
  `test_unhygienix_image`, `test_unhygienix_image_packaging`, and
  `test_unhygienix_runner`. Run with `PYTHONPATH=tools/libs/pbl-cli`.
- Four rebuilt native tests: `test_i18n`, `test_text_resources`,
  `test_gbitmap_resource_validation`, `test_timeline_resources`.
- Final Unhygienix build in `build-cmake`, including image-map and resource-size
  checks; representative stock Asterix build in
  `build/stock-resource-asterix-sdk`. Build graphs select exactly their intended
  system-bank source. Existing BLE LTO warnings remain on T5.

No flash or hardware capture was started for this slice. Startup latency,
watchface transfer/launch/rendering, Settings scrolling, phone reconnect and
selected-face persistence still require the retained candidate below. KernelBG
investigation remains deferred. Item 2 is the next source refactor and is not
part of this slice.

### 2. Resolve resource-watch ownership

`fw/resource/resource_storage.c` guards `resource_unwatch()` for T5. The stock
path assumes callbacks belong to `g_file_impl`, while this board omits the
system-file implementation and uses mapped system resources.

Make callback handles retain their owning backend, or select an appropriate
implementation. Preserve watch/unwatch lifetime and callback behavior for
existing boards. Test unsupported watchers, valid handles and correct backend
dispatch. Simply deleting the guard is unsafe. This may require a narrow generic
ownership improvement; document that justification separately from T5 policy.

### 3. Select unavailable update/recovery/coredump/reset capabilities

Review `fw/kernel/reset.c`, `fw/kernel/core_dump.c`,
`fw/kernel/util/factory_reset.c`, update services and associated console paths.
Move unsupported behavior to selected implementations or explicit unavailable
stubs, then restore shared control flow where the interface allows it.

Do not enable OTA, PRF/recovery installation, arbitrary flash writes or persistent
coredumps. There is no qualified container/slot/rollback/recovery contract.
Keep routine AP-only flashing independent of PFS and pairing data. Preserve
rejection behavior and validate reset/storage semantics before changing them;
factory reset is not merely an unsupported-update alias. Recovery design and
interrupted-reset qualification are prerequisites for enabling new behavior.

### 4. Finish framebuffer-placement ownership

Only the T5 framebuffer section/alignment declaration remains in shared
`fw/services/compositor/compositor.c`; board initialization now owns validation
and power setup. Find the smallest build/linker or allocation seam that removes
platform placement policy from the shared compositor.

The previous framebuffer-provider extraction caused repeatable startup
HardFaults. Review that historical failure before trying a new interface.
Preserve object lifetime, alignment, PSRAM mapping/cache assumptions and startup
order. Check linker placement and boot, then scrolling, composition, display
sleep/wake and app transitions. Do not duplicate the compositor.

### 5. Audit residual exceptions, build glue and generic fixes

- Review identity selection in `fw/kernel/system_versions.c` and T5 profiler
  branches in `fw/system/profiler.c` for appropriate board/SoC capabilities.
- Consolidate CMake source selection without cloning common implementations.
- Refresh stale `tools/tests/test_unhygienix_boards.py` expectations after the
  startup/service moves. The latest broad run had three existing failures:
  provisioned-filesystem startup sleep placement, shared-PRF write-call count,
  and Bluetooth initialization expected in `main.c`. The same failures occur
  using the pre-QMI-change driver; they are not new worker regressions.
- Review independently justified generic changes as separate fixes with focused
  tests and a stock-platform build. Do not mechanically remove every difference
  from upstream: some are generic fixes or hardware requirements.

## Deferred: KernelBG stalls and other runtime defects

The remaining stall investigation is explicitly deferred, not resolved.
MU-TH-UR T UUID: `8143d766-3fe6-4265-9732-5a2c5c6f7249`.
App-driven CPU starvation is the leading hypothesis: App priority 2 outranks
KernelBG priority 1, and the latest stall recovered after app throttling. A
communications/lock wait aggravated by starvation remains possible. Sampled
PCs do not establish the root cause or a deadlock.

When resumed, correlate KernelBG/App CPU-time deltas and ready/blocked state
with active communications endpoint, callback elapsed time, settings filename
and iteration/rewrite progress, and send-buffer wait duration. Consider the
optional state recorder; whole-chip reset retention/replay has not been proven.
Do not add stack/register tracing or change global scheduling policy from these
snapshots alone.

Other unresolved observations: SARADC `E_AGAIN` aborts battery sampling to avoid
reusing potentially CP-owned IPC storage; repeated PFS last-written-marker
warnings; app-entry/inbox and extended-font warnings; startup app-record loss
or invalidation around interrupted sync. Missing optional language extensions
are not by themselves evidence of corrupt system resources. Storage persistence
and interruption behavior need separate validation. Any unexpected reset
remains a release blocker.

## Follow-up: T5 flash cache correctness and driver cleanup

The next scoped slice changes only `fw/drivers/flash/t5ai.c` and adds
`tools/tests/test_t5ai_flash_cache.py`. Shared flash code, database locking,
PFS layout, CP mailbox ABI, transfer sizes, priorities and erase completion
polling are unchanged. Larger read batching and cache resizing remain deferred
until measured against matching workloads.

Both cache types now invalidate affected data before issuing a write or each
4 KiB erase. A failed/ambiguous mutation or a version verification failure
therefore cannot leave the old affected bytes marked valid. Successful header
writes retain the previous cached bytes, apply NOR bitwise-AND semantics and
mark them valid again only after success. Uncached headers are not synthesized
from a partial write. Successful erases still cache known-blank headers; a
sector erase that fails partway preserves cached data for unattempted pages.
Policy rejection happens before invalidation. No mutation retries were added.

Removed inactive non-Unhygienix consensus-read, version-plausibility and
verification-rewrite branches and their constants. Unhygienix is the only
configured board selecting `SOC_T5AI`; its existing verification behaviour is
preserved. The driver no longer branches on the board name. This contains
hardware-specific implementation behind the existing stock flash interface;
it does not make CP-backed flash equivalent to directly controlled SPI NOR.

Validation: the new host harness failed on the pre-change driver with header
caching enabled and disabled, then passed after the change. It covers failed
writes with no mutation or applied data, partially applied erases, reads blocked
by unavailable transport after failure, version mismatch, unaffected cache
lines, policy rejection, NOR write-through, uncached/late-PFS headers and
partial sector failures. The transport is stubbed; quarantine behaviour itself
is covered by existing tests. All 43 selected tool tests and both rebuilt native
flash suites (`test_flash_api`, `test_flash_erase_obelix`) passed. The stock
Asterix firmware build passed, and the final Unhygienix firmware build and
image-map check passed. Existing BLE LTO warnings remain.

The candidate was subsequently flashed AP-only and traced; see the latest
hardware evidence below. Cache validity fixes do not establish the cause of
app-persistence concerns or fix the deferred KernelBG stalls.

## Flash-cache cleanup candidate — retained, not flashed

Exact candidate: `build/traces/flash-cache-cleanup-20261009/`. It includes the
AP raw/protected images, ELF/map, log dictionary, system resource pack, pinned
CP artifacts, configuration/build cache, source diff/snapshots and test/build
logs. `manifest.json` hashes every archived file. The earlier resource-backend
candidate remains separately retained.

- Raw SHA-256: `fd7bc738d77f93678b7c470d6ff04b55eb9c605f7fdc713a4f109542a1c28380`.
- Protected SHA-256: `a7caf0fd77cde4d5d6093a9aed6eeb0525dccf7b02e7e3d551ab16e6ae064a3a`.
- Resources SHA-256: `f7abf4ce7112ba22c94c90b6207cd79e054d9e3feefacd456620e1609479ad07`.
- Protected-block decoding matches the raw image, and image-map validation passed.
- State tracing and PFS header caching remain disabled in the firmware candidate;
  host tests additionally validate the optional header-cache implementation.

This was unflashed at archival time; it was subsequently flashed AP-only and
traced as recorded below. Cold-boot persistence and broader soak qualification
remain outstanding. Runtime flash errors still follow the existing shared API's error
handling; cache invalidation does not recover a quarantined CP transaction or
make an interrupted write atomic. No KernelBG investigation was performed.

## Resource-backend candidate — retained, not flashed

Production HEAD at implementation: `e106202f9f51d3c33418364cdef60573c1a3bff0`.
Existing modified board/docs files and the untracked handoff were preserved.
Candidate directory: `build/traces/resource-backend-20261009/`. It contains raw
and protected AP images, ELF/map, generated log dictionary, system resource
pack, CP images/manifest, `.config`, CMake cache, generated resource headers,
source diff and snapshots, validation logs/results and SHA-256 manifest.

- Raw SHA-256:
  `816850bcbbfe80391932484eb454847f1d4f0bbc26829430384f15805d64b90a`.
- Protected SHA-256:
  `9ffacf211b5154c0113ffe2579cf854cdbb5a9d3d54cd0e9d306daca5e933fc1`.
- Resource-pack SHA-256:
  `f7abf4ce7112ba22c94c90b6207cd79e054d9e3feefacd456620e1609479ad07`.
- T5 linker usage: FLASH 1,606,784/1,638,400 bytes, RAM 94,560/126,976 bytes,
  display RAM 118,272/131,072 bytes. Resources: 803,525/1,048,576 bytes.
- `CONFIG_T5AI_STATE_TRACE` is disabled. `CONFIG_MMAP_RESOURCES` is also disabled
  in the actual candidate, as before; copied reads still use the driver's mapped
  resource path. Host tests additionally exercise the privileged-pointer branch.

The raw/protected hashes identify this unflashed candidate, not the firmware
currently on the watch. Physical write ranges and flash results must be recorded
only if hardware validation proceeds. No stability/persistence conclusion can
be drawn from host validation. The previously flashed image and runtime evidence
below are historical; the latest flash/trace is recorded in the next section.

## Connected-idle reboot-loop trace — 2026-10-09, 22:53 onward

Operator reports random **full reboot loops while connected and idle**. No
firmware changes, flashing, resets, priority changes or new stack/register
tracing were performed. The watch remains on sealed unlock diagnostic candidate
`build/traces/reminder-profile-unlock-20261009/`; matching raw/protected images,
ELF and log dictionary were hash-verified before capture. State tracing is off.

Host UART enumeration was rechecked; decoded watch logs and received raw bytes
verify a live physical capture on runtime `if02`, 460800 baud. Initial decoded
capture began 22:53:10; a second capture also retains raw RX bytes and timestamped
byte offsets, so non-PULSE boot/CP text is preserved. Evidence is at
`build/traces/reboot-loops-20261009-2253/`, including exact ELF/map/dictionary and
capture tool/wrapper provenance. This short capture has completed and its evidence is sealed. A follow-on one-hour
capture is receiving live bytes at `build/traces/reboot-loops-20261009-long/`.
Its host watcher freezes the first firmware boot banner or uptime decrease,
then preserves 30 seconds of decoded context. It does not notify automatically;
inspect the files or return to this chat after a reboot. No firmware intervention
is performed. Live files are not included in a sealed manifest until completion.

At 22:57:17.882, app UUID `8bd2b56b-a513-474e-890b-bed313ef68f0`, ID 24, was
requested from the phone; fetch cleanup result 0 appeared at 22:57:26.385.
Existing firmware app-fault logging then reported the same PC `0x1c60` and LR
`0x18d3` at 22:57:28.525 and 22:57:30.842. Watchface relaunch was attempted;
at 22:57:31.129 it had crashed twice in 2326 ms and reverted to default.
These are **app crashes without a full firmware reboot**: uptime continued.
Operator explicitly confirmed this is not the reported full reboot. Preserve
`first-app-crash-loop.log`; do not use this sequence to explain away the reported
resets. These app-relative addresses need the matching app binary/symbols,
not firmware addr2line. No matching app symbols were available in this capture.

A watchdog recovery message appeared at 22:57:47.412; its preceding stall report
was not captured, so no task/cause is assigned. Context is frozen separately in
`first-watchdog-recovery.log`. Broader KernelBG stall investigation stays deferred.
Across the completed short capture, recorded uptime samples rose from
158,008 to 638,037 ticks;
no new firmware boot banner, uptime decrease, kernel assertion or full reboot
was observed. This short window does not qualify stability or disprove random
resets outside it. Resource-check warnings for IDs 612/618/619 were also observed;
they do not establish physical flash corruption.

### First full reboot captured at 23:03:40

The follow-on capture recorded a genuine firmware boot banner at **23:03:40**.
Operator confirmed **"It rebooted by itself"**. This is a spontaneous full reboot,
separate from the earlier app crashes. The session sent no reset/flash commands.
Reboot reason was unknown; MCU reset reason mask `0x0`. The last preceding decoded
line was flash-write diagnostics at 23:03:28.589 (max 9 ms, last 1 ms). No assertion,
app fault, mailbox fault or watchdog-expiry message was captured immediately
before this reset. This does not exclude a lost fault message: quiet UART sessions
are periodically reopened, and absent logs do not identify the reset source.
Neither a CP reset, power problem nor kernel fault is established by this trace.

The host watcher froze the first boot detection and following 30 seconds of
context. Immutable log/raw snapshots and a nine-file hash manifest are in
`build/traces/reboot-loops-20261009-long/first-reset-evidence/`. The snapshot was
sealed before the operator answered, so its trigger status says clarification
pending; later confirmation is preserved separately in
`operator-confirmation.json` and its SHA256 file. Do not alter that first snapshot.
The exact symbols/dictionary are in the earlier sealed short-capture directory.

After reboot: PFS could not resolve its last-written page; 45 apps were
enumerated; PinDB next-item held its mutex for 2262 ms; an encrypted PPoGATT session
opened at 23:04:05.042. At 23:04:14, existing NewTimers logs reported repeated
missed callbacks (`0x2136245`, symbol `prv_timer_callback.lto_priv.1`; precise
source unavailable). These are post-reset observations, not demonstrated causes.
No stability or persistence qualification follows from reconnecting.

The one-hour raw/decoded recording remains active (started around 23:03,
scheduled to stop around 00:03 London time). Its first-reset watcher has completed
and the original first-failure snapshot will not be overwritten by later resets.
No firmware behavior, priorities or tracing flags were changed.

### Further full boots at 23:06:27 and 23:08:03

Operator reported another reboot. The unchanged live capture records two further
full firmware boots, **23:06:27** and **23:08:03**, with boot-banner intervals of
167 and 96 seconds. No reset or flash commands were sent. Both report unknown
reboot reasons; the second boot's MCU mask was not captured, while the third
reports `0x0`. No preceding assertion or watchdog-expiry message was captured.
The reset cause remains unresolved, including the UART gap limitation above.

Second boot startup reports i2c0 timeouts, CST92xx not detected at `0x5a`, and
QMI8658 reset failure. These occur after reset and are not established causes.
PFS again cannot resolve the last-written page. App enumeration changes from
45 / next ID 46 on the first boot, to 45 / 91 on the second, to 39 / 130 on the
third. Phone synchronization and AppDB flush activity occur between boots, so
this is a persistence concern, not proof of physical flash data loss. PinDB
next-item holds were 2006 and 2141 ms respectively. The third boot reconnects.

A separately sealed decoded/raw prefix, index and results are in
`build/traces/reboot-loops-20261009-long/second-third-reset-evidence/`.
The first-reset evidence remains untouched. The one-hour capture is still
receiving data; no firmware changes were made. KernelBG investigation remains
deferred and these observations do not qualify stability.

### Capture stopped; bounded CP heartbeat candidate — 2026-10-09

The operator requested stopping tracing after another spontaneous reboot.
Capture PID 1348794 was interrupted and exited cleanly, closing raw buffers.
No capture remains running. The completed recording has four full boot banners:
23:03:40, 23:06:27, 23:08:03 and **23:16:40**. The final boot again reports unknown
reason and MCU mask `0x0`, followed by repeated NewTimers missed-callback
warnings. No cause is established by that sequence. Final raw RX is 205,518
bytes. All 34 evidence files are sealed by
`build/traces/reboot-loops-20261009-long/final-manifest.json`; earlier immutable
first-reset and second/third-reset evidence remains intact. Earlier descriptions
of this capture as active are historical and superseded by this paragraph.

Source review found a concrete liveness defect in the T5 CP heartbeat thread:
`last_wake - pbl_uptime_ticks()` uses unsigned ticks. A delayed send or late
wake can pass its absolute deadline and request a sleep near UINT32_MAX ticks.
At 1000 Hz that is approximately 49.7 days. The pinned CP source supervises AP
heartbeats and asserts on timeout, providing a plausible reboot mechanism.
This is not proof that the observed resets followed that path; the capture did
not record CP heartbeat progress or a CP timeout assertion.

`fw/drivers/t5ai/cp_mailbox.c` now sleeps a relative 1000 ms after each async
heartbeat attempt. Delayed execution cannot underflow and produces no catch-up
burst. Normal period includes the small send-call duration; prolonged scheduler
or mailbox starvation can still violate CP supervision. Priorities, CP firmware,
mailbox protocol, watchdog timeout and global kernel scheduling are unchanged.
The regression compiles the actual production task function, delays both sends
and wakes by up to 6300 ms, and crosses tick wraparound. The original function
fails the bounded-sleep assertion; the new function passes.

A separate reporting defect was found: the live PMU R0 AP reason is cleared by
CP startup. Pinned vendor `reset_reason_init()` reads its saved R7A boot snapshot
instead. The T5 watchdog and retained-reason fallback now read that snapshot;
current-runtime writes/clears still use R0. Startup prints decoded AP and CP
source codes once, at the existing reason-clear point. Retained Pebble detail
wins, and clearing consumes the fallback so old causes cannot reappear later.
Tests cover CP-cleared live fields, independent CP/AP causes, retained precedence,
mailbox fallback, watchdog start/feed/stop and one-time consumption. No new
stack/register trace or state recorder was added. Snapshot validity on this
physical boot chain still needs validation; zero codes do not prove power loss.

The two targeted regressions and thirteen related resource/flash/profiler/sensor/
capture tests pass (15 total). Capture tests require the existing Pebble Python
library paths; an initial invocation without them failed to import `pebble`,
then passed with dependencies configured. Exact candidate build/results are
preserved in `build/traces/cp-heartbeat-fix-20261009/` before hardware validation.
This candidate has **not been flashed**. The watch still runs the previously
sealed unlock-profile diagnostic image. Reminder profiling stays enabled and
state tracing stays disabled; the patch introduces no configuration changes.
KernelBG investigation remains deferred. Reboot stability, app persistence,
I2C startup errors and battery ADC errors remain unqualified.

### Heartbeat candidate flashed; reboot persists — 2026-10-09 23:32

At the operator's request, flashed the exact sealed heartbeat-fix candidate
using the guarded AP-only writer. Programming passed in 31.4 seconds; physical
write `0x132000..0x2d356a` (end exclusive). CP, system resources and PFS were not
selected for programming. Protected SHA256:
`9a29155c3dbab2325b2d1410eeda094817bfb91657abb2ed10298d1a15f2879c`.
This supersedes the preceding "not flashed" candidate status; immutable build
results still correctly describe their pre-flash state.

The bounded 80-second boot check received actual runtime UART logs with the
exact candidate dictionary. Program boot at 23:32:03 had BUILD ID
`2ba46b935910ce91f99271b3849d4b32a82d32ce`, reached Launcher/communication-ready,
and established an encrypted phone session around 23:32:41. Another full boot
was captured at **23:32:44** with the same BUILD ID. No agent reset command was
sent after programming; operator reset status was not separately queried.
Reboot reason remains unknown, MCU mask `0x0`, and the new decoded boot sources
are **AP=0 CP=0** on both boots. The heartbeat source defect is corrected, but
this change does not resolve or establish the cause of the observed reboots.
The saved-source reporting has not yet yielded useful fault attribution; zero
codes do not establish power loss. Do not claim the reboot loop is fixed.

The subsequent boot reconnects and completes GATT discovery. PFS last-written-
page warnings recur, resource 614 fails a check, and PinDB next-item holds its
mutex for 2364 ms. These are observations, not proven reset causes. The bounded
check has finished; no ongoing capture is running. Programming log, tyutool log,
boot trace, results and hashes are sealed in
`build/traces/cp-heartbeat-fix-hardware-20261009/`.

### Optional reboot lifecycle diagnostic — 2026-10-10

Operator requested additional tracing after the heartbeat candidate still
rebooted. `CONFIG_T5AI_REBOOT_TRACE` is a separate default-off diagnostic, enabled
in the candidate build. Board/SoC/T5 driver hooks record CP heartbeat attempts,
skips, async-send status, physical ACK/logical response, mailbox timeout,
reason-set, AP forced reset and CP system-reset requests. Shared kernel code,
watchdog policy and global task priorities are unchanged. No stack/register
capture was added; the existing app/background state recorder stays disabled.

The 32-record checksummed NOLOAD ring is copied during board early init and
replayed by a static priority-1 worker. Live records drain every two seconds,
alongside independent hardware/kernel-clock liveness messages. Hooks perform no
UART, IPC, allocation or mutex waits. Runtime log/cache overhead and unproven
retention limit conclusions; missing events cannot exclude an uncaptured fault.
See tooling.md for event IDs and interpretation. Actual-source host tests cover
bounded overwrite, time/status preservation, retained replay, corruption rejection,
nonlogging event hooks, one-time initialization and disabled-header linkage.
All 17 relevant regressions pass. Exact firmware and configuration will be sealed
in `build/traces/reboot-lifecycle-20261010/` before hardware validation.

### Reboot diagnostic hardware validation — 2026-10-10

The sealed lifecycle diagnostic was flashed AP-only in 31.3 seconds, range
`0x132000..0x2d38bc` (end exclusive). Protected SHA256:
`892ab56677e7332569bcdfeac56fe3ee4acd0d121815156c64d57f872418fc22`.
BUILD ID `23c49fa56c6bd7468b1a15178771e753b5e53cea` booted at 10:07:06 and
reconnected to the phone. CP/resources/PFS were not selected for programming.
The 240-second raw/decoded check completed, retaining 71,595 raw bytes and 578
parsed live records. No further full boot, mailbox-timeout, AP forced-reset or
CP reset-request event was captured in that interval. This does not establish
stability or a fix; workload timing differs from the previous loop capture.

Physical heartbeat ACKs report complete (`event=4 value=2`); source observations
include three watchdog-reason-set events (`event=7 value=16`) and watchdog
recovery messages, while heartbeat completions continue. These are not full
resets. The deferred KernelBG investigation was not resumed. Complete-sequence
intervals include 6048 ms of hardware time versus 1000 kernel ticks between
heartbeat attempts (sequences 16 and 19). This clock/scheduling discrepancy is
measured, but its origin and causal relation to reboot are not established.
Some decoded sequences are missing; timing statistics exclude such gaps.
The first programmed boot cannot validate retention through a spontaneous reset.

The completed check, programmer/tyutool logs and results are sealed in
`build/traces/reboot-lifecycle-hardware-20261010/`. A follow-on 1800-second
capture is **receiving actual bytes** in
`build/traces/reboot-lifecycle-long-20261010/`, host PID 1463673, exec session
8155. First received bytes: 10:12:08 London; approximate stop
10:42:08 London on 2026-10-10. The watcher (exec session 10040)
freezes the first boot/sequence restart plus 30 seconds of decoded/raw context,
with hashes, without sending reset commands. No first failure has been claimed.
Live files are not sealed yet; check process/received bytes before reporting
continued monitoring. Session metadata and capture scripts are preserved there.

### First lifecycle-diagnostic reboot — 2026-10-10 10:14:32

Operator reported "first reboot" and confirmed **USB-only power**. No flash or
reset command was sent after the program boot. The diagnostic capture records
a full boot at **10:14:32**, with unchanged BUILD ID
`23c49fa56c6bd7468b1a15178771e753b5e53cea`. Before boot, four consecutive recorded
heartbeat attempts at hardware times 439549, 440549, 441549 and 442549 ms have
successful async-send status and physical ACK state 2 (complete). Final captured
ACK is sequence 1206, kernel tick 400007, logged at 10:14:28.944. No skipped
heartbeat, mailbox-timeout, AP forced-reset or CP reset-request event is present
in that captured tail. This argues against a persistent heartbeat stoppage in
the observed interval; it cannot exclude an unlogged terminal failure.

A watchdog-reason-set event (code 16) at source rtc_ms 405836 / tick 374014 and
a recovered-stall message preceded the reboot by tens of seconds. It is not
established as the reset cause. Watchface ID 420, UUID
`a265518f-b30b-4ff4-b917-17a028e3aeba`, faulted around 10:14:19, PC `0x198a`, LR
`0x216fe15`, then relaunched. The exact firmware ELF resolves LR to
`prv_next.lto_priv.0` without a source line; app PC needs matching app symbols.
This is a preceding app fault, not proof it caused the full reset.

The next boot says `previous ring unavailable`, unknown reboot reason and MCU
mask `0x0`. The sealed map places the retained section at `0x280100c0..0x28010450`;
native RAM-function copy ends at `0x280100a8`, data copy starts at `0x28010460`,
and BSS zeroing starts at `0x28011880`. Thus those native startup ranges exclude
the ring. Bootloader clearing, corrupt retention or a power interruption remain
possible; USB-only operation does not prove a supply fault. No plaintext
`heartbeat timeout`, `CP crash`, `ASSERT FAILED`, `HardFault`, `BK_ASSERT` or
`brownout` marker was found in the frozen raw prefix; lack of such text is not
proof that CP did not fault.

The first-reset watcher finished and sealed five decoded/raw prefix files.
Nine-file supplementary evidence, results, symbol and memory-range checks are
sealed in `build/traces/reboot-lifecycle-long-20261010/first-reset-evidence/`.
The later USB-only operator confirmation is preserved separately, leaving that
snapshot unchanged. The host capture continues (PID 1463673, expected stop near
10:42 London). First-reset evidence will not be overwritten by later boots.
No firmware changes were made in response to this report. Root cause remains
unresolved; KernelBG investigation remains deferred.

### Further reboot cluster — 2026-10-10

The continuing capture records additional full boots at **10:17:14, 10:18:25,
10:18:48 and 10:19:55** London, following the operator-confirmed 10:14:32 boot.
Each reports MCU mask `0x0` and `previous ring unavailable`. This establishes
repeated full resets, without establishing their source. USB-only power is
operator-confirmed; a supply interruption remains a hypothesis, not a diagnosis.
A separate hashed snapshot is preserved in
`build/traces/reboot-lifecycle-long-20261010/reboot-cluster-evidence/`; the first
failure snapshot is unchanged. Live raw and decoded files continue growing,
so snapshot endpoints may differ. No firmware changes or reset commands were
issued. Capture was verified receiving data through 10:20:09; expected end
remains approximately 10:42 London. Recheck process and bytes before claiming
continued monitoring.

### Operator power clarification and later reset tails — 2026-10-10

The operator explicitly confirms **no power interruption**, with further
reboots observed. Treat this as uninterrupted USB-powered operation; do not
attribute the unavailable retained ring or zero reset-reason fields to a supply
interruption. Earlier power-loss possibilities above were unproven hypotheses,
not findings.

All four completed later boot segments end with successful heartbeat send status
and physical ACK state 2 in the captured tail. None contains a mailbox-timeout,
AP forced-reset or CP reset-request event. Physical ACK does not establish CP
application-level processing, and the final unlogged interval remains unknown.
The 10:18:25 to 10:18:48 boot interval is only 23 seconds and is still in startup;
the earlier watchface fault does not explain the entire reset cluster. Reset
origin remains unresolved. Investigate boot-reason preservation and trace RAM
retention before interpreting their absence as a reset classification. Analysis
and operator clarification are separately hashed in
`build/traces/reboot-lifecycle-long-20261010/uninterrupted-power-followup.json`.
No firmware changes, hardware reset commands or KernelBG investigation were
performed for this follow-up. Capture remains live at this snapshot.

## Improved boot-reason candidate — 2026-10-10, prepared only

Candidate `build/traces/reboot-boot-evidence-20261010/` preserves exact AP
raw/protected images, ELF/map/dictionary, resource and unchanged CP artifacts,
configuration, dirty-source snapshots/diff, tests and build logs. It has **not
been flashed**; the lifecycle-diagnostic image remains on the watch and its
1800-second capture is left running, expected end approximately 10:42 London.
Verify live process/received bytes independently before reporting monitoring.

The optional T5-local reboot recorder now copies the previous ring and raw PMU
R0/R7A reset-reason words at native SoC entry, after `SystemInit()` and before
native cache setup, startup IPC and board initialization. Board initialization
captures a second reason/header snapshot before clearing the live ring. Logs
show both raw words plus old ring magic/cursor, even for an invalid ring. The
worker reports subsequent reason-word changes at its existing two-second drain.
The early capture never logs or writes reset settings. No shared kernel changes,
CP changes, flash-layout changes, stack dumps or priority changes were made.
State tracing remains off; reminder profiling remains on.

Thirteen focused actual-source regressions pass, including capture-once,
later-cleared reason words, an overwritten live ring after early copy, raw log
values, runtime changes, disabled-header linking, and prior reset/heartbeat,
resource/flash/profile/QMI/state-recorder regressions. Unhygienix firmware build,
image-map validation and protected/raw comparison pass. FLASH 1609920 bytes,
RAM 99456 bytes, display RAM 118272 bytes; existing BLE LTO warnings remain.

- Raw SHA-256: `698d20c8ac6dc4cebe27761ea1b8fd95cb81f0c7420287e6b6c329e8e0b355ac`.
- Protected SHA-256: `180e9db3c4cdb27d16538bd02ceb8a1784c922c4733d37150e0764b192ef7235`.
- ELF SHA-256: `3c11cea5c701e3e34a30f3491dfff597d1ca10caed6d0ebaea4f85567ff4da9f`.
- Dictionary SHA-256: `5b283d56918b64fd6d5039ee827fd183f81fcbb2c0ded84744c383b312d3442c`.

This improves reset evidence, not reset behavior. It cannot recover anything
cleared by native C initialization or the vendor boot chain before the snapshot.
Inherited cache state can affect early observations. Reason-word polling can
miss brief changes, and the old RAM ring has not survived the observed full
resets. Zero reason fields or invalid retention do not establish power loss;
the operator confirms uninterrupted USB power. Root cause remains unresolved.
Hardware validation should distinguish the programming boot from a spontaneous
reset, inspect both stages and raw CP/AP fields with this exact dictionary, and
preserve the first complete raw/decoded failure. If both stages already lack
evidence, further AP-side logging alone cannot establish the missing cause;
boot-stage/CP evidence must be investigated without assuming a classification.

## Improved boot-reason candidate flashed; long capture — 2026-10-10

The operator requested flash and long trace. The prior lifecycle capture was
stopped cleanly (275964 raw bytes), and all 37 files were sealed in its
`final-manifest.json`, preserving its first-failure evidence. The new candidate's
45-file manifest was verified before flashing its sealed AP images. Guarded
AP-only programming succeeded in 31.3 seconds, physical range
`0x132000..0x2d39cc` end exclusive. CP, resources and PFS were not selected.
Programmer/tyutool logs are in
`build/traces/reboot-boot-evidence-long-20261010/`. The immutable candidate's
pre-flash `flashed=false` result remains a preparation snapshot.

A **four-hour** capture started receiving actual UART bytes at **10:30:07 London**,
expected end approximately **14:30:07 London**. Host PID 1497908, exec session
23693; first-reset watcher exec session 92997. It uses the candidate's sealed
log dictionary SHA-256
`5b283d56918b64fd6d5039ee827fd183f81fcbb2c0ded84744c383b312d3442c`.
Raw bytes, receive-time index and decoded logs are recorded separately. The
watcher freezes the first later boot/sequence restart, then saves 30 seconds of
post-reset context and hashes the raw/decoded prefix. Live files are not yet
sealed. Verify process and growing received bytes before claiming monitoring.

Capture attached after the programming boot's earliest messages at approximately
10:30:06; initial boot snapshot and BUILD ID lines were missed. Heartbeat ACKs
and decoded firmware output confirm a live link; the new boot-stage snapshots
still need validation on the next boot. No reset command was sent after the
programmer reset. Host PULSE reconnection is not proof of a watch reboot. No
stability or reset-cause claim is made. USB-only uninterrupted power remains the
operator's report. See `session.json` and preserved capture scripts for resumption.

## Reboot analysis with early snapshots — 2026-10-10

The four-hour capture is left running, PID 1497908, expected end near 14:30
London. Analysis is frozen separately in
`build/traces/reboot-boot-evidence-long-20261010/analysis-103922/`, with raw and
decoded snapshots, receive index, metrics, exact stock-CP disassembly excerpts,
source reference and a hash manifest. Snapshot endpoints may differ because
capture continues. The first spontaneous reset's five-file manifest was checked
and remains intact. No firmware changes, reset commands or flashing were done.

The improved candidate recorded full spontaneous boots at **10:32:29** and
**10:33:16**, BUILD ID `e0c493a4108afd128dd08f244e93ed54e8eb282f`. The first follows
successful app fetch cleanup at 10:32:25 and a successful heartbeat send/ACK
(sequence 325/326/327). The second ends with completed send/ACK 85/86/87, then
attempt 88 without a captured completion. This is a truncated terminal interval,
not proof of failed delivery. Neither completed segment contains a mailbox
timeout, AP forced-reset, CP reset-request or watchdog-reason-set event. Thus the
later recovered-stall marker cannot explain these two resets. Physical ACK is
not CP application-level processing; terminal events can be missing.

Both boots report **SoC and board stage r0=r7a=0x00000003, magic=0, next=0**.
The old header is already observed zero before native cache setup and board
initialization. The recorder's later live-ring clear does not explain this.
Native C startup precedes the capture, although its mapped copy/zero ranges
exclude the ring. Earlier boot-chain writes or inherited-cache observations
remain unqualified; these results do not establish a supply interruption.
Operator confirms uninterrupted USB-only power.

**Correction to the earlier R7A assumption:** During the same running boot,
R0 and R7A both change to `0x70000003` at 10:34:48 and back to `0x00000003` at
10:34:53, with no full boot. A reason-set event records code 16, followed by a
recovered stall. Therefore treating AP-visible R7A as an immutable previous-boot
snapshot is not justified on this target. This is a diagnostic interpretation
correction; the currently flashed getters have not been changed. Zero decoded
fields at AP startup are not authoritative evidence that CP had no reset cause.

The SHA-verified pinned stock CP binary confirms its reset initialization at
file offset `0x44b78`: it reads PMU R7A, extracts CP bits 4..11 and AP bits 24..30,
then stores them through base `0x2809f000` plus offsets `0x718` and `0x71c`
(**0x2809f718 CP; 0x2809f71c AP**) before clearing the fields through its setters.
This matches the pinned source and reference RAM map. The current AP diagnostics
do not read these saved words. The next scoped diagnostic should snapshot those
read-only CP-owned saved causes early, qualify cache visibility and field
validity against the pinned binary, and preserve their raw values independently
of the live PMU fields. They may still be zero; no reset cause is claimed here.
CP modification is not required merely to read these established saved slots.

There is also a complete heartbeat interval from sequence 13 to 16 after the
10:32:29 boot: hardware RTC **7270 ms**, kernel **1001 ticks**, successful send
and ACK on both sides. It occurs during startup, roughly 30 seconds before the
next boot, rather than proving a terminal heartbeat loss. The first segment has
a complete 5050 ms / 1000-tick interval, and the currently running segment has a
4771 ms / 1000-tick interval. Some records are missing, so the metric excludes
incomplete groups. This supports a wall/kernel-time discrepancy requiring
separate investigation; it does not prove a CP supervision timeout or justify
changing watchdog periods/priorities. KernelBG investigation remains deferred.

An app fault at approximately 10:36:14 occurs in the continuing boot, with no
full reboot through the frozen analysis endpoint. App faults, recovered stalls
and full resets must remain distinct. Persistence warnings and battery ADC
errors remain unresolved. No stability claim follows from the later quiet run.

## Standard-priority and input-path review — 2026-10-10

The operator resumed a scoped task-priority/starvation review after reporting
Select intermittently failing on watchfaces and accelerometer input missing in
**FdF Time**; Select works in menus. They specifically require standard tasks
to retain the same priorities as other boards. No global priorities were changed.
The four-hour capture continues; no flash/reset/trace stop was performed.

The current Unhygienix and saved Asterix reference configs both use five levels.
Higher number wins. Shared standard task/scheduler sources are unchanged from
HEAD: NewTimers and watchdog 4; KernelMain, PULSE and NimBLE host 3; App/watchface
2; app worker and KernelBG 1; idle 0. All faces use the same App priority;
FdF Time has no board-specific priority override. Equal-priority tasks rotate on
each kernel tick; this does not guarantee service for lower-priority ready work.

T5-local roles: physical mailbox 4 (bounded drains and blocking retry); CPHeartbeat,
HCI IPC and CST92xx 3; QMI8658 acquisition 2; battery and reboot-log drain 1.
CP runs on a separate processor. AP priority mutation for CP flash/SARADC is in
`fw/drivers/flash/t5ai_flash_ipc.c`: save **base** priority, boost below-3 callers
to 3, restore through shared success/error exits. The nested READ/READ_DONE
transaction preserves the outer boost. Code review finds no missed restore exit;
this is not runtime proof that an effective inherited boost cannot persist.
Shared app throttle temporarily sets App 0 for 300ms; raised callbacks use
KernelBG 3 with reference-counted restoration; mutex inheritance may also raise
effective priority. These exceptions must be recorded as base/effective pairs.
A stale CST92xx comment says it outranks KernelMain; actual configured priorities
are equal at 3. No priority change is warranted from that comment.

Two T5 input dependencies can explain load-sensitive failures without incorrect
standard priorities. Button ISR queues ordinary KernelBG work to **start** the
2ms/20-sample debounce timer. If it runs after a whole short press has ended,
that press cannot be reconstructed. nRF/SiFli instead start their hardware timer
at the edge. QMI I2C acquisition has its own priority-2 worker, but its single
pending sample stays pending until an ordinary KernelBG callback delivers it;
all further timer ticks are skipped during that wait. The existing QMI bounded
pipeline tests cover this intentional backpressure. Moving I2C off KernelBG did
not remove delivery's dependence on it. A five-second watchdog and later app
throttle cannot guarantee responsive button/sample delivery.

Current passive logs show recovered stalls and low-priority trace-consumption
overruns, but contain no all-thread ready/blocked samples or CPU deltas;
`CONFIG_T5AI_STATE_TRACE` is off. CPU starvation, callback/lock blocking and UART
logging delays cannot be separated conclusively yet. Do not certify starvation
as absent or proven. FdF Time's heap-exit line at 10:47:22 is an exit report,
followed by a UUID-identified fault without a verified FdF/UUID mapping; this
alone does not identify FdF as faulting.

Review snapshot, source/config evidence and manifest:
`build/traces/reboot-boot-evidence-long-20261010/task-review-105400/`. Next diagnostics should record all task IDs, base/effective priorities,
state and CPU deltas (without stack/register dumps), plus button edge-to-debounce
latency and QMI acquisition/pending/delivery/drop counts. Preserve standard
priorities; fix T5-local latency paths only after tests and candidate preservation.
The task-priority/input review is now authorized; unrelated broad KernelBG and
reset investigations should not be silently conflated with its evidence.

## Reminder mutex investigation — 2026-10-09

The 6,762 ms warning measures wall time **after acquisition** of the reminder
storage mutex, through `settings_file_rewrite_filtered()`, before unlock. It
excludes time waiting to acquire that mutex. It covers replacement creation,
record scanning/filtering/copying, both closes (including commit/deletion), and
reopening with settings validation/statistics. It is not an erase-only timer.
The filter preserves watch-originated reminders; a flush is not necessarily an
empty replacement. Reminder allocation is 40 KiB, with PFS space rounded from
120% of that allocation even when few records survive.

Optional `CONFIG_T5AI_REMINDER_PROFILE` (default off) adds SoC-local linker
wrappers; shared sources remain unchanged. For reminderdb only, they report
hardware-RTC wall milliseconds and scheduler-accounted CPU milliseconds by phase: 0 replacement PFS open, 1 subsequent settings
initialization and scan/copy, 2 old-file close, 3 replacement close/commit, 4
reopen and final settings checks. Flash R/W fields are calls/requested bytes/wall
milliseconds; E is blocking sector-erase calls/wall milliseconds. Erase can
contain measured reads, so totals overlap and must not be summed. Flash times
include flash-lock/scheduling waits; phase residuals can include PFS-lock waits,
explicit sleeps, processing and preemption. Filter counters exclude skipped
stale record headers. CPU statistics use scheduler tick accounting and may diverge from hardware
wall time; they are not physical CPU measurements. The summary reports hardware
wall and scheduler elapsed time separately. No stack or register tracing. Diagnostic messages are emitted after the measured rewrite, while
the caller still holds its mutex, so the generic hold includes logging overhead.

Instrumentation disables LTO only for seven intercepted translation units in
this diagnostic configuration. ELF disassembly confirms the original six wrappers have
linked call sites. This changes optimization/timing and is not a controlled
performance comparison. All 44 relevant tool tests pass, including deterministic
phase accounting, callback forwarding, failed open and other-task isolation.
Unhygienix diagnostic build and image-map/loghash checks pass (AP 1,608,384 bytes;
RAM 94,848 bytes). State tracing remains off. The sealed candidate, source,
config, ELF/map/dictionary and 41-file hash manifest are at
`build/traces/reminder-profile-20261009/`; raw SHA256
`bac008885443e5ae2f42df63c6656d3cd392498c255bb391eb7fa3e96f57025c`,
protected SHA256
`b4b1c58a87b69003cab46d9a43578de928ca3dea7ec4b920f73ef73371796b58`.
First diagnostic capture `build/traces/reminder-profile-flash-20261009.log`:
at 22:14:03 a successful reminder flush had zero filter invocations, no sector
erases and a 354 ms generic mutex hold. Its initial scheduler-based phase totals
were 93 ms (70 create, 5 initialization/copy, 18 commit). This disagrees with the
RTC mutex timer and includes post-measurement logging overhead; those phase
numbers must not be interpreted as reliable physical wall durations.

The corrected RTC candidate is sealed at
`build/traces/reminder-profile-rtc-20261009/` (39-file manifest), raw SHA256
`e533f206d29017301a72910a953a1750bdcc2e3326418542fefdfe6bf6b7b3a9`,
protected SHA256
`709798a47886c2e7b1a7107be50e936fb23f44cc6ff6d7c9adb4c1eddf2f8138`.
It was flashed AP-only, programmer success in 31.5 s; decoded boot and phone
session at 22:17:35 confirm live hardware capture. All 44 tests pass again;
stock Asterix incremental build and both native flash suites pass. The first
candidate was also flashed AP-only successfully in 31.5 s. Capture 1 completed;
RTC capture completed. Operator repeated **full resync and app start**. At
22:18:22 the successful rewrite took **6,235 ms** hardware wall time (5,996 ms
scheduler elapsed), one filter invocation, zero retained records:

| Phase | Wall ms | Flash read ms | Flash write ms | Erases |
|---|---:|---:|---:|---:|
| Replacement create | 6,141 | 111 | 6,024 (15 calls / 121 bytes) | 0 |
| Initialize/scan/copy | 17 | 2 | 14 | 0 |
| Old close | 0 | 0 | 0 | 0 |
| Replacement commit | 76 | 8 | 67 | 0 |
| Reopen/check | 1 | 0 | 0 | 0 |

This reproduction rules out record-copy volume and sector erase as the cause of
that long rewrite. First failure context is frozen at
`build/traces/reminder-mutex-hardware-20261009/first-slow-rewrite-rtc.log`.

A third, sealed diagnostic candidate at
`build/traces/reminder-profile-write-20261009/` added flash mutex acquisition,
analytics-add and driver-write wrappers; all nine interception sites were
confirmed in disassembly, tests passed and AP-only flash succeeded (31.4 s).
Raw SHA256 `1ad0982a81b9230275e2e97e08b66aa323e82e81f763db27712adf99348acb82`,
protected SHA256 `2b06290706cefdbf8b616f5f5bbe5e628a7eb578e176a736dd3f24bf4a7d50df`.
At 22:23:57, the rewrite was **6,278 ms**, generic mutex hold **6,307 ms**.
Creation took 6,179 ms: reads 3,656 ms, writes 2,519 ms, but driver-write bodies
only 198 ms, write-lock acquisitions 1 ms and analytics 1 ms. Largest intercepted
mutex acquisition was 2 ms. No erase; scan/copy 19 ms; one record, none retained.
This excludes long flash-lock acquisition and analytics for that run; most time
is elsewhere in the flash API, consistent with preemption on lock release.
The fourth candidate at `build/traces/reminder-profile-unlock-20261009/`
adds mutex-unlock wall timing (all ten wrappers confirmed in disassembly).
AP-only flash succeeded in 31.4 s; final 44 tool tests pass. Raw SHA256
`a17eeb71939c18b0b7e156dd510452511ee9e383c944d8960985e31031af5423`,
protected SHA256
`87bc081349888f111809e9a5c2002085f708d80faaf0daaf24b2cb992f787d3d`.
It remains a diagnostic build (`CONFIG_T5AI_REMINDER_PROFILE=y`, state trace off).

At **22:28:31**, full resync with the app drawing reproduced **6,300 ms**
reminder mutex hold; rewrite itself 6,266 ms. Creation was 6,172 ms, scan/copy
16 ms, commit 77 ms; one filter invocation, no retained record, **no erases**.
Creation CPU accounting was 54 scheduler ms. Flash reads took 4,627 ms, writes
219 ms, write-lock acquisitions 1 ms, analytics 0 ms. **4,548 ms elapsed across
flash mutex unlock calls**. Longest unlock was **1,158 ms**, address
`0x28022124`, which the exact candidate ELF identifies as **s_flash_lock**.
Largest intercepted lock acquisition was 1 ms. Driver-write bodies took 219 ms.

Confirmed mechanism for most of the long hold: the flash lock is released and
the resync task is preempted before unlock returns, while its outer reminder
mutex remains held. This is wall-time scheduling delay, not six seconds of flash
programming, scanning records, waiting to acquire the flash lock or sector erase.
The interval spanning an unlock is not time retaining that flash lock. Remaining
~1.7 s of rewrite wall time is not fully attributed; allocation has explicit
sleeps and other release/preemption opportunities, but those are not separately
measured. No quiet-face/app-closed resync comparison was captured in this session. Do not claim the
whole 6.3 s has been assigned to one boundary or that a scheduling fix is proven.

No priorities, watchdog timing, flash policy, shared rewrite semantics, or
resource_unwatch ownership were changed. All new instrumentation is default-off
and SoC/board-local. Avoid presenting erase-delay or larger header-cache changes
as a fix for this reproduced case. A future correction should address progress
of the critical rewrite under competing app work or shorten the outer critical
section with a correctness-preserving design; no such behavior change was made.
The broader KernelBG stall investigation remains deferred.

All four captures have completed; **no capture is running**. Full traces,
programmer/tyutool logs, tests, frozen first-slow-rewrite and first-unlock-delay
traces, structured results and hash manifest are preserved at
`build/traces/reminder-mutex-hardware-20261009/`. Candidate directories remain
sealed and unchanged. Latest AP is 1,608,832 bytes, RAM 94,944 bytes. The current
watch has the final unlock diagnostic, not a behavior fix. Do not interpret this
short investigation as stability validation; prior release blockers remain. Previous release blockers and deferred KernelBG investigation still apply.

## Latest AP flash and trace — 2026-10-09

Operator requested "flash and trace". The sealed flash-cache-cleanup candidate
was verified against its 41-file hash manifest, then flashed using
`tools/flash_unhygienix_firmware.py` and tyutool 3.2.9. Programmer reported
success in 31.4 s. Exact AP-only physical range: `0x132000..0x2d2cc8`, end
exclusive. CP/resources/provisioning were not selected; PFS and pairing ranges
were outside the programmed range. No rollback was performed.

Host UARTs were re-enumerated and the runtime UART received 1,272 bytes during
preflight. A six-minute streaming capture used the exact candidate dictionary
at 460800 baud. It has completed; no capture is running. Full evidence:
`build/traces/flash-cache-cleanup-hardware-20261009/`, including original and
readable traces, frozen first-reset/first-assertion traces, programmer logs,
preflight bytes, results and hash manifest. Candidate ELF/map/dictionary remain
in `build/traces/flash-cache-cleanup-20261009/`. Initial trace lines precede the
flash and contain old-firmware hashes; do not attribute them to the candidate.

Candidate boot records (London time): 21:56:55.000, 21:57:08.000, 21:58:06.000, 21:59:20.000. The first was the
programmer-requested boot; the other 3 were not requested by this session.
Reset reason was unknown/MCU mask 0. An old-firmware boot also appeared before
flashing, so reset symptoms predate this candidate.

At 21:58:04.801–.802 the watch asserted at logged LR `0x021e5b0f`:
`Cannot delete @00000073/res, it is currently in use`. The exact ELF resolves
the address to `pfs_remove_files.constprop.0`; no precise source line was
available. Operator reported phone sync or app/watchface changes at that time.
This is a concrete resource-file lifetime failure, not proof that flash cache
invalidation caused the assertion. The first failure traces were preserved.

The phone re-established encrypted PPoGATT sessions after reboot. App records
were enumerated as 45, then 45, then zero after an explicit AppDB flush and
reboot. MU-TH-UR T fetch for app ID 115 returned result 0, but subsequent lookup
for that ID failed. Reminder-DB mutex hold reached 6,762 ms at 21:59:15.855.
These workloads differ from earlier captures; there is no controlled timing
comparison or proof of physical PFS erasure/data loss. Persistence remains
unqualified.

At 22:00:54.573 KernelBG was unfed for 5,269 ms; MU-TH-UR T throttling began,
and watchdog recovery was logged at 22:00:55.902. No new tracing or priority
changes were made and stall investigation remains deferred. Battery ADC `-12`
recurred. Last captured heartbeat: [22:01:55.043] <dbg> hci_t5ai: T5AI HCI heartbeat: active=1 adv=1 queue=0 started=1 fault=0.

Repeated uncommanded reboots and the deletion assertion block release. Later
reconnection or a quiet interval do not establish stability. No firmware source
changes were made during hardware validation.

## Previous erase-delay flash and hardware evidence

The preceding erase-delay flash was AP-only, preserving the CP image, resource
partition and PFS write ranges. State recording is disabled. Exact artifacts:

- `build/traces/erase-delay-fix-20261009/`: raw/protected images, ELF, log
  dictionary, `.config`, manifest, source diff, flash-driver snapshot and results.
- Raw SHA-256:
  `699733ebf79032570c4cc02c6430274c44c7d6b3592f7c9e60d0241e0d1cff43`.
- Protected SHA-256:
  `2e043d27b3c6556972f2fbd952f17befd461c795c1d1fc7bbcc692e1dee932f2`.
- Physical write: `0x132000..0x2d2d72`, end exclusive.
- Programmer log: `/home/baz/.local/share/tyutool/tyutool-20261009-211912.log`.
- Baseline: `build/traces/boot-loop-before-erase-fix-20261009.log`.
- Candidate trace: `build/traces/erase-delay-fix-flash-20261009.log`.
- Earlier full issue inventory: `build/traces/issues-20261009-results.json` and
  `build/traces/issues-20261009.log`.

Baseline captured two reboots and a 6793 ms reminder-database mutex hold. The
new build booted at 21:19:44 London time, reconnected and recorded a 275 ms
reminder-database hold. This is not a controlled benchmark: startup removed a
temporary file and enumerated zero app records, then fetched MU-TH-UR T with
result 0. AP-only flashing did not intentionally erase PFS, and the app-record
observation must not be presented as healthy persistence.

At 21:21:03–04 KernelBG was unfed for 5435/6442 ms; sampled PCs resolved to
`app_install_get_entry_for_install_id` and `comm_session_send_data`. At
21:21:05 the watchdog explicitly reported recovery after throttling. No further
boot occurred through the final heartbeat at 21:23:05, approximately 3 min 21 s
after flash boot. Battery ADC `-12` recurred. This does not establish long-term
stability or resolution of the boot loop. All captures from this session have
finished; no live monitor is running at handoff.

## Validation and resumption

Recent checks passed: firmware build, actual-source QMI worker, state recorder
and T5 flash completion regressions, native accelerometer manager tests, and
six native flash API tests including short-delay blocking and asynchronous
completion. Existing BLE LTO type-mismatch warnings remain in the firmware
build. See the stale board-test exceptions above.

Canonical build: `pbl configure --board unhygienix`, then `pbl build`.
This host's working build directory is `build-cmake`; its bundled tools are
`/tmp/pebbleos-merge-gitlint/bin/ninja` and `.../ctest`. Native tests are in
`/tmp/pebble-wrapper-tests`; use `CCACHE_DISABLE=1` there. Actual-source tests:

```sh
python3 -m unittest tools.tests.test_t5ai_qmi8658 tools.tests.test_t5ai_state_trace tools.tests.test_t5ai_flash_completion tools.tests.test_t5ai_dbgserial -v
```

For each refactor slice, inspect current source/diff, make one scoped change,
run relevant host tests and a native firmware build, then retain the exact
candidate before hardware validation. App install/launch, Settings scrolling,
phone reconnect and selected-face persistence are necessary regressions.
Broader qualification still includes 25 reconnect cycles, true cold boot and
an overnight connected-idle soak.

Host hardware access may require sandbox escalation. Re-enumerate the CH342F:
`/dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00` is programming/reset;
`...-if02` is runtime capture at 460800 baud. Confirm received bytes before
claiming monitoring. Use the supported guarded AP-only flash path described in
[tooling](tooling.md); preserve the first failure and matching ELF/dictionary.
