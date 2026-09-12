# Getafix DVT3: Waveshare T5-E1 hardware bring-up

`getafix@dvt3` is the native PebbleOS port for the Waveshare
[T5-E1-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75),
which uses the Tuya T5AI/BK7258. This page is the operator handoff for the
hardware currently under test. It reflects the state validated on 2026-09-12.

The only PebbleOS source of truth is:

```text
/home/baz/coredevices/pebbleos
```

Do not build or edit a source copy under
`/home/baz/Documents/ChatGPT/pebble`. That directory contains reference SDKs,
vendor firmware, factory backups, tools, and generated lab artifacts only.

DVT3 has an isolated defconfig and does not inherit DVT2's SiFli SoC or legacy
peripherals. DVT and DVT2 remain unchanged. DVT3 is an experimental port, not a
shipping hardware identity, and is excluded from release matrices.

## Current status

The native target configures, builds, packages, boots, logs over USB, renders
through the native CO5300/QSPI path, reads the board's input and sensors, and
runs Pebble's NimBLE host against the CP-owned Bluetooth controller.

| Area | Hardware status |
| --- | --- |
| CPU1 startup and CP handshake | Proven |
| Dual USB UART roles and runtime logging | Proven |
| FreeRTOS scheduler, SysTick, AON and external RTC | Proven |
| GPIO and Back/Select buttons | Proven |
| I2C0 and PCF85063 RTC | Proven |
| CO5300 AMOLED and native QSPI transport | Proven with colour bands and display off/on tests |
| CST9217 touch | Proven |
| QMI8658 accelerometer | Proven |
| SARADC and charger-status input | Transactions proven; battery behavior blocked because no cell is fitted |
| Bluetooth HCI command/event and ACL transport | Proven |
| Connectable advertising and BlueZ connection | Proven |
| Pebble GATT service discovery | Proven |
| Pairing, bonding, reconnect, and Pebble protocol transfer | Not yet validated |
| Sleep/wake and low-power operation | Not yet validated |
| OTA, recovery, and general writable flash | Deliberately disabled pending a safe storage/recovery contract |

The normal T5AI path still performs a partial service startup in
`src/fw/main.c`; it does not yet run the complete Pebble launcher/application
stack. A black display after ordinary startup can therefore mean that no UI has
drawn a frame, not that the display path failed.

## Device and USB ports

The test board has a CH342F dual serial bridge (`1a86:55d2`, serial
`5C75025844`). Its two USB interfaces have different jobs:

- `if00`, currently `/dev/ttyACM0`: Tuya programming and reset.
- `if02`, currently `/dev/ttyACM1`: CP shell and native runtime log at 460800
  baud.

ACM numbers can change. Prefer the stable by-id paths containing
`usb-1a86_USB_Dual_Serial_5C75025844-if00` and
`usb-1a86_USB_Dual_Serial_5C75025844-if02`.

If the CP becomes wedged after a failed mailbox or Bluetooth sequence, an AP
reset or reflash may not recover it. Disconnect the shared USB-C cable for five
seconds and reconnect it to power-cycle both cores and the dual-UART bridge.

## Build

Run all commands from the real PebbleOS checkout:

```sh
cd /home/baz/coredevices/pebbleos
. .venv/bin/activate
python pbl configure --board getafix@dvt3
python ./waf build
```

The last clean build on 2026-09-12 succeeded with these measurements:

- FLASH: 852,083 bytes of 1,984 KiB (41.94%).
- KERNEL_RAM: 82,304 bytes of 106 KiB (75.83%).
- T5AI_DISPLAY_RAM: 67,614 bytes of 128 KiB (51.59%).
- LOG_STRINGS: 119,239 bytes of 512 KiB (22.74%).
- Resources: 1,026,537 bytes of 1,048,576 (97.90%).

The build emits `build/pebbleos.t5ai-crc.bin`, an AP-only protected-block
payload. It is not a complete QIO, UA, UG, factory, recovery, or OTA image.

## AP-only lab flashing

The pinned partition places the AP at physical flash offset `0x132000`, mapped
to logical XIP address `0x02120000`. Pebble links into an experimental
`0x1f0000`-byte logical slot within the factory AP allocation.

`pbl` runner support remains intentionally disabled. For the already-backed-up
lab board, use the known manual AP-only procedure. Compute the protected image
end as `0x132000 + file_size`, then flash exactly that range:

```sh
stat -c '%s' build/pebbleos.t5ai-crc.bin

/home/baz/Documents/ChatGPT/pebble/.tools/tyutool-3.2.9/tyutool_cli \
  write -d t5ai -p /dev/ttyACM0 \
  -s 0x132000 --end 0x20f088 \
  -f /home/baz/coredevices/pebbleos/build/pebbleos.t5ai-crc.bin --plain
```

The shown `--end` value is only correct for the current 905,352-byte image.
Recompute it whenever the image size changes. Do not use this payload with a
factory or OTA programming mode, and do not erase outside the exact AP range.

To monitor runtime output:

```sh
picocom --baud 460800 /dev/ttyACM2
```

## Bluetooth handoff

`CONFIG_T5AI_CP_HCI_VALIDATED=y` is enabled for DVT3. Pebble's NimBLE host is
the sole AP-side Bluetooth host; the vendor Bluetooth host must not be started
alongside it.

The central transport rule discovered on hardware is that the physical
AP-to-CP mailbox permits only one non-transport-ACK message in flight. This is
the same serialization expressed by the vendor `bk_ipc` semaphore. The native
mailbox now waits for physical completion before sending another ordinary
frame, while physical ACK frames bypass that wait.

HCI callbacks immediately copy and physically acknowledge the borrowed CP
frame. An eight-entry FIFO then preserves interleaved physical ACKs, HCI
events, ACL data, MIPC object ACKs, and application replies. Asynchronous
callback-response descriptors remain owned until their logical MIPC ACK. These
ownership rules fixed the descriptor overwrite, double-ACK, and command-order
failures found during bring-up.

The final diagnostics-free image completed all legacy advertising commands:
LE Set Advertising Data (`0x2008`), LE Set Scan Response Data (`0x2009`), LE
Set Advertising Parameters (`0x2006`), and LE Set Advertise Enable (`0x200a`).
BlueZ then:

- discovered `Pebble 0AAB` at public address `20:F1:B2:B8:0A:AB`;
- connected successfully;
- resolved Generic Access, Generic Attribute, Device Information, Battery,
  Pebble `fed9`, and Pebble vendor
  `40000000-328e-0fbb-c642-1aa6699bdada` services; and
- disconnected cleanly, leaving the board advertising.

The test connection was not paired or bonded. The next Bluetooth milestone is
pairing, encrypted reconnect, and bidirectional Pebble protocol traffic over
the `fed9` service, including MTU and notification behavior.

A 2026-09-12 follow-up build reached the Pebble numeric-comparison popup and
BlueZ displayed the same six-digit value, proving the pairing UI and SMP
callback path. The attempt expired before confirmation completed, and
advertising did not resume until AP reset. The NimBLE backend now uses a stable
confirmation context and cancels an in-progress confirmation on disconnect.
The apparent advertising failure was partly a stale BlueZ device record; a
subsequent canceled attempt was rediscovered in the same boot after removing
that record. The black pairing screen was traced to the partial startup
bypassing `light_init()`, so T5AI now initializes the self-contained light
service without enabling the broader persistent-storage and OTA service path.
Automatic and debug-console confirmation remain deliberately unsupported.

The stable-context and optional-name-read builds passed the focused 78-test
suite, built, flashed, and booted successfully. The light-service follow-up
also passes all 78 tests and builds successfully. Its 905,760-byte protected
payload has SHA-256
`96ae871f34a2cf3e47120b7e3df97a6cec278b43d20e18b95e0baad633a59a7d`;
it flashed, rebooted, and reached numeric comparison, but the panel remained
black and confirmation expired. T5AI had started the asynchronous splash task
before driver initialization and then reset the CO5300 while the task could
still be writing QSPI rows. Splash/display startup now occurs once after
driver setup. The corrected build passes all 78 focused tests; its 905,318-byte
protected payload has SHA-256
`8b7f44b7b1768ac6c30e07de14e1b671ec4dc28497b308b6ee4991292b40b54a`.
AP-only flashing and reboot succeeded; visible-display and pairing validation
remain pending.

