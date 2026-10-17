# Unhygienix dependency, build and flash tooling

Unhygienix means "The village fishmonger." The independent hardware target is
`unhygienix` (`CONFIG_BOARD_UNHYGIENIX`), with `PLATFORM_GABBRO` apps and the
existing Round 2/Getafix DVT2 companion identity. Getafix DVT/DVT2 hardware
configuration is separate.

Run commands from the PebbleOS repository root. Keep supplied build inputs and
generated validation artifacts under `build/`.

## Dependency setup

```sh
git submodule update --init third_party/hal_tuya/TuyaOpen-T5AI
pbl configure --board unhygienix
pbl build
```

Use the normal PebbleOS SDK/toolchain setup and Python requirements. Native AP
builds do not compile or link the Tuya application, vendor RTOS or vendor BLE
host. Native board, SoC and drivers retain ownership of the AP implementation.
The platform submodule pins `13379b63e07e78770fb4d0bffe36db2754658132`.
TuyaOpen pins `e82840dc9b629cb77fa922fb04e3f820dc2e78b4` and is optional for AP/CP
packaging. Neither submodule is upgraded implicitly.

## CP packaging

The stock CP is a generated vendor artifact, not tracked source in either SDK.
Supply the validated stock `app.bin` with SHA-256
`0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856`.
A fresh checkout needs this artifact separately to package CP. Keep the original
for rollback; unknown input images are rejected before compilation.

```sh
pbl configure --board unhygienix -DT5AI_CP_STOCK="$PWD/build/inputs/stock-cp.bin"
pbl build t5ai_cp
```

This uses the configured ARM compiler, builds the board-local RF hook, and emits
`build/pebbleos.t5ai-cp.bin`, `pebbleos.t5ai-cp-crc.bin`, and
`pebbleos.t5ai-cp.json`. The manifest records SDK revisions, stock and hook
hashes, output hashes and the supported Unhygienix mailbox/HCI ABI. It describes a
source-hook integration into a pinned binary, not a full CP source rebuild.
Changes to CP ABI, addresses or radio policy require revalidation.

## Native flashing

The tyutool runner uses the Tuya UART boot protocol; generic OpenOCD/PULSE
programming is not established on this hardware. The validated backend is
`tyutool_cli` 3.2.9; install it independently and pass its executable path.
Serial ports are explicit; prefer the stable programming UART `if00` by-id path.

```sh
pbl flash --dry-run --tyutool tyutool_cli --tyutool-tty /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00
```

Remove `--dry-run` to write. Default is AP firmware only. `--resources` includes
system resources. `--t5ai-provision --resources` also formats PFS, destroying
stored apps/data. `--t5ai-cp` includes CP from the build directory after vector,
CRC, size and manifest validation. All selected inputs are checked before the
first write. CP is written first if selected; failed writes stop the sequence.
Multiple partition writes are not atomic. Factory bootloader, configuration,
RF calibration and shared pairing storage are excluded from these operations.
Erase, recovery and OTA remain disabled. Existing dedicated guard scripts remain
usable for validation and rollback.

## Optional vendor reference build

```sh
git submodule update --init --recursive third_party/hal_tuya/TuyaOpen
python tools/build_tuya_hardware.py --python .venv-tuya/bin/python
```

Use the pinned TuyaOpen `pyproject.toml` and `uv.lock` for its separate Python
build environment. The builder stages both SDKs under `build/unhygienix_hw/sdk`
and invokes the vendor generator there, keeping submodule source pristine.
Vendor toolchain downloads remain governed by the pinned T5AI scripts. This
reference target is optional; it does not build the native PebbleOS kernel or
flash hardware. The retired vendor MPU/kernel bridge experiment is available
in Git history; the native PebbleOS kernel does not use those patches.

## Validation

Check native AP builds and existing image/map tests, then runner tests for exact
write ranges, default PFS preservation, explicit provisioning and rejection of
corrupt AP/CP inputs before any write. Hardware qualification still requires the
watchface regression, 25 reconnect cycles, cold-boot bond/face restoration and an
overnight connected-idle soak. Preserve the complete first failure trace.

## Task-state recorder

`CONFIG_T5AI_STATE_TRACE` is an optional compile-time diagnostic flag, disabled
by default. Enable it explicitly for an investigation:

```sh
pbl configure --board unhygienix -DCONFIG_T5AI_STATE_TRACE=y
pbl build
```

Disable it again in the same build directory:

```sh
pbl configure --board unhygienix -DCONFIG_T5AI_STATE_TRACE=n
pbl build
```

CMake equivalents are `cmake -S . -B build -DBOARD=unhygienix
-DCONFIG_T5AI_STATE_TRACE=y` (or `n`) followed by `cmake --build build`.
The override persists in that build directory's CMake cache. Check `.config`
for `CONFIG_T5AI_STATE_TRACE=y` or `# CONFIG_T5AI_STATE_TRACE is not set`.
Rebuild and flash the resulting firmware to change the watch's behavior;
there is no runtime toggle.

When disabled, CMake excludes `soc/t5ai/state_trace.c` and the start/freeze
hooks compile to empty inline functions. There is no recorder thread, sampling,
retained ring or `T5STATE` output. When enabled, the recorder adds a 1536-byte
thread stack and an 848-byte retained ring, plus thread bookkeeping and code.
A dedicated thread samples KernelBG and App once per second into a 16-entry
internal-RAM ring. Sampling performs no logging, allocation, flash writes,
stack walks, or register capture. This flag does not change watchdog timing:
the board independently configures checks every 1000 ms and 2000 ms overdue
before reset; the task timeout remains 5000 ms.

The T5 watchdog reason setter and whole-chip reset path freeze the first
failure snapshot. On the next boot, valid retained records appear in the
normal UART trace with the `T5STATE` prefix. Record checksums reject torn or
corrupt samples. Retention through the vendor whole-chip boot path still
requires hardware validation; power loss can erase the ring. There is no
persistent-flash fallback.

Each sample reports uptime ticks, the background callback address, and paired
BG/APP records with thread ID, cumulative CPU ticks, state, base/effective
priority, and wait-queue address. States are 0 ready, 1 running, 2 blocked,
3 suspended, 4 dead, and 255 absent. Compare CPU counters only for the same
thread ID; app IDs change when threads are recreated. Resolve callback
addresses with the exact candidate ELF. `same_callback_ticks` measures how
long the sampled pointer stayed unchanged; repeated invocations of the same
function are indistinguishable. A zero callback means no callback was active
when sampled.

A ready KernelBG with little CPU progress while the app advances supports
starvation. A consistently blocked KernelBG and stable wait queue supports a
wait on an object. Increasing KernelBG CPU time indicates continued execution
rather than complete starvation. The ring freezes on the first watchdog
report even if throttling later recovers; reboot starts a fresh recording.

### QMI8658 polling isolation

Periodic accelerometer I2C acquisition runs on the static `QMI8658` worker
(priority 3, 1536-byte stack). NewTimers only signals a binary semaphore;
NewTimers delivers the acquired sample and runs the existing shake filter through
the standard accel-manager offload callback with the manager mutex held.
The sensor ODRs, requested sampling intervals, startup settling and axis mapping
are unchanged. Configuration and explicit uncached `accel_peek()` calls remain
synchronous. A driver-local mutex serializes register-pointer/read pairs with
configuration writes.

Only one acquisition or delivery is outstanding. Ticks during that work are
dropped. Delivery uses the standard timer work queue with its bounded enqueue
wait; a full queue drops that sample and allows the next tick to retry; no catch-up
burst is generated. Power, rotation and rate changes invalidate stale samples.
This isolates periodic I2C waits from KernelBG; it does not establish the cause
of the previously observed stalls or prove hardware stability. Validate with
MU-TH-UR T app loading and a sustained accelerometer subscription after flashing.

