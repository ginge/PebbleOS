# Getafix DVT3: Waveshare T5-E1 hardware bring-up

`getafix@dvt3` is the local port for the Waveshare
[T5-E1-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75).
**The peripheral backend and a Tuya SDK bring-up application are implemented;
the PebbleOS kernel and watch applications are not yet running on this MCU.**
No physical board has been tested or flashed as part of this work.

The original DVT2-derived Kconfig files remain as a reference. `pbl configure`,
`pbl build`, and the `pbl` device runner reject that legacy DVT3/SiFli route,
including cached DVT3 builds. Never flash old `pebbleos.bin` baseline images to
this board. DVT and DVT2 are unchanged. DVT3 is not a shipping hardware identity
and is excluded from release matrices.

## Implemented hardware path

`boards/getafix/tuya/` is a standalone application for the official TuyaOpen
SDK, which supplies the T5AI/BK7258 startup, FreeRTOS, compiler, bootloader,
partition layout, QSPI/I2C HAL, and image packaging.

| Device | Implementation |
| --- | --- |
| CO5300 AMOLED | QSPI0, 80 MHz requested, reset P29, native 466 x 466, X offset 6; vendor initialization sequence |
| Framebuffer | Pebble ARGB2222 260 x 260 expanded to RGB565 in PSRAM; respects panel byte order |
| Transfer ownership | Waits for the vendor completion callback before reusing the buffer; after timeout it refuses reuse until reset |
| CST9217 | Vendor CST92xx driver on I2C0 (SCL P20, SDA P21), reset P42; polling, bounds checks, mirrored axes mapped to 260 x 260 |
| QMI8658 | Vendor driver at 0x6b on the shared bus; 4 g accelerometer at 125 Hz, gyroscope disabled after initialization; returns sensor-frame milligravity values |
| PCF85063 | Reads/writes 24-hour calendar at 0x51; rejects invalid/stopped clocks; transaction-safe calendar writes and daily alarm programming/clear; no wake IRQ yet |
| Buttons | P18 mapped to Back, P12 to Select, active low, 30 ms debounce with tick-wrap handling |
| Power | P19 held high to latch power; active-low amplifier P28 held disabled |
| Charger | Active-low CHRG on P30; charging indication only, not a battery percentage |
| Battery | P13 / ADC channel 15; 16-sample peak with explicit two-point battery-terminal calibration; no guessed percentage |
| Navigation | Pebble ButtonId and TouchEvent types; Back/Select keys plus optional vertical swipe Up/Down; bounded touch coordinates and wrap-safe gesture timing |
| OS primitives | Pebble mutex, recursive mutex, timeout, tick conversion, assertion and checked allocation APIs on vendor FreeRTOS; uses the vendor allocator pair |
| BLE controller adapter | TKL HCI command/ACL TX and copied event/ACL RX queue; framing checks and sticky failure on packet loss; compiled but not started or connected to Pebble's host |

The backend is a singleton, initialized once, with public operations called by
one application task. Its I2C transactions share a mutex. On initialization
failure the bring-up application stops and keeps the power latch asserted until
reset; it does not attempt to unwind partially initialized vendor drivers.

AMOLED brightness uses the CO5300 command, not the Getafix backlight driver.
The vendor callback maps 0% to a small nonzero value: this is **not** a sleep/off
API. Brightness starts at 20%. Runtime sleep/power optimization is still pending.
Display completion confirms buffer ownership release, not electrical success;
the panel must be checked on hardware.

## Runtime adapter contracts

The build stages the actual Pebble public headers, not local copies of their
interfaces. `os_adapter.c` uses the pinned application CPU FreeRTOS headers and
configuration. Its on-device startup self-test checks ordinary and recursive
mutex ownership, zero-timeout contention, and recursive unlock depth. The
self-test is linked but has not been executed on a physical board. These are
**privileged runtime primitives**, not an MPU/syscall or app-loading port.

