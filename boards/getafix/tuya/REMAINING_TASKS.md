# Getafix DVT3 native port: hardware validation and remaining tasks

This is the continuation handoff for the native PebbleOS port to the Waveshare T5-E1 Touch AMOLED 1.75 board. Continue on branch `tuya`.

## 2026-09-11 native hardware progress

- Both CH342F USB serial interfaces are usable, but they have different roles.
  Interface `if00` (currently `/dev/ttyACM0`) is the Tuya programming/reset
  port. Interface `if02` (currently `/dev/ttyACM2`) is the 460800-baud CP shell
  and runtime-log port. Use the stable by-id names
  `usb-1a86_USB_Dual_Serial_5C75025844-if00` and
  `usb-1a86_USB_Dual_Serial_5C75025844-if02` rather than relying on ACM numbers.
- CP-shell input now reaches native PebbleOS through the vendor-compatible
  CP-to-AP log mailbox channel `0x4e`. `ap_cmd PBLRX` is copied from the shared
  payload, acknowledged with the complete original 16-byte transport contents,
  and answered with `PBLRX-OK: T5AI USB mailbox RX passed` on the runtime port.
  Processing pending receive frames before the periodic heartbeat and
  snapshotting the command before constructing its ACK removes the shared-frame
  overwrite and one-shot-channel failure seen during initial testing.
- After a full DTR/RTS chip reset, the AP has a startup interval during which
  the CP shell is already visible but the Pebble mailbox route is not yet ready.
  Once ready, hardware validation completed 10/10 consecutive response-aware
  `ap_cmd PBLRX` transactions. A following 30-second watch saw no Tuya boot,
  CPU1-start retry, or Pebble reset marker.
- Native PebbleOS now reaches the normal launcher path with the production
  diagnostic loops removed. Build `6da7d158a9253150f7c256bb74f016f235e3fe91`
  ran for more than 90 seconds without the former approximately 71-second
  reboot, a Pebble task-watchdog warning, a CP heartbeat timeout, or a new boot
  banner.
- The reset loop was a scheduler-starvation issue during the partial T5AI
  bring-up. `KernelMain` remains runnable at priority 3 while the full service
  stack is disabled, starving priority-1 `KernelBG`. T5AI now starts
  `KernelBG` at the same priority and uses a one-second queue wait so the task
  proves its own idle liveness. Other SoCs retain their existing priority and
  tickless-friendly watchdog behavior.
- The CP heartbeat is dispatched from the regular one-second timer rather than
  the SysTick ISR. It uses a zero-timeout mailbox mutex acquisition and a
  non-blocking FIFO write, so CP supervision cannot block shared Pebble timer
  infrastructure. The local AP watchdog remains disabled, matching the pinned
  vendor adapter where reset supervision belongs to the CP.
- The AP-to-CP mailbox transport now distinguishes physical ACKs from logical
  responses, uses the hardware-proven directional channels (`0x11` AP-to-CP,
  `0x41` CP-to-AP), preserves a logical response that precedes its physical
  ACK, acknowledges CP notifications, and copies outbound frames into a
  cache-flushed ring so callers need not retain their source buffers.
- SARADC channel/configuration ABI was matched to the pinned CP firmware
  (`T5AISaradcConfig` 36 bytes and `T5AISaradcCommand` 132 bytes). Eight
  consecutive on-device battery reads completed successfully at approximately
  3.97-4.00 V; USB and charging state were both detected.
- Focused T5AI regressions currently pass 39 tests, the native firmware builds
  successfully, and AP-only flashing remains constrained to physical range
  `0x132000` through `0x205460` using the verified vendor QIO base.

The native target configures, compiles, links, and packages on the host. A
physical board was backed up and used for bring-up on 2026-09-10. The vendor
diagnostic image boots and validates the display and buttons, but the native
Pebble AP replacement does not enter its reset handler. The board has been
restored to the known-good four-quadrant diagnostic image. Treat CPU1/AP boot
handoff as the immediate blocker; do not spend more cycles changing the display
driver until native reset entry is proved.

## 2026-09-10 hardware-session handoff

### Device and recovery state