`python3 -m unittest tools.tests.test_t5ai_qmi8658 -v` compiles the production
driver with controlled I2C and scheduler stubs. It checks pending-work bounds,
queue pressure, read failures, stale samples, startup settling, worker priority,
and that neither the timer nor the delivery callback performs I2C.

T5 buttons use an ISR-safe binary semaphore and a static priority-4 worker with
a 1536-byte stack. It sleeps between 2 ms debounce samples and blocks when idle,
without KernelBG or a droppable activation queue. Validate the production driver
with `python3 -m unittest tools.tests.test_t5ai_buttons -v`; physical button
latency, reset-hold timing and app interaction still require hardware checks.

### Synchronous flash erase completion

T5 CP flash erase IPC completes synchronously: all requested 4 KiB erases
finish before the T5 driver's `erase_*_begin()` returns. The stock flash API
still polls once to consume the completion, release its erase semaphore and
deliver callbacks. The T5 driver therefore advertises a 2 ms completion-poll
duration, which the shared API rounds to a 1 ms first poll. This is a scheduling
hint after synchronous completion, not a claim about physical erase time.
Do not set it to zero or one: the shared API would skip polling and strand the
completion.

The previous 50/800 ms hints inserted 43/700 ms sleeps after already-completed
erases. Settings/database rewrites kept their mutex held through those sleeps.
The driver-local change removes that avoidable delay without changing database
locking, rewrite atomicity, watchdog limits or the CP flash protocol. Actual
flash work, settings scans and scheduling delays can still extend mutex holds;
compare the `FIRM-1649` hold-time logs across a phone full sync after flashing.

Host validation: `python3 -m unittest tools.tests.test_t5ai_flash_completion -v`
checks the production T5 driver; native `test_flash_api` covers both blocking
completion and asynchronous callbacks with the short polling hint.

### Flash cache failure regression

`python3 -m unittest tools.tests.test_t5ai_flash_cache tools.tests.test_t5ai_flash_completion tools.tests.test_tuya_flash -v`
compiles the actual T5 driver with controlled flash IPC. Header-cache enabled
and disabled cases cover unsuccessful and partially applied mutations,
verification failure, unavailable transport, unaffected cache lines and partial
sector erases. Affected caches are invalidated before each attempted mutation;
successful header writes and erases retain their existing cache fast paths.
These are host checks, not evidence of hardware persistence or stability.

### Reminder rewrite timing

`-DCONFIG_T5AI_REMINDER_PROFILE=y` enables temporary T5-only timing wrappers;
normal/default builds leave them off. Keep `CONFIG_T5AI_STATE_TRACE` disabled.
Capture with the exact candidate's log dictionary. `Reminder phase` values are
0=create, 1=initialize/scan/copy, 2=close old, 3=commit, 4=reopen/check. Wall/CPU
and R/W/E durations are milliseconds; R/W triples are calls/bytes/time and E is
calls/time. Reads inside erase overlap E; do not add these fields together.
See the refactor handoff for limitations. The profiler does not trigger a flush;
reproduce through the normal phone-sync workload.

```sh
python3 -m unittest tools.tests.test_t5ai_reminder_profile -v
```

The deeper profiler also reports `write_lock`, `driver`, `analytics`, and
`unlock` per phase. These are nested wall durations, not additive categories.
`Reminder wait` records the longest intercepted mutex acquisition and sampled
owner ID; `Reminder unlock` records the longest flash-API unlock and mutex
address. Resolve data addresses against the exact candidate ELF's `nm` output.
An unlock wall duration can include time when the caller is preempted after
releasing the lock; it is not time keeping that flash lock held.

## Reboot lifecycle tracing

`CONFIG_T5AI_REBOOT_TRACE` is a separate, default-off T5 diagnostic. Enable with
`pbl configure --board unhygienix -DCONFIG_T5AI_REBOOT_TRACE=y`, then build and
preserve the exact candidate before flashing. Disable with the same command and
`=n`. This does not enable `CONFIG_T5AI_STATE_TRACE` or investigate KernelBG.

`T5REBOOT` records contain sequence, hardware `rtc_ms`, kernel `ticks`, event and
value. Events: 1 heartbeat attempt, 2 skipped attempt (bits: 1 runtime not ready,
2 prior heartbeat pending, 4 mailbox fault), 3 async-send status, 4 physical ACK
(state 1 pending, 2 complete, UINT32_MAX no ACK, or unsigned failure status),
5 logical heartbeat response, 6 mailbox timeout/channel, 7 Pebble reason set/code,
8 AP force-reset/source and 9 CP system-reset request. Send status values are
unsigned renderings of `status_t`; interpret negative errors as signed 32-bit.
ACK callbacks can run before the send-status event because the mailbox worker
has higher priority. Each attempt precedes its skip/send and only one heartbeat
is outstanding. These are AP observations, not proof that CP consumed a packet.

Event hooks do not allocate, log, send IPC or wait on mutexes. They briefly mask
kernel interrupts, write/checksum a bounded 32-record RAM ring and flush its cache
lines. A static priority-1 diagnostic worker drains records every two seconds
and emits an independent `drain` liveness message even without heartbeat events.
It logs outside the IRQ lock. It can be delayed by higher-priority tasks or UART
backpressure; overrun/overwrite warnings make dropped records visible.

The 776-byte NOLOAD ring is copied to a 776-byte replay buffer during board early
init, before new events overwrite it. The worker validates and prints previous
records (`previous=1`) before live records (`previous=0`). A 1536-byte stack and
thread bookkeeping are additional costs. No flash persistence is used. Hardware
RAM retention remains unproven: absent previous records do not distinguish a
power reset, bootloader clearing or corrupt retention. Logs and checksums cannot
prove that a failure occurred outside AP paths if a marker is missing.

Existing task priorities, watchdog timing, CP binary and mailbox/HCI ABI remain
unchanged. Logging and cache maintenance add diagnostic overhead. Capture raw
UART as well as decoded PULSE logs to retain any unframed vendor diagnostics.

### Reboot-reason evidence at native entry

With `CONFIG_T5AI_REBOOT_TRACE=y`, `pbl_soc_early_init()` reads and snapshots the two
AON reset-reason words after core initialization, before native cache
setup, startup IPC and board initialization. It copies the previous 32-record
ring at the same point. Board initialization captures a second header/reason
snapshot before resetting the live ring. The worker later prints:

- `T5REBOOT boot stage=soc|board r0=... r7a=... magic=... next=...`;
- `T5REBOOT reason change r0=... r7a=...` when those words differ at a drain.

`r0` is the live PMU word at `0x44000000`; `r7a` is the boot snapshot at
`0x440001e8`, used by the pinned BSP. Keep the raw words: decoded zero fields
alone do not classify the reset. `magic` and `next` describe the ring before
initialization, including when invalid. Both stages are logged later, outside
early boot and IRQ locks. Sampling changes no reset settings, priorities,
CP image, shared layout or flash contents. Existing heartbeat diagnostics remain.
There is no stack/register dump or KernelBG sampling.

Native C data/BSS initialization and the vendor boot chain precede this snapshot.
Evidence erased before native entry remains unavailable. Bootloader cache state
is inherited until native setup; different stage observations localize a change
but do not alone establish its cause. Runtime reason-word changes are polled
once per drain and can miss short-lived changes. The retained RAM ring has not
yet survived the observed full resets; this diagnostic does not claim otherwise.

## Optional interrupt/timebase measurements

Enable `CONFIG_T5AI_INTERRUPT_PROFILE=y` together with `CONFIG_T5AI_REBOOT_TRACE=y`
for a diagnostic candidate, then build and preserve it before flashing. Default
is off. Disable with `=n`. State tracing is independently disabled.

