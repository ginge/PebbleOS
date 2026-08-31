# Getafix DVT3 native port: hardware validation and remaining tasks

This is the continuation handoff for the native PebbleOS port to the Waveshare T5-E1 Touch AMOLED 1.75 board. Continue on branch `tuya`.

The native target now configures, compiles, links, and packages on the host, but
the output is not ready to flash. No physical device is available yet. Preserve
the factory image and establish the boot/recovery procedure before attempting
to install the experimental payload.

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
- The 260x260 compositor framebuffer has a bounded 128 KiB PSRAM region. The
  pinned AP BSP leaves controller initialization to the boot stages (its AP
  `bk_psram_init()` is compiled out), so Pebble validates that boot handoff with
  complementary address-pattern passes over only the dedicated NOLOAD
  framebuffer before the compositor uses it; hardware validation is pending.
- The native build compiles and links without unresolved platform symbols or a
  RAM overflow, and emits ELF, map, raw binary, Intel HEX, protected-block
  binary, and image-layout validation artifacts.
- The vendor bridge remains reference code and is not linked into production PebbleOS.
- Display/backlight, GPIO/buttons, I2C/RTC, touch, accelerometer, battery/ADC,
  absent-hardware service behavior, manufacturing OTP fallback, and the
  NimBLE-to-controller IPC protocol all have native implementations or explicit
  safe disabled states. The Bluetooth controller IPC remains deliberately
  gated until its CP startup and shared-memory behavior are proven on hardware.
- Thirty-nine Tuya host tests and fourteen Getafix board-selection tests pass.
- Both `getafix@dvt3` and the DVT2 compatibility target configure and build.
  DVT2 retains only its pre-existing stored Golf app unused-variable warning.

## Resume commands

```sh
cd /home/baz/coredevices/pebbleos
. .venv/bin/activate
python pbl configure --board getafix@dvt3
python pbl build
```

Keep the clean build green while addressing the remaining host-auditable work.
Do not hide regressions with placeholder symbols; represent missing hardware
through an explicit disabled configuration or an honest service-boundary
implementation.

## Remaining work, in recommended order

The broad host-side port is complete. Sections 2 through 13 primarily record
implemented behavior and the corresponding on-target validation still needed.
The remaining immediately actionable host work is the boot-image/recovery
investigation in section 14. Do not enable destructive
flash testing, OTA, resource-bank writes, or the Bluetooth controller path
merely to advance the checklist.

### 1. Audit the working tree — complete; repeat before material changes

- The tracked tree was audited after the host port reached its clean-link
  baseline. No untracked source, tracked build output, editor backup, rejected
  patch, empty tracked file, or exact duplicate was present.
- The standalone `boards/getafix/tuya` application and kernel patch stack are
  intentional reference assets: build/check tools, host tests, and the board
  documentation still exercise them. They are not linked into native PebbleOS
  and must not be removed merely because the native path now builds.
- Generated Waf build/lock/cache state, Python/Ruff caches, and generated Python
  package metadata were removed. The project `.venv` was retained.
- Recheck the working tree before material changes, preserve DVT2 behavior and
  shared Pebble abstractions, and do not commit unless explicitly requested.

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
- Reset-time initialization now enables full CP10/CP11 access using the pinned
  BK7258 CMSIS sequence before any pre-scheduler C can execute compiler-emitted
  FP instructions. FreeRTOS retains ownership of lazy FP context preservation;
  cache and clock-tree state remain boot-stage assumptions pending hardware
  validation.
- Confirm `SystemCoreClock`, SysTick frequency, PendSV/SVC priorities, and FreeRTOS tick behavior.
- Reset initialization now masks IRQs, stops and clears inherited SysTick
  state, clears pending SysTick/PendSV exceptions, and enforces eight-byte
  exception-stack alignment before relocating VTOR. This prevents a boot-stage
  scheduler interrupt from reaching Pebble before FreeRTOS installs its 1 kHz
  tick and lowest-priority handlers; validate exception entry on hardware.
- CMSIS interrupt setup now uses logical priorities derived from
  `__NVIC_PRIO_BITS`, rather than passing FreeRTOS's raw left-aligned BASEPRI
  byte as though it were a CMSIS priority. On T5AI the syscall-safe boundary
  is therefore logical priority 5 of 7, with UART at 5 and GPIO/I2C at 6;
  validate nesting and FromISR calls under load on hardware.
- The raw 32 kHz AON oscillator is now kept private to the T5AI RTC driver and
  scaled to Pebble's 1 kHz `rtc_get_ticks()` contract. This prevents FreeRTOS
  from programming a pathological 32 kHz SysTick while retaining raw-counter
  precision for wall-clock seconds and milliseconds. Confirm oscillator
  accuracy and the resulting 1 kHz scheduler cadence on hardware.
