# Fixed dim-display energy workload — 14 November 2026

This is an independent default-off measurement harness, not a change to normal
redraw policy or dim behavior. `CONFIG_T5AI_DISPLAY_ENERGY_TEST` requires native
QSPI/DMA profiling and retained-sleep support, excludes mixed-I/O qualification,
panel Sleep In and raw-zero brightness. Board calls it only after PSRAM power
and both range checks, before splash, UI/Bluetooth startup and `t5ai_sleep_start`.
No shared Pebble code changes. No CP firmware/settings/frequency changes.

## Workload and failure handling

Keep phone Bluetooth off during the bounded comparison and leave USB/UART wiring
unchanged: battery absent, FNB58 feeds only watch USB, no UART power lead. Signal
back-power remains unmeasured. Restore exact normal `faae34b9` after testing,
then allow the operator to re-enable phone Bluetooth. Startup/reconnect diagnosis
remains explicitly deferred; do not reset again to manufacture a power match.

Render constant byte 0x55 across the exclusive native 260 x 260 framebuffer and
request brightness zero through the unchanged native mapping, which clamps to
the existing visible dim floor. Neither raw-zero nor panel sleep is enabled.
Wait 1.5 seconds. Run two epochs of four seconds idle followed by 24 synchronous
redraws at 250 ms deadlines (six seconds). Each epoch has three full and 21
partial redraws of rows 17–32, all with identical pixels; no microphone, SD
workload, orientation or display off/on cycle is introduced. The initial full
redraw is excluded from the 48 measured redraws. Brightness is restored, then
normal splash/startup continues. Runtime is approximately 21.5 seconds, within
the existing 30-second startup task-watchdog suspension; the harness does not
feed, extend or disable the watchdog.

Native transfer completion and wire drain precede every completed-frame count.
A missing synchronous callback asserts before framebuffer reuse. Submission or
completion failure, missed frame deadline, an active/error DMA channel, software
owner, retained entry/fault, invalid channel snapshot, brightness restore failure
or incomplete final count invalidates the run. No retry/reset or periodic task.
Callback state is static in board PSRAM, and framebuffer storage has static
lifetime. Default normal configuration adds no harness code/state/timer.

## Matched comparison and time boundaries

Freeze control and trial from the same source/configuration with only
`T5AI_DMA_INHERIT_CLOCK_POLICY` differing. Reflash exact control for A/B/A rather
than comparing unrelated startup runs. Both measurements take place before
retained sleep is registered, so AP shallow WFI is available and retained entries
must be zero. This deliberately measures shallow idle and fixed redraw work,
not normal connected retained idle. Phone Bluetooth is disabled by the operator;
no active companion session participates in the benchmark.

Eight boundary markers record phase/edge, 32 kHz AON ticks, cumulative completed
redraws and status. Paired rows record actual DMA global, peripheral source,
software owners and retained entries/fault. Initial and final normal boot banners
and exact dictionaries establish the installed AP. Meter capture begins with
verified live samples before programming and covers the whole workload. UART
capture must receive fresh bytes. Preserve all failed attempts; stop on an
incomplete workload and restore the known-good AP.

Reference `artifacts/dma-energy-20261114/analyze.py` checks exact phase and final
counts, zero failures/owners/sleep entries, stable globals/source, bounded AON
durations, UART marker offset spread and sample coverage. Align meter host time
to AON using median UART-receipt offset, integrate nominal 100 Hz samples and
report +/-50/100 ms boundary sensitivity. This is a host-aligned estimate, not
external trigger calibration or a hard absolute timing-error bound. FNB58 emits
four samples per packet with inferred sample times; nominal sample integration
and USB latency limit precision. Report gross whole-watch USB joules per mixed
redraw workload, and optionally subtract same-run idle baseline as an estimate.
Do not describe this as isolated panel energy, pure DMA energy, ordinary
watchface idle or a demonstrated retained-idle saving. A/B/A drift and timing
sensitivity must accompany any effect-size claim.

## Validation and evidence

72 focused actual-source firmware tests pass with ASan/UBSan, including fixed
content/cadence/counts, early PSRAM failure, board ordering, busy completion,
submission/completion failure, deadlines, owners, retained entries and brightness
restoration. Five analysis tests check a known integral and reject repeated,
incomplete, delayed-marker or owner-invalid runs. The first sandbox attempt hit LeakSanitizer's
ptrace restriction; preserved host rerun passed without weakening sanitizers.