- Board: Waveshare T5-E1 Touch AMOLED 1.75 / Tuya T5AI (BK7258).
- USB bridge: CH342F `1a86:55d2`, product `USB Dual_Serial`, serial
  `5C75025844`. USB interface `if00` is the programming port and interface
  `if02` is the 460800-baud runtime log/CP-shell port. At the latest session
  they enumerated as `/dev/ttyACM0` and `/dev/ttyACM2`, respectively.
- Two matching 8 MiB factory reads are stored at
  `/home/baz/Documents/ChatGPT/pebble/factory-backups/` as
  `t5e1-factory-5C75025844-read1.bin` and `read2.bin`. Both have SHA-256
  `56c7d0170c47db35290397f74ab5709fd8e51e7dc45f76147bd3cb377f1a8cfd`.
- The verified vendor diagnostic QIO is
  `/home/baz/Documents/ChatGPT/pebble/build/getafix_dvt3_hw/known-good/getafix_dvt3_hw_QIO_1.0.0.bin`,
  SHA-256 `edbea99920b61b87c1bf56f46cb975e38793e55532b5d75da3e9662db143bd4a`.
  Its AP portion is saved as
  `/home/baz/Documents/ChatGPT/pebble/build/native-pebbleos/known-good-bringup_AP.bin`.
- Current on-device state: known-good diagnostic restored. Expected output is
  four coloured quadrants; button presses draw black edge indicators. This
  proves the panel, reset line, QSPI wiring, and relevant power rails work.
- Flash used tyutool 3.2.9 at
  `/home/baz/Documents/ChatGPT/pebble/.tools/tyutool-3.2.9/tyutool_cli`.

```sh
cd /home/baz/Documents/ChatGPT/pebble
.tools/tyutool-3.2.9/tyutool_cli write -d t5ai -p /dev/ttyACM0 \
  -s 0x132000 --end 0x205460 \
  -f build/native-pebbleos/known-good-bringup_AP.bin --plain
```

The complete factory backup remains the ultimate recovery source. Do not erase
or overwrite bootloader, TuyaBoot, CP, RF/calibration, or data partitions while
investigating native AP startup.

### Results and immediate blocker

- The AP begins at physical flash `0x132000`, mapped by the 34:32 transform to
  XIP `0x02120000`.
- Readback of the first `0x440` bytes after a native flash matched the generated
  image exactly, ruling out transport corruption and the selected offset.
- `/dev/ttyACM1` shows Tuya boot and CP reaching `co-procresser startup` and
  `start cp1`. A raw UART byte at the first instruction of Pebble's
  `Reset_Handler` never appears; neither do later QSPI traces.
- Matching the vendor initial stack-vector value `0x28066000` did not change
  this. The vendor AP uses the same broad RAM window beginning at `0x28010000`.
- Preserving the vendor AP tail after Pebble's protected executable instead of
  padding the experimental slot also did not make reset entry observable. Keep
  preserving it because it may contain boot metadata, but do not call it fixed.
- The blocker is the CP-to-CPU1/AP release and image/security contract:
  partition metadata, authentication, TrustZone/SPE state, vector expectations,
  or another early CPU1 requirement. The dependency-free probe in
  `tools/t5ai_boot_probe/` isolates that contract from Pebble startup. Build it
  with `tools/build_t5ai_boot_probe.py`; it sends the native CP power-up and
  heartbeat control messages, then emits `PBL-CPU1-RESET` repeatedly through
  the UART state inherited from TuyaBoot. It uses only a fixed 36-byte shared
  RAM window and has no libc, interrupt, or RTOS dependency. Package and flash
  it only through the same reviewed AP-only recovery procedure used for native
  experiments.
- The probe was exercised on hardware on 2026-09-10. TuyaBoot and the CP
  started, then the CP repeatedly logged `IPC retry to start core1,
  retry_cnt:4`; `PBL-CPU1-RESET` was not observed. The approved AP was restored
  immediately and its normal sensor/RTC/input logs resumed. This proves the
  dependency-free image does not satisfy the complete CPU1 handoff contract.
  It does not distinguish boot-time image rejection from a released CPU1 that
  cannot use the inherited UART or has not sent the CP's expected power-up IPC
  handshake. The probe now reproduces the native power-up and periodic
  heartbeat messages before using UART; validate this revision next.
