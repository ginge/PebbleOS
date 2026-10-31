# SD local-clock experiment — 31 October 2026

This increment starts from `1d8f10eb6`. `CONFIG_T5AI_SD_IDLE_GATING` is an
Unhygienix/SD-PFS-only, **default-off qualification option**. It releases the
SD controller's local clock-gate bypass between successful operations. It does
not release the CP-managed SDIO source/domain vote or shared GDMA/BAKP power.
Display configuration, resources, clocks, transport and completion are unchanged.

## Lifetime contract

The SD mutex serializes acquisition and release. Acquisition restores bypass,
barriers/readback and settling before controller access or SDIO IRQ delivery.
Release follows CMD12 where required, final CMD13 card-ready, TX FIFO drain and
DMA termination. It additionally checks command-start clear and zero TX/RX DMA
residue with successful status. It masks/clears only SDIO interrupts, resets the
idle data/FIFO engine and clears bypass. Late inactive SDIO/DMA callbacks return
before SD MMIO. Invalid or repeated ready calls leave the idle state alone.

Uncertain command/DMA completion retains bypass and IRQ delivery. I/O failure
retains the clock, aborts and preserves existing fault quarantine; no automatic
retry of a potentially completed write is introduced. Failed initialization
before controller access avoids SD MMIO, and a successful later initialization
clears the previous initialization fault. Shared clocks, other DMA channels,
CP protocol, PFS geometry and the existing-volume guard are preserved.

Pinned register definitions are in `bk7258_ap/soc/sdio_struct.h` and
`sdio_ll_macro_def.h`; native stop/drain sequencing follows the existing driver.
These references and a software model do not prove automatic gating on silicon.
Microphone already releases its audio vote on shutdown; cross-core GDMA/domain
power ownership remains a prerequisite for further shared-domain gating.

## Host and image validation

Sixteen relevant tests pass across SD clock/PFS, microphone DMA, native object
lifecycle, I2C clock, board Kconfig and native shared-DMA isolation suites. The SD
model executes production C in one- and four-bit modes, checking busy polling,
three completion orders, reads/writes, late IRQ/callbacks against inaccessible
MMIO, pin failure/retry, uncertain command/DMA status, timeout/error quarantine
and preservation of other owners. Kconfig checks default-off, SD dependency and
absence from stock board configurations. Python lint/format, supported C format
checks and `git diff --check` pass.

An initially attempted wider suite retained one pre-existing display-fixture
error: `test_t5ai_dma_irq` extracts the retired `prv_pixel_stream_finish_dma`
function. That fixture was not changed during this non-display increment.
An initial stock-board assertion assumed the board-local symbol existed; the
corrected check verifies its absence. Both failed logs are preserved.

| Normal image | ELF build ID | FLASH | Static RAM | Free FLASH |
| --- | --- | ---: | ---: | ---: |
| Off control | `86d1dcbaaf59cc38df62e23e576d26fd485f27b1` | 1,638,072 B | 97,824 B | 328 B |
| On candidate | `d1a3d567c270ef860ef3e15b00121272cb2f4f7a` | 1,638,392 B | 97,824 B | 8 B |

Both pass map, protected-image/vector/CRC bounds, resource size, log-hash and
syscall checks. The sole effective config difference is SD gating; I2C gating,
tickless WFI and tick profiling stay off. Resource bytes match the prior installed
archive (`62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46`).
The candidate fills its physical slot through `0x2db000` exclusively and cannot
absorb further code without a validated size solution. No partition was enlarged.