Frozen images, config/ELF/map/dictionaries, source snapshots, linked SRAM audit,
UART, meter CSV, analysis and final installed state belong under reference
`artifacts/dma-energy-20261114/`. See its RESULTS.md and result.json for actual
outcomes. No overnight, shutdown or new default gating is authorized by this
measurement harness.

## First timing rejection

Original control c4ae51f1 completed 48 redraws with global 3 and no owners or
retained entries. Its UART boundary offsets varied by 6.003 seconds: the local
lazy debug buffer retained partial PULSE frames until the next phase. The
analyzer rejected the run; its raw UART/meter records remain in `control-a/`.
The corrected harness explicitly calls existing `dbgserial_flush()` after both
marker rows at each boundary. This is measurement-local, outside frame loops;
common logging/serial code and normal runtime remain unchanged. Revalidate
marker spread before accepting any corrected electrical estimate.

## Fixed-workload DMA energy result — latest checkpoint, 14 November

A corrected control/trial/control comparison completed all 48 redraws per run,
with zero boundary owners, faults or retained entries and the same source word
0x83188491. Phone Bluetooth was off by operator confirmation; battery absent,
FNB58-only watch USB and no UART power lead remained unchanged. Measurement was
before UI/BT startup and retained registration, deliberately matching shallow
AP WFI rather than normal connected retained idle. See [method](display-energy.md).

| Run | DMA global | Idle USB mW | Redraw USB mW | Gross mJ / redraw |
| --- | --- | --- | --- | --- |
| Control A, af972d11 | 3 | 128.096 | 129.223 | 32.333 |
| Inheritance B, 2800bff0 | 1 | 127.135 | 128.341 | 32.112 |
| Exact control repeat C | 3 | 128.182 | 129.197 | 32.353 |

Relative to the controls' mean, trial was 1.005 mW lower idle (0.78%),
0.869 mW lower during the fixed redraw workload (0.67%), and 0.231 mJ lower
per completed scheduled redraw (0.71%). Control drift was +0.086 mW idle and
-0.026 mW redraw. This is a small observed whole-watch USB input reduction in
this bounded workload, not isolated panel/DMA power or qualified everyday savings.
Gross energy includes scheduled idle time. Idle-subtracted estimates were
0.289 / 0.308 / 0.287 mJ per redraw and do not show an incremental redraw-energy
improvement. UART marker spread was 0.57–0.61 ms; +/-100 ms alignment sensitivity
was evaluated. Absolute transport delay, meter accuracy and burst aliasing at
nominal 100 Hz remain uncalibrated; one trial boot does not establish a confidence
bound or release qualification.

The first control c4ae51f1 passed its workload but was rejected electrically
because lazy serial buffering delayed marker tails by up to six seconds. Its
capture remains in `control-a/`. The corrected default-off harness flushes
existing serial output only at phase boundaries. Shared logging code is intact.
72 actual-source firmware tests and five analysis tests pass. All frozen builds
pass resource/CRC/layout/heap/SRAM checks; normal runtime is unchanged apart
from its build ID. Artifact: `artifacts/dma-energy-20261114/`.

Exact normal faae34b9 was restored and its exact banner, reset sources 0/0
and 32 clean AP returns (all CP deltas zero) received. See `restore/` and the
final result. Benchmark, qualification and DMA inheritance remain
off in normal; normal dim/QMI WOM/storage/resources/bonds are preserved. No CP
partition or settings changes. Startup/BT diagnosis remains explicitly deferred.
Keep inheritance default-off pending repeated matched trials and normal-use
qualification. No shutdown or overnight test was performed.


## Cleanup and subsequent installed policy

Workload timing, full-frame interval and partial-row bounds now have named
constants; values, frame count, boundary flushing, error handling and callback
lifetimes are unchanged. The harness is disabled in the subsequent operator-
requested normal-use DMA inheritance candidate. Its source default remains off.
The historical normal-restoration checkpoint above describes the end of the
measurement; consult [current status](current-status.md) for the latest flash.

## Subsequent default change

DMA inheritance was later enabled by default at the operator's request and
decoupled from profiling. This measurement harness and profiling remain default-
off; historical control/trial configurations and measured limits above are
unchanged. Installed 4865f074 is unchanged by that source-default request.
