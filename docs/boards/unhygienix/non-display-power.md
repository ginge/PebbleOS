# Non-display power continuation — 31 October 2026

A later increment starts from committed I2C HEAD `198385cb0`; its opt-in ordinary-
WFI timer experiment, recovered CP build environment and final device/capture
state are in [the timer continuation](tickless-wfi.md). That record supersedes
the source-head, installed-image and stopped-capture statements below.

Power work remains in progress. This record supersedes the old A5 installed-image
and retired display-transport assumptions in the power handoff. No electrical
saving or completed retained-sleep contract is established by the changes below.

## Baseline and reproducibility

Production HEAD is `2eacc9eaa9682f9e30e72abf1211b8675ecdef67`. The four existing
documentation edits were preserved. Production edits in this continuation are
limited to board Kconfig, the T5AI I2C driver, tests and board documentation.
Stock shared/common code, CP, storage geometry and display source are unchanged.

A receive-only host capture and one commanded DTR/RTS **warm** reset verified the
entry AP build `6b314eb53dbc97521f1ac7ca9250d99c51371e85`, encrypted bonded
reconnection, four-bit SD, the existing 32 MiB PFS (`new=0`) and 49 apps. That is
the later native-driver/PSRAM image, rather than A5 `901c6c58`. Its exact raw,
protected, ELF, map, resources and dictionary rollback inputs were copied and
checked against the archived native-migration hashes. CP was not written; its
last recorded hash is `1713dee17dd98cd8aea8114a438d24ed87112585c16c45960221c4b96f344efb`.
A fresh CP readback identity is still unavailable and must not be implied.