A subsequent cold boot still showed no splash. The boot log contained no QSPI
timeout or CO5300 initialization error, but the driver was sending short panel
commands through FIFO mode. The pinned vendor driver sends payloads up to four
bytes inline with a length-specific command configuration and reserves FIFO
mode for larger transfers. Native CO5300 command framing now matches that
contract. All 78 focused tests pass and the build succeeds; its 905,352-byte
protected payload has SHA-256
`7c2ba1487dfccaf1579bd6aa23b6b9bbc4b13f9d910ed83e631afb34f6d7ceaa`.
AP-only flashing and reboot succeeded; visible-output validation is pending.

That restored-framing build still produced no visible output. A temporary
synchronous panel probe now sends CO5300 `all pixels on` (`0x23`) immediately
after maximum-brightness and display-on initialization, bypassing the splash
framebuffer, compositor, and Bluetooth paths. All 78 focused tests pass and
the build succeeds with unchanged memory use. Its 905,352-byte protected
payload has SHA-256
`579d6823a3906c91fc7ec3a800e0d90b364623b5e633466c8919ffc43585c680` and
occupies `0x132000..0x20f088`. The reconnected bridge enumerated as
`1a86:55d2`; its serial nodes required host-level access from the current task.
The diagnostic image was flashed successfully over `if00`, restricted to
`0x132000..0x20f088`, and the AP rebooted. The panel remained completely
black, isolating the fault below the splash framebuffer, compositor, and
Bluetooth paths.

Comparison with the earlier native display revision that produced visible
output found four simultaneous QSPI-controller changes. The diagnostic build
now restores that known-visible setup: 40 MHz serial clock, an asserted then
released controller soft reset, the reset-default FIFO threshold, and the QSPI
clock-gate-enable bit. It retains checked mailbox power-up, corrected
short-command framing, and the `all pixels on` probe. All 78 focused tests pass;
memory use is unchanged. The 905,352-byte payload has SHA-256
`1f3877dda7587d52a851259d35c06b11722db9ee998ff5c73691d73bafb37818` and
occupies `0x132000..0x20f088`. It flashed successfully to that exact AP range
and rebooted. The panel again remained completely black, ruling out those four
later QSPI-controller settings as the active fault.

For a source-versus-hardware boundary test, the exact historical display probe
from revision `cc642becb` was built in an isolated `/tmp` checkout. It contains
the original maximum-brightness/all-pixels-on sequence, raw QSPI and reset-pin
logging, and a display-only main loop. The 736,304-byte protected payload has
SHA-256
`e4ec3628cc5e1a6cdcf6c296a6bf7955229d514b66ea0964098d418c3141b5c4` and
was flashed successfully to its exact `0x132000..0x1e5c30` AP range.
It also remained completely black after the flash reboot. This moves the
active investigation below the current source diff to panel power/reset or
CP-mediated QSPI power state. The attempted cold-boot capture was invalid:
`if00` and `if02` are two channels of the same USB-C dual-UART bridge, so
unplugging that connector necessarily removed both capture channels. Board
power and the virtual UARTs share this connector, so future cold-boot capture
must use a firmware-side delay or host-triggered gate after re-enumeration. The
current Bluetooth image was restored successfully.

The next probe uses the CO5300's documented `0x03` command-read transaction and
the pinned BK7258 command-D/FIFO conversion to read DCS power mode (`0x0a`).
Because the AP log UART is not exposed reliably, the returned byte is encoded
into the discoverable Bluetooth name as `PBL-Dxx`. This keeps the test on the
current known-booting Bluetooth path. All 78 focused tests pass; the build uses
852,211 bytes of FLASH and otherwise unchanged memory. Its 905,488-byte
protected payload has SHA-256
`84faaab946359a27bee6070ec573b52ebb9c860ff1495033a83f00cb77acc608` and
occupies `0x132000..0x20f110`. It flashed successfully. Removing the unbonded
BlueZ record and rediscovering the board still returned the default
`Pebble 0AAB` name, proving the power-mode read was not marked valid rather
than exposing a cached advertisement.

The Bluetooth self-report now distinguishes QSPI power failure (`PBL-QER`),
panel write-initialization failure (`PBL-PER`), command-D read failure
(`PBL-RER`), and successful power-mode data (`PBL-Dxx`). All 78 focused tests
pass; the build uses 852,275 bytes of FLASH and otherwise unchanged memory. Its
905,556-byte payload has SHA-256
`a8d3faa14dec6376735e01f7aa0a89e648f8ed7940857db18d524988de73e66f` and
occupies `0x132000..0x20f154`. It flashed successfully, and a fresh BlueZ
discovery returned `PBL-QER`. Later source-level tracing showed that this was
the probe's untouched default rather than evidence that the CP-mediated QSPI
request had failed: normal firmware compiles the boot-splash service to a no-op,
so the T5AI panel initializer was never called.

The original visible display probe treated the CP power request as advisory
and applied the AP's validated QSPI power/clock registers regardless. The
current driver now retains the CP request but falls back to those direct AP
registers instead of aborting display initialization when the CP service is not
ready. The focused 78-test suite passes and the build succeeds with 852,275
bytes of FLASH, 82,304 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM,
and 119,265 bytes of LOG_STRINGS. Its 905,556-byte payload has SHA-256
`9727acb7bfc495438d1f3262dad30d938bfe5049feecccab66a035aed92b9a05` and
occupies `0x132000..0x20f154`. It flashed successfully to that exact AP range
and rebooted. The board did not advertise during the first post-flash scan,
consistent with CP state after repeated AP-only resets; cold-boot display and
diagnostic-name observations are pending. Perform that cold boot by leaving the
dual-UART bridge connected and cycling only board power.

A subsequent full USB replug re-enumerated the bridge as `ttyACM0`/`ttyACM1`
(`if00`/`if02`) and fresh BlueZ discovery again returned `PBL-QER`. Inspection
then found the actual integration regression: `main.c` skipped its explicit
T5AI `display_init()` and called `boot_splash_start()`, but the normal-firmware
implementation of that service is a no-op. Explicit panel initialization is now
restored immediately after driver setup. The focused 78-test suite passes and
the build succeeds with 852,851 bytes of FLASH, 82,304 bytes of KERNEL_RAM,
67,614 bytes of T5AI_DISPLAY_RAM, and 119,265 bytes of LOG_STRINGS. Its
906,168-byte protected payload has SHA-256
`d80c64909d963fb0849fdcf9cdc1ecc910402df03e2de75fe8b7988ca3d3a55a` and
occupies `0x132000..0x20f3b8`. It flashed successfully to exactly that AP range
and rebooted. Fresh BlueZ discovery returned `PBL-D00`: explicit panel
initialization ran, every write command completed, and the command-D read
transaction completed with power-mode byte `0x00`. A visible-screen observation
is pending; if it remains black, distinguish a real all-zero panel response
from incorrect BK7258 read-data reconstruction before changing the proven write
sequence again.