- The revised probe stopped the CP retry loop on hardware even though inherited
  UART output remained unavailable. This proves CPU1 release/reset entry and
  validates the channel `0x10` power-up message as the first observable AP
  milestone. Native startup now sends that message immediately after
  `.data`/`.bss` initialization and `SystemInit()`, before enabling caches or
  entering `main()`. The old inherited-UART marker was removed. Validate this
  native milestone before investigating later startup stages.
- The native image containing that early acknowledgement was then tested. Its
  CP retry loop also stopped, proving native PebbleOS executes `.data`/`.bss`
  initialization and `SystemInit()` and reaches the new reset-stage mailbox
  milestone. No native UART log or display output followed. The next boundary
  is therefore cache enable through early `main()` initialization; instrument
  it with mailbox milestones rather than inherited UART. The approved AP was
  restored and normal diagnostic logs resumed after this test.
- Progressive acknowledgement placement subsequently cleared both cache-enable
  calls, entry to `main()`, `kernel_heap_init()`, `mbuf_init()`, `delay_init()`,
  and `dbgserial_init()`: the CP retry loop remained absent at every boundary.
  The acknowledgement now remains immediately after `dbgserial_init()`, the
  latest hardware-proven safe point. No Pebble UART bytes were emitted, so the
  next blocker is the native UART transmit/configuration contract rather than
  reset entry or those early initializers. Audit the UART clock gate, source
  clock/divisor, pin function, reset/FIFO sequence, and status-bit semantics
  against an authoritative BK7258 source before another flash.
- The pinned TuyaOpen-T5AI UART driver confirmed two omissions in Pebble's
  UART0 initialization: the UART0 peripheral clock was not enabled/selected
  from the 26 MHz crystal, and `global_ctrl` bit 1 was incorrectly treated as
  soft reset (soft reset is bit 0; bit 1 bypasses clock gating). Both are now
  corrected. A hardware run still emitted no native bytes. A follow-up image
  called `dbgserial_putstr()` immediately after `dbgserial_init()` and was also
  silent, proving the failure occurs in the first native write/flush path rather
  than in `pulse_early_init()`. Next instrument `fifo_status` without depending
  on UART, or temporarily use a bounded/nonblocking write to distinguish a
  stuck `fifo_wr_ready` wait from electrical pin/function configuration. The
  approved AP was restored and its live diagnostic logs verified after testing.
- A bounded-write follow-up waited only 1024 status polls per byte, wrote the
  FIFO even when `fifo_wr_ready` stayed clear, and bounded the final
  `tx_fifo_empty` wait. It still produced no electrical UART output. This rules
  out an infinite FIFO-status wait as the sole cause. The temporary forced-write
  behavior and marker were removed; only the authoritative clock-source,
  clock-enable, and soft-reset corrections remain. Continue by checking the
  UART clock/power domain and GPIO routing state through a non-UART transport or
  debugger before changing higher-level console code.
- The pinned application-CPU BSP revealed that `sys_drv_dev_clk_pwr_up()` does
  not directly set the CPU1 clock register: it sends PM command 2 on mailbox
  logical channel `0x12`, with module 2 (UART0) and state 1, for CP0 to apply.
  Pebble now emits that CP clock vote before local UART setup and waits 10 ms;
  the CPU1-alive announcement was moved ahead of UART initialization. The
  physical mailbox accepted the request, but the immediate UART marker remained
  silent. The marker was removed. Next make this a response-aware PM transaction
  and validate CP PM-service readiness instead of assuming a fixed settle time.
- The UART0 clock vote is now a response-aware PM transaction. It waits for a
  channel `0x12`, command 2 response, accepts only the vendor-defined success
  result in parameter 1, and times out without touching the UART registers when
  CP0 does not respond. Transport sequence values are deliberately not compared:
  CP0 sends the response as a new logical-channel command with its own sequence.
  The DVT3 image builds cleanly with this boundary. Validate the CP response and
  UART output on hardware before moving on to later peripherals.
