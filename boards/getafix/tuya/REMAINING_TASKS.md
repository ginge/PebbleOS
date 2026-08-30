# Getafix DVT3 native port: remaining tasks

This is the continuation handoff for the native PebbleOS port to the Waveshare T5-E1 Touch AMOLED 1.75 board. Continue on branch `tuya`.

The current output is not ready to flash. No physical device is available yet, so continue source, build, and host-side validation first.

## Port direction

- Use PebbleOS's patched FreeRTOS 8.2.1 ARM Cortex-M33 MPU/syscall port for production.
- Reuse Pebble startup, task model, MPU layout, services, app loader, filesystem, NimBLE integration, and driver interfaces wherever practical.
- Use T5AI/vendor reference code only for unavoidable silicon-specific register operations and initialization sequences.
- Do not bring in the Tuya RTOS or Tuya application framework.
- Keep `boards/getafix/tuya` as a standalone vendor/reference bring-up target; it must not silently become the production firmware path.
- Do not add no-op drivers merely to obtain a successful link. Missing hardware should be represented honestly in configuration or implemented behind the existing Pebble interface.

## Current native state

- `getafix@dvt3` has an isolated defconfig and does not inherit the DVT2 SiFli platform.
- The target selects the T5AI SoC, STAR-MC1 CPU, and Pebble FreeRTOS.
- Native configuration succeeds.
- A minimal CMSIS device header and the exact 64-entry IRQ table from the pinned BSP have been added.
- Startup, VTOR, SysTick, memory-map, MPU, board selection, and storage layout compile natively.
- UART0, retained boot state, watchdog, AON RTC, protected-flash translation,
  the Pebble flash backend, the native AP-to-CP mailbox flash transport, and
  native GPIO, EXTI, buttons, I2C0, and external PCF85063 wall-clock support
  are implemented.
- The 260x260 compositor framebuffer has a bounded 128 KiB PSRAM region; native
  PSRAM controller initialization is still required before it can be used safely.
- The native build compiles all sources and reaches the final link without a RAM overflow.
- The vendor bridge remains reference code and is not linked into production PebbleOS.
- The current unresolved symbols are in display/backlight, GPIO/buttons, I2C-backed
  sensors, battery/ADC, Bluetooth controller transport, manufacturing OTP,
  and legacy services for hardware DVT3 does not contain.
- Eighteen Tuya host tests and three Getafix board-selection tests pass.
- The latest mailbox/flash build log is `/tmp/dvt3-mailbox-build.log` if it still exists.

## Resume commands

```sh
cd /home/baz/coredevices/pebbleos
. .venv/bin/activate
python pbl configure --board getafix@dvt3
python pbl build
```

Fix the first real compiler or linker error, rebuild, and repeat. Do not hide failures with placeholder symbols unless the hardware capability is deliberately disabled in Kconfig.

## Remaining work, in recommended order

### 1. Audit the working tree

- Review every uncommitted change on `tuya` before editing.
- Check for incomplete experiments, accidental generated files, and changes that affect other boards.
- Preserve DVT2 behavior and shared Pebble abstractions.
- Do not commit unless explicitly requested.

### 2. Native UART and debug console — implemented, hardware validation pending

- Define the DVT3 debug UART through the existing Pebble console/serial interfaces.
- Implement T5AI pinmux, clocks, baud generation, TX, RX, IRQ handling, and error clearing.
- Start with a correct polled TX path if needed for early boot diagnostics.
- Add interrupt-driven RX through Pebble's existing serial plumbing.
- Use DMA only when it is a real implementation with correct ownership and cache handling.
- Remove remaining assumptions that `DBG_UART` is a legacy STM32/Getafix peripheral.

### 3. Native flash controller and storage — implemented, hardware validation pending

- Finish the T5AI flash controller implementation behind Pebble's flash interfaces.
- Verify erase sizes, write alignment, read behavior, XIP/cache coherency, timeouts, and error reporting against authoritative T5AI reference code.
- Verify the 34-byte physical / 32-byte logical protected-block CRC translation.
- Ensure erase and write operations cannot reach the bootloader or vendor calibration sectors.
- Review the experimental DVT3 flash layout before treating it as final.
- Keep OTA disabled until packaging, metadata, slotting, rollback, and recovery are defined and tested.