The screen remained black with `PBL-D00`. Panel bring-up is now isolated from
Bluetooth diagnostics and uses direct AP UART0 output at 460800 baud on
`if00`/`ttyACM0`. Rechecking the pinned Tuya adapter found that the prior
short-parameter change was incorrect: the four-byte `02 00 <register> 00`
header stays in command C and every parameter length, including one byte, is
written through the QSPI FIFO. The diagnostic image restores that exact FIFO
framing and the board's production 80 MHz clock, then reports QSPI registers,
each initialization command, and raw command-D data over UART after a
host-triggered capture gate. Bluetooth-name instrumentation has been removed.
All 78 focused tests pass. The current build uses 853,171 bytes of FLASH, 82,464 bytes
of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 119,266 bytes of
LOG_STRINGS. Its 906,508-byte protected payload has SHA-256
`24877ffdf7b2170b60538e29be71ee3aebf02017efb4dbccabd0c836ef84b86f` and
occupies `0x132000..0x20f50c`; flashing and UART capture are pending. The board
and both virtual UARTs share the same USB-C connection, so unplugging power also
removes both tty devices; there is no separate board-power cable. A
host-triggered AP-UART image was silent and did not accept its `G` byte,
matching the earlier documented finding that native AP UART0 has no electrical
output. The trace is therefore routed through Pebble's established CP-mailbox
logger to `if02`/`ttyACM1` at 460800, with a ten-second delay after boot so the
tty can be reopened before panel initialization starts.
The resulting 906,508-byte image (SHA-256
`24877ffdf7b2170b60538e29be71ee3aebf02017efb4dbccabd0c836ef84b86f`)
flashed successfully to `0x132000..0x20f50c`, but a 30-second `if02` capture
received no forwarded trace. Early CP log forwarding is therefore not a
dependable panel diagnostic channel either. The corrected FIFO/80 MHz panel
sequence did execute after its ten-second delay, but the screen remained
completely black. Comparison with Tuya's QSPI HAL then exposed a controller
reset-state mismatch: Pebble set `QSPI_GLB_CTRL.soft_reset` and immediately
cleared it, while Tuya leaves that bit set after initialization. The next probe
leaves it set and restores the known FIFO threshold. All 78 focused tests pass
and the build succeeds with 853,171 bytes of FLASH, 82,464 bytes of KERNEL_RAM,
67,614 bytes of T5AI_DISPLAY_RAM, and 119,266 bytes of LOG_STRINGS. Its
906,508-byte protected payload has SHA-256
`21b3aff34168b47ffe0b3c62ca876fbd5978ec8ccda10a15bde0d37823f62803`
and occupies `0x132000..0x20f50c`. It flashed successfully to exactly that
protected AP range and rebooted. A 25-second `if02` capture at 115200 baud
received undecodable binary-like traffic rather than the expected panel text;
`if02` is in fact the previously proven 460800-baud CP shell. Sending
`ap_cmd PBLTIM` at 460800 produced a readable AP mailbox response, confirming
the live USART diagnostic route. The screen remained black. Register-level
comparison then found that offset `0x64` bit 1 is the active `sw_rst_fifo`
control, not reset release. The failing probes left it set, so their commands
could complete while parameter bytes were discarded. The next image restores
`QSPI_RST_CFG=0`, matching both the last hardware-proven Pebble revision and
Tuya's normal command path. All 78 focused tests pass. The 906,508-byte
protected payload has SHA-256
`fef6582b3c4aa8835ae8f2f1f82d6e8adfb4012010a66826dd352960e94bc0d2`,
occupies `0x132000..0x20f50c`, and flashed successfully to exactly that AP
range. The 460800-baud post-flash capture showed repeated CP IPC CRC warnings
plus mixed runtime traffic, and the screen remained black. A subsequent build
removed noisy boot-time command logging and added the safe `ap_cmd PBLDSP`
USART probe. Existing `PBLRX` and `PBLTIM` commands responded, but the first
`PBLDSP` handler (which also issued live QSPI operations) did not return output.
The probe was not blocking in QSPI: it was unreachable because `PBLDSP` and
`PBLTIM` are both eight-byte requests and the parser chose a branch by length
before comparing the display command. The parser now compares both requests in
the shared-length branch, and the display probe is deliberately read-only. It
reports driver state plus QSPI, clock, gate, P19 power-latch, and P29 reset
registers. All 78 focused tests pass and the build succeeds. The resulting
906,100-byte protected AP payload has SHA-256
`078c16aa87c95140aa3c2cabacec1a1db22f482c3b9bc1c7d77c81cd16cf6d8c`
and occupies exactly `0x132000..0x20f374`. It has not yet been flashed because
both USB serial interfaces disappeared immediately before programming. Replug
the shared USB-C cable, flash only this exact range, then send `ap_cmd PBLDSP`
through `if02` at 460800 baud and capture its complete response.

That image was flashed successfully and returned `PBLDSP i/e/u=1/1/0
glb=00000001 cfg=08000009 rst=00000000 st=0001100a clk=05000570
gate=87188491 p19=00000103 p29=00000003`. This proves QSPI was enabled, its
FIFO reset was clear, command completion was recorded with an empty FIFO, and
both P19 board power and the released P29 panel reset were physically high.
The follow-up probe retries the CP QSPI power vote after boot, reconfigures the
controller, and repeats the exact vendor reset/init/all-pixels-on sequence. It
passes all 78 tests and builds with 852,947 bytes of FLASH. Its 906,270-byte
payload has SHA-256
`7b155c26591ddc9d75dba4c5f42c9ae9da6ab45e92a01795253fe7fec543f920`
and occupies exactly `0x132000..0x20f41e`; exact-range flashing succeeded. The
live result was `PBLDSP bootpm=0 nowpm=0 reinit=1 i/e/u=1/1/0 glb=00000001
cfg=08000009 rst=00000000 st=0001100a clk=05000570 gate=8f188491
p19=00000103 p29=00000003`. Both early and late CP power votes succeeded, so
CP service readiness is ruled out. Visual observation after the live reset is
still completely black.

The next probe added a vendor-framed command-D power-mode read plus live
power-domain, QSPI GPIO, and pin-mux registers. It passes all 78 tests and
builds with 853,235 bytes of FLASH. Its 906,576-byte payload has SHA-256
`54a178ac86d25fedda4105aff062f64d4b99780e3527a27d6af9df1a75f83abf`
and occupies exactly `0x132000..0x20f550`; exact-range flashing succeeded.
Three repeated probes returned identical `rd=1 pm=ff raw=bce9bdff` data, with
`mem=00000000/00000000`, `sleep=200000c0`, QSPI GPIO values
`70348/378/30378/150378/150378/150378`, and mux words
`33000000/00003333`. The QSPI engine raises RX-done, but `0xff` is not a valid
CO5300 power-mode response and the complete FIFO word remains unchanged across
panel resets. Power, clocks, GPIO configuration, and mux match the pinned
vendor implementation; the panel is not electrically answering.

The earlier short-command experiment never called `display_init()` and was not
a valid framing test. A new A/B build uses the pinned dedicated BK LCD driver's
inline command-C encoding for zero-to-four-byte register writes, retaining FIFO
mode for pixel payloads. All 78 tests pass. Its 906,576-byte payload has
SHA-256
`0c705b0de7ecb10067d574455c996b880d1357e7d855f1565d23501fb144d5af`
and occupies exactly `0x132000..0x20f550`; exact-range flashing succeeded. The
live sequence returned `reinit=1`, but the read remained `pm=ff
raw=beedfdff` with unchanged power and mux registers. The panel remained
completely black.

A software-SPI isolation build then bypassed the QSPI controller, FIFO, mux,
and command packing. It drove CLK P22, CS P23, and DATA/IO0 P24 directly as
GPIO, used the exact vendor reset timing and initialization sequence, and sent
sleep-out, display-on, and all-pixels-on. All 78 tests pass. The build uses
853,683 bytes of FLASH, 82,464 bytes of KERNEL_RAM, 67,614 bytes of
T5AI_DISPLAY_RAM, and 119,266 bytes of LOG_STRINGS. Its 907,052-byte protected
payload has SHA-256
`eaee4df317032ef3e5e66bef88a4882738ee16522e1f60dddcb719bf32baa46d`
and occupies exactly `0x132000..0x20f72c`; exact-range flashing succeeded. The
live probe ended with `PBLBIT done=1 clk=00030301 cs=00000303 data=00030301
reset=00000003`, after the hardware-QSPI portion again reported `reinit=1
rd=1 pm=ff raw=bfe9ffff`. The screen remained black. Since two valid hardware
QSPI encodings and an independent GPIO transport all completed without panel
output or a credible read response, further blind protocol changes are not
justified. Inspect/reseat the display FPC and connector, verify panel power
rails, and scope P22/P23/P24 and P29 while issuing `PBLDSP`. That
physical-fault conclusion was subsequently disproved: the stock vendor firmware
works on the same board, so the hardware path is sound and the native setup
remains wrong.