`T5IRQ` durations are raw AON ticks (32 ticks/ms, 31.25 us/tick). All counters
are per-boot cumulative modulo 2^32. `tick` counts serviced SysTicks; `gap_max`
measures the largest entry-to-entry gap; `gap_gt1p5` and `gap_gt3` count gaps above
1.5/3 ms; `body_max` includes time preempted by other interrupts. `mask_*` reports
outermost BASEPRI windows through the kernel's arch mask functions, excluding
direct PRIMASK sites. `source` 0=I2C, 1=GPIO, 2=mailbox, 3=masked idle confirm/WFI
window; counts/totals/maxima include preemption and must not be summed as CPU time.
Compare successive count/total deltas against raw elapsed RTC time for rates.

`clock_changes` samples the core source/divider and CPU1 speed bit at tick entry;
`reload_changes` samples SysTick reload (including initial setup). `mode` reports
the latest shared clock-mode word; only source/divider bits contribute to the
change count. `cpu1_speed` is the raw divider-selection bit, not decoded Hz.
The hooks write no clock/tick settings and log only from the existing reboot
worker. Three kernel files compile without LTO for interception; shared source
is unchanged. Verify the actual ELF vector/wrapper calls, not just symbol presence.

These diagnostics cannot capture every transient clock change or direct
interrupt mask and are not retained through reset. More logging and per-tick
reads perturb timing. Use the exact candidate dictionary and keep raw UART.
`python3 -m unittest tools.tests.test_t5ai_interrupt_profile -v` exercises the
actual source with controlled AON time and original-handler stubs.

## T5 elapsed kernel time and clock tracking

T5-owned SysTick recalibrates against the live CP-selected clock and accounts
kernel time against AON. Delayed service uses the existing bulk elapsed-time
API followed by one normal tick, without replaying missed IRQs. Unsupported
DCO frequencies retain the previous reload; AON accounting remains active.
No new hardware timer ownership or task/IRQ priority changes are required.

With profiling enabled, T5IRQ tick still counts serviced SysTick entries, not
elapsed kernel ticks. Compare T5REBOOT ticks and rtc_ms deltas for kernel time;
use IRQ gaps/mask maxima for latency. The wrapper samples reload before the T5
handler, so reload changes may appear one sampled entry after clock changes.
Arch wrappers exclude direct common heap masks and PRIMASK outside measured idle.

Run python3 -m unittest tools.tests.test_t5ai_timing -v for actual-source
clock/elapsed-time and allocator/transport regressions. Native test_kernel
covers bulk deadline expiry. Preserve candidate and dictionary before flashing;
never switch a live capture to a newly built dictionary. Hardware validation
is required; this does not prove all long masks removed or classify resets.

## Configured T5 watchdog period — 2026-10-10

The T5 AON watchdog uses CONFIG_WATCHDOG_TIMEOUT_MS for both start and feed;
its millisecond period must fit a nonzero 16-bit field. Current configuration
requests 30000 ms. Task watchdog timeout/grace remain separate and unchanged.
`python3 -m unittest tools.tests.test_t5ai_reset -v` exercises actual-source
start/feed/stop and reset-state behavior at multiple configured periods. A
correct programmed period does not prove that spontaneous resets are resolved.

## DB/heap investigation candidate — 2026-10-10

Keep `CONFIG_T5AI_STATE_TRACE=n`. Enable `CONFIG_T5AI_REMINDER_PROFILE=y`,
`CONFIG_T5AI_REBOOT_TRACE=y` and `CONFIG_T5AI_HEAP_TRACE=y` for this temporary
investigation, then build and seal the candidate before flashing. Default flags
are off. Prepared candidate: `build/traces/db-heap-investigation-20261010/`.
Current running capture still belongs to the previous watchdog-timeout candidate.
Always decode a fresh post-flash capture with the newly sealed dictionary.

The reminder flag now instruments explicit pin/reminder storage flushes. New
`T5DB name` identifies each block; existing phase numbers are unchanged. DB wait
and hold are separate. R/W/E cover full synchronous flash API calls; mutex wait
is nested within these operations. New summaries are emitted after DB unlock.
Concurrent/nested flushes skip the one bounded profiling slot and increment
`skipped`. Automatic compaction and unrelated rewrite entry points are not traced.

`T5HEAP` reports used/free/largest every ten AON seconds from the existing reboot
worker. When low, or before kernel croak_oom, it dumps at most 16 recent external
kernel heap operations: op 1 malloc, 2 zalloc, 3 calloc, 4 realloc, 5 free. Pair
record/caller lines by seq; resolve client/site PCs with exact candidate ELF.
Allocation ptr=0 is failure; free bytes=0 is not failure. Supplied client PC can
still be zero; site is an independent fallback. No stack walking is performed.
Recording allocates/logs/scans nothing; totals scanning and dump logging occur
outside the hook. Global dump guard omissions are visible on the next report.
History is not a leak ledger or reset-persistent storage. This candidate reduces
kernel heap by 640 bytes compared with the flashed watchdog candidate.

`T5REBOOT saved` reports raw CP-saved causes at SoC/board entry; validate their
semantics on hardware. `T5REBOOT watchdog` reports running, feeds (including
start), current age_ms and max_gap_ms; stop/start excludes the stopped interval.
All are AON based. A late drain is a delayed observation, not a measurement of
the final hardware feed gap at reset. Counters/history can be lost at full reset.

Run `python3 -m unittest tools.tests.test_t5ai_heap_trace
 tools.tests.test_t5ai_reminder_profile tools.tests.test_t5ai_reboot_trace
 tools.tests.test_t5ai_reset tools.tests.test_t5ai_timing -v` on one shell line.
See refactor handoff for candidate validation and interpretation limits.

## T5 reset-reason preservation

Prepared candidate `build/traces/reset-reason-preserve-20261010/` fixes generic
hard-reset overwriting of specific mailbox/Pebble reasons. Boot getters use a
once-only pre-cache snapshot of CP-saved AP/CP words rather than mutable PMU R7A.
The behavior is independent of diagnostic flags; original reset timing and
supervision ownership are unchanged. Validate actual reason persistence after
flashing using the exact sealed dictionary. This is an evidence-preservation
fix, not a fix for the fault that initiates reset. Existing live capture remains
on its previous firmware/dictionary until an explicitly requested flash.

`python3 -m unittest tools.tests.test_t5ai_reset -v` exercises diagnostics-on/off,
boot snapshots, generic/specific reset reasons, non-AP PMU fields and both reset
period registers, plus configured watchdog and heartbeat timing behavior.

## Minimal HardFault diagnostic candidate (2026-10-10)

`CONFIG_T5AI_FAULT_TRACE=y` enables a default-off T5-local fatal vector capture.
Sealed candidate: `build/traces/minimal-hardfault-20261010/`; flashed AP-only
on 2026-10-10, boot BUILD verified at 14:08:21 London. Runtime evidence is in
`build/traces/minimal-hardfault-long-20261010/`; see its session and flash results.
Use its exact ELF for PC/LR resolution and its dictionary for the normal logs.
Emergency plain text begins `T5FAULT`; raw UART capture is needed even if normal
framing/dehashing does not recognise the line. V/X indicates accepted/rejected
frame; zero MMFAR/BFAR with their validity bits absent means unavailable.
The FIFO/CP transmission is bounded and best effort; missing output does not
establish absence of a fault. Only the HardFault vector is intercepted.
See the refactor handoff for scope, memory cost and validation limitations.

## HardFault MSPLIM repair flash (2026-10-10)