The production path now uses Pebble's flash interfaces and a small native BK7258
mailbox client for the authoritative CP flash server. It does not link the Tuya
RTOS or application framework. Before enabling destructive tests, verify the
mailbox register alias, shared-buffer cache coherency, CP availability, erase
geometry, and the experimental partition boundaries on hardware.

### 4. SoC bring-up boundary

- Complete reset entry, vector relocation, clock tree, cache/FPU setup, and core initialization using the Pebble startup path.
- Confirm `SystemCoreClock`, SysTick frequency, PendSV/SVC priorities, and FreeRTOS tick behavior.
- Confirm which T5AI core boots PebbleOS and keep unused cores quiescent.
- Validate MPU regions, privilege transitions, syscall wrappers, MSPLIM/PSPLIM, and fault handlers.
- Implement watchdog and low-power entry through existing Pebble services.
- Validate the native microsecond delay, hardware RNG, factory device ID, and
  runtime CPU statistics on target. Define a safe manufacturing OTP allocation.
- Avoid pulling vendor scheduler/runtime initialization into Pebble startup.

### 5. Remove legacy Getafix hardware assumptions

Audit common Getafix code for peripherals that DVT3 does not have. Select generic Pebble implementations or explicitly disable unsupported capabilities instead of emulating DVT2 hardware.

DVT3's isolated defconfig explicitly cancels the legacy SiFli peripheral
selections, including PMIC, haptics, ALS, microphone, magnetometer, legacy
touch/accelerometer, OTP-flash, PWM, and the AW9364E backlight controller. The
corresponding Pebble service APIs remain present because the shell and app ABI
consume them independently of hardware availability; their no-hardware
behavior must be handled at the service boundary rather than by omitting ABI
providers.

Known areas include PMIC, magnetometer, vibration/haptics, ambient-light assumptions, button topology, display transport, and charger detection. The generic provisional battery curve must be calibrated when hardware is available.

### 6. GPIO, EXTI, and buttons — implemented, hardware validation pending

Native T5AI GPIO input, pull, push-pull output, and software open-drain output
are implemented behind Pebble's GPIO interface. GPIO-backed rising, falling,
and emulated both-edge interrupts are also implemented. The provisional P18
Back and P12 Select inputs use a 40 ms FreeRTOS-timer debounce path; absent
Up/Down buttons are represented explicitly. Confirm the mapping, wake sources,
and safe boot pin states on hardware.

Board signals recorded from the reference design:

- Power latch: P19
- Amplifier enable: P28; keep disabled until audio is implemented
- Back/select button candidates: P18 and P12; confirm mapping
- Charge detect: P30

Verify all pins from the schematic/BSP before hardware testing.

### 7. I2C and RTC — implemented, hardware validation pending

- Native I2C0 uses SCL P20 / SDA P21 with controller timeouts, reset recovery,
  repeated-start reads, and Pebble locking semantics; validate on hardware.
- The PCF85063 at address `0x51` seeds Pebble wall-clock time after I2C0 starts
  and is updated by `rtc_set_time`; stopped, 12-hour, oscillator-stop, invalid
  BCD, and inconsistent calendar values are rejected without corrupting the
  retained AON-derived clock.
- The T5AI AON counter remains the monotonic/subsecond source so routine time
  reads do not block on I2C. Test coherent reads and time retention on hardware.
- The external RTC alarm output is not enabled because its MCU interrupt/wake
  routing has not been confirmed. Pebble alarm timing continues to use the AON
  counter without claiming hardware wake support.

### 8. Display — initial native path implemented, hardware validation pending

- Native QSPI0 and the pinned CO5300 initialization sequence are implemented
  for the exact module parameters: 466x466, X offset 6, reset P29, mode 0 at
  80 MHz. Validate the clock, pin drive, reset timing, and panel response.
- Pebble's 260x260 ARGB2222 contract is preserved. A bounded 932-byte scanline
  buffer converts to big-endian RGB565 and nearest-neighbor scales dirty rows
  at the display boundary, including software 180-degree rotation.
- The initial transport is deliberately synchronous and FIFO-driven; it avoids
  claiming DMA/cache ownership before those paths are proven. Add asynchronous
  DMA, transfer timeout recovery, optimized dirty-region batching, brightness
  policy, deeper sleep/wake, and teardown after hardware validation.