A pad-level follow-up then enabled input sensing while each QSPI signal
remained a push-pull GPIO output and sampled CLK, CS, and DATA/IO0 after both a
low and high drive. This does not introduce another panel protocol variant.
All 78 focused tests pass. The build uses 853,875 bytes of FLASH, 82,464 bytes
of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 119,266 bytes of
LOG_STRINGS. Its 907,256-byte protected payload has SHA-256
`cf2ace1d5b42618a0abbaceb15a43231976530922ee4ec42f15a9c942cc57d58`
and occupies exactly `0x132000..0x20f7f8`; exact-range flashing succeeded. The
live probe reported `PBLPAD low/high clk=2 cs=2 data=2 expected=2`. Each value
encodes a low sample of zero and a high sample of one, proving all three MCU
pads follow the requested levels. This rules out a stuck output latch or
disabled GPIO pad. Together with the working stock image, this confirms the
remaining failure is native software configuration rather than hardware.

An exact re-read of Tuya's T5AI `tkl_qspi_init()` confirmed that its 80 MHz
case selects 480 MHz with system divider 5 and leaves the QSPI controller's
`clk_div` field at bits 8..15 zero. The high byte in live `cfg=08000009` was
initially misidentified as that divider; it is actually `spi_cs_h_wait`. The
next build restores the generic Tuya CO5300 command path
(parameter bytes through FIFO and `CFG1=0x300`), programs `QSPI_CONFIG` as
`0x00000009`, and removes the bit-bang/pad experiment so diagnostics preserve
QSPI ownership. All 78 focused tests pass. The build uses 853,203 bytes of
FLASH, 82,464 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and
119,266 bytes of LOG_STRINGS. Its 906,542-byte protected payload has SHA-256
`94ae5985ed3a66013af611bd01d6972e6f462fc3938fe10f2de1d2e5469694a8`
and occupies exactly `0x132000..0x20f52e`; exact-range flashing and reboot
succeeded. A live `PBLDSP` confirms `cfg=00000009` and `reinit=1`, while the
command-D read remains noncredible at `pm=ff raw=bfedffff`. Visible-output
observation is pending.

That image was still not fully equivalent to the working path. Its live
`glb=00000001` showed that native code left `QSPI_GLB_CTRL.bit0`, the active
controller soft reset, asserted. Tuya's generic
`qspi_hal_init()`/`qspi_ll_init_common()` leaves the bit clear, while the earlier
native display-bring-up revision `052392be5` explicitly pulsed it from 1 to 0.
The follow-up deasserts this reset and retains `cfg=00000009`, FIFO parameter
transfers, and `RST_CFG=0`. All 78 focused tests pass. Its 906,542-byte
protected payload has SHA-256
`a07590d77788662e5bc98fa75ac61490c53ce6a2adf87627a925cc1597af7c03`
and occupies exactly `0x132000..0x20f52e`; exact-range flashing and reboot
succeeded. The live probe confirms `glb=00000000 cfg=00000009 rst=00000000
reinit=1`; command-D still returns invalid `pm=ff raw=bee9ffff`. Visible-output
observation confirmed the panel remained black.

Repository history places the display regression at `05292094e`, which
simultaneously changed the preceding native bring-up from 40 to 80 MHz, inline
to FIFO short commands, released to asserted soft reset, and `RST_CFG.bit1`
set to clear. The immediately preceding diagnostic in `bc29e5967` forced full
brightness and all pixels on through the 40 MHz inline-command path. The
focused A/B restores that complete earlier native transport/register state,
retains explicit brightness `0xff` plus command `0x23`, and does not copy or
link vendor code. All 78 focused tests and `git diff --check` pass. The build
uses 853,267 bytes of FLASH, 82,464 bytes of KERNEL_RAM, 67,614 bytes of
T5AI_DISPLAY_RAM, and 119,266 bytes of LOG_STRINGS. Its 906,610-byte protected
payload has SHA-256
`0e098ebe7088e44ac3a6a54b3821e7f9e4a68c4738d4fac3d1f77e2ee7fda729`
and occupies exactly `0x132000..0x20f572`; exact-range flashing and reboot
succeeded. The live diagnostic reports `bootpm=0 nowpm=0 reinit=1 rd=1 pm=ff
raw=befdbdff i/e/u=1/1/0 glb=00000000 cfg=00000009 rst=00000002
st=0001100b` and clock register `050006f0`, confirming the intended historical
state on hardware. Visible-output observation confirmed the panel remained
completely black. The four display-driver changes bundled into `05292094e` are
therefore not the sole regression cause; compare the complete known-working
native bring-up state against current power, clock, pinmux, and startup code
before another flash.

Read-only disassembly of the preserved native artifact
`known-good-native-cpu1_AP.bin` then found that its executed transport does not
match the nearby source history: it uses FIFO framing and `CFG1=0x300` for every
command length, while retaining the 40 MHz system divider, global-reset
pulse/release, and `RST_CFG.bit1`. It also sends brightness `0xff` after
sleep-out. The next A/B matches that actual machine-code combination. All 78
focused tests and `git diff --check` pass. The build uses 853,235 bytes of
FLASH, 82,464 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 119,266
bytes of LOG_STRINGS. Its 906,576-byte protected payload has SHA-256
`f639c7718992f7bba361881820b70f501f14d2c01debbf5eeb9d3ecb99567447`
and occupies exactly `0x132000..0x20f550`; exact-range flashing and reboot
succeeded. Visible-output observation is pending; do not issue the QSPI read
probe until that observation is recorded.

During display bring-up, the T5AI backlight driver forces every requested
level, including zero/off requests from the light service, to 100 percent and
sends CO5300 brightness `0xff`. Restore dynamic brightness only after the panel
produces reliable visible output. All 78 focused tests and `git diff --check`
pass. The 906,576-byte protected payload has SHA-256
`be7f894052087a91219248a27e392e9bc6e45b43e28c922a4c56b2cc83e664d7`
and occupies exactly `0x132000..0x20f550`; exact-range flashing and reboot
succeeded. The panel remained completely black, ruling out light-service
dimming and CO5300 brightness level as the cause.

The next diagnostic image deliberately uses `CONFIG_BT_FW_STUB=y`. Bluetooth
is paused for panel recovery, and its binary HCI stream was flooding the same
CP log USART needed by `PBLDSP`. The display diagnostic now prints the complete
QSPI register block from offset `0x00` through `0x7c` as `PBLREG` lines.

Read-only disassembly of the exact working Tuya ELF also resolves the QSPI pin
mapping. Tuya maps P22 through P27 using board-specific function IDs
`0xad`, `0xae`, and `0x64` through `0x67`; its GPIO mapping table maps all six
to peripheral mux index 3. Native mux values are therefore correct. The exact
Tuya binary calls `qspi_hal_enable_soft_reset()`, which sets
`GLB_CTRL.bit0`, so a live value of one is vendor-normal rather than evidence
that the controller is held in reset.