Candidate `build/traces/hardfault-msplim-fix-20261010/` was sealed before AP-only
flash. Exact BUILD `78782b8cb6617c9cdef0e86c82c6bb99d8d95aa2` booted14:30:48
London. Live capture/evidence: `build/traces/hardfault-msplim-long-20261010/`.
Use this candidate's ELF and dictionary. The repair keeps hardware stack limits
active while switching both MSP/MSPLIM; emulator tests cover actual entry.
Physical CP fault-message delivery and original reboot cause remain unqualified.

### Fault-time MPU records (prepared 2026-10-10)

The optional T5AI_FAULT_TRACE fatal report retains T5FAULT, then emits raw
T5MPU (TYPE/CTRL/RNR/MAIR/count), T5CPU (fault-time control/masks, original stack
limit, SHCSR/CCR), T5ORIGIN (one guarded original exception PC/LR/xPSR), and
T5REGION (index/RBAR/RLAR). Check raw UART, because PULSE decoding can omit these
plain text records. A missing tail may mean the shared100ms transport deadline
expired; do not assume omitted regions are disabled. TYPE gives implemented
count, count gives captured count (maximum16); disabled regions are emitted too.

For enabled RLAR (bit0), bounds are RBAR&~31 through (RLAR&~31)|31. RBAR bit0
is execute-never; bits2:1 are AP, bits4:3 shareability. RLAR bits3:1 select MAIR
attribute. Compare the handler PC and original PC against all enabled ranges;
interpret permissions using the actual exception/privilege context. The static
expected flash region alone does not prove the runtime mapping was correct.
Unknown/unsafe original frames emitX; no full stack dump is collected.

## Disable investigation tracing — 2026-10-10

All T5 investigation tracing is optional and defaults off. For an existing build
cache, explicitly disable every flag (disabling REBOOT_TRACE also hides its
interrupt/heap dependents):

```sh
cmake -S . -B build-cmake -DBOARD=unhygienix \
  -DCONFIG_T5AI_STATE_TRACE=n -DCONFIG_T5AI_REMINDER_PROFILE=n \
  -DCONFIG_T5AI_REBOOT_TRACE=n -DCONFIG_T5AI_INTERRUPT_PROFILE=n \
  -DCONFIG_T5AI_HEAP_TRACE=n -DCONFIG_T5AI_FAULT_TRACE=n \
  -DCONFIG_T5AI_FLASH_PROFILE=n -DCONFIG_T5AI_DISPLAY_PROFILE=n \
  -DCONFIG_T5AI_HCI_PROFILE=n
cmake --build build-cmake
```

FLASH_PROFILE gates flash transaction timestamp/counter collection and its HCI
worker reports. DISPLAY_PROFILE gates frame/DMA-failure counters and draw/render
reports. HCI_PROFILE gates transport counters, periodic diagnostic heartbeat,
disconnect snapshots and task/stack sampling. These flags are independent;
ordinary errors, protocol handling, CP supervision heartbeat, timing corrections
and the shared ARMv8 MPU fix remain active. Stock Pebble operational logging and
upstream timeline mutex warnings remain unchanged. Disabled trace headers compile
to no-op hooks; diagnostic source files, linker wrappers, forced diagnostic
no-LTO options and fatal-vector override are excluded by their existing flags.
Candidate build/traces/tracing-disabled-20261010/ was sealed before flashing.


## Animated watchface profiling

`CONFIG_T5AI_DISPLAY_PROFILE=y` now selects the existing `CONFIG_PROFILER`.
It leaves animation cadence, rendering, DMA ownership, clock policy and shared
Pebble source unchanged. Normal builds still default both options off. Configure
a separate build directory so an ongoing fault investigation keeps its own image:

```sh
pbl --build-dir build-fdf-profile configure --board unhygienix \
  -DCONFIG_T5AI_DISPLAY_PROFILE=y
pbl --build-dir build-fdf-profile build
```

The T5 active-clock initialization starts the stock DWT profiler. No console
`profiler start` command is needed. Do not reset the profiler during a capture.
Keep other optional diagnostics disabled for the timing baseline. Preserve the
exact ELF, raw/protected images, `.config`, source diff and log dictionary before
using the supported guarded AP-only flash procedure. Coordinate with the owner
of any freeze investigation before resetting, flashing or opening a UART already
owned by another capture process. Preparing this build does not require any of
those hardware actions.

After the freeze investigation releases the watch, flash the profiling candidate
using the guarded procedure above, start one runtime capture with its matching
dictionary, launch FdF Time and leave its animation running for at least 60 seconds:

```sh
python3 tools/unhygienix_trace.py --duration 90 \
  --dict build-fdf-profile/pebbleos_loghash_dict.json \
  --output build-fdf-profile/fdf-time.log
python3 tools/unhygienix_display_profile.py build-fdf-profile/fdf-time.log \
  --label 'FdF Time' --session 1 --first-window 10 --last-window 80 \
  > build-fdf-profile/fdf-time-report.json
```

The sequence bounds above are examples: select the sequences actually captured
while FdF Time was animating, excluding launch transitions, menus and static/idle
time. Record the selected face, its settings, app version, launch/stop times and
Bluetooth connection/sync state separately. The report label is operator-supplied;
it does not identify the running app. Verify received UART bytes before claiming
that capture is live. Repeat the same workload with normal Bluetooth activity
only after obtaining an idle-connected baseline.

Every sixteen display updates emit three matched records:

- `T5FRAME`: update count, interval count, summed 32 kHz interval ticks (low/high
  words), minimum and maximum intervals. The first boot window has only fifteen
  intervals. Later windows include the interval from the preceding update.
- `T5DRAW`: summed and maximum transfer microseconds, interval count exceeding
  the exact 1/30-second budget, cumulative DMA failures, invalid-window flag and
  CPU frequency. Transfer covers conversion, row callbacks and DMA waits after
  acquiring the panel mutex; diagnostic logging is outside that duration.
- `T5PHASE`: app-render, compositor and flash-text-render call counts and summed
  elapsed microseconds derived from the existing profiler counters. Font timing
  is nested within app rendering; do not add it to rendering time.

`T5PEAK` computes rolling maxima on the watch before logging, counting every
update. It reports peak counts in half-open 250 ms and 1 second windows, the
shortest interval, and separate maxima for the latest sixteen-update window.
The cumulative maxima survive dropped UART records; they include startup and
menus. Use the local maxima for selected workload windows. Missing local peak
records make their reported maximum a lower bound. Rolling windows may start
before the selected sixteen-update window. A 128-entry history overflow flag
invalidates the rolling peaks; the ring costs 512 bytes only in this diagnostic.
The shortest interval is a burst bound and must not be presented as sustained FPS.

`T5CADENCE` is now a sparse anchor every sixteen updates: cumulative frame number,
raw start tick, transfer ticks, CPU frequency and DMA failures. Host monotonic
receive timestamps allow independent clock-span and cumulative-count checks;
these anchors do not reconstruct a per-frame timeline. UART buffering affects
receipt times, so use long spans for calibration. The redundant legacy `DRAW`
UART record is removed; its CP-visible counters remain. Per-update UART logging
was found to lose records and has been replaced by these sparse anchors and
on-device peak counters. Diagnostic logging adds overhead to subsequent intervals.

In the JSON report, use `device_peak_sessions` for peak FPS. Local rolling peaks
are `peak_250ms_fps` and `peak_1s_fps`; cumulative values have a `since_boot_` prefix.
Check `history_overflow` and `missing_peak_windows`. `sparse_anchor_sessions`
contains host calibration and counter gaps, not frame-rate burst measurements.

