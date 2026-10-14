# QMI8658 acquisition on Unhygienix

The default T5AI driver uses GPIO9 (QMI INT2) to wake a bounded acquisition
worker. Software shake and one-sample requests retain per-sample interrupts;
requests allowing batching use the sensor FIFO. The existing worker priority,
100 kHz bus, axis mapping, 4 g range and rounded sampling periods are retained.
Shared accelerometer service, kernel and I2C implementations are unchanged.

## Hardware evidence

The [Waveshare board schematic](https://files.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75/T5-E1-Touch-AMOLED-1.75_schematic.pdf)
connects QMI INT1 to GPIO7 and INT2 to GPIO9. Production I2C0 shares P20/P21
with the touch controller and RTC. The Tuya board reference initializes CTRL1
with 0x60 and reads little-endian XYZ values; the new driver retains this
qualified setting and enables INT2 with bit 4 (0x70). Watch register comparison
confirmed combined repeated-START reads match separate address-write/read calls:
WHO_AM_I, revision, CTRL1 = 05, 7c, 70. Read the ID registers individually before
auto-increment is enabled; a reset-time burst otherwise repeats WHO_AM_I.

[QST QMI8658C Rev A](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf),
sections 5.4, 5.10 and 8, specifies a 1536-byte FIFO, configurable for
16/32/64/128 frames. Accel-only frames contain six bytes, little-endian XYZ.
The count is in 16-bit words, despite the byte-count description in older
revisions. Watch FIFO count 0x0c with status 0x50 confirms four accel frames.
CTRL1 bit 2 routes the watermark to INT2. CTRL9 commands 0x04/0x05 reset/request
the FIFO; ACK is 0. Clearing FIFO_CTRL bit 7 resumes writes and deasserts the
watermark after draining. Read mode discards new conversions, so its duration
must stay below an ODR period. The T5AI HAL uses byte interrupts and blocks the
caller on a completion semaphore; it does not provide an I2C DMA path.

The output timestamp is a 24-bit conversion counter, not a clock. Watch revision
7c leaves this counter frozen in FIFO mode. FIFO batches therefore use host RTC
anchoring, like the other Pebble FIFO drivers; direct samples use counter deltas
and wrap handling. A frozen FIFO counter must never reject successive batches.

## Acquisition and recovery

The ISR masks the pin, coalesces a single pending acquisition and gives a
semaphore. A raw slot remains owned until service processing finishes. No bus
work occurs in the ISR or NewTimer callback. Combined register reads reduce
transfer completion wakeups. The FIFO uses a 16-frame hardware buffer, a maximum
four-frame watermark and a bounded six-frame host buffer to limit drain time at
100 kHz. The shared service determines the permitted batching interval; smaller
watermarks do not extend subscriber latency. Shake forces single-sample mode.

A mutex serializes complete hardware operations against reconfiguration.
Already acquired data is processed before changing configuration; generation
checks discard stale work. Active peek uses the cache without destructive FIFO
output reads. Inactive peek temporarily uses 125 Hz, waits for settling, then
restores standby and the requested ODR. All six original rounded rate choices
remain: 8, 16, 32, 48, 91 and 333 ms.

Malformed counts, overflow and I2C/command failures trigger bounded recovery.
The watchdog runs at max(100 ms, two watermark periods), skips acquisitions
when interrupts are progressing, and resets a persistently stuck stream after
three missed-edge passes. A delayed watermark may receive one catch-up pass per real edge/watchdog
pass after at least one conversion period since the last drain. A pin still high
immediately after resume is allowed to settle. Empty FIFO reads resume without
a reset; a stuck-high pin cannot create an immediate work loop. Failed
standby writes also get bounded retries; successful standby recovery stops the
timer. Command status polling has fixed attempt limits and sleeps through I2C
completion. The complete CTRL9R handshake, including ACK-low, finishes before FIFO data
is consumed or read mode is cleared. Reset uses the normal ODR before selecting
a low-power ODR, because low-power settings slow command processing too.

## Build and verification

`CONFIG_T5AI_QMI8658_LEGACY=y` selects the retained periodic driver. It defaults
to `n`. `CONFIG_T5AI_QMI8658_PROFILE=y` enables five-second acquisition, transfer
and service-processing counters; it defaults to `n`. The explicit diagnostic
CMake override `-DT5AI_QMI8658_SELF_TEST=ON` requires profiling and non-legacy
acquisition. The current diagnostic compares a status precheck against an IRQ-proven
watermark, exercises FIFO batch/rate combinations, then restores the original
configuration. It skips the standby phase used in the earlier failed experiment. Keep it OFF
for normal firmware.

Focused verification:

```
python3 -m unittest tools.tests.test_t5ai_qmi8658 \
  tools.tests.test_t5ai_qmi8658_fifo tools.tests.test_t5ai_i2c
```

The tests compile actual driver C with modeled I2C and scheduler operations,
strict warnings and address/undefined-behavior sanitizers. Coverage includes
all rate selections, axis mapping/units, timestamp wrap and frozen FIFO counters,
shake filtering, queue pressure, stale callbacks, reconfiguration, peek,
overflow/malformed counts, command timeouts and bounded watchdog recovery.

Hardware evidence and image/source hashes are retained under
`build/traces/qmi-fifo-20261012/`. Failed experiments remain there too; the first
FIFO experiments exposed frozen timestamp filtering and are not successful
FIFO validation. Matched performance captures use FdF Time animating with phone
Bluetooth off. Only complete timing/profile windows are included in summaries.
Transfer elapsed time includes semaphore blocking and preemption and must not
be described as CPU busy-wait time. Worker CPU accounting does not include all
byte ISR time attributed to other threads. GPIO acquisition counts and transfer
completion counts do not establish the total hardware interrupt rate. Service
processing counts in the automatic diagnostic do not prove an app subscriber
received samples. Instrumented results are not a production utilization baseline.

## Validation status

The refactor remains experimental. On 2026-10-12 the operator reported a frozen
screen followed by a spontaneous reboot with the automatic diagnostic image
`5479b0f459bcac6cef98f4bc95362f22f3190454`. USB was not disconnected for this
failure. The trace contains no explanatory assertion or fault; the last test
phase was standby/peek. CPU and frame log activity does not establish that the
visible screen was functioning. The preserved legacy image was restored.

The 125 Hz FIFO experiment also reported nine dropped samples and a recovery
in one window. The conservative command-start-to-resume measurement reached
78.156 ms, including any preliminary ACK cleanup and scheduling delay. This
exceeds an 8 ms period and does not establish lossless acquisition or acceptable
latency. Slower FIFO windows alone cannot qualify the complete path. Failure
evidence is in `selftest2/sequence-stable.*` and `FAILURE-stable.txt`; recovery
evidence is in `control/freeze-recovery.*` under the trace directory above.

The operator confirmed animation and motion recovered with the legacy image,
then also confirmed both worked with the normal interrupt image
`bd893a82ec29a79fbec45f3382e9c9cce6e1d2e0` with automatic self-test disabled.
The bounded follow-up capture is `paused-candidate/normal-after-freeze.*`.
This narrows the reproduction conditions but does not explain the spontaneous
reboot or qualify FIFO transitions. The watch was left on this normal image.
The legacy compile-time fallback remains available, and no commits were made.

## FIFO model investigation

The first host model made command ACK and watermark clearing instantaneous.
It therefore missed two asynchronous boundaries: CTRL9R completion before data
reads/resume, and a pin that remains high briefly after resume. Tests now delay
ACK completion and exercise both an old high level immediately after draining
and a fresh watermark while shared delivery holds the raw slot. A valid empty
FIFO must not cause reset/reconfiguration.

The handshake-only watch experiment (`handshake/stable.*`, image
`8269311785e7dc3fd12a4624359fdf4b7746e4fa`) completed and restored normal
settings without a further reset; the operator confirmed animation and motion.
Complete interior windows measured 114.56 samples/s with a two-frame watermark
and 115.11 samples/s with four, versus 121.11 samples/s after restoring direct
mode. Maximum command-start-to-resume spans were 8.281 and 9.062 ms. These
measurements exclude interrupt-to-request latency and do not prove an exact
number of lost FIFO conversions: the output counter is frozen in this mode.
The counter value `drops=0` counts host-discarded buffered frames only.

The same experiment requested 3 Hz but retained 11 Hz after a failed
configuration. Treating requested rate as achieved would have hidden this
failure. The follow-up changes reset ordering and logs failed configuration.
The `fastfifo/sequence.*` experiment deliberately retains failure evidence: an
eager high-pin catch-up performed empty reads and repeatedly reset the stream,
suppressing delivery during those phases. This is not a successful comparison
of the status-read optimization. The corrected settling experiment is saved
separately under `settle/`.

Stage timings use the always-on counter at 32 kHz. Instrumentation adds RAM
counters and two five-second diagnostic records; it adds no per-byte logs or
extra sensor reads. CPU counters and transfer elapsed spans remain separate.
`analyze_fifo.py` in the trace directory excludes partial/mixed phase windows
and boot boundaries; summary JSON files preserve the included rows.


### Shake interruption during settling experiment

The operator reported accelerometer behavior stopping during `settle/sequence.*`
and identified the affected action as wrist/shake. This diagnostic explicitly
turned software shake off while changing rates directly through driver APIs.
The experiment therefore cannot validate normal shake/wake behavior or app
subscription semantics. Acquisition and driver-to-service calls continued, but
those counters do not prove subscriber callbacks or wake events. The experiment
was stopped by restoring the previously confirmed normal interrupt image
`bd893a82ec29a79fbec45f3382e9c9cce6e1d2e0` from `paused-candidate/`.
`motion-recovery.*` verifies received serial bytes and `shake=1` with roughly
606 acquired/processed samples per five seconds; operator confirmation of the
wake action was subsequently confirmed working by the operator.

Interior settling windows measured 115.47 samples/s at watermark two and
116.05 samples/s at watermark four for the nominal 125 Hz request. The latter
used 29.01 acquisition passes/s and 2.31% worker CPU in this instrumented run;
maximum request-to-resume elapsed span was 9.031 ms. Direct mode before the
experiment measured 121.01 samples/s over two windows. FIFO still has a
throughput deficit and is not yet a qualified replacement at this rate.
There were no reported I2C errors, host FIFO discards, or recoveries in those
windows. Frozen FIFO timestamps prevent using zero counter gaps as proof of
lossless acquisition. The slowest-rate fix was not reached in this capture.

Future hardware experiments must preserve active shake/wake and subscriber
configuration, or explicitly arrange the temporary feature change with the
operator. Automatic rate-changing self-tests are unsuitable for a normal-use
image; keep `T5AI_QMI8658_SELF_TEST=OFF`.


## DMA and controller FIFO investigation

The BK7258 has DMA controllers, but this does not establish an I2C DMA path.
[Beken BK7258 datasheet DS-BK7258-E12 V2.1](https://docs.riselink.ai/spec/BK7258/BK7258%C2%A0Datasheet.pdf),
section 4.12 (page 68), specifies 16-byte TX/RX FIFOs for I2C and makes no DMA
claim there. Section 4.16 (page 70) lists DMA peripherals without I2C. The
[newer Beken I2C guide](https://docs.riselink.ai/arminodoc/bk_avdk_smp/ap_doc/bk7258/en/v3.1.1/developer-guide/peripheral/bk_i2c.html)
does claim DMA support; this conflicts with the available detailed sources.
Treat that general guide as insufficient evidence for implementation.

The pinned TuyaOpen-T5AI revision is
`13379b63e07e78770fb4d0bffe36db2754658132`. Its AP DMA device enum,
BK7258 AP request-selector definitions, and `dma_ll_dev_to_req_mux()` have no
I2C request. The CP selector definitions also have no I2C request. The AP I2C
register layout has no DMA control, and its I2C driver uses FIFO interrupts and
completion semaphores. Relevant files under `third_party/hal_tuya/TuyaOpen-T5AI/`:

- `t5_os/ap/include/driver/hal/hal_dma_types.h`
- `t5_os/ap/middleware/soc/bk7258_ap/soc/dma_reg.h`
- `t5_os/ap/middleware/soc/bk7258_ap/hal/dma_ll.h`
- `t5_os/ap/middleware/soc/bk7258_ap/soc/i2c_struct.h`
- `t5_os/ap/middleware/soc/bk7258_ap/hal/i2c_ll.h`
- `t5_os/ap/middleware/soc/common/hal/i2c_hal.c`
- `t5_os/ap/middleware/driver/i2c/i2c_driver.c`

Conclusion: there is no established, supported peripheral-paced I2C DMA route
in this SDK. This is not proof that undocumented silicon support is impossible.
Do not invent a request-selector value or use unpaced memory DMA against the
I2C data register: that would not wait for arriving bytes or generate the I2C
ACK/STOP sequence. Moving a six- or twelve-byte chunk with software-triggered
DMA after each FIFO interrupt still needs CPU control and is unlikely to justify
setup, completion, cache/coherency and channel-ownership overhead.

### Supported alternative: I2C receive FIFO interrupts

The sensor FIFO and the I2C controller FIFO are separate. The existing Pebble
T5AI HAL uses read interrupt mode `3`, which the pinned low-level driver maps
to one byte. Modes `2`, `1`, and `0` correspond to 4, 8, and 12 received bytes.
The vendor ISR drains that many data-register entries and selects one-byte mode
for a short tail. It explicitly warns that ACK, START, STOP, interrupt mode and
interrupt clearing must be written together. Merely changing the threshold in
our existing one-byte ISR would lose bytes or stall transfers.

A T5AI-local prototype could select a threshold from remaining length, drain a
bounded maximum of twelve bytes per ISR, and retain a one-byte tail for final
termination. A conservative analytical schedule gives two data IRQs for the
a nine-byte read (8+1), and six for a 24-byte sensor-FIFO burst
(12+8+1+1+1+1), versus nine and twenty-four in byte mode. The current normal direct path reads thirteen bytes (status, counter, temperature and XYZ), which can use 12+1 data IRQs. Address/repeated-START IRQs,
short command/status transfers and completion wakeups remain. These counts are
estimates, not watch measurements. The analytical schedule covers lengths 1..512;
it does not verify silicon FIFO behavior or final ACK/STOP timing.

This can preserve shake and requested rates while reducing interrupt entries in
direct mode. It may also shorten actual transfer elapsed time by reducing CPU
service/clock-stretch pauses. It does not change the 100 kHz wire-clock floor.
For a 24-byte register read, address-write/register/address-read plus data require
27 nine-clock byte slots, or approximately 2.43 ms before START/STOP overhead.
The observed FIFO-data stage averaged about 2.80 ms. Eliminating all of that
stage's excess would save only about 0.37 ms in that run; the command/ACK/count/
resume stages remain. Therefore controller FIFO batching is a credible CPU
optimization but not evidence that nominal 125 Hz sensor FIFO becomes lossless.
Larger sensor batches reduce wakeups but lengthen the period during which QMI
read mode discards conversions. A 128-frame accel-only burst alone needs about
69.39 ms at 100 kHz, so maximum-capacity batching is unsuitable at 125 Hz.

### Hardware trial requirements

Keep the working image and shake/wake configuration intact until the prototype
passes a modeled peripheral FIFO test. Cover lengths around each threshold,
repeated START, short command/status reads, final termination without an extra
byte, NACK/timeout after a partial FIFO, bounded ISR loops and successful reuse
after abort. Exercise touch and RTC as well as QMI because they share I2C0.
Use a build flag for the controller optimization and compare otherwise identical
images. Preserve subscription settings and avoid the automatic rate-changing
self-test used by earlier FIFO experiments.

Count actual I2C IRQ entries separately from acquisition GPIO IRQs, measure ISR
CPU without double-counting interrupted-thread time, and retain elapsed bus
measurements separately. Validate direct samples and wake/shake first; only then
use a real subscriber with an explicitly permitted FIFO batch/latency. Compare
throughput, app callback latency, loss evidence and frame intervals. Exact FIFO
conversion loss is still not observable through this revision's frozen counter;
physical SCL/INT captures would help determine when the read-mode pause crosses
a conversion boundary. Keep 100 kHz: the board source already records touch
address-write timeouts at 400 kHz. SPI DMA would require verifying different
physical wiring; the populated watch's qualified acquisition route is I2C0.

Evidence and local source hashes are saved in
`build/traces/qmi-fifo-20261012/dma-investigation/`. No DMA route was enabled and
no new image was flashed during this investigation.


## Controller FIFO experiment implementation

`CONFIG_T5AI_I2C_RX_FIFO=y` enables the T5AI-local receive path. It selects
12/8/4/1-byte interrupt levels from remaining length, copies at most twelve
entries per interrupt, keeps the final byte separate and rejects inconsistent
levels before touching the destination. Writes retain their byte-driven path.
`CONFIG_T5AI_I2C_RX_FIFO_LIMIT4=y` caps receive bursts at four bytes for a second
comparison. Receive FIFO batching defaults on at the operator’s request; the
four-byte cap defaults off. These options do not enable DMA or change sensor
FIFO policy, I2C frequency, thread priorities or shared Pebble code.

`tools/tests/test_t5ai_i2c_fifo.py` compiles actual ISR code with only the MMIO
FIFO-pop expression intercepted. It verifies byte mode, twelve-byte mode and
four-byte mode over lengths 1..256, both raw and register reads, exact data and
canaries, repeated START, isolated final termination, spurious interrupts,
partial-transfer timeout/arbitration recovery, address NACK, malformed levels
and zero-length rejection. The focused six-test suite passes, with address/undefined-behavior sanitizers on the new FIFO model. This model does
not prove electrical timing or silicon behavior.

`CONFIG_T5AI_I2C_PROFILE=y` is a separate opt-in dependent on QMI profiling.
It reports actual handler entries, RX entries/bytes, hardware timeout/arbitration
flags and DWT cycles at the QMI five-second boundary. The cycle interval includes
the ISR body, MMIO stalls and any nested higher-priority interrupt; it excludes
exception entry/exit and the counters updated after the ending DWT read. It is
an inclusive handler-occupation measurement, not exclusive instruction CPU.
Scheduler worker CPU includes interrupts attributed to the running worker and
must not be added to this figure. Instrumentation overhead is present in all
comparison images and may change with entry count. No per-byte records are
emitted. No DWT counter reset is added.

The normal direct acquisition is one thirteen-byte read containing status,
counter, temperature and XYZ. At approximately 121 samples/s, the byte control
reports sixteen I2C IRQ entries per read; the twelve-byte candidate reports
five (three address/protocol entries, two data entries). This validates a real
interrupt-count reduction without disabling software shake. It does not test
the sensor FIFO's command/read/resume sequence or qualify lossless 125 Hz FIFO.

Images and correct serial dictionaries are sealed as `i2c-control/`,
`i2c-fifo/` and `i2c-fifo4/` in the trace directory. The operator confirmed FdF
Time animating and phone Bluetooth off. After the twelve-byte capture the
operator confirmed animation, shake/wake and touch all work. Comparison excludes
boot and twenty seconds of warmup, rejects incomplete display records, and
requires complete interior profile/frame windows. In particular, a candidate
startup frame window contains a 16.42-second maximum interval and overlaps
warmup; it is retained in the raw capture and excluded from steady-state FPS.
The capture does not establish the cause of that startup interval.


### Matched watch results

Four 130-second captures received 36,338 / 33,757 / 35,955 / 35,564 raw serial
bytes respectively. Complete interior acquisition windows cover 70.03 / 75.08 /
75.04 / 60.04 seconds. Profile counters report no QMI I2C failures, host discards
or recoveries in those windows. Shake remains enabled throughout. These are
normal direct-acquisition tests, not sensor-FIFO acquisition tests.

The primary optimization metric is accelerometer worker CPU time per second,
with CPU time per acquired sample as a throughput-normalized companion metric.
FPS is secondary context. Preserve sample throughput, latency and shake/wake
behavior while reducing worker CPU; fewer interrupts alone do not establish
success.

| Image | Accel worker CPU | Worker ms/s | Worker ms/sample | Acquisition CPU | I2C IRQ/s | Samples/s | FPS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| i2c-control | 2.288% | 22.88 | 0.1892 | 1.310% | 1934.64 | 120.93 | 6.722 |
| i2c-fifo | 2.419% | 24.19 | 0.2005 | 1.348% | 603.17 | 120.63 | 6.857 |
| i2c-fifo4 | 2.569% | 25.69 | 0.2123 | 1.502% | 847.14 | 121.02 | 6.835 |
| i2c-control-repeat | 2.159% | 21.59 | 0.1783 | 1.262% | 1937.13 | 121.09 | 6.858 |

Worker CPU is the whole acquisition thread's scheduler-accounted runtime.
Acquisition CPU is the subset bracketed by acquisition-pass instrumentation;
it must not be added to worker CPU. Both use millisecond scheduler accounting
and include ISR time charged while that thread runs; neither is exclusive
thread instruction time. I2C handler DWT occupation remains a separate metric
in the comparison JSON and must not be added to these totals. Service delivery
runs on shared work, whose exclusive CPU cost is not measured by this table.

Worker CPU increased to 2.419% with twelve-byte bursts and 2.569% with four-byte
bursts, versus 2.288% initially and 2.159% on the repeated byte control. CPU per
sample also increased. Thus this experiment did not meet the primary objective
of lowering acquisition-thread CPU usage, independently of the FPS results.

The twelve-byte limit reduced IRQ entries by approximately 69%, but inclusive
handler occupation increased from approximately 0.31% to 0.45%. The four-byte
limit reduced entries by approximately 56%, with occupation increasing to 0.53%.
The repeated byte-mode control reproduces the original lower handler cost and
matches candidate FPS. These tests do not demonstrate improved CPU utilization
or animation from controller FIFO batching. The operator subsequently requested
twelve-byte controller FIFO batching as the default despite this measured result.

QMI transfer elapsed share was approximately 20% in all cases; this includes
blocking bus time and scheduling, not CPU busy time. Handler measurements must
not be added to worker scheduler accounting. All images had identical profiling
except the burst selector, so the experiment includes instrumentation overhead;
ISR-entry overhead outside the measured body is omitted. Exact causes of the
higher burst handler cost (MMIO stalls, code execution, nested preemption) were
not isolated. Do not attribute it definitively to hardware without finer traces.

Direct conversion-counter gaps were 16, 48, 24 and 17 over the respective
interior windows; maximum driver processing age was 22, 28, 31 and 30 ms.
These figures neither show a throughput benefit nor prove lossless delivery.
Profile delivery counts identify service calls, not application callback counts.
The sensor FIFO still has its previously measured throughput deficit and frozen
counter limitation; this experiment does not resolve those.

Last flashed normal image: experimental receive FIFO off, four-byte limit off, I2C/QMI
profiling off and automatic self-test off. The display diagnostic configuration
and pixel fast path are retained. Image/source hashes and its smoke capture are
under `i2c-normal/`. No source changes were made in the Tuya reference tree or
shared Pebble code, and no commits were made.

Normal restoration flashed successfully: build `bcad5e1e28b14f07aba9e994cd74eba1f80ebe45`. The smoke capture received 9,594 serial bytes over 85.15 seconds, verified that build ID and recorded subsequent frame progress. It is a boot/smoke check, not a matched performance capture; acquisition counters are disabled. The operator confirmed animation, shake/wrist-to-wake and touch all work on the restored normal image.

The source default now enables `CONFIG_T5AI_I2C_RX_FIFO`, with
`CONFIG_T5AI_I2C_RX_FIFO_LIMIT4` disabled (twelve-byte maximum). This default-only
change has not been flashed; the last verified watch image remains the one above.
Existing build configurations with an explicit disabled setting must enable the
option to adopt the new default.