This diagnostic passes all 78 focused tests and builds successfully with
759,315 bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of
T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. The 806,786-byte AP-only
payload has SHA-256
`53e6fc492d142b44376ddb520a1c8a62e20628e278e6a684a206714e234333ae`
and occupies exactly `0x132000..0x1f6f82`; exact-range flashing and reboot
succeeded. The clean probe returned `reinit=1 rd=1 pm=ff raw=bfedfdff`,
`glb=0 cfg=9 rst=2 st=0001100b`, and valid QSPI device ID `51535049`. The full
register dump contains no command-failure bit; its final command-C state is
`CMD_C_H=00230002`, the CO5300 all-pixels-on command. The panel still does not
drive IO0 during the command-D read. The screen remained completely black,
ruling out Bluetooth runtime contention.

The next A/B reproduces the exact compiled Tuya controller ordering rather
than only its final values: configure and enable QSPI, select 80 MHz, program
command-A, then set `GLB_CTRL.bit0`, with `RST_CFG=0`. Brightness `0xff` is
restored to its vendor position before sleep-out; the final all-pixels-on probe
is retained for an unambiguous visual result. All 78 focused tests and `git
diff --check` pass. The build uses 759,283 bytes of FLASH, 49,216 bytes of
KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS.
Its 806,752-byte protected AP payload has SHA-256
`5efc66cb220bd5baa9cbd165eb277ff0c3b2530d7aeb398790ea9cb3fb207ffd`
and occupies exactly `0x132000..0x1f6f60`; exact-range flashing and reboot
succeeded. The clean probe returned `reinit=1 rd=1 pm=ff raw=bcedfdff`,
`glb=1 cfg=9 rst=0 st=0001100b`, clock register `05000570`, the expected mux
words, and final `CMD_C_H=00230002`. The panel remained completely black.
Exact vendor controller enable/reset ordering and brightness placement are
therefore ruled out; the command-D result remains consistent with undriven
IO0, not a valid panel response.

A pad-handoff A/B cleared GPIO register bit 3 on P22 through P27. The complete
146-test host suite and `git diff --check` passed. The build used 759,283 bytes
of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and
110,737 bytes of LOG_STRINGS. Its 806,752-byte AP-only payload had SHA-256
`d268d1fda0653386e356f90b6649c8dd6c9cfb3e6e58957b9a69aba6080630d3`
and occupied exactly `0x132000..0x1f6f60`; exact-range flashing and reboot
succeeded, and the panel remained black. This A/B was invalid: BK7258 GPIO
output-enable is active-low, so bit 3 set means disabled. Clearing it enabled
GPIO output ownership instead of releasing the pads. Bit 3 must be restored
before further testing, and this result does not rule out a hypothesis.

The active-low GPIO output-disable bit was restored. The corrected build
passes the complete 146-test suite and was flashed safely to the same exact AP
range; its protected payload SHA-256 is
`cf3272f5d86a83dd6cc0416eebe0ba8e915bf8d81a9df751711a5f986950e786`.
The next one-variable A/B adds the remaining action from the compiled Tuya
initializer: open QSPI0's CPU1 system interrupt-router bit before controller
enable. Command completion remains polled and the QSPI NVIC line remains
disabled, avoiding an unhandled interrupt during this test. The complete
146-test suite and `git diff --check` pass. The build uses 759,283 bytes of
FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and
110,737 bytes of LOG_STRINGS. Its 806,752-byte protected AP payload has SHA-256
`4c0d3bf8987d12a570085eb397510c69cd3fc386f734d8e7bbd0de6455c27492`
and occupies exactly `0x132000..0x1f6f60`; exact-range flashing and reboot
succeeded. The live probe reports `irq=00100040`, proving QSPI0 router bit 20
is set alongside the existing I2C bit 6, while the correct active-low GPIO
disable values and expected QSPI state remain intact. The panel remained
completely black, ruling out the CPU1 system interrupt-router bit as the
missing condition.

Rechecking the register definition corrects an earlier label: bits 24..31 of
`QSPI_CONFIG` are `spi_cs_h_wait`, not the controller clock divider (which is
bits 8..15). Tuya's compiled initializer read-modify-writes the configuration
and therefore preserves this field, while current native code assigns
`0x00000009` and clears it. The preserved native setup used a value of eight.
The next isolated A/B programs `QSPI_CONFIG=0x08000009`, retaining the
otherwise current 80 MHz, FIFO, reset, IRQ-router, maximum-brightness, and
all-pixels-on configuration. This tests whether the panel requires a nonzero
chip-select-high gap between initialization commands. The complete 146-test
suite and `git diff --check` pass. The build uses 759,315 bytes of FLASH,
49,216 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes
of LOG_STRINGS. Its 806,786-byte protected AP payload has SHA-256
`c77a73bd3911b8963ef42abe2e64bece8379ed473eb110bb80f08bc7b80202fa`
and occupies exactly `0x132000..0x1f6f82`; exact-range flashing and reboot
succeeded. The post-boot USART probe confirms `cfg=08000009`,
`irq=00100040`, `glb=00000001`, safe active-low GPIO output-disable values,
and successful command completion (`st=0001100b`). The panel remained
completely black, ruling out the preserved `spi_cs_h_wait=8` field as the
missing condition.

The exact preserved native AP artifact that originally produced visible
colour stripes is now the source-versus-hardware boundary test. The stored
file has 25,451 valid 34-byte protected blocks followed by 42 erased padding
bytes; only its CRC-validated 865,334-byte prefix was replayed. That prefix has
SHA-256
`375ed67e04a8da44e7a84fcdfc10e19a73c4ba706d909470628acdb41845ce6d`,
decodes to a valid `sp=0x28066000` and `reset=0x0212ee05`, and occupies exactly
`0x132000..0x205436`. Exact-range AP-only flashing and reboot succeeded. This
older image is silent on the runtime UART and the panel remained completely
black. The exact formerly visible native binary no longer reproduces visible
output, so the active fault is below the current display source diff. Continue
with a direct GPIO-bit-banged panel-init boundary test before changing more
QSPI-controller state.

A separate USART command, `ap_cmd PBLBIT`, now implements that boundary test.
It disables QSPI0, returns P22/P23/P24 to GPIO ownership, and sends the exact
one-wire CO5300 command/address/payload byte stream in SPI mode 0 at
approximately 125 kHz. It repeats the validated reset timing and init list,
including brightness `0xff`, display-on `0x29`, and all-pixels-on `0x23`, then
leaves the GPIO pins idle for inspection. It does not replace the production
transport. The complete 146-test suite and `git diff --check` pass. The build
uses 759,827 bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of
T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 807,330-byte protected
AP payload has SHA-256
`80190b3e59221608c368d12411237da65671976432102761c4c5760235e7950e`
and occupies exactly `0x132000..0x1f71a2`; exact-range AP-only flashing and
reboot succeeded. `ap_cmd PBLBIT` completed and reported
`cfg=08000008 clk/cs/io0/rst=30301/303/30301/003`, confirming QSPI0 is
disabled, CS is idle high, CLK and IO0 are idle low, and reset is high after
the complete slow GPIO sequence. The panel remained completely black. This
rules out QSPI clocking, FIFO/DMA behavior, controller interrupts, and the
peripheral mux as the sole cause; extend the GPIO probe with a direct DCS
power-mode read before moving to panel power/reset sequencing.

`PBLBIT` now follows its write sequence with a GPIO-bit-banged DCS `0x0a`
power-mode read. It releases and samples both IO0 and IO1 for 32 clocks so the
result does not depend on an assumed single-wire return pin or byte alignment.
The complete 146-test suite and `git diff --check` pass. The build uses 760,083
bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of
T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 807,602-byte protected
AP payload has SHA-256
`4a76dfcfa69016a5d187827b0a767b3eca6c9557ba314749eec2398deddffbb6`
and occupies exactly `0x132000..0x1f72b2`; exact-range AP-only flashing and
reboot succeeded. The live readback was `rd0=00000000 rd1=ffffffff`, which is
not a valid DCS power-mode response but does not yet distinguish active drive
from floating lines retaining opposite idle levels. The next revision repeats
the read with pull-up and pull-down bias on both possible return lines.