The older aggregate records remain useful for phase timings. The old sampled `RENDER` record is
replaced by `T5PHASE`; it no longer repeats stale last-render measurements when
only the compositor updates. The reporter pairs records by sequence, separates
observed counter restarts, rejects incomplete/invalid windows, and computes FPS
from all included intervals rather than one sampled interval per sixteen frames.
It reports phase means per call, transfer mean/maximum and interval mean/minimum/
maximum. These are elapsed timings including preemption, not exclusive CPU time,
per-frame percentiles or physical panel scan-rate measurements. Logging overhead
appears in subsequent frame intervals. Clock changes and counter/aggregate
anomalies invalidate the affected window. The 32-bit raw clock supports one wrap
between successive updates; gaps of approximately 37 hours or more are ambiguous.
The final partial window is not emitted.

This measures the bottleneck without changing it. To establish whether the
48 ms animation-service cadence applies to FdF Time, inspect the exact face's
source/package for Animation API versus custom app-timer usage; FPS alone cannot
prove the timer mechanism. No FdF package was present during preparation.

```sh
python3 -m unittest tools.tests.test_t5ai_display_profile -v
```

## KernelBG blocking and scheduling trace

`CONFIG_T5AI_BG_TRACE=y` enables a T5-only diagnostic. All other investigation
flags can remain disabled. Common source and standard priorities are unchanged.
Board-local compiler instrumentation observes `sched_block`, `sched_wake`,
`sched_tick` and `sched_switch_in`; observing the tick includes timeout wakeups.
Linker wrappers capture KernelBG's block wait queue/caller and mutex lock/unlock
object, caller, owner ID and acquisition LR. The scheduler hooks only update a
bounded 64-record RAM ring. No stacks, registers, allocation or UART in hooks.
A priority-3 worker drains the ring and samples per-thread scheduler CPU deltas
once per second. This extra worker and compiler hooks perturb timing.

`T5BG` state codes: ready=0, running=1, blocked=2, suspended=3, dead=4.
Operation codes: generic wait=1, mutex acquisition=2, mutex release=3.
`now`/`at` use the independent 32 kHz AON RTC. Segment durations >=100 ms
are retained for active callbacks and for ready intervals. Normal idle poll waits
are omitted from segment records. `lost` counts overwritten device ring records;
UART loss is separate. Each segment is printed in two rows keyed by sequence.
Callback changes are observed at the next instrumented scheduler/operation
boundary, not by rewriting the callback dispatcher. Running elapsed time includes
interrupts. Window ready/blocked/run totals include idle time since the previous
sample, and must not all be assigned to the callback named at the sample end.
Thread CPU deltas use kernel ticks. Owner ID/acquisition LR in segments are the
values at operation entry; live blocked mutex snapshots refresh the current owner.
The snapshot lists up to 32 live threads; IDs disambiguate truncated names.

A long ready interval with another thread accumulating CPU proves scheduler
starvation for that interval. A blocked mutex interval identifies the object and
owner; the owner's state/waitq helps follow the dependency. A long ready interval
inside operation 3 demonstrates preemption across mutex unlock, after releasing
the lock. A running interval alone does not distinguish callback work from ISR
occupancy. Missing records or uninstrumented IRQ/sleep paths limit attribution.

Save the exact ELF, dictionary, config and source before AP-only flashing. Decode:

```sh
python3 tools/unhygienix_bg_trace.py CAPTURE/trace.log \
  --elf CANDIDATE/pebbleos.elf > CAPTURE/bg-report.json
python3 -m unittest tools.tests.test_t5ai_bg_trace -v
```

Disabling the flag omits the translation unit, wrappers, compiler instrumentation
and no-LTO overrides. Existing diagnostic flags retain their independent gates.

## App execution during BG starvation

`CONFIG_T5AI_APP_TRACE=y` selects the BG trace worker and is incompatible with
`T5AI_INTERRUPT_PROFILE`'s outer SysTick wrapper. T5's native tick entry samples
PC and LR from a validated Armv8-M thread/PSP exception frame, at jittered 7–13 ms
intervals. LR is the interrupted register, not a stack walk or necessarily a
complete caller chain. Samples exclude handlers and intervals with SysTick
masked. Pending ticks can concentrate samples at interrupt-unlock boundaries;
use LR and source to interpret these. Sixteen-byte PC buckets can cross function
boundaries; the decoder resolves both ends. The table is bounded to 256 buckets,
four probes per sample. Five-second reports print only the twelve busiest
PC/LR/event buckets; `overflow` counts rejected samples, while omitted lesser
buckets and UART loss are separate. Tables use the board's existing reserved
PSRAM cache section and are initialized after App creation, preserving kernel
heap space. Sampling starts after that initialization.

A linker wrapper matches the App thread and its exact event queue. Processing
spans run from a successful receive until the next receive request, including
post-dispatch event-loop upkeep. Spans >=100 ms retain wall time, scheduler CPU
ticks, callback and queued count. A sixteen-record ring counts overwrites.
`receive_ms` includes queue wait plus scheduling delay, not processing. Event
254 denotes receive; 255 denotes startup/no observed event. Other event numbers
are `PebbleEventType` in `fw/kernel/events.h`. CPU ticks include interrupts.
No common source, task priorities or scheduling policy change. SysTick sampling,
compiler boundaries and worker logs perturb timing; this is a diagnostic build.

```sh
python3 tools/unhygienix_app_trace.py CAPTURE/trace.log \
  --elf CANDIDATE/pebbleos.elf > CAPTURE/app-report.json
python3 -m unittest tools.tests.test_t5ai_app_trace tools.tests.test_t5ai_bg_trace
```

The firmware ELF resolves OS PCs. Loaded watchface RAM addresses need the exact
matching watchface binary/debug symbols. Keep raw UART bytes, receive index,
dictionary, ELF, configuration, source and hashes with each candidate. Disabling
the flag restores the normal tick handler and removes the App queue wrapper and
translation unit; BG tracing retains its independent gate.

### Accelerometer-free comparison

`CONFIG_T5AI_ACCEL_DISABLED=y` is a default-off Unhygienix/QMI8658 diagnostic.
`accel_init()` writes CTRL7=0 once (also handles an AP-only reset leaving the
sensor active), then returns before creating the sensor worker or poll timer.
The stock service and APIs remain linked; reads report unavailable and no samples
or shake events are produced. Sampling/rotation/shake API calls cannot restart
acquisition because the driver remains uninitialized. A failed power-down write
is logged, while worker/timer omission still prevents sensor acquisition load.
Leave this flag off for normal firmware. No standard priorities change.

Keep App/BG trace flags and the exact watchface consistent with the enabled run.
Compare long event-processing spans, scheduled App ticks, BG READY intervals and
resolved PC/LR hotspots using each candidate's own ELF. A TicToc or Launcher run
cannot establish whether disabling the accelerometer changes FdF starvation.
The driver and diagnostic tests cover both flag states, including subscriptions
and peeks after disabled initialization.

## SDK compatibility draw cache and scheduler audit (2026-10-10)

`CONFIG_T5AI_SDK_COMPAT_CACHE` defaults on for this board. Its SoC-local linker
wrappers publish App/Worker legacy SDK flags into the existing unprivileged
read-only data region during process setup and invalidate them after cleanup.
Normal graphics compatibility queries avoid privilege transitions and repeated
process-context lookup. Unknown/uninitialized process flags retain the stock
syscall fallback. This changes neither rendering semantics nor task priorities.
Shared firmware source is unchanged; the board build preserves the process-manager
call boundary for linker wrapping.

`build/traces/sdk-compat-draw-20261010/` preserves the cache-only hardware sample:
223 observed FdF event-9 render/upkeep spans, median 185 ms (176–216 ms), compared
with the saved earlier 448 ms median. This is a large software-path improvement,
not a direct panel-FPS measurement or a starvation fix. The faster run overflowed
the original event-history ring; reboots and retained-sample selection limit it.
Both captures reported 240 MHz active CPU with QMI enabled.