RTC set/alarm transactions hold the shared I2C mutex for the whole operation.
If writing a calendar fails partway through, the clock stays stopped so readback
rejects the partial value; retry with a complete time. Alarms are daily matches,
not Pebble UTC deadline alarms, and do not yet wake the MCU. Retained time is
never automatically replaced at boot.

Input callbacks run synchronously in the application task and must not retain
the supplied touch-event pointer. A vertical swipe of at least 40 logical pixels,
with less than 30 horizontal pixels and duration at most one second, emits an
Up/Down press on liftoff and a release on the next sample. Touch is marked
non-navigational in this mode. Disable `swipe_buttons` when connecting Pebble's
touch navigation service to avoid duplicate navigation. The current demo logs
callbacks at DEBUG; they are not delivered to the Pebble event queue yet.

Battery calibration requires two raw peak readings paired with measured battery
terminal voltages, using the same sampling procedure at both points. Supply
these through `TuyaBatteryCalibration`. The demo reports uncalibrated ADC values;
no battery percentage, charging curve, or low-voltage shutdown is inferred.

`hci_port.c` requires exclusive Bluetooth host ownership. Its start function is
intentionally not called by the demo: do not initialize it alongside Tuya's
Bluetooth host. RX callbacks copy borrowed IPC buffers into an eight-packet
queue. Malformed frames, overflow, and TX failures fail the session closed;
a reset is required, with queues kept alive for late callbacks. The Pebble
NimBLE NPL/transport integration, bonding storage, pairing and reconnection are
still missing. Host tests use TAL/HCI test doubles, not a radio simulator.

## Reproducible build

The SDK sources are not vendored into PebbleOS. The build tool requires the
revisions in `boards/getafix/tuya/sdk.lock.json`:

- TuyaOpen: `e82840dc9b629cb77fa922fb04e3f820dc2e78b4`
- TuyaOpen-T5AI: `13379b63e07e78770fb4d0bffe36db2754658132`

Prepare an official TuyaOpen checkout at the pinned commit and its T5AI platform
at the specified revision. Install TuyaOpen's `pyproject.toml` dependencies in a
Python 3.12 virtual environment, following the SDK's setup instructions. The
vendor tools can download submodules and their required ARM compiler during
build; this requires network access. Do not substitute the PebbleOS compiler.

From the PebbleOS checkout:

```sh
python tools/build_tuya_hardware.py \
  --sdk /path/to/TuyaOpen \
  --python /path/to/tuya-venv/bin/python
```

The tool stages sources under `build/getafix_dvt3_hw`, checks SDK revision IDs,
and invokes the official build and packaging flow. It refuses to overwrite a
pre-existing output directory it does not own. `--out` can select another build
directory. Do not run two builds against the same Tuya SDK concurrently: the
vendor platform build tree is shared.

The vendor `dist/getafix_dvt3_hw_1.0.0/` output contains QIO, UA, and UG images
and debug files. **These are peripheral bring-up images, not PebbleOS firmware.**
No cloud credentials, pairing, or AI service initialization is used by this app;
the vendor SDK may still compile its default cloud/radio libraries.

The local validation SDK and Python environment used for this change are at
`/tmp/pebble-tuyaopen` and `/tmp/pebble-tuya-venv`. These are temporary, not
installed project dependencies.

## Hardware validation

Use the vendor's documented programming procedure only after selecting the
correct device and saving its current firmware/settings. Full QIO programming
can replace the bootloader and partition contents. The build command never
flashes, erases, resets, or opens a serial port.

The application logs that it is **not running the PebbleOS kernel**. It renders
red/green/blue/white quadrants with a black touch marker; Back/Select produce
bottom-edge indicators. Serial logs report accelerometer, valid retained RTC
time, buttons, touch, and charging state. The Tuya T5 default log port runs at
460800 baud; confirm the board's dual-serial-port assignment in the vendor wiki.
An unset RTC is reported as unavailable, not replaced with a fabricated time.

Verify colors, touch orientation/edges, button debounce, sensor axes at rest,
retained RTC time, and the CHRG signal before connecting any service layer.
Power sequencing and current consumption need measurement on the actual board.