The pull-bias revision repeats the same 32-bit IO0/IO1 capture with no pull,
pull-up, and pull-down. A driven panel response should remain stable while a
floating return follows the selected bias. The complete 146-test suite and
`git diff --check` pass. The build uses 760,339 bytes of FLASH, 49,216 bytes of
KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS.
Its 807,874-byte protected AP payload has SHA-256
`e10b6a82924cf62d60e8fcfb9c724a198987fbae7063df1d5ad1ef3477f51f4e`
and occupies exactly `0x132000..0x1f73c2`; exact-range AP-only flashing and
reboot succeeded. The live probe reported `float=00000000/ffffffff`,
`pull-up=ffffffff/ffffffff`, and `pull-down=00000000/00000000`. Both possible
return pins therefore follow their configured bias instead of presenting a
panel-driven DCS response. This rules out the earlier opposite idle levels as
a real readback value and directs the next comparison to the pinned vendor
implementation's read turnaround, lane selection, panel-enable/reset
prerequisites, and write framing.

The pinned vendor reference has no CO5300 register-read implementation, but
confirms the native write framing, P29 reset timing, mode 0, initialization
sequence, and maximum brightness exactly. The next controlled A/B therefore
stops relying on optional DCS all-pixels-on behavior: `PBLDSP` now exits that
mode and writes saturated red, green, and blue bands through the real
address-window and GRAM data path used by the formerly visible native
bring-up. The complete 146-test suite and `git diff --check` pass. The build
uses 760,467 bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of
T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 808,010-byte protected
AP payload has SHA-256
`9b1234809472fe3a76d35c423660c4caa92230f4c530e69b99b6e853183d2ede`
and occupies exactly `0x132000..0x1f744a`; exact-range AP-only flashing and
reboot succeeded. The live command reported `reinit=1 pattern=1`: all 1,864
pixel-transfer commands for the 466-row RGB pattern completed without a
controller timeout. The final state remained `glb=00000001 cfg=08000009
rst=00000000 st=0001100b`, with the expected pins and mux. The panel remained
completely black. This rules out normal UI/framebuffer contents and optional
DCS all-pixels-on behavior: even explicit saturated GRAM data is not reaching
visible panel output despite successful controller-side completion.

The next investigation checks the cold-boot boundary instead of introducing
another panel protocol variant. The exact formerly visible AP image also
stopped producing output after a complete USB power cycle, which is consistent
with a shared SoC state that an earlier vendor AP configured but native startup
does not. Review of the pinned Tuya display layer confirms that this QSPI path
does not configure a separate panel-power GPIO: its board configuration uses
`TUYA_GPIO_NUM_MAX`, while the application asserts the same active-high P19
global power latch already asserted by native startup. The unrelated generic
LCD driver's external-LDO call is therefore not part of this QSPI path.
`PBLDSP` now read-only dumps BK7258 system analog registers 0 through 27 as
`PBLANA` lines. No undocumented analog or voltage register is written. The
complete 146-test suite and `git diff --check` passed. The resulting
808,146-byte protected AP payload had SHA-256
`3badd8e90e899ccfac61c70702786fcc9ab40596581cd87f2e99682eb258d003`
and occupied exactly `0x132000..0x1f74d2`; exact-range AP-only flashing and
reboot succeeded. The captured analog register 5 value was `6c0fa76c`, whose
clear `en_lcd` bit is specifically the vendor's segment-LCD control and is
never enabled by its QSPI panel path. It is therefore not a justified write.

The remaining concrete transport difference is the production pixel stream.
Tuya forces CS low across one `32 00 2c 00` preamble and all 434,312 pixel
bytes, selecting the QSPI memory path and DMA-writing incrementing words via
the non-cacheable `0x64000000` aperture. The earlier RGB diagnostic used 1,864
separate command-C FIFO transfers. The next `PBLDSP` build reproduces Tuya's
continuous transaction with CPU stores through that same aperture, avoiding
an otherwise unnecessary new DMA subsystem. All 146 tests and
`git diff --check` pass. The build uses 760,787 bytes of FLASH, 49,248 bytes of
KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS.
Its 808,350-byte protected AP payload has SHA-256
`3268133fbbc4a8c8f93a235f58a2270dd05d6976161be0efeae8aca94b39f13b`
and occupies exactly `0x132000..0x1f759e`; exact-range AP-only flashing and
reboot succeeded. The live result was `PBLPIX continuous=1 words=108578
bytes=434312 cfg=08000009 st=0001102a`, followed by `PBLDSP ... reinit=1
pattern=1`. This proves the AP completed the entire memory-aperture stream and
restored the temporary QSPI controls. The panel displayed the expected
saturated colour stripes. This identifies the root cause: CO5300 pixel data
must remain in one uninterrupted CS-low transaction, while the former
256-byte command-C FIFO chunks did not provide a valid continuation. The
normal row-update path is now being converted to the proven continuous
aperture stream. The CO5300 brightness setter itself also ignores lower
requests and retains `0xff`, reinforcing the backlight service's forced 100%
policy. All 146 tests and `git diff --check` pass. The production-path build
uses the same memory totals and produces an 808,350-byte protected AP payload
with SHA-256
`35c4999e0503d79b3af038376bbc5d3e5a692059733a7449ce088fc33368f6e0`,
occupying exactly `0x132000..0x1f759e`. Exact-range AP-only flashing and reboot
succeeded, and a ten-second runtime UART check found no fault output. Regular
framebuffer observation was black. This is not caused by the absent external
flash: the Getafix splash logo is compiled into the AP image. Source tracing
found that `boot_splash_start()` was compiled as a no-op for normal T5AI builds;
moreover, its existing animated implementation would allocate a 67,600-byte
framebuffer from only about 56 KiB of free kernel RAM. T5AI now renders the
compiled logo and a static progress indicator through `display_update()` using
one bounded 260-byte row buffer before resource/flash startup. Build,
exact-range AP-only flash, and visible validation are pending. All 146 tests
and `git diff --check` pass. The build uses 761,363 bytes of FLASH, 49,504
bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of
LOG_STRINGS. Its 808,962-byte protected AP payload has SHA-256
`6240d077ead944138475d4cba5dab646a4c239466603fde5efc6b8ffedf38a7c`
and occupies exactly `0x132000..0x1f7802`; exact-range AP-only flashing and
reboot succeeded. The panel remained black. Because the full-frame diagnostic
stripes were visible while the same aperture transport restarted once per
932-byte row was not, the CO5300 requires the uninterrupted transaction to
cover the complete 434,312-byte frame. The normal unrotated Pebble update,
boot-frame, and clear paths now set one full-panel window and hold CS low while
scaling and streaming all 466 physical rows. Build, exact-range AP-only flash,
and visible validation were then exercised. All 146 tests and
`git diff --check` pass. The build uses 761,523 bytes of FLASH, 49,504 bytes of
KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS.
Its 809,132-byte protected AP payload has SHA-256
`099cefffa0fa4702c49a8e1d08371ab1ee3fadca69e91c99ecfad07a100a2501`
and occupies exactly `0x132000..0x1f78ac`; exact-range AP-only flashing and
reboot succeeded. The panel showed a stable full-white frame at full
brightness. This proves the normal `display_update()` callback path and
uninterrupted full-frame transfer reach panel GRAM. The remaining fault is
therefore either splash row generation or a later white frame overwriting the
splash. A safe `ap_cmd PBLSPL` mailbox probe now reruns the bounded renderer
after startup so those cases can be distinguished independently of boot
timing. All 146 tests and `git diff --check` pass. The probe build uses 761,587
bytes of FLASH, 49,504 bytes of KERNEL_RAM, 67,614 bytes of
T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 809,200-byte protected
AP payload has SHA-256
`acd536f5786181edf91c4dcbc4650cff9a3e3269f09fe79fb9a41232f4f25642`
and occupies exactly `0x132000..0x1f78f0`; exact-range AP-only flashing and
reboot succeeded. The settled runtime accepted `ap_cmd PBLSPL` and returned
`PBLSPL done`, proving the renderer completed. The panel remained solid white,
ruling out a later compositor overwrite. Source inspection found the direct
cause: `prv_panel_init()` still ended with the temporary DCS `0x23`
all-pixels-on diagnostic, which masks every GRAM update as white. The stripe
probe appeared only because it sent DCS `0x13` normal-display mode before its
frame. Production initialization now sends `0x13` followed by display-on
`0x29` and no longer enters `0x23`. All 146 tests and `git diff --check` pass.
The resulting memory totals are unchanged; the 809,200-byte protected AP
payload has SHA-256
`705c2fb06ff9925d573b15bac15380fd3f118f6e7ef063791633542e84f4c242`
and occupies exactly `0x132000..0x1f78f0`. Exact-range AP-only flashing and
reboot succeeded, and a post-startup `PBLSPL` again returned `PBLSPL done`;
the panel displayed the PebbleOS logo and static progress indicator at full
brightness. This proves panel power/reset/init, normal DCS mode, the compiled
splash row generator, GColor8-to-RGB565 conversion, logical-to-physical
scaling, and one-transaction full-frame delivery. The T5AI splash is static by
design; the next display task is to identify why partial common/application
startup does not replace it with its first compositor frame.