- The response-aware image was exercised on hardware. TuyaBoot and CP0 started,
  but CP0 then continuously reported `Mailbox send data fail[ret:-4107]` while
  starting CPU1; neither the existing channel `0x10` power-up acknowledgement
  nor native UART output was observed. The new channel `0x12` PM transaction was
  therefore not reached and is not yet hardware-validated. The approved AP was
  restored immediately, and its display plus normal sensor, RTC, and input logs
  resumed. The CPU1-alive message has consequently been restored as the first
  operation in `soc_early_init()`, before interrupts and all Pebble subsystem
  initialization, matching the previously proven CP startup-handshake timing.

### Bluetooth controller evidence from the vendor-runtime diagnostic

The following results were observed over the air and on HCI while the vendor
runtime owned the controller. They establish expected controller behavior, not
correctness of Pebble's native CP mailbox transport, so
`T5AI_CP_HCI_VALIDATED` must remain disabled:

- HCI Reset completed. Read Local Version reported Bluetooth 5.2 / HCI and LMP
  version 11, revision and LMP subversion `0x0006`, manufacturer 1520.
- The public address was `20:F1:B2:B8:0A:AB`; LE features were
  `00000000fc0159ff`, with 13 LE ACL buffers of 251 bytes.
- Legacy connectable advertising as `Pebble DVT3` was visible to BlueZ and a
  peripheral-role connection completed on handle `0x001`.
- Controller-to-host ACL carried an ATT Exchange MTU request. Host-to-controller
  ACL carried the corresponding MTU 23 response, followed by a Disconnection
  Complete event for handle `0x001`.

Native validation must reproduce command/event and bidirectional ACL traffic
through `third_party/nimble/transport/t5ai_hci_ipc.c`, with the Pebble NimBLE
host as the sole host owner, before lifting the Kconfig gate.

### Display work already attempted

The native CO5300 driver was compared with the exact local Tuya sources:

- `.vendor/tuyaopen/src/peripherals/display/tdd_display/src/qspi/tdd_disp_qspi_co5300.c`
- `.vendor/tuyaopen/src/peripherals/display/tdd_display/src/qspi/tdd_display_qspi.c`
- `.vendor/tuyaopen/platform/T5AI/tuyaos/tuyaos_adapter/src/driver/tkl_qspi.c`

Command framing, FIFO use for short register data, QSPI A/C fields, FIFO reset,
AHBP power, clock gate, GPIO mux/pulls/drive, panel reset timings, the CO5300
sequence, maximum brightness, all-pixels-on, and the pixel precommand were
tested or aligned. The screen stayed black, but those tests are inconclusive
because the reset-handler marker later proved the native image was not running.
The tree contains temporary UART/QSPI and white-frame diagnostics; remove them
after AP execution is established.

### Native packaging used during the session

```sh
cd /home/baz/coredevices/pebbleos
.venv/bin/python ./waf build
.venv/bin/python tools/package_t5ai_qio.py \
  --base-qio /home/baz/Documents/ChatGPT/pebble/build/getafix_dvt3_hw/known-good/getafix_dvt3_hw_QIO_1.0.0.bin \
  --base-sha256 edbea99920b61b87c1bf56f46cb975e38793e55532b5d75da3e9662db143bd4a \
  --protected-ap build/pebbleos.t5ai-crc.bin \
  --output /home/baz/Documents/ChatGPT/pebble/build/native-pebbleos/getafix_dvt3_pebbleos_QIO.bin
dd if=/home/baz/Documents/ChatGPT/pebble/build/native-pebbleos/getafix_dvt3_pebbleos_QIO.bin \
  of=/home/baz/Documents/ChatGPT/pebble/build/native-pebbleos/getafix_dvt3_pebbleos_AP.bin \
  bs=1 skip=1253376 status=none
```