## Tests

```sh
python -m unittest discover -s tools/tests -p 'test_tuya_hardware.py' -v
python -m unittest discover -s tools/tests -p 'test_getafix_boards.py' -v
```

The first suite compiles and runs the C core tests and checks build-directory
ownership and staging of the real Pebble headers. C tests cover pixel colors/byte order, buffer limits, scaling, touch
bounds, RTC invalid data/leap days, debounce, and tick wrap. Additional tests inject
failure at every RTC set/alarm bus operation (including a torn calendar write),
check touch liftoff/Up/Down transitions and calibration limits, and exercise HCI
framing, borrowed-buffer ownership, queue overflow, failed initialization and TX
failure. They do not simulate
QSPI DMA, the vendor drivers, or the real devices. The core was also run under
AddressSanitizer/UBSan (leak detection disabled because the sandbox's ptrace
prevents LeakSanitizer from working).

## Remaining PebbleOS integration

The bring-up backend is not connected to `src/fw/main.c` or Pebble's services.
Further work is required before this can act as a Pebble watch:

1. Choose and implement the kernel/RTOS integration. Tuya's application CPU runs
   its vendor FreeRTOS port; Pebble owns its own startup, interrupt handlers,
   scheduler hooks, MPU, syscalls, heaps, and app loading. Linking both unmodified
   will not provide a working OS. The pinned vendor build has `configENABLE_MPU=0`;
   do not load third-party Pebble apps into that privileged configuration.
2. Replace SiFli flash/OTP/boot metadata and filesystem assumptions with the
   Tuya partition and image model, including recovery and firmware update.
   The stock 8 MiB layout consumes all flash with boot/AP/CP/OTA/vendor data;
   there is no Pebble PFS partition. Do not repurpose OTA, RF, or Tuya data
   without a coordinated boot/update layout and a restore procedure.
3. Connect display completion and touch/button events to the Pebble compositor
   and input services. The four-button input adapter now supplies Up/Down swipes.
4. Connect the RTC and tick adapters to Pebble time services and the IMU to its service, including axis
   calibration, wake interrupts, and power management.
5. Measure battery calibration and implement fuel estimation, audio, and the
   Pebble BLE host/NPL binding over the HCI adapter, including pairing. Getafix PMIC, backlight, magnetometer, ambient light, and haptics
   must not be carried over as if present on the Waveshare board.
6. Test normal operation, suspend/resume, charging, flash endurance/recovery,
   and application compatibility on hardware before assigning a firmware ID or
   enabling release/OTA distribution.

## Source map and references

- `boards/getafix/tuya/src/hardware.c`: Tuya driver integration and ownership.
- `hardware_core.c`: transport-independent pixel, touch, calendar, debounce code.
- `rtc_port.c`, `input_port.c`, `battery_port.c`: portable runtime adapters.
- `os_adapter.c`: privileged Pebble libOS API implementation for the vendor RTOS.
- `hci_port.c`: controller packet transport; exclusive ownership required.
- `main.c`: standalone peripheral validation app.
- `tools/build_tuya_hardware.py`: SDK checks, staging, and build invocation.
- `soc/`, `src/fw/startup/`, `src/fw/linker/`, `lib/os/`, and `src/fw/kernel/`:
  existing Pebble hardware/RTOS integration to replace.
- `src/fw/board/`, `src/fw/drivers/`, and `src/fw/services/`: board, driver,
  compositor and service interfaces for the next integration stage.

Hardware configuration and controller behavior were checked against the pinned
[Tuya BSP](https://github.com/tuya/TuyaOpen/tree/e82840dc9b629cb77fa922fb04e3f820dc2e78b4/boards/T5AI/WAVESHARE_T5AI_TOUCH_AMOLED_1_75),
its CO5300/CST92xx drivers, the
[T5AI platform](https://github.com/tuya/TuyaOpen-T5AI/tree/13379b63e07e78770fb4d0bffe36db2754658132),
and [NXP's PCF85063A datasheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf).