A subsequent clean reset without issuing `PBLSPL` also remained on the static
splash, confirming this is not merely the diagnostic redraw covering an
already-running UI. The read-only `PBLTIM` command now reports a numbered
main-initialization stage and the app manager's `appinit` and `first` flags, so
the stalled boundary can be identified without enabling writable storage or
changing runtime state.

Hardware returned `PBLTIM ... stage=6 appinit=1 first=0`: main-task
initialization completed, `launcher_main_loop()` was entered, and the app
manager initialized, but the first built-in app had not launched. Finer
read-only stage markers now bracket each launcher initialization call to
identify the exact blocker before changing startup policy.

The inner probe remained at stage 17 and never reached the marker immediately
after `shell_prefs_init()`. It was blocking while opening the settings
filesystem, which T5AI intentionally leaves uninitialized until a writable PFS
partition and recovery contract are proven. T5AI shell preferences now retain
their compiled in-RAM defaults, and settings-DB compaction migration is
skipped; neither path writes flash.

With volatile preferences, hardware advanced through shell stage 179: every
shell initializer through workout returned. It then blocked on the final
shared-PRF getting-started read/write, whose mutex and storage are deliberately
not initialized on the partial T5AI path. That persistent first-use transaction
is now skipped on T5AI, matching its volatile identity/bonding policy without
changing other platforms.

With the shared-PRF transaction gated, hardware advanced to stage 18,
confirming `shell_event_loop_init()` returned. It then blocked in
`services_set_runlevel()` because that function still invoked setters for
common and normal services omitted from partial T5AI startup. T5AI now applies
runlevel only to its initialized light and Bluetooth-control services; the
accel-manager, vibe-service, HRM-manager, and normal-service runlevel table stay
gated until their corresponding initialization is enabled.

The first runlevel-gated build asserted in `cron_job_unschedule()` (captured LR
`0x0217682b`) because `light_allow()` depends on the cron service, which is also
outside partial T5AI startup. T5AI already forces the panel and backlight to
100%, so its light runlevel setter is gated too; only initialized Bluetooth
control remains in the T5AI common runlevel table.

A corrected runlevel-table build still asserted at the same cron boundary
because the following synthetic `light_button_pressed()`/`released()` boot
gesture independently uses cron. That synthetic gesture is skipped on T5AI,
where brightness is already forced to 100%; physical input initialization and
other platforms are unchanged.

A following build still reached `cron_job_unschedule()` from another shell
component. Cron is a self-contained RAM/new-timer service with no flash
dependency, and several normal-shell components legitimately assume it was
initialized by `services_common_init()`. Partial T5AI startup now explicitly
calls `cron_service_init()` before light/compositor/launcher use while keeping
writable-storage services gated.

With cron initialized, hardware advanced to `stage=21 appinit=1 first=0`,
isolating the next block to `app_manager_start_first_app()`. Process setup was
unconditionally opening a per-app persistence client despite the deliberately
disabled T5AI persist service and writable PFS. T5AI now skips the symmetric
persistence-client open/close and keeps system-app state volatile. The
807,194-byte AP-only build (SHA-256
`2ac85c0feffe8aba65c47871c967a09bc3cec36b4ceb8a34fdf6b55042d6e1a7`,
range `0x132000..0x1f711a`) flashed successfully and advanced to
`stage=22 appinit=1 first=1`, proving first-app setup and task creation return.

The launcher additionally assumed initialized app-order and AppDB storage.
T5AI now uses compiled ordering and enumerates only the built-in application
registry, with no PFS or AppDB access. Independent kernel and app counters were
added to remove scheduling-order ambiguity. The current 807,024-byte AP-only
image (SHA-256
`e655f4b91a13d692b1b26d5b310528b3f7c0600794d9f2ac37bcac073c27e720`,
range `0x132000..0x1f7070`) flashed successfully and reports
`stage=22 appstage=0 appinit=1 first=1`. The app task has not executed its
entry function, so the frozen splash is now isolated to FreeRTOS task
scheduling/context entry, before app-state initialization, launcher menu
loading, compositor rendering, or display transport. The next check is the
live app task state/priority followed by the T5AI CM33 restricted-task
context/MPU path.

Extended `PBLTIM` telemetry showed that the app task was valid, continuously
Ready at priority 2, and had almost its entire stack available. Priority-3
KernelMain was not blocking because the partial T5AI service set kept an event
queue continuously selectable. A T5AI-only one-tick delay let the app execute
and reach `appstage=201`, ruling out task creation, privilege, MPU, and initial
context restore. Fine checkpoints progressed to `appstage=316`, exactly before
the touch-navigation subscription. That call requires the touch service mutex,
but partial startup had omitted the RAM-only `touch_init()` normally performed
by `services_common_init()`. T5AI now initializes touch with cron and light
before compositor and launcher use.

The resulting 807,398-byte protected AP payload has SHA-256
`adc63b2858f3c012b53690efbf19f35f139ef5e31f21d172a811b1f831c66df2`
and occupies exactly `0x132000..0x1f71e6`. All 146 host tests and
`git diff --check` passed; exact-range AP-only flashing and reboot succeeded.
Hardware advanced beyond touch initialization and displayed a full-brightness
PebbleOS charging animation. This is the first confirmed live compositor UI,
so panel power, initialization, framebuffer conversion, full-frame USART/QSPI
delivery, and normal compositor presentation are working beyond the static
boot splash.

Two later reads advanced from `appstage=325` to `307`, across the 67,614-byte
application framebuffer clear, while the task remained Ready with a 955-word
stack high-water mark. It is progressing extremely slowly rather than faulting:
one tick of kernel delay required tens of seconds for a single framebuffer
clear. The next build increases the T5AI-only compatibility pause to ten ticks,
enough for useful app/compositor execution while remaining bounded. Once the
launcher is interactive, trace the event source that keeps KernelMain's queue
continuously selectable and restore the normal blocking event-loop behavior.

The ten-tick build reached launcher checkpoint 222 within seconds. Finer
instrumentation isolated the next stop to built-in app insertion: the launcher's
Music/Workout ordering override consults `app_install_is_prioritized()`, whose
recent-app recursive mutex had not been initialized because normal services are
still gated. T5AI now calls `app_install_manager_init()` after the system event
service is ready. App enumeration remains built-in-only, and AppDB/PFS access
remains disabled.

That fix advanced through app-list construction and exposed the next resource
boundary. Launcher glance initialization asserted at LR `0x02139493`, mapped
to a NULL generic glance reel because the external system resource bank is not
yet validated. The T5AI launcher therefore uses the existing basic app-menu
row renderer and skips glance reels/animations; builds for every other platform
retain the full glance launcher.