- The pinned BK7258 120 MHz AP boot setting is now an explicit
  `T5AI_CORE_CLOCK_HZ` contract instead of an unexplained constant.
  Compile-time checks require it to divide exactly into the scheduler rate and
  produce a valid 24-bit SysTick reload. Pebble still does not reprogram or
  measure that boot-owned clock; confirm it against a hardware timebase.
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
- Kconfig now enforces that boundary: `BT_FW_NIMBLE` is unavailable on T5AI
  unless `T5AI_CP_HCI_VALIDATED` is deliberately enabled. DVT3 keeps that gate
  off, so the SoC default cannot accidentally replace the controller stub.
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
- Ambient-light and temperature interfaces also have explicit absent drivers.
  Their console commands report the unavailable hardware instead of presenting
  zero as a sensor measurement; an unavailable temperature read returns
  `INT32_MIN`.
- OTP storage remains unmapped on DVT3. Its explicit unprovisioned driver
  reports every slot unlocked, returns no identity data, and rejects writes;
  the manufacturing layer consequently uses its existing dummy identity.
- Orientation changes no longer call the MMC5603NJ-specific rotation hook when
  `CONFIG_MAG` is disabled. Display, buttons, accelerometer, and touch still
  rotate normally without manufacturing a placeholder magnetometer.
- The watch-side voice reminder creator is omitted from the launcher when
  `CONFIG_MIC` is disabled, and its dictation workflow is not compiled. This
  does not disable receipt or display of timeline reminders.
- Keep amplifier P28 disabled until the I2S/codec/speaker path is implemented safely.
- Implement audio only through Pebble's audio interfaces.
- Explicitly disable any unsupported peripheral rather than retaining a legacy DVT2 driver.

### 14. Image packaging and boot flow — pinned host format audited; recovery pending

- The pinned `tuya_app` table places 68 KiB primary bootloader, 68 KiB Tuya
  BL2, and 1088 KiB CP application partitions before the AP. The resulting AP
  physical offset is `0x132000`; 34:32 translation maps it to logical offset
  `0x120000` and XIP address `0x02120000`. Its 3808 KiB physical allocation is
  exactly `0x380000` logical bytes. Host tests enforce these relationships.
- The official outputs are not interchangeable with Pebble's artifact: QIO is
  a complete physical-flash image, UA concatenates Tuya BL2 + CP + AP, and UG
  adds a 64-byte compressed-OTA header. `pebbleos.t5ai-crc.bin` is deliberately
  an AP-only protected payload with no QIO/UA/UG header and cannot boot or
  update a factory device by itself.
- The pinned BK7258 packager's protected-block transform is implemented and
  host-tested: 32 logical bytes, `0xff` tail padding, then a big-endian Beken
  CRC-16 (polynomial `0x8005`, initial value `0xffffffff`). T5AI builds now
  reproducibly emit `pebbleos.t5ai-crc.bin` in addition to the raw binary. The
  packager now rejects a raw image larger than the `0x1f0000` experimental slot
  and verifies that the slot remains inside the pinned factory AP partition.
- The pinned SDK selects its normal bootloader and its packaging path contains
  no signing step for this configuration. That does not prove the factory
  device's OTP/fuse policy or authorize replacing its AP payload.
- Do not flash the protected payload yet. The factory bootloader's installed
  version, AP/CP compatibility requirements, signing/fuse policy, programming
  transport, boot-selection behavior, and recovery path remain unverified on
  the actual device.
- DVT3 now has an explicit empty runner override, while DVT/DVT2 retain
  `sftool`. Both the board manifest and the CLI therefore fail closed for
  `pbl flash`, including an attempted explicit runner override. The same CLI
  gate rejects T5AI reset/run/erase/debug-server runner operations and direct
  resource/recovery imaging. Re-enable a runner only together with a reviewed
  image container and recovery procedure.
- The firmware-side boundary also fails closed on T5AI. Firmware-update start
  requests are cancelled before entering the update runlevel, and Put Bytes
  rejects firmware, recovery, and system-resource objects while retaining
  watch-app, app-resource, and file transfers. A stale PRF-update boot bit is
  cleared without copying flash. Configuration rejects the PRF variant and the
  bundle command rejects legacy Pebble bundles because neither represents a
  validated T5AI install or recovery format.
- DVT3 also excludes the legacy Pulse flash-imaging protocol and the BulkIO
  external-flash domain from its firmware image. This closes raw UART console
  erase/write paths that would otherwise bypass the host runner and OTA gates;
  DVT2 retains both legacy handlers, and non-flash diagnostics remain present.
- The unrestricted legacy serial/Pulse command prompt is disabled for T5AI as
  well. It exposed raw flash erase/fill/stress, arbitrary boot-bit mutation,
  PRF reboot, and factory-reset commands. UART logging and the non-prompt Pulse
  transports remain compiled; DVT/DVT2 retain their existing prompt behavior.