The proposed completed-draw sleep checkpoint was removed and never flashed.
Original FreeRTOS and current native definitions both use App priority 2 and
KernelBG priority 1. Selector instrumentation in
`build/traces/scheduler-picks-20261010/` observed valid KernelBG selections during
Launcher activity and no selection of a lower-priority task while BG was runnable,
but startup OOM prevented a stable FdF comparison. This does not rule out every
scheduler or ARM exception defect. The common native kernel host suite passed all
19 cases.

Do not repeat the combined heavy App/BG diagnostic on the connected workload
without budgeting its static allocations against real peak kernel-heap demand.
That candidate repeatedly failed a 681-byte allocation with only 544–620 bytes
free. The recovery image at `build/traces/sdk-cache-clean-20261010/` disables both
heavy diagnostics and retains the SDK cache. Its internal static RAM is 96672
bytes versus 105824 bytes for the selector candidate, reclaiming 9152 bytes for
the heap aperture. Any follow-up scheduler probe should use small counters and
existing timer infrastructure rather than a dedicated diagnostic thread, stack,
PC table and full thread-history snapshots.


## Lightweight KernelBG selector trace (2026-10-10)

`CONFIG_T5AI_BG_PICK_TRACE` counts actual PendSV selector choices, reports the
peak observed consecutive KernelBG ready age and samples App/BG scheduled ticks
using an existing five-second timer. It adds 64 bytes of static internal RAM
compared with the recovered image, with no dedicated task or stack. Do not
combine it with the heavy App/BG tracers. Counts and ready/selector-gap peaks
are cumulative; a startup gap peak must not be attributed to a later face.

`build/traces/bg-pick-light-20261010/analysis.txt` records 200 seconds of FdF:
zero BG selections/runtime, 475095 runnable-BG choices all selecting higher
priority, and a still-growing observed ready-age peak of 197.734 seconds.
The selector remains active. This narrows the issue without ruling out every
ready-list or ARM-port defect; the other higher-priority tasks are aggregated.

An actual-source host reproduction also establishes a generic watchdog flaw:
KernelBG's waiting flag survives its transition from blocked to READY until it
runs, while the watchdog exempts that flag regardless of thread state. The
existing starvation recovery is consequently bypassed in this wait path.
The relevant changes are `864cd70aa` and `fb1f13e12`. Correct waiting/deadline
semantics and test READY starvation separately from idle waiting; this does
not itself explain expensive draws. No shared source was changed in this run.

The capture received fresh boot and runtime bytes, ended normally and left no
monitor running. The lightweight diagnostic remains installed with SDK cache
and QMI enabled. No OOM, assertion or second boot appeared in this bounded run.


## Watchdog recovery hardware check (2026-10-10)

`build/traces/wdt-recovery-effectiveness-20261010/` preserves the flashed image,
matching dictionary/source, raw UART bytes and completed 300-second capture.
The imported upstream grace-period fix and generic waiting-state correction
produced four KernelBG expiry/recovery cycles, with recovery reports 1.003–1.007
seconds after expiry. KernelBG selections resumed; no second boot/OOM/assertion
was observed. FdF was observed across two launches, separated by Launcher; the
reason for the intervening exit was not confirmed. Monitoring has ended.

The peak continuous READY episode was still 64.699 seconds during an active
communication callback. Settings compaction's global 60-second watchdog suspend
is a candidate explanation for delayed intervention, but this image does not
trace suspension callers. Do not claim a five-second bound, FPS improvement or
long-term qualification. Preserve the separate draw-performance investigation.
See `analysis.txt` for filtering, workload transitions and measurement limits.

## Watchdog suspension caller trace (2026-10-11)

The opt-in `CONFIG_T5AI_WDT_CAUSE_TRACE` diagnostic wraps watchdog suspend/resume
and explicit BG feed APIs locally in the T5AI build. It uses the existing selector
trace timer, adds 352 bytes of image static RAM, and changes no scheduling policy.
Control calls preserve RTC, caller LR, current thread, BG state and callback; feed
reports aggregate counts with only the latest context. Ring loss is reported.

The completed captures in `build/traces/wdt-cause-20261011/` attribute two
60-second requests to `settings_file_rewrite_filtered`, with explicit resumes
after 0.6215 and 0.6156875 seconds. These preceded FdF; no further suspension was
recorded during FdF. Seven watchdog recovery reports occurred; the peak recorded
READY age was 5.564 seconds. The previous 64.699-second episode did not reproduce,
so its cause remains open. The operator confirmed the face exit was caused by
syncing another watchface. See `analysis.md` for exact coverage and limitations.
Both captures have ended. Raw UART, exact flashed image, ELF, dictionary and
source snapshots are retained. Host tracer tests and firmware checks passed.

## Watchdog investigation disposition (2026-10-11)

The operator considers starvation recovery fixed enough for now, with no
recurrence of the prolonged stall during the latest trace. The generic waiting
state correction and upstream grace-period fix are retained. Further work on
the earlier, unattributed 64.699-second episode is deferred unless it recurs;
the trace does not prove settings compaction caused that episode.

`CONFIG_T5AI_WDT_CAUSE_TRACE` and `CONFIG_T5AI_BG_PICK_TRACE` are disabled in the
firmware build. Both remain default-off, opt-in diagnostics. The heavier App/BG
tracers and display profiler remain disabled. SDK compatibility caching remains
enabled with its existing removal TODO; expensive drawing is a separate issue.

The diagnostic-free image was built, sealed and AP-only flashed successfully.
Fresh UART logs show the matching build ID
`3a61b846c6a0ee93d7bc8758610f5dda4c5b808d` reaching “Ready for communication.”
ELF checks confirm the watchdog caller and selector trace wrappers are absent.
Static RAM is 96,704 bytes, 416 bytes below the traced image; raw flash image
is 1,606,944 bytes. Evidence: `build/validation/wdt-trace-disabled-20261011/`.

The operator subsequently reported a reboot during validation. Diagnostic removal
is therefore not stability qualification. A fresh raw/decoded UART capture was
started before an explicitly requested hardware reset, with the same flashed
build and diagnostics still disabled. Evidence is being collected in
`build/traces/reboot-repro-trace-off-20261011/`; the commanded boot is labelled
in `session.json` and must not be counted as a spontaneous reboot. This reopens
reboot investigation while retaining the watchdog recovery changes.

2026-10-11 reproduction update: three spontaneous reboots were captured after
one commanded reset with diagnostics disabled. Each follows unrecovered KernelBG
watchdog expiry and reports AP=112 (0x60 + Watchdog=16), CP=16. Recovery is not
reliable under this reproduction; the earlier accepted disposition is superseded
by this evidence. Root cause of failed recovery is still open. Frozen evidence:
`build/traces/reboot-repro-trace-off-20261011/three-reboots/analysis.txt`.

## BG recovery duration and flash priority scope (2026-10-11)

An expiry-only SoC probe reproduced watchdog resets while App base/effective
priority changed from 2/2 to 0/0. BG remained READY at 1/1 holding four mutexes;
it received 240 and 237 scheduled CPU ticks (1 ms each) in the next intervals.
This establishes task switching and effective throttling in the sampled episode,
rather than an inherited App priority defeating the recovery callback.

The system-task recovery service previously restored App priority after 300 ms
without checking whether BG had fed. It now rechecks at that interval and keeps
the same App throttled until a successful `system_task_watchdog_feed`, retaining
the watchdog's existing timeout and grace. A replacement App is left alone.
The check/restore decision is protected from a new watchdog recovery interrupting
it. This shared change belongs to the recovery service that owns the timer; no
SoC scheduler workaround or draw sleep is added.

Separately, the T5 flash client's temporary priority scope now restores its old
base priority only while its own boost is still installed. Snapshot/boost and
check/restore are atomic. A concurrent throttle or other priority change is
preserved. Nested mailbox transactions and normal priority restoration still
work. The race is host-reproduced but not separately attributed to hardware
resets; the hardware candidate contains both corrections.