All 146 tests and `git diff --check` pass for the basic launcher. The resulting
799,170-byte protected AP image has SHA-256
`428695001028a4aac2a43d6f8de330d4cbe54b72780f7e0597ce3e961f574579`
and occupies exactly `0x132000..0x1f51c2`. Exact-range AP-only flashing and
reboot succeeded. Hardware reports
`stage=22 appstage=215 appinit=1 first=1 apptask=1 state=1 prio=2 stack=769`.
Checkpoint 215 is immediately before the non-returning `app_event_loop()`, so
the launcher window, built-in app list, basic menu layer, idle timer, and app
event loop are live. A visible-screen observation for this exact image remains
to be recorded; the preceding image displayed the full-brightness charging
animation, which can be a kernel modal above the running launcher.

Development power and both UARTs share the same USB-C cable, so the board cannot
be observed while unplugged and the charging modal continuously obscures the
launcher. The T5AI shell now preserves battery monitoring but suppresses battery
UI state transitions until a battery is fitted; other platforms are unchanged.
All 146 tests and `git diff --check` pass. The resulting 799,000-byte protected
AP payload has SHA-256
`9f8f1b569d4ac256fc4c29ef3949eaf1c8399cadd536b00d069eb43f360fbf93`
and occupies exactly `0x132000..0x1f5118`. Exact-range AP-only flashing and
reboot succeeded. Runtime reports `appstage=215 state=2 stack=602`, the expected
steady state for the launcher blocked in `app_event_loop()` with substantial
stack headroom. The panel displayed the PebbleOS launcher menu at full
brightness. This confirms the complete normal UI path from splash replacement
through compositor presentation, built-in launcher rendering, and steady-state
app event handling. Physical button/touch navigation is the next validation
step before diagnostic checkpoints are removed or the temporary scheduling
pause is reduced.

The first USART navigation attempt sent Pebble prompt text through the CP's
`ap_cmd` shell, but T5AI intentionally exposes only a bounded mailbox probe
surface rather than the unrestricted Pebble prompt. The new `PBLB0` through
`PBLB3` probes map to Back, Up, Select, and Down and request one 120 ms press
through Pebble's shared remote-input service. The first probe build exposed one
more partial-startup dependency: `remote_input_init()` had been omitted with
`services_common_init()`, so its mutex was NULL and `PBLB3` reset the AP. T5AI
now initializes that RAM/timer-only service immediately after the system event
service, without enabling writable storage or Bluetooth.

The corrected source passes all 146 tests and `git diff --check` and builds
cleanly. Its 799,204-byte protected AP payload has SHA-256
`5011c77c55832312fdd95832c609e7d8b3ca4bf37e1679d62ee7bbdeb67c0d91`
and occupies exactly `0x132000..0x1f51e4`. Exact-range AP-only flashing and
reboot succeeded. Runtime remained at the healthy launcher state and returned
`PBLB3 result=0`, proving that a timed Down press was admitted into Pebble's
real event queue. Visible row movement remains to be confirmed on the panel.

The CP can print repeated `bk_ipc_crc_check crc check failed` diagnostics even
while every transaction succeeds. The native implementation intentionally
matches the pinned vendor behavior and continues dispatch: the mutable
list/result fields can change while the CRC is being checked. Treat a failed or
missing logical reply as an error; do not treat this CP line alone as a failed
transaction.

## Bluetooth persistence

The T5AI build currently uses a volatile Pebble Bluetooth persistence backend.
It keeps identity, bond, and capability state in RAM and seeds per-boot root
keys from the AP TRNG. A reboot loses bonds and restores the default name.

Shared PRF remains read-only because its partition, compaction, corruption
recovery, and factory-recovery policy are not validated. Persistent bonding
must not be enabled merely to advance pairing tests. First define and test the
safe writable-storage and recovery contract.

## Other hardware notes

- The AMOLED has no conventional backlight. Brightness is controlled by the
  CO5300 panel command. Native display bring-up produced colour stripes and
  confirmed display off/on behavior.
- The board under test has no battery fitted. A USB-powered ADC result around
  4.0 V is the unloaded charger node, not a cell measurement or battery
  validation.
- The active-high P19 power latch is asserted early. Active-low amplifier P28
  remains disabled because audio is not implemented.
- Missing vibration, ambient light, temperature, magnetometer, microphone,
  PMIC, and OTP hardware are represented by explicit unavailable or safe
  fallback implementations rather than inherited DVT2 drivers.
- Touch and accelerometer polling are proven, but full end-to-end application
  delivery still depends on completing the normal common-service startup.

## Validation

The focused host baseline is:

```sh
cd /home/baz/coredevices/pebbleos
.venv/bin/python -m unittest \
  tools.tests.test_getafix_boards \
  tools.tests.test_t5ai_boot_probe \
  tools.tests.test_tuya_flash \
  tools.tests.test_tuya_hardware \
  tools.tests.test_tuya_image \
  tools.tests.test_tuya_image_packaging \
  tools.tests.test_tuya_kernel \
  tools.tests.test_tuya_memory
```

The latest 2026-09-12 run passed all 146 tests. Finish a handoff with:

```sh
git diff --check
git status --short --branch
```

## Next work

Continue in this order:

1. Confirm visible launcher movement from the acknowledged `PBLB3` probe, then
   validate physical Back/Select GPIOs and touch Up/Down navigation end to end.
2. Remove temporary startup checkpoints and identify the event source requiring
   the T5AI-only ten-tick KernelMain scheduling pause.
3. Pair from BlueZ, validate encryption and the security callbacks, then
   disconnect and reconnect within the same boot.
4. Exchange real Pebble protocol data over the `fed9` characteristics and
   verify notifications, writes, MTU negotiation, and sustained ACL ordering.
5. Design and validate persistent bonding storage only after shared-PRF write,
   compaction, corruption recovery, and full-board recovery are safe.
6. Enable the remaining common services and validate accelerometer and broader
   Pebble application behavior together.
7. Validate sleep/wake, Bluetooth low-power operation, and peripheral resume.
8. Fit a battery and validate voltage calibration, charging transitions,
   retention, and state of charge against external measurements.
9. Leave OTA, resource-bank writes, and general flash mutation until the image,
   rollback, and recovery design is complete.

## Source and reference map

Key native files:

- `src/fw/drivers/t5ai/cp_mailbox.c`: physical mailbox ownership and ACK
  serialization.
- `third_party/nimble/transport/t5ai_hci_ipc.c`: MIPC/HCI framing, callback
  ownership, and receive FIFO.
- `src/fw/services/bluetooth/bluetooth_persistent_storage_t5ai.c`: volatile
  T5AI Bluetooth persistence.
- `src/fw/main.c`: partial T5AI startup path.
- `boards/getafix/defconfig.dvt3`: DVT3 feature selection.
- `boards/getafix/tuya/REMAINING_TASKS.md`: detailed chronological handoff and
  remaining checklist.

Pinned vendor references are under
`/home/baz/Documents/ChatGPT/pebble/.vendor/tuyaopen`; revisions are recorded in
`boards/getafix/tuya/sdk.lock.json`. Useful mailbox comparisons include
`bk_api_ipc.c`, `tkl_ipc.c`, `tkl_hci_0.c`, and `tkl_hci_1.c` in that pinned
tree.

Two matching 8 MiB factory reads are stored as
`factory-backups/t5e1-factory-5C75025844-read1.bin` and `read2.bin` below the
reference workspace. Both have SHA-256
`56c7d0170c47db35290397f74ab5709fd8e51e7dc45f76147bd3cb377f1a8cfd`.
The known-good vendor diagnostic is
`build/getafix_dvt3_hw/known-good/getafix_dvt3_hw_QIO_1.0.0.bin` in that same
reference workspace, with SHA-256
`edbea99920b61b87c1bf56f46cb975e38793e55532b5d75da3e9662db143bd4a`.