Operator-confirmed setup: **no cell fitted, USB power, Bluetooth connected,
dimmed Nadir watchface**. Numeric brightness and panel luminance are unverified;
settings, display content policy and resources were held fixed, with no commanded
UI workload during idle windows. ADC battery voltage on a board without a cell
is not a fitted-cell measurement. FNB58 measures USB input, including regulator
and charger circuitry, rather than battery-side sleep current. Meter resolution
is not accuracy. Nadir's periodic activity, radio state and thermal drift limit
small differences. See [meter limitations](tooling.md#fnb58-usb-power-measurement).

Evidence, raw UART/CSV, image/config snapshots, hashes, build and test logs:
`/home/baz/Documents/ChatGPT/pebble/artifacts/power-continuation-20261031/`.
The successful control build is frozen in `baseline-built/`; `i2c-on-built/`
contains its matched candidate. Existing build directories were not overwritten.
All experimental flashes select only the validated AP range; resource payloads
match the installed archive byte-for-byte. No CP, resources, PFS or bonds are
included in those writes. Frozen controls are deliberately restored unchanged.

The build is reproducible with CMake 3.31.6, ARM GCC 14.2.1, Python 3.12 and
**source-built picolibc** from the pinned submodule. The SDK prebuilt-library
attempt overflowed FLASH by 7,480 B; it is retained as a failed build, not a flash
candidate. Source-built picolibc reproduces the previous library selection and
its 32-bit time override. Configure example (put the SDK compiler, CMake, Ninja,
Meson and the recorded nanopb/protoc wrappers on PATH first):

```sh
cmake -S /home/baz/coredevices/pebbleos -B /tmp/unhygienix-power-build -G Ninja \
  -DBOARD=unhygienix -DPEBBLEOS_SDK_ROOT=/home/baz/pebbleos-sdk-0.1.8 \
  -DPython3_EXECUTABLE=/usr/bin/python3.12 \
  -DCONFIG_LIBC_PICOLIBC_FROM_SOURCE=y -DCONFIG_T5AI_I2C_IDLE_GATING=n
cmake --build /tmp/unhygienix-power-build -j6
```

Keep identical inputs for the on build, changing only `T5AI_I2C_IDLE_GATING=y`.
Run the map/protected-image/resource/log-hash/syscall checks, freeze inputs and
verify the exact ELF build ID from UART after each AP-only write. Allow at least
90 seconds from boot before each 120-second idle recording. Capture the existing
sparse UART stream alongside; do not enable extra logging between comparisons.
The installed AP identity is distinct from the identity of a newly built control.

## Wi-Fi CP build progress

CMake 3.31.6, the SHA-256-verified GCC 10.3-2021.10 archive, and isolated Python
prebuild dependencies are now available under `/tmp/unhygienix-power-tools` and
`/tmp/unhygienix-power-python`. The staged SDK is still outside the production
submodule. The CP-only Wi-Fi-on probe now passes configuration and partition/
security-header prebuild and reaches C compilation. It stops because the adapter
includes TuyaOpen `tuya_iot_config.h`, which requires the missing generated
`tuya_kconfig.h`. The adapter root and pinned TuyaOpen common include root were
supplied explicitly; this probe is not a recovered complete stock build environment.

Generated CP config/cache and the failure are retained in `cp-build-probe/` and
`cp-on-tuyaopen-headers.log`. Recover or regenerate the matching TuyaOpen
configuration/build-parameter/header set and verify it against archived effective
stock settings. Do not substitute the unrelated simulator header or create an
empty header to force compilation. No stock CP reproduction, off-variant link,
variant packaging or CP device qualification passed. CIF/VNET/lwIP dependencies,
Bluetooth-only PHY/calibration and verified variant-specific hook/manifest work
remain after this build-environment gate. No CP was changed or flashed.

## I2C local-clock candidate

`CONFIG_T5AI_I2C_IDLE_GATING` is **default off**. When explicitly selected, init
and a successfully drained release clear only the controller-local clock-gate
bypass, after disabling/clearing the AP NVIC interrupt. Enable restores bypass
before the controller/IRQ and retains the settle delay. The pinned
`i2c_ll_enable()` explicitly requires bypass to read ACK reliably; it stays
asserted throughout each transaction. A stuck STOP keeps bypass on and marks
recovery; ISR faults/cancellation still defer GPIO recovery to task context.
Late inactive interrupts return before reading controller status.

This is local automatic-gating qualification, **not** a system-source-clock or
BAKP power release. Those shared controls and CP votes are unchanged. Releasing
them requires an ownership contract rather than another unsynchronized shared
register update. Pin pulls, 100 kHz timing, FIFO limits and client policy remain.

Normal control: build `7c7d8a292457cb60338229e845a382b1a3329b77`, FLASH 1,638,072 B.
On candidate: build `1ce0c3b77ca8b691754fb4a6a86ad3889d14d687`, FLASH 1,638,136 B.
The candidate adds 64 B flash, no RAM; only 264 B of its normal AP FLASH slot
remain. Both normal images and a separate release/on image pass map, protected
packaging, resource, log-hash and syscall checks. The release/on image uses
1,629,232 B FLASH (9,168 B free), 88,768 B static RAM; it was not flashed. A final normal/default-off build of the edited source also passes all image
checks: build `dc7693cc24a2350667e5e9b8d216749aaf6abca1`, 1,638,072 B FLASH
and 97,824 B static RAM. It was not flashed. The installed off control is the
frozen entry-source control, not this final source build. The native DMA staging
reservation and all display configuration match.

The native sanitized regression covers delayed physical STOP, stuck STOP,
NACK/rejected kickoff, timeout in ISR, deferred recovery, repeated init/acquire/
release, cancellation and late IRQs. It also asserts unrelated source/domain/
route bits survive. The final combined driver/storage/RTC/sensor/mailbox/power regression
batch passes **37 tests**; Ruff passes for both edited Python test files. The separate power-startup fixture was stale after the
native API migration; it now checks both PSRAM ranges before splash/brightness,
with 3 focused power/I2C tests passing. Initial sandbox LeakSanitizer failures
were rerun on the authorized host; the real pre-existing startup fixture error
is retained in the entry log. These are host correctness results, not electrical
or physical fault-injection qualification.

The selected candidate's exact ID, existing SD/PFS/apps and bonded session were
verified on hardware, and the operator reported it running normally. An early
serial-output gap was initially suspected as a startup failure; the complete
trace subsequently established normal startup. Preserve the raw gap and later
trace, but do not label that observation a confirmed firmware failure. Physical
stuck-bus, sensor traffic/gesture latency and repeated fault recovery remain open.

## Measurement disposition

The matched off/on/off windows each returned 11,980 CRC-enabled samples. All
logger processes exited cleanly (0); USB logs contain no reported CRC errors.
The nominal sample interval is 10 ms; nominal integrated energy is not an
independent calibrated measurement.

| Local I2C bypass release | Mean current | Mean input power | Nominal window energy |
| --- | ---: | ---: | ---: |
| Off A | 30.011 mA | 149.619 mW | 4.97897 mWh |
| On | 30.072 mA | 149.921 mW | 4.98903 mWh |
| Off B, restored | 29.996 mA | 149.543 mW | 4.97646 mWh |

Mean voltage was 4.98540–4.98545 V. The meter's displayed **0.15 W** agrees with
these roughly **150 mW** readings; mV denotes voltage, not power. The on window
is 0.068 mA above the mean of its flanking off controls, with one-second mean
variability of 0.247–0.343 mA (sample standard deviation). This single sequence
with periodic watchface/radio activity and uncalibrated system uncertainty does
not prove a causal increase or any saving. **No saving demonstrated; retain
default off.** The exact off AP `7c7d8a292457cb60338229e845a382b1a3329b77` was
restored and verified with existing SD/PFS/apps and encrypted bonded reconnection.
Entry identified-idle observation was 29.916 mA / 149.145 mW from 11,980 samples;
it uses the older installed AP and is not substituted for the matched controls.

The continuous on/restore UART trace includes both commanded boots and retains
all received bytes, including framing gaps. No additional uncommanded boot,
fatal/assertion or bus-init error is seen in the bounded decoded trace; this is
not a liveness or soak proof. The earlier output-gap suspicion was resolved by
normal startup and the operator check, as recorded above.

These are idle-window observations. No counted app-transfer, UI, flash or
microphone workload was completed for an energy-per-workload comparison, and no
frequency/radio policy was changed. Do not infer task energy or battery life
from the average idle current. Matched completed-workload measurements remain a
gate before promoting a power policy, as do the device tests below.

## Non-display dependencies and concrete next actions

1. **Clock lifetimes:** retain the I2C experiment as opt-in until its physical
   timing/fault behavior and a repeatable benefit are established. Establish CP
   ownership before source-clock release. Runtime logs use the CP-owned UART;
   four-bit SD leaves AP UART0 pins unassigned, and no current caller acquires
   `enable_uart0_clock()`. Do not invent a releasable AP vote. UART FIFO-empty
   (bit 17) alone does not establish final shifter completion; qualify TX-finish
   (bit 5), drain/error cleanup and RX wake before gating. Keep bounded fault
   output. Compare debug attached/detached with back-power accounted for.
2. **Shared DMA/domain:** current native `drivers/dma/t5ai.c` centrally owns AP
   channels 0–3 and preserves CP channels. The old GDMA facade has been retired.
   IRQ ownership is implemented, with host channel-isolation tests; this does
   not establish cross-core BAKP/domain power ownership or physical mixed-DMA
   qualification. Do not gate the global controller/domain from MIC or SD.
3. **Wake/alarm, then tickless:** CP BK7258 declares one AON RTC unit, and its
   `low_pwr_core.c` registers `lv_rtc` with the CP alarm list/compare. AP must not
   overwrite that compare directly. Pinned PM commands `0x0f` (wake config) and
   `0x10` (asynchronous wake notification) provide a reference protocol, but the
   native PM client currently expects transaction responses, not a wake-event
   dispatcher. Implement/cache-qualify persistent config payload lifetime,
   acknowledgement vs asynchronous notification routing, cancellation/generation
   and timeout/quarantine behavior first; measure lead time and wake delivery.
   Alternatively audit an independent AP timer for ordinary-WFI tick suppression.
   Only then handle fractional AON ticks, low-word wrap, early/pending wake,
   elected-clock changes and exactly-once elapsed accounting. The nRF52 deadline
   pattern and SiFli blocker/accounting pattern are architectural references;
   their registers/RTOS calls are not interchangeable with BK7258.
4. **Retained sleep:** follow pinned `sys_pm_hal.c`: CP sleep state, final
   interrupt/DMA/SysTick-pending checks, wake routing save/restore, AP WFI state,
   sibling wake and peripheral/time restoration. Add native mailbox, flash/PFS,
   I2C, UART and DMA blockers with balanced failure/cancel paths. SysTick and
   the permanent application sleep veto remain in place. The one-second CP
   heartbeat must remain serviced. Qualify standby/latch separately from sleep;
   a DTR/RTS warm reset is not a cold-power or shutdown test.
5. **Sensors:** measure motion-backlight on/off IRQ counts and I2C cost first.
   Qualify fitted QMI8658 WOM or bounded FIFO batches against sensitivity,
   requested rates and gesture latency. Keep 100 kHz and current FIFO limits
   until shared touch/RTC/IMU fault/overflow checks pass. Touch reset-on-disable
   already exists; qualify independent gesture wake without changing display
   policy during non-display tests. Do not equate session filtering with sleep.
6. **Memory:** identify fitted parts and CP retention owners, then agree balanced
   votes/self-refresh and cache-coherent restoration for live AP PSRAM, CP heap
   and shared flash. No AP-only flash deep-power-down is safe by assumption.
   Verify PFS/SD durability and concurrency before any default transition.
7. **CPU/radio:** keep actual 240 MHz and current BLE policy for this comparison.
   UART confirmed accepted 60 ms/latency 4 idle and 15 ms/latency 0 app-message
   states. Compare fixed 120/240 MHz against counted identical completed work,
   elected clocks, accepted radio states and total energy before adding coarse
   votes/hysteresis. Wi-Fi-off still needs the separately verified CP build,
   Bluetooth PHY/calibration, variant-aware hooks/manifest and service tests.
8. **Qualification:** reconnect counts, actual USB cold boot/bond/face restore,
   sustained transfers, physical bus faults, concurrent MIC/SD/display, heap/
   stack/PFS integrity and overnight idle with periodic UI remain open. Startup
   reconnects are not 25 disconnect/reconnect cycles. Current tools cannot toggle
   the phone UI or remove the physical USB supply; operator participation is
   needed for those portions. No overnight qualification has been completed. All bounded UART/power captures
   from this continuation are stopped. The continuous on/restore UART recording
   received 10,467 B over 740.736 seconds; raw bytes and both decodes are retained.

The remaining items include unfinished implementation as well as ownership and
hardware gates; documenting them does not mark the non-display phases complete.
Continue in this order. The normal FLASH headroom is also a practical gate for
larger instrumentation/suspend code; assess the validated release size or a
board-local size solution before adding more normal-image features.

## Display stays last

No display source, brightness/sleep policy, QSPI gating or DMA completion change
was made. The old two-row experiment measured IRQ completion about 26 vs 20 ms
per update and remains evidence against blindly enabling that retired path.
The current native transport instead uses 16-row PSRAM staging, SPI/DMA completion
and centralized DMA IRQ ownership, so the old switch/default claims in historical
status entries do not describe it. Preserve the existing native path during
non-display measurement. Physical mixed MIC/SD/display ownership tests are still
required before any final-phase completion experiment. Compare energy as well as
latency at the actual batch size when that phase is reached; non-display work
has not yet reached its completion gate.