Regression tests execute actual recovery and flash-priority helpers. They cover
delayed/failed feeds, repeated recovery, replacement Apps, nested flash boosts
and externally changed priorities. Twelve focused host tests and both native
kernel/watchdog suites pass. Exact flashed diagnostic candidate and raw traces:
`build/traces/wdt-progress-recovery-20261011/`; failing CPU probe evidence:
`build/traces/wdt-recovery-cpu-20261011/`. Keep stability qualification separate
from improved recovery, and keep the draw/SDK cache TODO open.


The final candidate failed hardware validation: build
`a6a8697cb47428676579b5930585b9fd24401a5d` reset AP=112/CP=16 despite App
staying at priority 0. BG CPU advanced 356 → 1027 → 1688 ticks during the same
communication callback. Saved execution locations include PFS/FTL flash reads;
these are resume contexts, not fault PCs or definitive handler identification.
The latest probe therefore also selects `T5AI_FLASH_PROFILE` (80 bytes of
counters), prints operation counts/max/last timing only on expiry, and wraps
`settings_blob_db_mark_all_dirty` to identify that full-sync operation without
changing shared source. No periodic worker is added. The marker is a hypothesis
check; the last settings log alone does not prove that operation caused reset.
The preliminary five-recovery/no-reboot interval does not establish stability.


Expanded flash/full-sync diagnostic image and trace:
`build/traces/wdt-flash-recovery-20261011/`. The capture starts before flashing;
exclude pre-flash traffic, and distinguish the commanded flash boot from later
boots using reset-source records. `T5REC io` op 0/1/2 denotes read/write/erase;
counts and maxima are cumulative and last fields may span adjacent transactions.
`T5REC flash settings=1` identifies active settings mark-all-dirty work, with
`settings_start` in uptime ticks. This probe remains enabled for reproduction;
the previous heavy tracers and display profiler remain disabled.


Capture finished at operator request (175,988 raw bytes flushed). See the
[settings full-sync watchdog investigation](settings_sync_watchdog.md) for the two
reproductions, verified CP binary findings, limitations and staged debugging
plan. No live UART capture remains; diagnostic firmware is still flashed.


## Settings sync phase trace (2026-10-11)