Builds use GCC 14.2.1, CMake 3.31.6, Python 3.12 and pinned source-built picolibc
with the same nanopb/protoc wrappers as the preceding experiment. Put the SDK
`arm-none-eabi/bin` on PATH for Meson as well as CMake; the first configure failure
omitted it and is preserved. Configure the existing baseline options in
[the continuation](non-display-power.md#baseline-and-reproducibility), adding
`-DCONFIG_T5AI_SD_IDLE_GATING=n`; freeze outputs, change only that option to `y`
and rebuild. Build directory: `/tmp/unhygienix-sd-clock-20261031`.

Frozen raw/protected images, ELF/maps, configs, dictionaries, SHA-256 manifests,
UART, test/build logs and all failed attempts are under
`/home/baz/Documents/ChatGPT/pebble/artifacts/sd-idle-clock-20261031/`.

## Hardware qualification

The operator confirmed USB/no cell, dim Nadir and bonded Bluetooth. The debug
adapter and FNB58 temporarily disappeared from host enumeration; the old receiver
last received at 15:20 UTC, then received again after reconnection at 15:53 UTC.
This gap is retained, not counted as monitored idle. A fresh receive-only SD trace
received actual data following one explicit preflight Bluetooth reconnect.
The off control AP-only write completed and UART verifies its build ID, actual
240 MHz, four-bit SD, existing 32 MiB PFS (`new=0`) and encrypted bonded protocol
recovery. CP, resources, PFS and bonds were not selected for writes.

The matched off/on/off sequence is complete. Return attempt B was excluded whole
after a notification at 16:37:50 UTC; its first trace, power data and preliminary
idle value are retained but excluded from this accepted comparison. Each batch has 90 seconds quiet
settling, a 120-second connected-idle window, then five encrypted/bonded protocol
recoveries with the same 5-second user-Bluetooth-disabled / 40-second on holds
and 60-second tail. The operator confirmed dim Nadir throughout all windows and
Bluetooth recovered at each end. Actual CPU remains 240 MHz; CP selection,
resources, BLE scan policy, display content/settings and debug connection are
fixed. These are USB/no-cell input measurements, not battery-side current.

| SD idle-gating option | Idle mA | Idle mW | Idle J | J / completed reconnect | Workload s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off A | 29.718 | 148.467 | 17.817515 | 9.013737 | 291.244 |
| On | 29.646 | 148.127 | 17.775288 | 8.992238 | 292.169 |
| Off C, restored | 29.616 | 147.968 | 17.757686 | 8.989747 | 292.333 |

On minus mean flanking off controls is **-0.090250 mW**
at idle and **-0.009504 J/completion**.
On is 0.0609% below the mean idle controls and
0.1056% below the mean workload controls
(negative percentages mean higher input). Return-off drift is
-0.498533 mW (-0.3358%) at
idle and -0.2662% in gross J/completion.
Report actual elapsed time with energy; the whole workload includes advertising,
recovery, idle and its fixed tail. All three power loggers close cleanly (0).

The three accepted batches have exactly five measured disconnects, encrypted
recoveries and protocol sessions per batch; device-time review extends beyond
each measured tail. No notification/app transfer, app fault or reset is found in
the accepted windows. Known ATT handle-3 subscription errors remain in their
reviews; this is not an error-free-stack claim. Explicit post-window reconnects
are separately recorded and excluded from energy/count acceptance.

This single nominal 100 Hz USB ABA does **not establish a causal, repeatable or
hardware-qualified saving**. Meter calibration/systematic uncertainty, periodic
watchface/radio activity, thermal/time drift, supply variation and possible debug
back-power remain limitations. Actual automatic-gating state/release counts were
not independently probed. The selected configuration and functioning SD/PFS are
verified, not the electrical state of each block. **Keep default off.**

## Cold and longer qualification

The operator reported two all-power-removed (at least 10 seconds) checks with
automatic Nadir/Bluetooth restoration. Attempt A receives a fresh expected
`d1a3d567…` boot and bonded protocol recovery, but two USB reattachments lose the
SD/PFS startup lines. Attempt B receives post-check bonded recovery without a new
boot record. Both reports, initial zero-byte inspections, all later data and
attachment gaps are preserved. These are capture-limited checks, not confirmed
firmware failures, and **the complete captured cold-start contract remains open**.
A receive-only, independently powered UART setup that remains connected without
back-powering the watch is needed for a clean repeat. A phone reconnect or warm
AP write is not substituted for this gate.

Fifteen matched reconnect completions are measured across the three batches;
this AP increment has not completed a 25-consecutive-cycle qualification. The
prior CP-only 25-cycle result remains separate. Durable SD/PFS writes, physical
bus/DMA faults and recovery, mixed MIC/SD/display/CP operation, application/PFS
fault root causes and longer stability remain open. Overnight qualification is
explicitly deferred by the operator; no overnight soak ran or passed here.

The identical default-off AP control `86d1dcba…` is restored. CP, resources,
PFS and bonds were not selected for writes. Both bounded SD UART receivers and
all power captures are stopped after final review; no monitoring is left running.
See `power-comparison.json`, per-run `window-review.json`, the two `cold-sd-on-*`
directories and `hardware-phases.jsonl` in the evidence directory.

Next collect sparse SD gate/release/fallback counts in a matched release pair;
normal on-image headroom is only 8 B. Qualify controlled durable writes and physical
fault quarantine/recovery, then obtain a complete cold trace and the deferred
longer run. Do not infer gate activity from configuration alone.

After this local-clock increment, continue CP-owned UART/drain/RX and shared-domain
ownership, then the [wake/alarm contract](wake-alarm-contract.md). The permanent
application sleep veto, SysTick and CP heartbeat remain intact. Display remains
last; preserve the earlier evidence against blindly restoring the old DMA IRQ
completion path.