- Do not expose the vendor framebuffer or UI framework to Pebble applications.
- Pebble brightness control maps 0..100 onto the CO5300 `0x51` register;
  requests before initialization or during an update are cached for the next
  safe transport boundary. With no ALS fitted, the ambient-light driver
  explicitly reports unavailable/unknown rather than emulating a sensor.

### 9. Touch — initial native path implemented, hardware validation pending

- The CST9217/CST92xx controller is implemented at address `0x5a` using I2C0,
  P42 reset, validated report framing, and Pebble's touch service. Native
  466x466 coordinates are range-checked, mirrored to match the module, and
  mapped into Pebble's 260x260 space; service-level rotation remains intact.
- No interrupt pin is identified in the module BSP, so the initial driver uses
  bounded 20 ms polling offloaded to the system task. Confirm interrupt/wake
  routing before replacing polling or claiming low-power wake support.
- Validate edge behavior and touch-up reporting on hardware. Add controller
  gestures only if they preserve Pebble recognizer and button-substitution
  behavior.
- Keep classic Pebble button behavior stable for applications.

### 10. Accelerometer — initial native path implemented, hardware validation pending

- The QMI8658 is implemented on I2C0 at address `0x6b`, with WHO_AM_I `0x05`
  validation, 4 g scaling, Pebble sampling-rate negotiation, timestamps, axis
  rotation, direct peeks, and power-down while unsubscribed.
- Button debounce, touch polling, and accelerometer polling use Pebble's native
  high-priority timer service; DVT3 no longer depends on the deliberately
  disabled FreeRTOS software-timer daemon.
- No interrupt pin is identified in the module BSP, so the initial driver uses
  bounded system-task polling and delivers one sample at a time. Confirm the
  interrupt routing before adding FIFO batching, wake-on-motion, shake, or
  double-tap handling.
- Validate physical axis orientation and calibration on hardware.

### 11. Battery measurement and charging state — initial native path implemented

- The CP-owned T5AI SARADC is accessed through its native mailbox service.
  P13/channel 15 uses the reference 16-sample peak strategy and the module's
  measured two-point SAR calibration plus the schematic 2 MOhm/510 kOhm divider.
- Active-low P30 feeds Pebble's charging and cable-present state. The module has
  no separate standby/full signal, so a completed charge cannot currently be
  distinguished from unplugged state.
- Validate voltage against a meter across the usable battery range and refine
  calibration/state-of-charge curves if production units differ.

### 12. Bluetooth/NimBLE — NimBLE transport implemented, controller IPC pending

- Keep Pebble's NimBLE host and Bluetooth services.
- The T5AI NimBLE transport now validates and copies borrowed CP event/ACL
  buffers into NimBLE-owned storage, serializes outbound ACL mbufs, rejects ISO,
  and exposes a narrow controller-only IPC contract. DVT3 intentionally remains
  on the Bluetooth stub until that contract is backed by the native shared
  mailbox dispatcher; enabling it earlier would race the flash/SARADC clients.
- The physical CP mailbox now has a single native owner and transaction mutex;
  flash and SARADC no longer access or drain its FIFO registers directly.
- The transaction lock uses Pebble's lightweight mutex abstraction and does not
  depend on FreeRTOS's deliberately disabled queue-mutex feature.
- Logical-channel routing now prevents channel `0x11` flash/SARADC waits from
  consuming frames belonging to registered services such as MIPC `0x17`.
- The vendor-compatible MIPC ABI is now explicit and host-tested: fixed 32-bit
  shared pointers, list/config/handle/data sizes, channel `0x17` framing,
  command/ACK type bits, and the BSP's CRC-8 polynomial/coverage (including
  its deliberate exclusion of the receiver-mutated flags word).
- The two-stage Tuya MIPC/HCI state machine is implemented: physical delivery
  ACKs, MIPC object ACKs, application replies, command/ACL requests, callback
  registration, controller init/deinit, and controller-originated event/ACL
  polling. Separate shared objects preserve an in-flight host request when a
  controller event is interleaved. Keep the board stub selected until the CP
  firmware/clock startup assumptions and this protocol are validated on target.
- Validate cache ownership, polling latency, timeouts, and CP startup/clock
  requirements on target; replace polling with IRQ delivery once proven.
- Ensure the vendor Bluetooth stack and Pebble NimBLE never own the controller simultaneously.
- Verify pairing, bonding persistence, reconnect, MTU, notifications, and low-power behavior when hardware arrives.