Do not flash the native image again merely to inspect the screen. First add a
stronger test of the CP release/CPU1 reset contract.

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
- Thirty-nine Tuya host tests and twenty-five Getafix board-selection tests pass.
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
- The protected payload has been flashed experimentally and read back correctly,
  but did not enter Pebble's reset handler. Do not repeat native flashes until
  the AP/CP compatibility, security policy, and CPU1 release contract are
  understood. Programming transport and recovery to the known-good diagnostic
  AP are proven; complete factory restoration is backed up but has not needed
  to be exercised.
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
- BulkIO read, write, and CRC requests now reject wrapped 32-bit ranges. Write
  and stat callbacks cannot claim more data than their supplied buffers, and
  write acknowledgements report actual progress. The PFS domain propagates
  failed seeks instead of reading or writing at a stale prior file position.
- BulkIO descriptors with queued streaming-read callbacks are no longer reused
  until that work drains, callbacks safely abandon descriptors closed by the
  host or link loss, and overlapping reads are rejected. Close failures still
  release descriptors as required by the domain contract rather than leaking
  one of the three protocol slots.
- The shared reliable Pulse transport now requires an information packet's
  declared length to include its complete header and fit in the received frame
  before subtracting or dispatching it. Malformed frames cannot underflow into
  huge protocol payloads or advance the receive sequence before validation.
- Variable-size BulkIO read, stat, malformed-command, and internal-error
  responses now use the reliable transport's actual payload capacity rather
  than the larger generic Pulse buffer size. Maximum responses therefore leave
  room for framing instead of tripping the reliable sender size assertion.
- The BulkIO packet handoff now rejects nonempty null buffers, frames above the
  transport MRU, and allocation-size overflow before copying or queueing work.
  Empty commands retain their protocol error response without invoking
  `memcpy` on a null source.
- Every BulkIO response now handles an unavailable reliable transmit buffer
  during link teardown. Interrupted opens close and release their domain, and
  interrupted reads clear active state. If teardown occurs after acquiring the
  shared transmit buffer, the reliable sender releases its lock before dropping
  the packet so the next link cannot deadlock.
- The Pebble Protocol transport now drops truncated or null packets before
  reading their opcode or subtracting the header length. Deferred control
  replies also tolerate reliable-link teardown and always release their queued
  allocation instead of copying through a null transmit buffer.
- Pebble Protocol streaming now revalidates both the session pointer and a
  connection generation after temporarily releasing the Bluetooth lock to
  obtain a transmit buffer. A close/reopen cannot leave it operating on a stale
  session, and a failed session open returns before assigning capabilities.
- Both reliable and legacy Pulse prompt handlers now validate null, truncated,
  and oversized commands before reading their fields, changing execution
  state, or copying into the fixed command buffer. DVT3 still excludes the
  prompt entirely; the shared fix preserves and hardens DVT2 diagnostics.
- Prompt responses are now truncated to the payload capacity of their selected
  reliable or legacy Pulse transport. Length calculation is bounded and occurs
  before acquiring the transmit buffer, preventing oversized command output
  from asserting the sender or monopolizing its shared buffer unnecessarily.
- The shared best-effort Pulse transport now requires a complete fixed header
  before reading its declared length, and requires that length to include the
  header and fit within the received frame. Null, truncated, and undersized
  declarations can no longer underflow the payload passed to protocol handlers.
- Pulse control messages now reject null input and tolerate unavailable transmit
  buffers for echo, unknown-code, and queued port-closed replies. Reliable-link
  teardown can no longer turn those response paths into null-pointer writes.
- The shared PPP control state machine now rejects null and incomplete frames
  before reading their header, caches and validates the declared length before
  dispatch, and initializes the length field of Code-Reject replies rather than
  transmitting stale buffer contents.
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

Current host baseline: 39 `test_tuya*.py` tests and 25 Getafix board tests pass.
The DVT3 native firmware now compiles and links without unresolved platform
symbols or internal-RAM overflow. Host-generated ELF, map, raw binary, Intel
HEX, and protected-block binary artifacts are available for inspection. They
remain experimental; section 14 records the failed reset-entry test and the
condition for trying another image.

### 16. Continued hardware validation

The factory image is preserved, AP-only recovery is proven, and CPU1 reset
entry plus the CP power-up handshake are proven. Next complete native UART
write-path validation, then validate clocks, GPIO,
I2C/RTC, display, touch, sensors, Bluetooth, sleep, watchdog, charging, and
finally writable storage/update behavior.

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
