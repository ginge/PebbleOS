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