The AP phase diagnostic is sealed in
`build/traces/settings-sync-phases-20261011/`. It retains the pinned CP and
existing recovery candidates. It adds 480 bytes static RAM (total 97,216 bytes),
no task/heap allocation, and buffered timings dumped on expiry or file completion.
The linked settings call boundaries are verified in `settings-call-boundaries.txt`.
Thirteen focused tests pass; packaging and layout checks pass. See the
[stage definitions and limits](settings_sync_watchdog.md#ap-phase-trace-implementation--2026-10-11).
Completion-wait timing is an AP observation; CP protection/program splitting
is not implemented in this image. The 30-minute capture begins before flashing;
exclude pre-flash data and label the commanded boot before interpreting resets.


## Trace disabled for commit — 2026-10-11

The board defaults explicitly disable `T5AI_WDT_RECOVERY_TRACE` and
`T5AI_FLASH_PROFILE`. The local build override is also disabled. Diagnostic
sources and tests remain available behind the opt-in flag; settings linker
wrappers, diagnostic LTO exclusions, counters and timing hooks are excluded
when disabled. Earlier enabled-probe descriptions above are historical.
The capture has finished. The watch still has the previously flashed diagnostic
image until a subsequent flash; this configuration change does not update it.


## Software pixel plotting fast path — 2026-10-12

`CONFIG_T5AI_PIXEL_FAST_PATH` is an experimental, default-off, board-local
replacement for `graphics_private_plot_pixel`. It combines ordinary clipping
and row addressing, skips fully transparent pixels, and directly stores opaque
pixels. Partial alpha uses the stock Photoshop-derived blend table unchanged.
Circular row limits retain the stock signed conversion; negative/wrapped clip
geometry, wide coordinates and other bitmap formats fall back to the original.

The board CMake rule renames only the stock definition in its translation unit;
all other callers and exported symbols resolve to the local implementation.
LTO remains enabled. There are no added allocations, buffers, MPU changes,
clock changes or scheduler changes, and shared Pebble sources are unchanged.
The selected ARM implementation removes clipping, row-info and blend helper
calls on opaque pixels. This is instruction-path evidence, not an FPS result.

Configure a separate build with `-DCONFIG_T5AI_PIXEL_FAST_PATH=y` and
`-DCONFIG_T5AI_DISPLAY_PROFILE=y`. Build the matching control with only
`CONFIG_T5AI_PIXEL_FAST_PATH=n`. Keep SDK compatibility caching, CPU clock,
accelerometer, watchface settings, Bluetooth activity and resource inputs equal.
Use the display profiling workflow above; exclude boot/menu transitions and
report sustained frame intervals and rendering separately from transfer time.

`python3 -m unittest tools.tests.test_t5ai_pixel_fast` compiles the actual local
implementation against the unchanged stock pixel, clipping, row-addressing and
blend C bodies. It compares all 256-by-256 colour pairs at four alpha levels,
all 65,536 opacity inputs, six bitmap formats, clipping/fallback cases, signed
circular limits, random geometry and 260-by-260 framebuffers with row offsets
above 64 KiB. Undefined-behaviour/bounds sanitizers are enabled.

The matched profiling images and validation evidence are sealed under
`build/traces/pixel-fast-20261012/{baseline,candidate}/`. The candidate adds
256 bytes of FLASH; both linker maps report 97,440 bytes of RAM and 118,272
bytes of display PSRAM. The offline FdF Time comparison at 240 MHz measured:

| Pixel path | Complete windows / frames | FPS | Mean frame interval | Mean transfer |
| --- | ---: | ---: | ---: | ---: |
| Stock control | 15 / 240 | 6.057 | 165.085 ms | 21.658 ms |
| Fast path, before control | 19 / 304 | 6.547 | 152.745 ms | 21.353 ms |
| Fast path, after control/reboot | 15 / 240 | 6.526 | 153.240 ms | 21.418 ms |

Each capture lasted about 60.5 seconds with phone Bluetooth disabled. Every
complete valid 16-frame window was included; missing UART records were rejected
without interpolation. All included windows had the observed FdF signature
(`render_n=0`, `font_n=0`, `comp_n=16`), with no DMA failures. The candidate
improved sustained FPS by 7.7–8.1% in these short runs. Using all received
`T5FRAME` records, including windows lacking phase/draw records, gives
6.076 FPS stock versus 6.534/6.531 FPS candidate (about 7.5% higher).

The images differ only in `CONFIG_T5AI_PIXEL_FAST_PATH`; resources and other
configuration are identical. This measures frame cadence, not isolated CPU
render time: the phase probe does not bracket FdF's custom drawing path.
Transfer remains about 21–22 ms. These runs establish a modest workload-specific
gain, not a general FPS or long-term stability guarantee.

The first cable-interrupted control run is invalid and retained separately.
Connected exploratory runs contain phone full-sync activity, native UI changes,
background-task stalls and app throttling; their aggregate rates are not used
for qualification. The earlier background-stall problem remains unresolved.
One offline candidate log also records a watchdog recovery. A transient UART
busy error before the offline control flash wrote nothing; its successful retry
and build-ID verification are retained.

`comparison.json` records complete-window metrics, exclusions and frame-record
cross-checks; `analyze.py` reproduces it. Sealed image manifests validate every
listed artifact. The final watch image is the fast-path profiling candidate,
build ID `40210cb8ec0151309c61e0a7c9133fd34573ed5d`, verified from its last boot.
The source flag remains default-off pending broader watchface and connected-soak
coverage. Phone Bluetooth can be re-enabled after this comparison.


## FdF frame breakdown — 2026-10-12

The next measurement keeps the pixel fast path enabled and adds driver-local
cycle counters behind `CONFIG_T5AI_DISPLAY_PROFILE`. `T5XFER` reports logical
rows, physical panel rows, payload bytes, DMA submissions and selected frame
mode. `T5PREP` reports conversion, duplicated-row copies, cache cleaning,
next-row callbacks, register commands and CPU streaming. `T5DMA` reports polling,
submission, lock acquisition and submission-to-observed-completion durations.
They share the existing sixteen-frame sequence number. The reporter's
`--transfer` option requires every timing record; a lost stage rejects the
whole window rather than creating a partial breakdown.

Driver stages use DWT cycles converted at the window CPU clock. They include
preemption, and individual stage differences use unsigned 32-bit subtraction.
The frame timebase remains the independent 32 kHz AON RTC. DMA observation is
an upper bound from submission to CPU observation, overlapping preparation of
the next row; it is neither exact DMA bus time nor panel wire time and must not
be added to conversion/copy/polling. Selected mode plus actual submission counts
establish DMA use; failures remain separately reported.

Two offline captures used operator-confirmed, animating FdF Time with phone
Bluetooth disabled and CPU at 240 MHz. Evidence is under
`build/traces/frame-breakdown-20261012/`, with ELF, dictionary, config, map,
source snapshots, image hashes, guarded flash logs, verified boot IDs and raw
UART bytes/index. The 90.23-second App/BG sampler run received 77,984 bytes;
its 22 complete transfer windows measured 5.505 FPS and 26.329 ms display
update time. This heavier build perturbs timing and is for hotspot discovery.
The subsequent driver-only run disables both App/BG flags and uses the same
pixel fast path and detailed display counters.

The 60-second driver-only capture yielded 14 complete windows / 224 frames:

| Measurement | Per frame |
| --- | ---: |
| Frame interval | 155.536 ms / 6.429 FPS |
| Display update, total | 22.869 ms |
| Colour conversion and horizontal scaling | 9.267 ms |
| Duplicating vertically scaled rows | 6.501 ms |
| Cache clean | 0.371 ms |
| Next-row callbacks | 1.297 ms |
| Register commands | 0.031 ms |
| DMA submission | 0.735 ms |
| CPU polling for DMA | 1.072 ms |
| Panel mutex acquisition, outside update timer | 0.012 ms |
| Submission-to-observed DMA completion, overlapping preparation | 21.965 ms |

Every included frame sends 260 logical rows / 466 physical rows / 434,312 bytes,
using 260 DMA submissions. There are zero DMA failures and zero CPU-fallback
frames. The largest single observed polling span is 0.251 ms. Stage totals do
not exhaust the update timer: loop/control work, instrumentation and unmeasured
preemption remain. Compositor time is 6.137 ms per call and is outside display
update. All included phase records have `comp_n=16`, `render_n=0`, `font_n=0`.
Eleven incomplete transfer windows are explicitly rejected. Sparse hardware
anchors cross-check against host time at 0.99937 RTC/host ratio and 6.446 FPS.
The earlier lighter pixel experiment measured 6.53–6.55 FPS and about 21.4 ms
update time; do not treat the new counters or sampler as zero-overhead.

The App sampler retains 6,338 samples in received headers, with 80 bucket
insertion overflows. Only the twelve busiest buckets per five-second report
are printed; 3,954 retained bucket samples are available for symbolication.
Pixel plotting (1,283), `gcolor_blend` (900) and `gcolor_alpha_blend` (815) are
the dominant reported hotspots. Sixteen-byte buckets can straddle functions;
RAM watchface PCs remain unresolved without the matching app binary. These
counts are not exact function costs or a complete CPU percentage breakdown.

The 499 reported long `PEBBLE_RENDER_FINISHED_EVENT` processing spans have
median/mean wall duration 175/178.828 ms and App CPU duration 125/124.327 ms.
CPU ticks are 1 ms and include interrupts while App is scheduled. The span
includes render-finished dispatch, event-loop upkeep/drawing, and time preempted
before the next queue receive; it is not an isolated render function timer.
Only spans >=100 ms are retained. Queue receive itself is short in the sampled
windows. This supports CPU-heavy drawing as the main target, while roughly
54.5 ms per long event was not App execution in this heavier diagnostic build.
It does not assign that whole difference to scheduler overhead.

The measured priorities are partial-alpha pixel/blend work first, then row
conversion/duplication. Faster DMA clocking or replacing the polling loop alone
cannot recover the roughly 55 ms/frame required for 10 FPS. Interrupt completion
may still help background responsiveness, especially if DMA submissions are
batched. No new rendering algorithm, clocks, DMA configuration, scheduler
policy or common Pebble source was changed during this measurement.

Reproduce the reports with:

```sh
python3 tools/unhygienix_display_profile.py CAPTURE/fdf-offline.log --transfer
python3 tools/unhygienix_app_trace.py CAPTURE/fdf-offline.log --elf CAPTURE/pebbleos.elf
python3 -m unittest tools.tests.test_t5ai_display_profile tools.tests.test_t5ai_app_trace tools.tests.test_t5ai_bg_trace tools.tests.test_t5ai_pixel_fast tools.tests.test_t5ai_sdk_compat
```

Both diagnostic builds passed image/map validation and AP-only flashing; CP
was unchanged. Nine relevant host tests and Python/style checks passed. The
actual driver also compiled with display profiling and generic profiler disabled.
The final installed image is the driver-only profiling build,
`fec61e56318f9ec60c17ead01da5956d0cc8a6d5`, verified from UART boot output.
Heavy App/BG tracing is off; detailed display profiling and pixel fast path are
on. No hardware capture remains running. Phone Bluetooth can be re-enabled.


## Task CPU metrics without investigation tracing — 2026-10-14

`CONFIG_T5AI_TASK_CPU_METRICS=y` selects a SoC-local five-second reporter of
existing scheduler runtime counters. It defaults off and is mutually exclusive
with App/BG tracing. Disable all other T5AI TRACE/PROFILE options, generic
PROFILER and `T5AI_QMI8658_SELF_TEST` for CPU-only captures. This leaves normal
operational logging available. No scheduler hooks or PC sampling are added.

Each TASKCPU header identifies sequence, AON interval/frequency, task count and
omissions. Rows identify task IDs/names and scheduler milliseconds, including
zero-runtime tasks. Retain only complete declared task sets with no omissions,
stable app identity and CPU totals consistent with elapsed time. Exclude boot,
app switching and phone sync; ISR runtime is included in interrupted task time.
The 4096-byte reporter thread's own runtime is included and reported separately.

AP-only build `913642c4ab449d3dfe73dadd9b9d50928aa2c7a0` was flashed successfully.
MU-TH-UR T was operator-confirmed visible and left untouched after phone sync.
Twenty accepted windows (100.649 seconds) measured 86.42% IDLE / 13.58% busy:
App 6.02%, KernelMain 2.93%, QMI8658 2.72%, NewTimer 0.96%, TaskCPU 0.52%,
mailbox 0.41%, HCI IPC 0.014% and KernelBG 0.001%; other live tasks accumulated
zero scheduler milliseconds. Startup, App Fetch and initial face exit were
excluded. UART losses exclude some windows; retained duration is not contiguous.
The runtime clock log reports 240 MHz despite a 120 MHz configuration value;
clock configuration was unchanged. This is an instrumented AP idle measurement,
not an exclusive accelerometer pipeline cost or a matched comparison with FdF.
Evidence and reproduction: `build/traces/task-cpu-idle-20261014/README.md` and
`analysis.json`. Both bounded captures ended; no live UART monitor remains.
