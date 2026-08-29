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

### 10. Accelerometer

- Port QMI8658 through Pebble's accelerometer service.
- Expected I2C address: `0x6b`; verify on the module.
- Initial target settings: 4 g range and 125 Hz, then match Pebble subscription/rate semantics.
- Implement interrupt routing, FIFO handling, timestamps, power modes, and calibration.

### 11. Battery measurement and charging state

- Implement the T5AI ADC path for the board voltage divider.
- Recorded input: P13 / ADC channel 15; verify pinmux and attenuation.
- Preserve the reference strategy of multiple samples where justified; the current reference uses a 16-sample peak-style reading.
- Derive voltage and state-of-charge calibration from measurements once a device is available.
- Implement charge detection from P30 and report it through Pebble's battery service.

### 12. Bluetooth/NimBLE

- Keep Pebble's NimBLE host and Bluetooth services.
- Implement only the T5AI-specific controller/HCI transport and required clock/IRQ/shared-memory boundary.
- Ensure the vendor Bluetooth stack and Pebble NimBLE never own the controller simultaneously.
- Verify pairing, bonding persistence, reconnect, MTU, notifications, and low-power behavior when hardware arrives.

### 13. Audio and optional hardware

- Keep amplifier P28 disabled until the I2S/codec/speaker path is implemented safely.
- Implement audio only through Pebble's audio interfaces.
- Explicitly disable any unsupported peripheral rather than retaining a legacy DVT2 driver.

### 14. Image packaging and boot flow

- Determine the exact image header, load/XIP address, CRC/ECC protected-block format, signing requirements, and bootloader expectations.
- Add a reproducible packaging command to `pbl`/Waf rather than a manual binary patching step.
- Define recovery before enabling OTA or destructive flash tests.
- Produce a map-file check that rejects overlap with protected areas.

### 15. Validation possible without hardware

- Reach a clean native compile and link with no unresolved placeholder drivers.
- Inspect the ELF/map for vector placement, stack/heap, MPU sections, flash/RAM overflow, and accidental vendor runtime linkage.
- Add host tests for board selection, memory/layout calculations, protected-block translation, display conversion, touch mapping, and battery conversion where practical.
- Run DVT2 configure/build after shared-code changes.
- Keep warnings introduced by DVT3 at zero or document unavoidable toolchain warnings.

Current host baseline: 18 `test_tuya*.py` tests and 3 Getafix board tests pass.
The full DVT3 build reaches the final link with no unresolved flash or mailbox
symbols and no internal-RAM overflow.

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