- DVT3 excludes the Pulse BulkIO raw-memory domain too. That legacy diagnostic
  accepted arbitrary-address reads, writes, and zeroing with no range or
  privilege policy, allowing corruption of kernel control state. Framebuffer,
  coredump, and PFS domains remain available; DVT/DVT2 retain raw-memory access.
- Persistent flash logging is disabled on DVT3 because its debug database is
  only an experimental subdivision of the unverified factory AP allocation.
  Serial/UART logging remains active, log-dump requests report no saved logs,
  and the unavailable backend drains rather than wedging the asynchronous log
  queue. DVT/DVT2 retain their existing circular flash logger.
- Factory reset on DVT3 now formats the PFS filesystem and reboots normally,
  but does not invalidate the experimental shared-PRF partition or set the
  unsupported force-PRF boot bit. The manufacturing consumer-mode helper is
  subject to the same gate. DVT/DVT2 retain their established PRF reset flow.
- Shared PRF storage is read-only on DVT3. Boot scanning can inspect the
  experimental region, but initialization, corruption recovery, compaction,
  pairing/key setters, first-use state, and wipe requests cannot program or
  erase it. Persistence can be enabled only after the partition and recovery
  contract are validated; DVT/DVT2 retain normal shared-PRF persistence.
- Persistent coredumps are disabled on DVT3 because their experimental flash
  partition and recovery behavior are unvalidated. Software dump requests log
  the limitation and reset immediately, BLE dump-slot reservation fails, and
  the internal write/erase wrappers plus mark-read path cannot mutate flash.
  DVT/DVT2 retain their existing persistent fault diagnostics.
- The DVT3 flash backend enforces the PFS filesystem as the only writable or
  erasable NOR range. Firmware slots and the experimental coredump, debug, and
  shared-PRF regions remain readable for diagnostics but cannot be mutated even
  if a higher-level safety gate regresses.
- The retained PFS BulkIO domain now rejects truncated open requests, empty
  erase requests, and undersized stat response buffers. Filename copies are
  bounded by the received frame length, closing malformed-frame out-of-bounds
  reads without changing valid PFS transfer behavior on DVT3 or DVT2.
- The shared BulkIO dispatcher now validates each opcode's complete fixed
  header before reading fields or deriving payload lengths. File-descriptor
  lookup checks its index before forming an array pointer, and streaming read
  and CRC callbacks cannot return oversized or zero-length progress that would
  underflow counters, loop forever, or checksum uninitialized stack bytes.
- Framebuffer BulkIO reads and writes now use overflow-safe bounds checks
  against the exact framebuffer allocation, and stat rejects undersized output
  buffers. Malformed offsets can no longer turn this retained diagnostic into
  an arbitrary adjacent-memory read or write.
- Coredump BulkIO now copies its slot selector without an unaligned word load,
  requires the exact selector payload size, rejects nonexistent slots, bounds
  reads to the selected slot, and validates stat output capacity. DVT2 retains
  valid coredump retrieval and mark-read behavior; DVT3 remains read-only.
- Define recovery before enabling OTA or destructive flash tests.
- A link assertion and mandatory pre-objcopy map check now require FLASH to be
  exactly experimental slot 0 (`0x02120000..0x02310000`) and reject any NOR
  VMA/load address outside it. A successful check stamps
  `build/pebbleos.image-layout.ok`; `.bin`/`.hex` generation depends on it.

### 15. Host validation — current baseline complete

- Maintain the clean native compile and link with no unresolved placeholder
  drivers as further changes are made.
- T5AI host packaging now enforces a 1 MiB logical system-resource pack ceiling;
  the current 1,026,537-byte pack fits narrowly. On-device resource banks remain
  deliberately zero-length until the factory partition table identifies safe
  storage. Define redundant banks and guarded write access only after that
  evidence exists; the pack is not currently installable on target.
- The mandatory T5AI pre-objcopy map gate now verifies the exact FLASH, kernel,
  worker, app, and display-memory regions; the 0x140-byte vector table at slot
  start; ordered stack/heap and framebuffer bounds; and absence of Tuya/Beken
  reference-runtime object paths. MPU behavior still requires on-target fault
  and privilege-transition validation.
- Add host tests for board selection, memory/layout calculations, protected-block translation, display conversion, touch mapping, and battery conversion where practical.
- Getafix DVT2 configure/build passes after the shared driver/service changes,
  preserving its real haptics, ALS, temperature, OTP, microphone/voice, and
  magnetometer paths. Its stored Golf app still emits the pre-existing unused
  `bounds` warning; no DVT3-port warning appeared in that compatibility build.
- Keep warnings introduced by DVT3 at zero or document unavoidable toolchain warnings.

Current host baseline: 39 `test_tuya*.py` tests and 14 Getafix board tests pass.
The DVT3 native firmware now compiles and links without unresolved platform
symbols or internal-RAM overflow. Host-generated ELF, map, raw binary, Intel
HEX, and protected-block binary artifacts are available for inspection; they
remain experimental and must not be flashed before the boot flow and recovery
requirements in section 14 are established.

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