### 13. Audio and optional hardware — safely disabled

- Native early board initialization asserts the active-high P19 power latch and
  drives active-low amplifier enable P28 inactive before other services start.
- Speaker, microphone, vibration, ambient light, pressure, temperature,
  magnetometer, and PMIC paths remain disabled for DVT3.
- DVT3 now uses an explicit absent vibration driver: the service/app ABI remains
  available, motor commands have no physical effect, calibration returns
  `E_INVALID_OPERATION`, and the console reports `Vibration unavailable`.
- Keep amplifier P28 disabled until the I2S/codec/speaker path is implemented safely.
- Implement audio only through Pebble's audio interfaces.
- Explicitly disable any unsupported peripheral rather than retaining a legacy DVT2 driver.

### 14. Image packaging and boot flow

- Determine the exact image header, load/XIP address, CRC/ECC protected-block format, signing requirements, and bootloader expectations.
- The pinned BK7258 packager's protected-block transform is implemented and
  host-tested: 32 logical bytes, `0xff` tail padding, then a big-endian Beken
  CRC-16 (polynomial `0x8005`, initial value `0xffffffff`). T5AI builds now
  reproducibly emit `pebbleos.t5ai-crc.bin` in addition to the raw binary.
- Do not flash that protected payload yet. The factory bootloader's accepted AP
  partition address/header, CP/AP bundle composition, signing policy, and
  recovery path are still unverified for the experimental Pebble layout.
- Define recovery before enabling OTA or destructive flash tests.
- A link assertion and mandatory pre-objcopy map check now require FLASH to be
  exactly experimental slot 0 (`0x02120000..0x02310000`) and reject any NOR
  VMA/load address outside it. A successful check stamps
  `build/pebbleos.image-layout.ok`; `.bin`/`.hex` generation depends on it.

### 15. Validation possible without hardware

- Reach a clean native compile and link with no unresolved placeholder drivers.
- Inspect the ELF/map for vector placement, stack/heap, MPU sections, flash/RAM overflow, and accidental vendor runtime linkage.
- Add host tests for board selection, memory/layout calculations, protected-block translation, display conversion, touch mapping, and battery conversion where practical.
- Run DVT2 configure/build after shared-code changes.
- Keep warnings introduced by DVT3 at zero or document unavoidable toolchain warnings.

Current host baseline: 29 `test_tuya*.py` tests and 3 Getafix board tests pass.
The current DVT3 build reaches the final link, where unresolved platform
integration symbols for disabled legacy services still prevent objcopy and
final artifact generation. Flash, mailbox, display/backlight, ambient-light
service, and vibration symbols are resolved. The remaining symbols cover the
temperature and ALS console commands, OTP identity, disabled voice UI, and
magnetometer rotation. There is no internal-RAM overflow.

### 16. Hardware validation when a device arrives

Proceed conservatively: inspect power rails and current limit first, preserve the factory image, identify recovery/boot pins, prove SRAM execution/debug access if possible, then test UART, clocks, flash reads, GPIO, I2C/RTC, display, touch, sensors, Bluetooth, sleep, watchdog, charging, and finally flash writes/packaging.

## Validation commands

```sh
cd /home/baz/coredevices/pebbleos
. .venv/bin/activate

python pbl configure --board getafix@dvt3
python pbl build

python -B -m unittest tools.tests.test_getafix_boards -v
python -B -m unittest discover -s tools/tests -p 'test_tuya*.py' -v

/tmp/pebble-tuya-venv/bin/ruff check \
  tools/waf/boards.py \
  tools/waf/kconfig.py \
  tools/tests/test_getafix_boards.py

git diff --check
git status --short --branch
```

For a DVT2 regression build:

```sh
python pbl configure --board getafix@dvt2
python pbl build
python pbl configure --board getafix@dvt3
```

Restore DVT3 configuration afterward so the working tree remains ready for this port.

## Reference material

Use the exact pinned Waveshare/Tuya BSP sources already downloaded or recorded by the port work. Prefer schematics, register headers, startup code, linker scripts, and component datasheets over copied application logic. Temporary reference checkouts may be under `/tmp`; recreate them from the documented pinned revisions if they have disappeared.

Before copying code, check its license and keep attribution where required. Translate vendor initialization into small T5AI hardware primitives called from Pebble-owned drivers.
