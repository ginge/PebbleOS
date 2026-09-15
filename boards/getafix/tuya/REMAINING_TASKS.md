# Getafix DVT3 native port: hardware validation and remaining tasks

This is the continuation handoff for the native PebbleOS port to the Waveshare
T5-E1 Touch AMOLED 1.75 board. Continue in the only valid source checkout,
`/home/baz/coredevices/pebbleos`. The similarly named directory under
`/home/baz/Documents/ChatGPT/pebble` is reference and artifact storage, not a
PebbleOS source tree.

## Current priority: stop unsafe phone sync and replace mailbox serialization

The 2026-09-14 watchface-sync session ended in a CP flash-write failure and a
dangerous AP reboot loop. Pairing, encryption, MTU 256, reversed PPoG, and
session setup are proven, and the phone now recognizes the identity as Round 2,
but sustained BlobDB/PFS sync is not proven. The final trace reached a 914-byte
endpoint packet before `flash_impl_write_page_begin` returned `E_ERROR`; each
restart then failed filesystem recovery erase with `E_INVALID_OPERATION` while
the CP rejected a QSPI power/clock request with `E_AGAIN`.

Stop using the older priority statement below as the active plan. Physically
power-cycle both cores before the next session, then implement the T5AI-local
per-channel mailbox dispatcher and flash fault state described in
`docs/boards/getafix_dvt3_phone_sync_incident.md`. Do not rebuild, flash, sync,
or reformat PFS until that plan is reviewed. PFS reformat is destructive and
requires explicit operator approval after the exact partition range is checked.

The shared event loop and app manager are stock, the app is priority 1, and no
T5AI scheduler guard exists in the current tree. Do not reintroduce an app
priority-3 or polling-task workaround. Preserve large protocol objects and
normal OS endpoints.

## Superseded Bluetooth checkpoint history

- Bluetooth pairing, encrypted GATT discovery, reversed PPoG, and reconnect are
  now hardware-proven with persistent Pebble-side bond metadata. The phone app
  reports the watch as `connected(factory)` with an unknown platform; resolving
  the advertised/device platform identity is the next Bluetooth task.
- The final successful run remained up through pairing, encryption, complete
  GATT discovery, reversed-PPoG subscription/open, music-service connection,
  and the transition to normal connection parameters. No temporary trace,
  mutex, allocator, ATT, timer, or music diagnostics remain in production code.
- The shared NimBLE GATT discovery implementation is stock. DVT3 selects a
  T5AI-local streaming implementation that emits and frees each completed
  service during discovery, avoiding a large transient kernel-heap peak.
- The current touch path is interrupt-driven while idle. GPIO42 is the active-low
  CST9217 interrupt and GPIO43 is reset, as shown by the official Waveshare
  schematic. The Tuya board file's `BOARD_TP_RST_PIN=42` is inconsistent with
  that schematic and must not be copied back into the native driver.
- Shared `src/fw/drivers/i2c/common.c` is stock. Its current and `HEAD` SHA-256
  are both `9b17cfe654ff6c389a20c13d957152bd2ff6c75961bf33a8deb36dcc89e040f5`.
  CST92xx retry/reset policy stays inside the touch driver; do not modify the
  common I2C layer to compensate for this panel.
- The CP shell/runtime USART is `if02` (normally `ttyACM1`) at 460800 baud;
  `if00` (normally `ttyACM0`) is the programming/reset interface. Both are on
  the same USB-C dual-UART bridge and disappear when the board is unplugged.
- The first `PBLDSP` command was unreachable because it has the same eight-byte
  request length as `PBLTIM`, while the parser selected the timebase branch by
  length before comparing the display request. The parser now compares both
  requests inside that shared-length branch. The diagnostic itself is
  read-only and reports display state plus QSPI, clock, gate, P19 power-latch,
  and P29 panel-reset registers.
- The corrected source passes all 78 focused tests and builds successfully. Its
  906,100-byte protected AP payload has SHA-256
  `078c16aa87c95140aa3c2cabacec1a1db22f482c3b9bc1c7d77c81cd16cf6d8c`
  and occupies exactly `0x132000..0x20f374`. It has not been flashed: both USB
  serial interfaces disappeared immediately before programming. After the
  shared cable is replugged, flash only that exact AP range, then send
  `ap_cmd PBLDSP` on `if02` at 460800 baud and record the complete response.
- That image was subsequently flashed successfully. Its read-only response was
  `PBLDSP i/e/u=1/1/0 glb=00000001 cfg=08000009 rst=00000000 st=0001100a
  clk=05000570 gate=87188491 p19=00000103 p29=00000003`. This proves the
  driver considered initialization successful, QSPI was enabled with an empty
  FIFO and completed-command status, the FIFO reset was clear, and both the
  board power latch and released panel reset were physically high.
- The next on-demand diagnostic retries the CP QSPI power vote after boot,
  reconfigures QSPI, and repeats the exact vendor reset/init/all-pixels-on
  sequence. It also passes all 78 tests and builds with 852,947 bytes of FLASH,
  82,464 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 119,266
  bytes of LOG_STRINGS. Its 906,270-byte payload has SHA-256
  `7b155c26591ddc9d75dba4c5f42c9ae9da6ab45e92a01795253fe7fec543f920`
  and occupies exactly `0x132000..0x20f41e`. The exact-range flash succeeded.
  Its response was `PBLDSP bootpm=0 nowpm=0 reinit=1 i/e/u=1/1/0
  glb=00000001 cfg=08000009 rst=00000000 st=0001100a clk=05000570
  gate=8f188491 p19=00000103 p29=00000003`. Both the early and late CP power
  votes succeeded, so CP service readiness is not the cause. Visual observation
  after this live reinitialization confirmed no flash or visible output.
- A deeper diagnostic adds the vendor-framed command-D power-mode read and
  dumps the AP power-domain, QSPI pin, and pin-mux registers. It passes all 78
  tests and builds with 853,235 bytes of FLASH. Its 906,576-byte payload has
  SHA-256
  `54a178ac86d25fedda4105aff062f64d4b99780e3527a27d6af9df1a75f83abf`
  and occupies exactly `0x132000..0x20f550`; exact-range flashing succeeded.
  Three repeated probes returned the identical result: `rd=1 pm=ff
  raw=bce9bdff`, `mem=00000000/00000000 sleep=200000c0`, QSPI GPIO values
  `70348/378/30378/150378/150378/150378`, and mux words
  `33000000/00003333`. The controller raises RX-done, but `0xff` is not a
  valid CO5300 power-mode response and the complete FIFO word is unchanged
  across resets. The AP power domain, clock, QSPI GPIO configuration, and mux
  match the pinned vendor implementation. The panel is not electrically
  answering; do not mistake controller completion for panel acknowledgement.
- The earlier short-command experiment was invalid because its normal-firmware
  startup never called `display_init()`. A valid A/B build now uses the pinned
  dedicated BK LCD driver's inline command-C encoding for zero-to-four-byte
  register writes while retaining FIFO mode for pixel payloads. All 78 tests
  pass. Its 906,576-byte payload has SHA-256
  `0c705b0de7ecb10067d574455c996b880d1357e7d855f1565d23501fb144d5af`
  and occupies `0x132000..0x20f550`; exact-range flashing succeeded. The live
  reinitialization completed with `reinit=1`, but the panel read remained
  `pm=ff raw=beedfdff` and all power/mux registers were unchanged. Visible
  output from this valid inline-framing test remained completely black.
- A final software-transport isolation build bypasses the QSPI controller,
  FIFO, pin mux, and both command-packing variants. It temporarily drives CLK
  P22, CS P23, and DATA/IO0 P24 as GPIO, performs the exact vendor panel-reset
  timing, bit-bangs the four-byte command header and initialization sequence,
  then sends sleep-out, display-on, and all-pixels-on. All 78 focused tests
  pass. The build uses 853,683 bytes of FLASH, 82,464 bytes of KERNEL_RAM,
  67,614 bytes of T5AI_DISPLAY_RAM, and 119,266 bytes of LOG_STRINGS. Its
  907,052-byte protected payload has SHA-256
  `eaee4df317032ef3e5e66bef88a4882738ee16522e1f60dddcb719bf32baa46d`
  and occupies exactly `0x132000..0x20f72c`; exact-range flashing succeeded.
  The live probe completed with `PBLBIT done=1 clk=00030301 cs=00000303
  data=00030301 reset=00000003`, after the hardware path again returned
  `reinit=1 rd=1 pm=ff raw=bfe9ffff`. The screen remained black. Software has
  now driven the panel through two valid QSPI command encodings and an
  independent bit-banged transport without visible output; stop changing the
  display protocol blindly. The next diagnostic must inspect the physical
  panel path: reseat/inspect the display FPC and connector, verify panel rails,
  and scope P22/P23/P24 plus P29 during `PBLDSP`. This physical-fault
  conclusion was subsequently disproved by the stock vendor firmware working
  on the same board; retain the experiment as evidence only.
- A non-protocol pad-sense follow-up enables each BK7258 input sampler while
  its push-pull output remains active, drives QSPI CLK, CS, and DATA/IO0 both
  low and high, and reports the two samples. All 78 focused tests pass. The
  build uses 853,875 bytes of FLASH, 82,464 bytes of KERNEL_RAM, 67,614 bytes
  of T5AI_DISPLAY_RAM, and 119,266 bytes of LOG_STRINGS. Its 907,256-byte
  protected payload has SHA-256
  `cf2ace1d5b42618a0abbaceb15a43231976530922ee4ec42f15a9c942cc57d58`
  and occupies exactly `0x132000..0x20f7f8`; exact-range flashing succeeded.
  The live result was `PBLPAD low/high clk=2 cs=2 data=2 expected=2`. A value
  of 2 means the pad sampled low after a low drive and high after a high drive.
  All three output pads therefore follow the AP commands; this rules out a
  stuck output latch or disabled GPIO pad. The working stock vendor firmware
  proves the remaining fault is in native software configuration.
- Recomparison against the exact Tuya `tkl_qspi_init()` path found that its
  80 MHz case selects the 480 MHz source with system divider 5 and leaves the
  QSPI controller `clk_div` field at bits 8..15 zero. The high byte in live
  `cfg=08000009` was initially misidentified as that divider; it is actually
  `spi_cs_h_wait`. The next build restores the exact
  generic Tuya CO5300 command path (FIFO parameter bytes and `CFG1=0x300`), sets
  controller config to `0x00000009`, and removes the bit-bang/pad test so the
  diagnostic leaves QSPI ownership intact. All 78 focused tests pass. The
  build uses 853,203 bytes of FLASH, 82,464 bytes of KERNEL_RAM, 67,614 bytes
  of T5AI_DISPLAY_RAM, and 119,266 bytes of LOG_STRINGS. Its 906,542-byte
  protected payload has SHA-256
  `94ae5985ed3a66013af611bd01d6972e6f462fc3938fe10f2de1d2e5469694a8`
  and occupies exactly `0x132000..0x20f52e`; exact-range flashing and reboot
  succeeded. The live probe confirms `cfg=00000009` and `reinit=1`; the
  command-D read still returns noncredible `pm=ff raw=bfedffff`. Visible-output
  observation is pending.
- That build was not yet fully vendor-equivalent: the live `glb=00000001`
  exposed that native code left QSPI `GLB_CTRL.bit0`, the active controller soft
  reset, asserted. Tuya's generic `qspi_hal_init()`/`qspi_ll_init_common()` does
  not assert that bit, and the earlier native display-bring-up revision at
  `052392be5` explicitly pulsed it from 1 back to 0. The next build adds that
  reset deassertion while retaining `cfg=00000009`, FIFO parameters, and
  `RST_CFG=0`. All 78 focused tests pass. The 906,542-byte protected payload
  has SHA-256
  `a07590d77788662e5bc98fa75ac61490c53ce6a2adf87627a925cc1597af7c03`
  and occupies exactly `0x132000..0x20f52e`; exact-range flashing and reboot
  succeeded. The live probe confirms `glb=00000000 cfg=00000009 rst=00000000
  reinit=1`; command-D still reads invalid `pm=ff raw=bee9ffff`. Visible-output
  observation confirmed the panel remained black.
- Git history identifies `05292094e` as the display regression boundary: it
  simultaneously changed the preceding native bring-up from 40 to 80 MHz,
  inline to FIFO short commands, released to asserted soft reset, and
  `RST_CFG.bit1` set to clear. The immediately preceding diagnostic in
  `bc29e5967` forced full brightness and all pixels on through the 40 MHz
  inline-command path. The focused A/B restores that complete earlier
  transport/register state, retains explicit brightness `0xff` and command
  `0x23`, and does not copy or link vendor code. All 78 focused tests pass and
  `git diff --check` passes. The build uses 853,267 bytes of FLASH, 82,464
  bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 119,266 bytes of
  LOG_STRINGS. Its 906,610-byte protected payload has SHA-256
  `0e098ebe7088e44ac3a6a54b3821e7f9e4a68c4738d4fac3d1f77e2ee7fda729`
  and occupies exactly `0x132000..0x20f572`; exact-range flashing and reboot
  succeeded. The live probe reports `bootpm=0 nowpm=0 reinit=1 rd=1 pm=ff
  raw=befdbdff i/e/u=1/1/0 glb=00000000 cfg=00000009 rst=00000002
  st=0001100b` and clock register `050006f0`, confirming the intended
  historical state on hardware. Visible-output observation confirmed the panel
  remained completely black. The four display-driver changes bundled into
  `05292094e` are therefore not the sole regression cause; compare the complete
  known-working native bring-up state against current power, clock, pinmux, and
  startup code before another flash.
- Read-only disassembly of the preserved native artifact
  `known-good-native-cpu1_AP.bin` found that its executed transport does not
  match the nearby source history: it uses FIFO framing and `CFG1=0x300` for
  every command length, while retaining the 40 MHz system divider, global-reset
  pulse/release, and `RST_CFG.bit1`. It also sends brightness `0xff` after
  sleep-out. The next A/B matches that actual machine-code combination. All 78
  focused tests and `git diff --check` pass. The build uses 853,235 bytes of
  FLASH, 82,464 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and
  119,266 bytes of LOG_STRINGS. Its 906,576-byte protected payload has SHA-256
  `f639c7718992f7bba361881820b70f501f14d2c01debbf5eeb9d3ecb99567447`
  and occupies exactly `0x132000..0x20f550`; exact-range flashing and reboot
  succeeded. Visible-output observation is pending; do not issue the QSPI read
  probe until that observation is recorded.
- During display bring-up, the T5AI backlight driver now forces every requested
  level, including zero/off requests from the light service, to 100 percent and
  sends CO5300 brightness `0xff`. Restore dynamic brightness only after the
  panel produces reliable visible output. All 78 focused tests and `git diff
  --check` pass. The 906,576-byte protected payload has SHA-256
  `be7f894052087a91219248a27e392e9bc6e45b43e28c922a4c56b2cc83e664d7`
  and occupies exactly `0x132000..0x20f550`; exact-range flashing and reboot
  succeeded. The panel remained completely black, ruling out light-service
  dimming and CO5300 brightness level as the cause.
- The runtime USART immediately filled with binary HCI traffic after that
  observation, preventing a reliable `PBLDSP` capture. DVT3 now selects
  `CONFIG_BT_FW_STUB=y` for the panel diagnostic build; restore NimBLE only
  after visible panel output is recovered. `PBLDSP` now also dumps every QSPI
  register from offset `0x00` through `0x7c` in `PBLREG` lines.
- Read-only disassembly of the exact working Tuya ELF confirms its QSPI0 pin
  calls map P22/P23/P24/P25/P26/P27 to function IDs
  `0xad/0xae/0x64/0x65/0x66/0x67`. The vendor GPIO mapping table places each
  of those board-specific IDs at mux index 3, confirming native mux words
  `33000000/00003333` are correct. The same disassembly confirms Tuya calls
  `qspi_hal_enable_soft_reset()`, which sets `GLB_CTRL.bit0`; do not describe
  a live value of one as proof that the vendor controller is held in reset.
  The Bluetooth-quiet diagnostic passes all 78 focused tests and builds with
  759,315 bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of
  T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 806,786-byte
  protected AP payload has SHA-256
  `53e6fc492d142b44376ddb520a1c8a62e20628e278e6a684a206714e234333ae`
  and occupies exactly `0x132000..0x1f6f82`; exact-range flashing and reboot
  succeeded. The clean probe returned `reinit=1 rd=1 pm=ff raw=bfedfdff`,
  `glb=0 cfg=9 rst=2 st=0001100b`, and valid QSPI device ID `51535049`.
  `PBLREG` shows no command-failure status and the final command-C state is
  `CMD_C_H=00230002`, the all-pixels-on command. The panel still does not
  drive command-D IO0. The screen remained completely black, ruling out
  Bluetooth runtime contention.
- The next A/B uses the exact compiled Tuya controller ordering, not merely its
  final register values: configure/enable QSPI, select 80 MHz, program
  command-A, then set `GLB_CTRL.bit0` and leave `RST_CFG=0`. It also restores
  brightness `0xff` to its exact vendor position before sleep-out while
  retaining the final all-pixels-on probe. This tests whether enabling the
  controller only after its register programming was the missing condition.
  All 78 focused tests and `git diff --check` pass. The build uses 759,283
  bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of
  T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 806,752-byte
  protected AP payload has SHA-256
  `5efc66cb220bd5baa9cbd165eb277ff0c3b2530d7aeb398790ea9cb3fb207ffd`
  and occupies exactly `0x132000..0x1f6f60`; exact-range flashing and reboot
  succeeded. The clean runtime probe reports `reinit=1 rd=1 pm=ff
  raw=bcedfdff`, `glb=1 cfg=9 rst=0 st=0001100b`, clock register `05000570`,
  the expected mux words, and final `CMD_C_H=00230002`. The screen remained
  completely black. Exact vendor controller enable/reset ordering and the
  vendor brightness position are therefore ruled out as the active fault; the
  command-D value remains a floating-bus result rather than a valid panel ID.
- A pad-handoff A/B cleared GPIO register bit 3 on P22 through P27. The
  complete 146-test host suite and `git diff --check` passed. The build used
  759,283 bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of
  T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 806,752-byte
  protected AP payload had SHA-256
  `d268d1fda0653386e356f90b6649c8dd6c9cfb3e6e58957b9a69aba6080630d3`
  and occupied exactly `0x132000..0x1f6f60`; exact-range flashing and reboot
  succeeded, and the panel remained black. This A/B was invalid: BK7258 GPIO
  output-enable is active-low, so bit 3 set means disabled. Clearing it
  re-enabled GPIO output ownership instead of releasing the pads. Restore bit
  3 before any further panel test; do not use this result to rule out a
  hypothesis.
- The active-low GPIO output-disable bit was restored. The corrected build
  passes the complete 146-test suite and was flashed safely to the same exact
  AP range; its protected payload SHA-256 is
  `cf3272f5d86a83dd6cc0416eebe0ba8e915bf8d81a9df751711a5f986950e786`.
  The next one-variable A/B adds the remaining compiled Tuya initialization
  action: open QSPI0's CPU1 system interrupt-router bit before controller
  enable. Command completion remains polled and the QSPI NVIC line remains
  disabled, avoiding an unhandled interrupt while testing whether the system
  route participates in controller operation. The complete 146-test suite and
  `git diff --check` pass. The build uses 759,283 bytes of FLASH, 49,216 bytes
  of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of
  LOG_STRINGS. Its 806,752-byte protected AP payload has SHA-256
  `4c0d3bf8987d12a570085eb397510c69cd3fc386f734d8e7bbd0de6455c27492`
  and occupies exactly `0x132000..0x1f6f60`; exact-range flashing and reboot
  succeeded. The live probe reports `irq=00100040`, proving QSPI0 router bit 20
  is set alongside the existing I2C bit 6, while the correct active-low GPIO
  disable values and expected QSPI state remain intact. The panel remained
  completely black, ruling out the CPU1 system interrupt-router bit as the
  missing condition.
- Rechecking the register definition corrects an earlier label: bits 24..31 of
  `QSPI_CONFIG` are `spi_cs_h_wait`, not the controller clock divider (which is
  bits 8..15). Tuya's compiled initializer read-modify-writes the configuration
  and therefore preserves this field, while current native code assigns
  `0x00000009` and clears it. The preserved native setup used a value of eight.
  The next isolated A/B programs `QSPI_CONFIG=0x08000009`, retaining the
  otherwise current 80 MHz, FIFO, reset, IRQ-router, maximum-brightness, and
  all-pixels-on configuration. This tests whether the panel requires a
  nonzero chip-select-high gap between initialization commands. The complete
  146-test suite and `git diff --check` pass. The build uses 759,315 bytes of
  FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and
  110,737 bytes of LOG_STRINGS. Its 806,786-byte protected AP payload has
  SHA-256
  `c77a73bd3911b8963ef42abe2e64bece8379ed473eb110bb80f08bc7b80202fa`
  and occupies exactly `0x132000..0x1f6f82`; exact-range flashing and reboot
  succeeded. The post-boot USART probe confirms `cfg=08000009`,
  `irq=00100040`, `glb=00000001`, safe active-low GPIO output-disable values,
  and successful command completion (`st=0001100b`). The panel remained
  completely black, ruling out the preserved `spi_cs_h_wait=8` field as the
  missing condition.
- The exact preserved native AP artifact that originally produced visible
  colour stripes is now the source-versus-hardware boundary test. The stored
  file has 25,451 valid 34-byte protected blocks followed by 42 erased padding
  bytes; only its CRC-validated 865,334-byte prefix was replayed. That prefix
  has SHA-256
  `375ed67e04a8da44e7a84fcdfc10e19a73c4ba706d909470628acdb41845ce6d`,
  decodes to a valid `sp=0x28066000` and `reset=0x0212ee05`, and occupies
  exactly `0x132000..0x205436`. Exact-range AP-only flashing and reboot
  succeeded. This older image is silent on the runtime UART and the panel
  remained completely black. The exact formerly visible native binary no
  longer reproduces visible output, so the active fault is below the current
  display source diff. Continue with a direct GPIO-bit-banged panel-init
  boundary test before changing more QSPI-controller state.
- A separate USART command, `ap_cmd PBLBIT`, now implements that boundary
  test. It disables QSPI0, returns P22/P23/P24 to GPIO ownership, and sends the
  exact one-wire CO5300 command/address/payload byte stream in SPI mode 0 at
  approximately 125 kHz. It repeats the validated reset timing and init list,
  including brightness `0xff`, display-on `0x29`, and all-pixels-on `0x23`,
  then leaves the GPIO pins idle for inspection. It does not replace the
  production transport. The complete 146-test suite and `git diff --check`
  pass. The build uses 759,827 bytes of FLASH, 49,216 bytes of KERNEL_RAM,
  67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its
  807,330-byte protected AP payload has SHA-256
  `80190b3e59221608c368d12411237da65671976432102761c4c5760235e7950e`
  and occupies exactly `0x132000..0x1f71a2`; exact-range AP-only flashing and
  reboot succeeded. `ap_cmd PBLBIT` completed and reported
  `cfg=08000008 clk/cs/io0/rst=30301/303/30301/003`, confirming QSPI0 is
  disabled, CS is idle high, CLK and IO0 are idle low, and reset is high after
  the complete slow GPIO sequence. The panel remained completely black. This
  rules out QSPI clocking, FIFO/DMA behavior, controller interrupts, and the
  peripheral mux as the sole cause; extend the GPIO probe with a direct DCS
  power-mode read before moving to panel power/reset sequencing.
- `PBLBIT` now follows its write sequence with a GPIO-bit-banged DCS `0x0a`
  power-mode read. It releases and samples both IO0 and IO1 for 32 clocks so
  the result does not depend on an assumed single-wire return pin or byte
  alignment. The complete 146-test suite and `git diff --check` pass. The
  build uses 760,083 bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes
  of T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 807,602-byte
  protected AP payload has SHA-256
  `4a76dfcfa69016a5d187827b0a767b3eca6c9557ba314749eec2398deddffbb6`
  and occupies exactly `0x132000..0x1f72b2`; exact-range AP-only flashing and
  reboot succeeded. The live readback was `rd0=00000000 rd1=ffffffff`, which
  is not a valid DCS power-mode response but does not yet distinguish active
  drive from floating lines retaining opposite idle levels. The next revision
  repeats the read with pull-up and pull-down bias on both possible return
  lines.
- The pull-bias revision repeats the same 32-bit IO0/IO1 capture with no pull,
  pull-up, and pull-down. A driven panel response should remain stable while a
  floating return follows the selected bias. The complete 146-test suite and
  `git diff --check` pass. The build uses 760,339 bytes of FLASH, 49,216 bytes
  of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of
  LOG_STRINGS. Its 807,874-byte protected AP payload has SHA-256
  `e10b6a82924cf62d60e8fcfb9c724a198987fbae7063df1d5ad1ef3477f51f4e`
  and occupies exactly `0x132000..0x1f73c2`; exact-range AP-only flashing and
  reboot succeeded. The live probe reported
  `float=00000000/ffffffff`, `pull-up=ffffffff/ffffffff`, and
  `pull-down=00000000/00000000`. Both possible return pins therefore follow
  their configured bias instead of presenting a panel-driven DCS response.
  This rules out the earlier opposite idle levels as a real readback value and
  directs the next comparison to the pinned vendor implementation's read
  turnaround, lane selection, panel-enable/reset prerequisites, and write
  framing.
- The pinned vendor reference has no CO5300 register-read implementation, but
  confirms the native write framing, P29 reset timing, mode 0, initialization
  sequence, and maximum brightness exactly. The next controlled A/B therefore
  stops relying on optional DCS all-pixels-on behavior: `PBLDSP` now exits that
  mode and writes saturated red, green, and blue bands through the real
  address-window and GRAM data path used by the formerly visible native
  bring-up. The complete 146-test suite and `git diff --check` pass. The build
  uses 760,467 bytes of FLASH, 49,216 bytes of KERNEL_RAM, 67,614 bytes of
  T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 808,010-byte
  protected AP payload has SHA-256
  `9b1234809472fe3a76d35c423660c4caa92230f4c530e69b99b6e853183d2ede`
  and occupies exactly `0x132000..0x1f744a`; exact-range AP-only flashing and
  reboot succeeded. The live command reported `reinit=1 pattern=1`: all 1,864
  pixel-transfer commands for the 466-row RGB pattern completed without a
  controller timeout. The final state remained `glb=00000001 cfg=08000009
  rst=00000000 st=0001100b`, with the expected pins and mux. The panel remained
  completely black. This rules out normal UI/framebuffer contents and optional
  DCS all-pixels-on behavior: even explicit saturated GRAM data is not reaching
  visible panel output despite successful controller-side completion.
- The exact formerly visible AP image also remaining black after a full USB
  power cycle makes retained shared-SoC state the next controlled boundary.
  The pinned Tuya QSPI display path has no separate panel-power GPIO: its device
  configuration uses `TUYA_GPIO_NUM_MAX`, and the application asserts the same
  active-high P19 global latch as native startup. The generic Beken LCD
  driver's external-LDO vote is not used by this Tuya QSPI path. `PBLDSP` now
  read-only dumps BK7258 system analog registers 0 through 27 as `PBLANA`
  lines; it deliberately does not write undocumented analog or voltage bits.
  All 146 host tests and `git diff --check` passed. The 808,146-byte protected
  AP payload had SHA-256
  `3badd8e90e899ccfac61c70702786fcc9ab40596581cd87f2e99682eb258d003`
  and occupied exactly `0x132000..0x1f74d2`; exact-range AP-only flashing and
  reboot succeeded. `PBLANA` showed analog register 5 as `6c0fa76c`, including
  a clear `en_lcd` bit, but the vendor symbol and only vendor use identify that
  bit as segment-LCD power, not this QSPI panel. The QSPI display path never
  enables it, so no speculative analog write was made.
- The remaining concrete transport difference is now isolated. Tuya's real
  frame path forces CS low, sends one `32 00 2c 00` preamble, selects the QSPI
  memory path, disables command-generated clocks, DMA-streams all 434,312
  pixel bytes through the incrementing `0x64000000` aperture, then reverses
  those controls and releases CS. The earlier explicit RGB test instead sent
  1,864 independent FIFO commands. The next `PBLDSP` build uses CPU stores to
  the same non-cacheable aperture to reproduce the exact continuous wire
  transaction without adding an unproven DMA driver. All 146 tests and
  `git diff --check` pass. The build uses 760,787 bytes of FLASH, 49,248 bytes
  of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of
  LOG_STRINGS. Its 808,350-byte protected AP payload has SHA-256
  `3268133fbbc4a8c8f93a235f58a2270dd05d6976161be0efeae8aca94b39f13b`
  and occupies exactly `0x132000..0x1f759e`; exact-range AP-only flashing and
  reboot succeeded. The live command reported `PBLPIX continuous=1
  words=108578 bytes=434312 cfg=08000009 st=0001102a`, proving that the CPU
  completed the entire aperture stream and that all temporary QSPI controls
  were restored. The following `PBLDSP` line still reported `reinit=1
  pattern=1`. The panel displayed the expected saturated colour stripes. This
  proves the root cause: CO5300 pixel data must remain in one uninterrupted
  CS-low transaction, and the former 256-byte command-C FIFO chunks were not a
  valid continuation mechanism. The normal row-update path is being converted
  to the now-proven continuous aperture stream. The CO5300 brightness setter
  itself now also ignores lower requests and retains `0xff`, in addition to the
  backlight service's forced 100% policy. All 146 tests and `git diff --check`
  pass. The production-path build uses the same memory totals and produces an
  808,350-byte protected AP payload with SHA-256
  `35c4999e0503d79b3af038376bbc5d3e5a692059733a7449ce088fc33368f6e0`,
  occupying exactly `0x132000..0x1f759e`. Exact-range AP-only flashing and
  reboot succeeded, and a ten-second runtime UART check found no fault output.
  The display was black. Source tracing found this is not an external-flash
  dependency: the Getafix logo is compiled into the AP image. Instead,
  `boot_splash_start()` was compiled as a no-op for normal T5AI builds, and its
  existing animated implementation would in any case try to allocate a
  67,600-byte framebuffer from only about 56 KiB of free kernel RAM. T5AI now
  has a bounded 260-byte row renderer that sends the compiled logo and a static
  progress indicator through `display_update()` before resource/flash startup.
  This also provides a direct normal-row-path test independent of external
  flash. All 146 tests and `git diff --check` pass. The build uses 761,363
  bytes of FLASH, 49,504 bytes of KERNEL_RAM, 67,614 bytes of
  T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its 808,962-byte
  protected AP payload has SHA-256
  `6240d077ead944138475d4cba5dab646a4c239466603fde5efc6b8ffedf38a7c`
  and occupies exactly `0x132000..0x1f7802`; exact-range AP-only flashing and
  reboot succeeded. The panel remained black. Since the full-frame diagnostic
  stripes were visible but the same aperture mechanism restarted once per
  932-byte row was not, the panel requires the uninterrupted transaction to
  cover the complete 434,312-byte frame. The normal unrotated Pebble update,
  boot-frame, and clear paths now set one full-panel window and keep CS low
  while scaling and streaming all 466 physical rows. Build, safe AP-only flash,
  and visible validation were then exercised. All 146 tests and
  `git diff --check` pass. The build uses 761,523 bytes of FLASH, 49,504 bytes
  of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of
  LOG_STRINGS. Its 809,132-byte protected AP payload has SHA-256
  `099cefffa0fa4702c49a8e1d08371ab1ee3fadca69e91c99ecfad07a100a2501`
  and occupies exactly `0x132000..0x1f78ac`; exact-range AP-only flashing and
  reboot succeeded. The panel showed a stable full-white frame at full
  brightness. This proves the production `display_update()` callback path and
  uninterrupted full-frame transfer now reach panel GRAM; the remaining issue
  is limited to why the black splash pixels are absent or subsequently
  overwritten. A safe `ap_cmd PBLSPL` mailbox probe now reruns the bounded
  splash renderer after startup, distinguishing those cases without touching
  flash or relying on boot timing. All 146 tests and `git diff --check` pass.
  The probe build uses 761,587 bytes of FLASH, 49,504 bytes of KERNEL_RAM,
  67,614 bytes of T5AI_DISPLAY_RAM, and 110,737 bytes of LOG_STRINGS. Its
  809,200-byte protected AP payload has SHA-256
  `acd536f5786181edf91c4dcbc4650cff9a3e3269f09fe79fb9a41232f4f25642`
  and occupies exactly `0x132000..0x1f78f0`; exact-range AP-only flashing and
  reboot succeeded. The settled runtime accepted `ap_cmd PBLSPL` and returned
  `PBLSPL done`, proving the renderer completed. The panel remained solid
  white, ruling out a later compositor overwrite. Source inspection then
  identified the direct cause: `prv_panel_init()` still ended with the
  temporary DCS `0x23` all-pixels-on diagnostic, which masks GRAM as white.
  The stripe probe appeared only because it sent DCS `0x13` normal-display
  mode before transferring its frame. Production initialization now sends
  `0x13` followed by display-on `0x29` and no longer enters `0x23`. All 146
  tests and `git diff --check` pass. The resulting memory totals are unchanged;
  the 809,200-byte protected AP payload has SHA-256
  `705c2fb06ff9925d573b15bac15380fd3f118f6e7ef063791633542e84f4c242`
  and occupies exactly `0x132000..0x1f78f0`. Exact-range AP-only flashing and
  reboot succeeded, and a post-startup `PBLSPL` again returned `PBLSPL done`;
  the panel displayed the PebbleOS logo and static progress indicator at full
  brightness. This proves panel power/reset/init, normal DCS mode, the compiled
  splash row generator, GColor8-to-RGB565 conversion, logical-to-physical
  scaling, and one-transaction full-frame delivery. The splash remains static
  by design on T5AI; continue by finding why the partial common/application
  startup does not replace it with its first compositor frame.
- A subsequent clean reset without issuing `PBLSPL` also remained on the static
  splash, confirming this is not merely the diagnostic redraw covering an
  already-running UI. The existing read-only `PBLTIM` probe now reports a
  numbered main-init stage plus `appinit` and `first` flags. This distinguishes
  a resource, compositor, Bluetooth, launcher-init, or first-app boundary
  without enabling writable storage or altering runtime state.
- Hardware returned `PBLTIM ... stage=6 appinit=1 first=0`: all main-task
  initialization completed, `launcher_main_loop()` was entered, and the app
  manager initialized, but the first built-in app was not launched. Finer
  read-only stage markers now bracket each launcher initialization call to
  isolate the exact blocking service before changing startup policy.
- The finer probe remained at stage 17 and never reached the marker immediately
  after `shell_prefs_init()`. That function was blocking while opening the
  settings filesystem, which T5AI intentionally does not initialize because no
  writable PFS partition/recovery contract is proven. T5AI shell preferences
  now retain their compiled in-RAM defaults, and the settings-DB compaction
  migration is skipped; neither change writes flash.
- With volatile preferences, hardware advanced through shell stage 179: all
  shell initializers through the workout service returned. It then blocked on
  the final shared-PRF getting-started read/write, whose mutex/storage is not
  initialized on this partial T5AI path. That persistent first-use transaction
  is now skipped on T5AI, consistent with its deliberately volatile identity
  and bonding policy and without weakening other platforms.
- With the shared-PRF transaction gated, hardware advanced to stage 18,
  confirming `shell_event_loop_init()` returned. It then blocked in
  `services_set_runlevel()` because that function still invoked setters for
  common and normal services omitted from the partial T5AI startup. T5AI now
  applies runlevel only to its initialized light and Bluetooth-control
  services; accel-manager, vibe-service, HRM-manager, and the complete normal
  service runlevel table remain gated until their initialization is enabled.
- The first runlevel-gated build asserted at `cron_job_unschedule()` (captured
  LR `0x0217682b`) and rebooted because `light_allow()` depends on the cron
  service, which is also outside partial T5AI startup. T5AI already forces the
  panel/backlight to 100%, so its light runlevel setter is now gated as well;
  only initialized Bluetooth control remains in the T5AI common runlevel table.
- A corrected runlevel-table build still asserted at the same cron boundary:
  the following synthetic `light_button_pressed()`/`released()` boot gesture
  independently uses cron. That synthetic gesture is now skipped on T5AI,
  where brightness is already forced to 100%; physical input initialization
  and other platforms are unchanged.
- A following build still reached `cron_job_unschedule()` from another shell
  component. Cron is a self-contained RAM/new-timer service with no flash
  dependency, while multiple legitimate normal-shell components assume it was
  initialized by `services_common_init()`. Partial T5AI startup now explicitly
  calls `cron_service_init()` before light/compositor/launcher use, preserving
  the writable-storage gates rather than suppressing each cron consumer.
- With cron initialized, hardware advanced to `stage=21 appinit=1 first=0`,
  placing the next block inside `app_manager_start_first_app()`. Process setup
  unconditionally opened a per-app persistence store even though T5AI's
  persist service and writable PFS are deliberately disabled. T5AI now skips
  the matching persistence-client open and close while system apps use
  volatile state. The 807,194-byte AP-only image (SHA-256
  `2ac85c0feffe8aba65c47871c967a09bc3cec36b4ceb8a34fdf6b55042d6e1a7`,
  `0x132000..0x1f711a`) flashed successfully and advanced to
  `stage=22 appinit=1 first=1`, proving first-app setup and task creation
  returned.
- The launcher also assumes initialized app-order and AppDB storage. T5AI now
  uses compiled launcher ordering and enumerates only the built-in registry;
  it performs neither PFS nor AppDB access. Separate kernel/app stage counters
  were then added because a single shared counter could be overwritten during
  scheduling. The current 807,024-byte AP-only image (SHA-256
  `e655f4b91a13d692b1b26d5b310528b3f7c0600794d9f2ac37bcac073c27e720`,
  `0x132000..0x1f7070`) flashed successfully and reports
  `stage=22 appstage=0 appinit=1 first=1`. The board is stable, but the app task
  has not executed its first instruction. The frozen splash is therefore now
  isolated to FreeRTOS app-task scheduling/context entry, before app-state,
  launcher menu, compositor render, or display transport. Inspect the task's
  live state/priority and the T5AI CM33 restricted-task context/MPU setup next.
- Extended `PBLTIM` telemetry showed that the app task existed, remained in
  FreeRTOS Ready state at priority 2, and retained nearly its whole stack, while
  priority-3 KernelMain never blocked long enough to schedule it. A T5AI-only
  one-tick delay in the kernel event loop allowed the task to enter
  `app_state_init()` (`appstage=201`), ruling out task creation, privilege, MPU,
  and initial context restore. Fine checkpoints then reached `appstage=316` and
  isolated the next block to `app_touch_nav_subscribe()`: partial T5AI startup
  had omitted the RAM-only `touch_init()` required by that path. The main task
  now initializes touch beside cron and light before compositor/launcher use.
- The touch-enabled 807,398-byte AP-only image (SHA-256
  `adc63b2858f3c012b53690efbf19f35f139ef5e31f21d172a811b1f831c66df2`,
  exact range `0x132000..0x1f71e6`) passed all 146 tests and `git diff --check`,
  flashed successfully, and advanced beyond touch setup. The panel displayed a
  full-brightness PebbleOS charging animation: this is the first confirmed live
  compositor UI and proves the normal panel path is no longer blocked at the
  static boot splash.
- Subsequent checkpoints moved from `appstage=325` to `307`, spanning the
  67,614-byte application framebuffer clear. The task remained Ready with a
  955-word stack high-water mark, so this is extreme CPU starvation rather than
  a fault or framebuffer corruption. The diagnostic one-tick pause took tens of
  seconds to clear one frame. The next build uses a bounded T5AI-only ten-tick
  kernel pause to give app initialization and rendering useful CPU time while
  retaining kernel service; after launcher bring-up, identify the event source
  that keeps KernelMain's queue continuously selectable and replace this
  compatibility pause with the normal blocking behavior.
- The ten-tick scheduling build reached launcher checkpoint 222 within seconds.
  Fine probes placed the next stop in the app-list sorter: its Music/Workout
  override calls `app_install_is_prioritized()`, but partial startup had never
  initialized the recent-app cache's recursive mutex. T5AI now runs
  `app_install_manager_init()` after the system event service is ready while
  retaining its built-in-only enumeration and disabled AppDB/PFS paths.
- With that manager initialized, launcher setup reached the glance service and
  asserted at LR `0x02139493`, mapped to the required generic glance-reel
  resource being NULL. That resource lives in the not-yet-validated external
  system resource bank. T5AI now uses the existing basic app-menu row renderer
  and omits glance-reel initialization/animation; all other platforms retain
  their full launcher glance behavior.
- The basic-launcher build passes all 146 tests and `git diff --check`. Its
  799,170-byte protected AP payload has SHA-256
  `428695001028a4aac2a43d6f8de330d4cbe54b72780f7e0597ce3e961f574579`
  and occupies exactly `0x132000..0x1f51c2`; exact-range AP-only flashing and
  reboot succeeded. Hardware reports `stage=22 appstage=215 appinit=1 first=1`
  with the app task Ready, priority 2, and 769 words of stack headroom.
  Checkpoint 215 is immediately before `app_event_loop()`, proving the launcher
  window, built-in app list, basic menu layer, idle timer, and event loop are
  all live. A visible-screen report for this exact image is pending; the prior
  image showed the full-brightness charging animation, which may be a kernel
  modal covering the now-running launcher.
- Because development power and both virtual UARTs share the same USB-C cable,
  the board cannot be observed while unplugged and the charging modal masks the
  launcher continuously. The T5AI shell now continues normal battery monitoring
  but suppresses only battery UI state transitions until a battery is fitted;
  other platforms are unchanged. All 146 tests and `git diff --check` pass. The
  799,000-byte protected AP payload has SHA-256
  `9f8f1b569d4ac256fc4c29ef3949eaf1c8399cadd536b00d069eb43f360fbf93`
  and occupies exactly `0x132000..0x1f5118`; exact-range AP-only flashing and
  reboot succeeded. It reports `appstage=215 state=2 stack=602`: the launcher
  app is correctly blocked in its event loop with substantial stack headroom.
  The panel displayed the PebbleOS launcher menu at full brightness. This
  closes panel/UI bring-up: the boot splash is replaced by a live compositor
  frame, the built-in launcher renders, and the app event loop remains healthy.
  Validate physical button/touch navigation next before removing diagnostic
  checkpoints or reducing the temporary scheduling pause.
- The first attempted USART navigation test used `ap_cmd click long 3 120`, but
  T5AI does not expose Pebble's unrestricted serial prompt. The AP mailbox
  deliberately accepts only its bounded `PBL*` probes, so the CP shell prompt
  alone was not evidence that a button event had reached Pebble. A bounded
  `PBLB0` through `PBLB3` mailbox diagnostic now maps to Back, Up, Select, and
  Down and injects one 120 ms press through the shared remote-input service.
- The first `PBLB3` image rebooted because partial T5AI startup also omitted
  `remote_input_init()` with the rest of `services_common_init()`. T5AI now
  initializes this RAM/mutex/timer-only service immediately after the system
  event service; writable storage and Bluetooth remain gated. The corrected
  image passes all 146 tests and `git diff --check`, and builds cleanly. Its
  799,204-byte protected AP payload has SHA-256
  `5011c77c55832312fdd95832c609e7d8b3ca4bf37e1679d62ee7bbdeb67c0d91`
  and occupies exactly `0x132000..0x1f51e4`; exact-range flashing and reboot
  succeeded. Hardware returned `PBLB3 result=0` without resetting, proving a
  timed Down press was admitted to Pebble's real event queue. The operator
  confirmed that the highlighted launcher row visibly moved down. This proves
  end-to-end USART mailbox input, kernel event routing, app click recognition,
  menu state change, compositor redraw, and panel delivery.
- The physical control names are now cross-checked against the pinned Waveshare
  board source: its `BOARD_BUTTON_PWR_PIN` is active-low GPIO18 and its ordinary
  `BOARD_BUTTON_PIN` is active-low GPIO12. Pebble maps those to Back and Select,
  respectively. Use a short press of the ordinary `KEY` button for Select; the
  `PWR` button is Back and should not be held during routine UI testing.
- A physical upward swipe produced no visible launcher movement. A read-only
  `PBLTP` mailbox probe is implemented to report the touch master/nav/app gates
  plus CST92xx enable state, poll/read-failure/valid-report counters, finger
  state, and last coordinates. The source passes all 146 tests and
  `git diff --check` and builds cleanly. The 799,544-byte protected AP payload
  has SHA-256
  `d266f3921c826408870bfaafcedc5dc4e399cee27e01eb853112b2b96d3c96f8`
  and occupies exactly `0x132000..0x1f5338`. Flashing has not started: after
  the USB interfaces re-enumerated at 16:24, both `/dev/ttyACM0` and
  `/dev/ttyACM1` lost the desktop user ACL and are accessible only to
  `root:dialout`; restoring the two-device ACL is the current blocker.
- The ACL was restored and the telemetry image flashed successfully. Initial
  hardware telemetry returned `global=1 nav=0 app=0 enabled=0 polls=0`, proving
  the T5AI volatile-preferences early return preserved default values but
  skipped the normal tail that applies them to touch. The T5AI branch now
  applies the default touch-backlight, global-touch, and navigation gates
  before returning, without opening PFS or initializing unrelated services.
- The corrected source passes all 146 tests and `git diff --check` and builds
  cleanly. Its 799,578-byte protected AP payload has SHA-256
  `18481c284f45c7c4b95fe09a3ee384f3a5e57454598ad1a351bdffd1ecf3330f`
  and occupies exactly `0x132000..0x1f535a`; exact-range flashing and reboot
  succeeded. Live `PBLTP` now reports
  `global=1 nav=1 app=1 enabled=1 polls=355 fail=6 valid=0`, proving the
  controller is powered, the app navigation twin is installed, and polling is
  active. Capture telemetry after a physical swipe next; track the small I2C
  failure count and confirm whether valid reports and launcher movement agree.
- Post-swipe telemetry reported
  `global=1 nav=1 app=1 enabled=1 down=0 polls=2910 fail=33 valid=13 x=148 y=247`.
  The physical controller therefore delivered 13 valid samples and a clean
  liftoff into the enabled driver while the launcher navigation twin was live.
  The cumulative I2C failure rate was about 1.1%; keep tracking it, but it did
  not prevent the gesture report. The operator confirmed that the menu did not
  move. Sensor power and basic I2C framing are therefore no longer the leading
  suspects; continue at touch-event delivery and gesture recognition.
- A bounded `PBLSU` mailbox probe now requests a synthetic upward swipe through
  Pebble's existing remote-input service. This follows the injected-touch event
  path and will separate physical CST92xx sample geometry/timing from app
  recognizer and menu routing. The source passes the focused 78-test suite and
  `git diff --check`, and builds cleanly. Its 799,680-byte protected AP payload
  has SHA-256
  `fe0356e5cecb7a828f7f6be2e36b345c7b3914730901a9f33083b996111d7691`
  and occupies exactly `0x132000..0x1f53c0`. After reconnect, both bridge
  interfaces enumerated with the expected identity and ACL. Exact-range AP-only
  flashing and reboot succeeded, and the settled runtime returned
  `PBLSU result=0`. The injected swipe was therefore admitted; visible menu
  movement was confirmed by the operator. This clears touch-event delivery,
  app gesture recognition, MenuLayer routing, compositor redraw, and panel
  delivery for a known-good path; the remaining defect is specific to physical
  CST92xx samples.
- `PBLTP` now retains the latest physical contact's sample count, distinct
  coordinate-change count, and first/minimum/maximum/last mapped coordinates.
  The path-telemetry source passes the focused 78-test suite and
  `git diff --check`, and builds cleanly. Its 800,122-byte protected AP payload
  has SHA-256
  `bb3909da3522810ee6d1a5618da49beba965366a3004244ede80e163a969577a`
  and occupies exactly `0x132000..0x1f557a`; exact-range AP-only flashing and
  reboot succeeded. The launcher returned to healthy `appstage=215 state=2`.
  Before physical contact, telemetry reports `valid=0 n=0 chg=0`, providing a
  clean baseline for the next swipe.
- A deliberate physical upward swipe then reported
  `n=16 chg=6 first=151,15 min=146,15 max=156,237 last=156,237`. The contact
  crossed almost the full panel, but reported Y increasing from top to bottom:
  the CST92xx axes were not mirrored into Pebble screen coordinates. This
  explains the apparently inert launcher gesture: it was interpreted as a
  downward drag at the top edge and settled back to the same row.
- The exact pinned Waveshare board configuration sets both `mirror_x=1` and
  `mirror_y=1`. The native driver now applies those two mirrors after scaling,
  with a host regression assertion for both axes. The fix passes the focused
  78-test suite and `git diff --check`, and builds cleanly. Its 800,122-byte
  protected AP payload has SHA-256
  `d87e65a2e06ce72b854b731dea055e1ad2591344596f0f857234bbc16892dafa`
  and occupies exactly `0x132000..0x1f557a`; exact-range AP-only flashing and
  reboot succeeded. The launcher returned to healthy `appstage=215 state=2`.
The first post-fix physical attempt did not move the menu and reported
`n=15 chg=0 first=min=max=last=131,10`: it was a stationary contact from the
driver's perspective, not a translated swipe. Repeat with an inset path from
the lower third to the upper third; do not change recognizer thresholds based
on this zero-motion sample.

The next inset physical swipe was recovered after correcting the host command
framing to `ap_cmd PBLTP`: `global=1 nav=1 app=1 enabled=1 down=0 n=20 chg=8
first=121,215 min=116,61 max=124,215 last=116,61`. This is a valid, nearly
vertical upward path after mirroring, so the remaining failure is downstream
of CST92xx sampling. A bounded `PBLTN` probe now reports app-task touch event
dispatch plus unified Tier-1 widget recognizer callbacks. The focused 78-test
suite and native build pass; the 800,496-byte protected AP payload has SHA-256
`0c8cb007d182f96776d13f68f290d649f2da5b2694d919cc7240a7b70e61c925` and
occupies exactly `0x132000..0x1f56f0`. Exact-range AP-only flashing and reboot
succeeded. The settled launcher reports `appstage=215 state=2`; the clean
`PBLTN` baseline is `ev=0/0/0 widget=0/0/0/0 route=0 mgr=0 menu=1 target=0
bridge=0/0/0/0/0/0`. Capture `PBLTN` after one physical upward swipe to
separate event-queue delivery, Tier-1 routing, and pan recognition.

That physical swipe reported `n=25 chg=11 first=130,220 min=120,31
max=139,220 last=120,31`, while `PBLTN` remained exactly at its zero-event
baseline. The CST92xx driver therefore sees a clean upward path, but the app
touch dispatcher receives none of its events. The next bounded diagnostic adds
counters at the touch-service queue boundary and KernelMain mask decision, plus
the app touch subscription/handler state. Its focused 78-test suite and native
build pass; the 800,938-byte protected AP payload has SHA-256
`818578ea98d46a95a280dac157afb3062906c4df5bc4ad559593e7296885626d` and
occupies exactly `0x132000..0x1f58aa`.
Exact-range AP-only flashing and reboot succeeded. The launcher is healthy at
`appstage=215 state=2`, and the expanded clean baseline reports
`svc=0/0/0/0/0/0/0/4 loop=0/0/0/0 sub=1/1`: four touch holds/subscribers,
with the app event subscription and system handler both installed.

After a clean upward swipe, the expanded probe reported
`svc=21/0/1/9/1/0/0/4 loop=0/0/0/0 sub=1/1`: the touch service accepted all
21 physical samples and queued one touchdown, nine moves, and one liftoff, but
KernelMain processed none of them even after a long delay. This isolates the
failure to the shared kernel event queue. `event_take_timeout()` now polls and
drains the priority kernel queue before blocking on its queue set, recovering
real events whose queue-set notification was lost during an unsynchronised
process-queue reset. The focused 78-test suite and native build pass. The
801,006-byte protected payload has SHA-256
`ce4ee7f05787b03fa9a14242221db30e6c88a5994e530ad924a2668b557f04c5` and
occupies exactly `0x132000..0x1f58ee`.
The first flash attempt stopped before handshake/erase because `/dev/ttyACM0`
had lost its desktop-user ACL. Restore `baz:rw` on both virtual UART nodes,
then retry the unchanged exact range.
After restoring `baz:rw` on both UART nodes, the unchanged exact-range flash
completed and rebooted successfully. The launcher returned at
`appstage=215 state=2`; all service, KernelMain, and app touch counters are
zero at baseline, with `sub=1/1` confirming the app subscription and handler.

The first physical swipe on the queue-poll build still did not move the menu.
It was a clean upward path (`n=22 chg=11 first=142,209 last=142,66`), and the
service again queued one touchdown, eleven moves, and one liftoff, but
KernelMain remained at zero processed touch events. The pre-wait kernel-queue
poll therefore did not execute after the events arrived, pointing to a broader
KernelMain stall rather than a touch-specific queue-set wait. A bounded
synthetic Down-button control was admitted with `PBLB3 result=0`; visible
movement is the next discriminator for general input-event processing.

The synthetic Down control did not move the menu. Startup checkpoints then
proved `stage=26`, so KernelMain had completed worker/communication setup and
entered the launcher loop; task telemetry showed `km=2/853 bg=2/859`. The
failure was the T5AI KernelMain queue-set wait itself remaining blocked, not an
earlier startup call. T5AI now polls the self, kernel, app, and worker event
queues in priority order without blocking; the already-proven ten-tick outer
loop delay supplies its bounded wait cadence. Other platforms keep the normal
queue-set path. The focused 78-test suite and native build pass. The
801,006-byte protected payload has SHA-256
`4dc0408ce3005924bcdc6ddfc3275669289378bf1df8717232f1d789cc80f623` and
occupies exactly `0x132000..0x1f58ee`; exact-range AP-only flashing and reboot
succeeded. The clean baseline is restored and a new `PBLB3` control was
admitted for visible confirmation.

The user confirmed that the polling build's synthetic Down control moved the
launcher, but polling is explicitly diagnostic-only and is not an acceptable
permanent event loop. Review of the shared FreeRTOS queue set found two concrete
violations: its capacity was 42 although its three members can hold 47 events,
and `event_reset_from_process_queue()` reset the entire set before manually
reconstructing notifications while producers remained live. The permanent fix
sizes the set to 47 and replaces the whole-set reset with a short critical
section that drains one set snapshot, drops and cleans only the selected
process queue's events, and restores every other member notification in its
original order. The direct T5AI polling branch and the pre-wait kernel poll are
removed; `event_take_timeout()` again uses the normal blocking queue-set path
on every platform.

The first permanent-fix image passed all 78 focused tests and built cleanly. Its
801,244-byte protected AP payload had SHA-256
`91830e55e3aaba93d9a85fd82aeef028c8f931b257896faca887c11dcfca0352` and
occupied exactly `0x132000..0x1f59dc`; exact-range AP-only flashing and reboot
succeeded. A follow-up added observation-only queue depth/take counters and
task/interrupt state to `PBLTIM`, without changing dispatch behavior. That
801,686-byte payload has SHA-256
`1c0459a5e61e6299510a9cb0cc0dabd5ff385e74f54be96545306749738b3b95` and
occupies exactly `0x132000..0x1f5b96`; exact-range AP-only flashing and reboot
succeeded. At settled baseline it reported empty queues and `take=16`. After
`PBLB3`, the queues drained, `take` advanced to 19, KernelMain and the app both
ran, and the app returned to Blocked. The user confirmed that the launcher
selection visibly moved down. This validates wake, dispatch, app delivery, and
rendering on the normal blocking path with no event-loop polling workaround.

## 2026-09-12 handoff: normal NimBLE advertising and GATT proven

- A flashed follow-up build reached the real Pebble numeric-comparison popup
  and BlueZ displayed the same six-digit value, proving the pairing UI and SMP
  callback path. That attempt expired before both sides were confirmed; the
  watch remained alive but did not resume advertising until AP reset.
- Skipping the optional peer Device Name GATT read did not remove the
  intermittent pre-confirmation timeout and has been reverted.
- The pairing screen remained black because the partial T5AI startup bypasses
  `services_common_init()`, which normally initializes the light service.
  T5AI now initializes that self-contained service before the compositor and
  launcher use its mutex, timers, runlevel state, and interaction wake path.
- NimBLE pairing confirmation now uses a real, stable context object instead of
  encoding the connection handle as a pointer. A disconnect during numeric
  comparison reports one failed completion and clears the pending transaction,
  preventing stale pairing state from leaking into the next attempt.
- The first updated source passed the focused 78-test host suite and a DVT3 firmware
  build. That image used 852,307 bytes of FLASH, 82,304 bytes of
  KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 119,239 bytes of
  LOG_STRINGS; its 905,590-byte protected payload has SHA-256
  `669f01d6b111ef8a2bb33ff8f63ac5957821a8c26b79f3e477d90a1a9fd6a8ab`; it
  was flashed successfully and booted. The optional-name-read build also passed
  all 78 tests, built, flashed, booted, reached numeric comparison, and resumed
  advertising after a canceled attempt once BlueZ's stale device record was
  removed. The light-service follow-up also passes all 78 tests and builds with
  852,467 bytes of FLASH, 82,304 bytes of KERNEL_RAM, 67,614 bytes of
  T5AI_DISPLAY_RAM, and 119,239 bytes of LOG_STRINGS. Its 905,760-byte
  protected payload has SHA-256
  `96ae871f34a2cf3e47120b7e3df97a6cec278b43d20e18b95e0baad633a59a7d` and
  occupies `0x132000..0x20f220`. It flashed and rebooted successfully, then
  reached numeric comparison `101692`, but the panel remained black and the
  attempted button confirmation expired with `AuthenticationCanceled`.
- A potential display ordering race was corrected: T5AI started the
  asynchronous boot-splash task before driver initialization, then reset and
  reinitialized the CO5300 while that task could still be writing QSPI rows.
  T5AI splash/display initialization now starts once, after driver setup;
  the focused 78-test suite passes and the build succeeds with 852,051 bytes
  of FLASH. Its 905,318-byte protected payload has SHA-256
  `8b7f44b7b1768ac6c30e07de14e1b671ec4dc28497b308b6ee4991292b40b54a`
  and occupies `0x132000..0x20f066`. AP-only flashing and reboot succeeded;
  visible-display and pairing validation are pending.
- A cold boot still showed no splash. A captured boot log had no QSPI timeout
  or CO5300 initialization error, but source comparison found that a later
  driver change had moved zero- and one-byte panel commands into FIFO mode.
  The pinned vendor `bk_lcd_qspi_send_cmd()` uses inline command-register data
  and a length-specific `CMD_C_CFG1` value for payloads up to four bytes, using
  FIFO mode only above four bytes. That framing is restored; all 78 focused
  tests pass and the final build uses 852,083 bytes of FLASH. Its 905,352-byte
  protected payload has SHA-256
  `7c2ba1487dfccaf1579bd6aa23b6b9bbc4b13f9d910ed83e631afb34f6d7ceaa`
  and occupies `0x132000..0x20f088`. A QSPI done bit alone does not prove the
  panel accepted a malformed command. AP-only flashing and reboot succeeded;
  the panel still produced no visible output.
- A temporary synchronous panel probe now sends CO5300 `all pixels on` (`0x23`)
  immediately after maximum-brightness and display-on initialization. This
  bypasses the splash framebuffer, compositor, and Bluetooth paths. All 78
  focused tests pass and the build succeeds with unchanged memory use. Its
  905,352-byte protected payload has SHA-256
  `579d6823a3906c91fc7ec3a800e0d90b364623b5e633466c8919ffc43585c680`
  and occupies `0x132000..0x20f088`. The reconnected bridge enumerated as
  `1a86:55d2`; its serial nodes required host-level access from the current
  task. The diagnostic image was flashed successfully over `if00`, restricted
  to `0x132000..0x20f088`, and the AP rebooted. The panel remained completely
  black, isolating the fault below the splash framebuffer, compositor, and
  Bluetooth paths.
- Comparison with the earlier native display revision that produced visible
  output found four simultaneous QSPI-controller changes. The diagnostic build
  now restores that known-visible setup: 40 MHz serial clock, an asserted then
  released controller soft reset, the reset-default FIFO threshold, and the
  QSPI clock-gate-enable bit. It retains checked mailbox power-up, corrected
  short-command framing, and the `all pixels on` probe. All 78 focused tests
  pass; memory use is unchanged. The 905,352-byte payload has SHA-256
  `1f3877dda7587d52a851259d35c06b11722db9ee998ff5c73691d73bafb37818`
  and occupies `0x132000..0x20f088`. It flashed successfully to that exact AP
  range and rebooted. The panel again remained completely black, ruling out
  those four later QSPI-controller settings as the active fault.
- For a source-versus-hardware boundary test, the exact historical display
  probe from revision `cc642becb` was built in an isolated `/tmp` checkout.
  It contains the original maximum-brightness/all-pixels-on sequence, raw QSPI
  and reset-pin logging, and a display-only main loop. The 736,304-byte
  protected payload has SHA-256
  `e4ec3628cc5e1a6cdcf6c296a6bf7955229d514b66ea0964098d418c3141b5c4`
  and was flashed successfully to its exact `0x132000..0x1e5c30` AP range.
  It also remained completely black after the flash reboot. This moves the
  active investigation below the current source diff to panel power/reset or
  CP-mediated QSPI power state. The attempted cold-boot capture was invalid:
  `if00` and `if02` are two channels of the same USB-C dual-UART bridge, so
  unplugging that connector necessarily removed both capture channels. Board
  power and the virtual UARTs share this connector, so future cold-boot capture
  must use a firmware-side delay or host-triggered gate after re-enumeration.
  The current Bluetooth image was restored successfully.
- The next probe uses the CO5300's documented `0x03` command-read transaction
  and the pinned BK7258 command-D/FIFO conversion to read DCS power mode
  (`0x0a`). Because the AP log UART is not exposed reliably, the returned byte
  is encoded into the discoverable Bluetooth name as `PBL-Dxx`. This keeps the
  test on the current known-booting Bluetooth path. All 78 focused tests pass;
  the build uses 852,211 bytes of FLASH and otherwise unchanged memory. Its
  905,488-byte protected payload has SHA-256
  `84faaab946359a27bee6070ec573b52ebb9c860ff1495033a83f00cb77acc608`
  and occupies `0x132000..0x20f110`. It flashed successfully. Removing the
  unbonded BlueZ record and rediscovering the board still returned the default
  `Pebble 0AAB` name, proving the power-mode read was not marked valid rather
  than exposing a cached advertisement.
- The Bluetooth self-report now distinguishes QSPI power failure (`PBL-QER`),
  panel write-initialization failure (`PBL-PER`), command-D read failure
  (`PBL-RER`), and successful power-mode data (`PBL-Dxx`). All 78 focused tests
  pass; the build uses 852,275 bytes of FLASH and otherwise unchanged memory.
  Its 905,556-byte payload has SHA-256
  `a8d3faa14dec6376735e01f7aa0a89e648f8ed7940857db18d524988de73e66f`
  and occupies `0x132000..0x20f154`. It flashed successfully, and a fresh
  BlueZ discovery returned `PBL-QER`. Later source-level tracing showed that
  this result was the probe's untouched default rather than evidence that the
  CP-mediated QSPI request had failed: normal firmware compiles the boot-splash
  service to a no-op, so the T5AI panel initializer was never called.
- The original visible display probe treated the CP power request as advisory
  and applied the AP's validated QSPI power/clock registers regardless. The
  current driver now retains the CP request but falls back to those direct AP
  registers instead of aborting display initialization when the CP service is
  not ready. The focused 78-test suite passes and the build succeeds with
  852,275 bytes of FLASH, 82,304 bytes of KERNEL_RAM, 67,614 bytes of
  T5AI_DISPLAY_RAM, and 119,265 bytes of LOG_STRINGS. Its 905,556-byte payload
  has SHA-256
  `9727acb7bfc495438d1f3262dad30d938bfe5049feecccab66a035aed92b9a05`
  and occupies `0x132000..0x20f154`. It flashed successfully to that exact AP
  range and rebooted. The board did not advertise during the first post-flash
  scan, consistent with CP state after repeated AP-only resets; cold-boot
  display and diagnostic-name observations were pending at that point. Because
  power and both UARTs share one USB-C connection, capture must begin through a
  firmware-side delay or trigger after the tty devices re-enumerate.
- A subsequent full USB replug re-enumerated the bridge as `ttyACM0`/`ttyACM1`
  (`if00`/`if02`) and fresh BlueZ discovery again returned `PBL-QER`. Inspection
  then found the actual integration regression: `main.c` skipped its explicit
  T5AI `display_init()` and called `boot_splash_start()`, but the normal-firmware
  implementation of that service is a no-op. Explicit panel initialization is
  now restored immediately after driver setup. The focused 78-test suite passes
  and the build succeeds with 852,851 bytes of FLASH, 82,304 bytes of
  KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 119,265 bytes of
  LOG_STRINGS. Its 906,168-byte protected payload has SHA-256
  `d80c64909d963fb0849fdcf9cdc1ecc910402df03e2de75fe8b7988ca3d3a55a` and
  occupies `0x132000..0x20f3b8`. It flashed successfully to exactly that AP
  range and rebooted. Fresh BlueZ discovery returned `PBL-D00`: explicit panel
  initialization ran, every write command completed, and the command-D read
  transaction completed with power-mode byte `0x00`. A visible-screen
  observation is pending; if it remains black, distinguish a real all-zero
  panel response from incorrect BK7258 read-data reconstruction before changing
  the proven write sequence again.
- The screen remained black with `PBL-D00`. Panel bring-up is now isolated from
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
  occupies `0x132000..0x20f50c`; flashing and UART capture are pending. The
  board and both virtual UARTs share the same USB-C connection, so unplugging
  power also removes both tty devices; there is no separate board-power cable.
  A host-triggered AP-UART image was silent and did not accept its `G` byte,
  matching the earlier documented finding that native AP UART0 has no electrical
  output. The trace is therefore routed through Pebble's established CP-mailbox
  logger to `if02`/`ttyACM1` at 115200, with a ten-second delay after boot so the
  tty can be reopened before panel initialization starts.
  The resulting 906,508-byte image (SHA-256
  `24877ffdf7b2170b60538e29be71ee3aebf02017efb4dbccabd0c836ef84b86f`)
  flashed successfully to `0x132000..0x20f50c`, but a 30-second `if02` capture
  received no forwarded trace. Early CP log forwarding is therefore not a
  dependable panel diagnostic channel either. The corrected FIFO/80 MHz panel
  sequence did execute after its ten-second delay, but the screen remained
  completely black. Comparison with Tuya's QSPI HAL then exposed a controller
  reset-state mismatch: Pebble set `QSPI_GLB_CTRL.soft_reset` and immediately
  cleared it, while Tuya leaves that bit set after initialization. The next
  probe leaves it set and restores the known FIFO threshold. All 78 focused
  tests pass and the build succeeds with 853,171 bytes of FLASH, 82,464 bytes
  of KERNEL_RAM, 67,614 bytes of T5AI_DISPLAY_RAM, and 119,266 bytes of
  LOG_STRINGS. Its 906,508-byte protected payload has SHA-256
  `21b3aff34168b47ffe0b3c62ca876fbd5978ec8ccda10a15bde0d37823f62803`
  and occupies `0x132000..0x20f50c`. It flashed successfully to exactly that
  protected AP range and rebooted. A 25-second `if02` capture at 115200 baud
  received undecodable binary-like traffic rather than the expected panel
  text; `if02` is in fact the previously proven 460800-baud CP shell. Sending
  `ap_cmd PBLTIM` at 460800 produced a readable AP mailbox response, confirming
  the live USART diagnostic route. The screen remained black. Register-level
  comparison then found that offset `0x64` bit 1 is the active
  `sw_rst_fifo` control, not reset release. The failing probes left it set, so
  their commands could complete while parameter bytes were discarded. The next
  image restores `QSPI_RST_CFG=0`, matching both the last hardware-proven Pebble
  revision and Tuya's normal command path. All 78 focused tests pass. The
  906,508-byte protected payload has SHA-256
  `fef6582b3c4aa8835ae8f2f1f82d6e8adfb4012010a66826dd352960e94bc0d2`,
  occupies `0x132000..0x20f50c`, and flashed successfully to exactly that AP
  range. The 460800-baud post-flash capture showed repeated CP IPC CRC warnings
  plus mixed runtime traffic, and the screen remained black. A subsequent
  build removed noisy boot-time command logging and added the safe
  `ap_cmd PBLDSP` USART probe. Existing `PBLRX` and `PBLTIM` commands responded,
  but the first `PBLDSP` handler (which also issued live QSPI operations) did
  not return output. The next probe is deliberately read-only to distinguish
  mailbox routing from a blocked or disruptive QSPI transaction.

- A diagnostics-free native build was flashed and validated. BlueZ discovered
  `Pebble 0AAB` at public address `20:F1:B2:B8:0A:AB`, connected, and reached
  `ServicesResolved: yes`. The board was disconnected cleanly and left
  advertising.
- The resolved server includes Generic Access (`1800`), Generic Attribute
  (`1801`), Device Information (`180a`), Battery (`180f`), Pebble (`fed9`), and
  Pebble vendor (`40000000-328e-0fbb-c642-1aa6699bdada`) services. This historical
  session was deliberately unpaired and unbonded; the 2026-09-13 checkpoint
  below supersedes it with pairing, reconnect, and real protocol transfer.
- All legacy advertising commands complete successfully in the normal path:
  LE Set Advertising Data (`0x2008`), LE Set Scan Response Data (`0x2009`), LE
  Set Advertising Parameters (`0x2006`), and LE Set Advertise Enable (`0x200a`).
  The temporary scan-response skip has been removed.
- The root cause of the intermittent HCI timeouts was mailbox ownership, not
  radio configuration. The physical AP-to-CP mailbox supports one ordinary
  message outstanding at a time, matching the pinned vendor `bk_ipc` semaphore.
  `cp_mailbox.c` now serializes every non-transport-ACK send until its physical
  ACK and clears that state on every receive and discard path. Hardware ACK
  frames bypass the wait to avoid deadlock.
- CP-owned HCI callback frames are copied and physically acknowledged before
  their shared descriptor can be reused. The T5AI NimBLE transport now uses an
  eight-entry FIFO, preserving interleaved physical ACKs, events, ACL packets,
  MIPC object ACKs, and application replies. Asynchronous callback-response
  descriptors remain owned until their logical MIPC ACK. This removed the
  descriptor overwrite, double-ACK, and event-order failures.
- At this checkpoint, the T5AI advertising-rotation timer bounced work to the
  system task because the launcher task was not initialized by the then-partial
  service startup. The later normal-service initialization supersedes this.
- At this checkpoint, Bluetooth persistence was intentionally volatile and
  shared PRF remained read-only. The 2026-09-13 checkpoint below supersedes
  this: bounded shared-PRF writes and BLE pairing enumeration are enabled.
- The CP may continue to print non-fatal
  `bk_ipc_crc_check crc check failed` lines while transactions succeed. The
  native code mirrors the pinned vendor dispatch behavior because mutable
  list/result fields can race the CRC. Judge success by the logical response,
  not this line alone.
- A wedged CP requires a complete power cycle: disconnect both USB cables for
  five seconds, then reconnect. AP reset or flashing alone does not necessarily
  reset the CP.
- The final image contained no temporary `T5ADV`, `T5CMD`, `T5TX`, `T5EV`,
  `T5POLL`, or `T5X` diagnostics and no phase-coded error offsets. The NimBLE
  submodule is clean.
- The final build used 852,243 bytes of FLASH, 82,304 bytes of KERNEL_RAM,
  67,614 bytes of T5AI_DISPLAY_RAM, and 119,131 bytes of LOG_STRINGS. The
  1,026,537-byte resource pack remains close to its 1 MiB ceiling.
- The focused test set in the validation section passes all 78 tests. The
  diagnostics-free protected image was 905,522 bytes and occupied physical
  flash range `0x132000..0x20f132` for that build only.

The dated 2026-09-10 and 2026-09-11 sections below remain as investigation
history. Their statements that native Bluetooth is gated, ACL is pending, or
the vendor diagnostic is the current device image are superseded by this
section.

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
- The application core reports `SystemCoreClock=120000000` while its SysTick
  counter runs at 60 MHz. Applying reload `59999` after `kernel_ui_init()`
  preserves vendor-compatible early startup timing and produces the intended
  1 kHz FreeRTOS tick. Ten consecutive `ap_cmd PBLTIM` samples measured median
  FreeRTOS/RTC `1.000000` and AON/RTC `32.001000`.
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
- Native I2C0 and the external PCF85063 RTC are hardware-proven. Reads at
  address `0x51` returned coherent BCD calendar data, an invalid oscillator-stop
  state was initialized from Pebble's 2010 minimum epoch, the OS flag cleared,
  and the clock advanced normally. Reflashing the unchanged AP image then took
  the valid external-clock path and seeded Pebble time without rewriting the
  RTC. T5AI RTC startup now also rebases stale retained AON-counter state after
  a hardware-counter reset, preventing unsigned elapsed-time underflow.
- Native controller IPC now registers and initializes the CP Bluetooth
  controller, submits consecutive HCI commands, and receives their asynchronous
  Command Complete events on the event callback. HCI Reset returned status zero;
  Read Local Version returned opcode `0x1001`, status zero, HCI version `0x0b`,
  and revision `0x0006`. The hardware-proven MIPC directions are `0x17` from AP
  to CP and `0x47` from CP to AP. CP-originated frames are routed through the
  shared mailbox owner, and callback replies use the vendor asynchronous echo
  sequence. Native NimBLE then advertised as `Pebble ACL`; BlueZ connected on
  handle `0x0001`, completed LE Read Remote Features, sent 19 ATT/GATT requests,
  and received all 19 Pebble host responses with status zero. Controller-owned
  event/ACL data is copied into aligned AP SRAM before acknowledging its shared
  IPC descriptor. Asynchronous callback-response descriptors are not reused
  until their logical MIPC ACK arrives; this removed the temporary 100 ms HCI
  command/ACL delays. The DVT3 config now enables NimBLE and records the CP HCI
  gate as validated. The CP still prints non-fatal MIPC CRC diagnostics while
  successfully dispatching every transaction; that vendor ABI discrepancy
  remains to be characterized.
- The native CO5300 display path is hardware-proven. GPIO bit-banging first
  established the reset sequence, command framing, panel wiring, and four-lane
  pixel order independently of the QSPI controller. Native QSPI then removed
  that retained image with command `28h` and replaced horizontal reference
  bands with four full-height vertical bands through 2,812 FIFO commands with
  no controller timeout. The CP accepted both the AHBP/QSPI power vote and the
  QSPI clock vote. Production register writes now use the vendor FIFO command
  mode, the controller setup matches the pinned T5AI implementation, and panel
  brightness no longer defaults to zero. Temporary bit-bang, register-dump,
  and display-probe code was removed after validation. A black screen after
  normal native startup is currently expected because the T5AI bring-up path
  deliberately skips the complete services/application stack; it is not a
  display-driver result.
- The AP-to-CP mailbox transport now distinguishes physical ACKs from logical
  responses, uses the hardware-proven directional channels (`0x11` AP-to-CP,
  `0x41` CP-to-AP), preserves a logical response that precedes its physical
  ACK, acknowledges CP notifications, and copies outbound frames into a
  cache-flushed ring so callers need not retain their source buffers.
- SARADC channel/configuration ABI was matched to the pinned CP firmware
  (`T5AISaradcConfig` 36 bytes and `T5AISaradcCommand` 132 bytes). Eight
  consecutive on-device battery reads completed successfully at approximately
  3.97-4.00 V; USB and charging state were both detected.
- GPIO interrupt routing and acknowledgement are now hardware-validated on the
  application core. GPIO IRQ 55 must be enabled in the CPU1 group-2 mask, and
  each channel must be acknowledged through its self-clearing pin-config bit 13;
  writing the aggregate status snapshot caused a held-button IRQ storm and task
  watchdog reset. P18 maps to Back/power and P12 maps to Select, both active-low
  with pull-ups. One press/release of each produced exactly two interrupts,
  returned to the idle state, and did not reset the device. The temporary
  `PBLBTN` register/counter probe was removed after validation.
- The native firmware builds successfully, and AP-only flashing remains
  constrained to the generated protected payload beginning at physical address
  `0x132000`, bounded by Pebble's `0x1f0000`-byte logical experimental slot.
  QIO packaging preserves the verified vendor boot/CP prefix and can now extend
  beyond a shorter vendor AP payload without crossing the pinned AP partition.

The native target configures, compiles, links, packages, and boots on hardware.
The original 2026-09-10 handoff below is retained as historical context; its
CPU1/AP reset-entry blocker and known-good diagnostic on-device state have been
superseded by the 2026-09-11 progress above.

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

### Historical Bluetooth controller evidence from the vendor-runtime diagnostic

The following results were observed over the air and on HCI while the vendor
runtime owned the controller. At that point they established expected
controller behavior, not correctness of Pebble's native CP mailbox transport,
so the gate remained disabled. This restriction was lifted after the native
transport passed the 2026-09-11 and 2026-09-12 hardware validation above:

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

- `getafix@dvt3` selects T5AI/STAR-MC1, Pebble FreeRTOS, and the validated CP
  HCI path in an isolated defconfig; it does not inherit DVT2's SiFli platform.
- Native startup, scheduling, runtime UART, CP heartbeat/mailbox, GPIO/buttons,
  I2C/RTC, display/QSPI, touch, accelerometer, SARADC, and Bluetooth are all
  proven on the DVT3 hardware. Battery-cell behavior is not proven because no
  battery is fitted.
- Pebble's NimBLE host now advertises, accepts a BlueZ ACL connection, responds
  to ATT traffic, and exposes the expected Pebble GATT services through the
  native CP controller path.
- The normal T5AI startup in `src/fw/main.c` remains board-sequenced rather than
  calling the complete shared `services_init()` path. After T5AI hardware-facing
  initialization it now runs `services_normal_init()`, ensuring every shared
  Pebble Protocol endpoint has its owning service, mutexes, and timers initialized.
  The launcher and live display have been observed on hardware.
- T5AI persistence gates were experimentally widened on 2026-09-13. Real
  pairing then proved that the CP protected-flash service rejects the normal
  SettingsDB write path (`flash_write_bytes` asserted after status `-8`). DVT3
  Bluetooth therefore uses the PRF persistence interface. Bounded shared-PRF
  writes and pairing enumeration are now enabled; the watch lists the saved
  phone name after reboot and reconnects without a new pairing. Firmware/recovery
  objects and arbitrary NOR writes remain rejected. Protected-flash logical
  write, erase, recovery, corruption, and endurance behavior is still
  outstanding and must be solved before re-enabling those metadata writes.
- The native build emits checked ELF, map, raw, Intel HEX, protected-block, and
  image-layout artifacts without unresolved platform symbols or RAM overflow.
- The touch/display-focused baseline is 44 passing tests. The complete current
  eight-module run is 88/88 after aligning the T5AI fail-closed persistence and
  update policy. Both DVT3 and the DVT2 compatibility target
  have built successfully; preserve DVT2 behavior.

## Resume commands

```sh
cd /home/baz/coredevices/pebbleos
. .venv/bin/activate
python pbl configure --board getafix@dvt3
python ./waf build
```

Keep the clean build green while addressing the remaining host-auditable work.
Do not hide regressions with placeholder symbols; represent missing hardware
through an explicit disabled configuration or an honest service-boundary
implementation.

## Remaining work, in recommended order

The broad host-side port and primary peripheral bring-up are complete. Bluetooth
pairing and Pebble protocol data transfer over the NimBLE/CP transport are now
proven. Resolve the `factory`/unknown-platform identity reported by the phone,
then perform longer reconnect and power-cycle soak testing. Several persistence gates are already
enabled in the current source; validate their bounds and recovery behavior
before relying on them. OTA and arbitrary/destructive flash paths remain out of
scope.

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

### 2. Native UART and debug console — implemented and hardware-proven

- Native UART0 initialization, pinmux, clocks, baud generation, TX, RX, IRQ
  routing, and error clearing are implemented through Pebble's serial plumbing.
- The runtime log and CP shell are proven at 460800 baud on CH342F interface
  `if02`; the programming/reset function remains on `if00`.
- CP-shell `ap_cmd` traffic reaches Pebble through the log mailbox and has
  completed repeated response-aware transactions without resetting the AP.
- ACM numbering is not stable. Select the port by CH342F serial and interface.
- DMA remains unnecessary for the current functional path; add it only with
  explicit ownership, cache, timeout, and recovery semantics.

### 3. Native flash controller and storage — bounded writes enabled; recovery pending

- The native BK7258 mailbox client and Pebble flash interface are implemented;
  flash/SARADC and Bluetooth now safely share the physical mailbox owner.
- The 34-byte physical / 32-byte logical protected-block translation, image
  bounds, and AP-only packaging are host-tested. Manual AP-only programming and
  recovery are proven on the backed-up lab board.
- Firmware-side policy permits writes only inside PFS, system-resource bank 0,
  coredump, debug-log, and shared-PRF regions. Firmware/recovery space and
  arbitrary NOR addresses remain blocked. This is broader than the earlier
  PFS-only handoff state.
- Before treating those regions as production-safe, verify erase geometry,
  alignment, XIP/cache
  coherency, interruption behavior, corruption recovery, and factory restore.
- Keep the experimental DVT3 layout provisional.
- Keep OTA disabled until packaging, metadata, slotting, rollback, and recovery are defined and tested.

The production path does not link the Tuya RTOS or application framework. Do
not broaden the writable range merely because AP programming through TuyaBoot
works; these are separate recovery and runtime-storage contracts.

### 4. SoC bring-up boundary — boot and timing proven; isolation/low power remain

- CPU1 reset entry, vector relocation, CP power-up acknowledgement, cache/FPU
  setup, and entry through Pebble startup are proven on hardware.
- Reset-time initialization now enables full CP10/CP11 access using the pinned
  BK7258 CMSIS sequence before any pre-scheduler C can execute compiler-emitted
  FP instructions. FreeRTOS retains ownership of lazy FP context preservation.
- The 120 MHz `SystemCoreClock`, 60 MHz SysTick source, 1 kHz scheduler tick,
  PendSV/SVC operation, and AON/RTC timebase ratios are measured on hardware.
- Reset initialization now masks IRQs, stops and clears inherited SysTick
  state, clears pending SysTick/PendSV exceptions, and enforces eight-byte
  exception-stack alignment before relocating VTOR. This prevents a boot-stage
  scheduler interrupt from reaching Pebble before FreeRTOS installs its 1 kHz
  tick and lowest-priority handlers.
- CMSIS interrupt setup now uses logical priorities derived from
  `__NVIC_PRIO_BITS`, rather than passing FreeRTOS's raw left-aligned BASEPRI
  byte as though it were a CMSIS priority. On T5AI the syscall-safe boundary
  is therefore logical priority 5 of 7, with UART at 5 and GPIO/I2C at 6.
- The raw 32 kHz AON oscillator is now kept private to the T5AI RTC driver and
  scaled to Pebble's 1 kHz `rtc_get_ticks()` contract. This prevents FreeRTOS
  from programming a pathological 32 kHz SysTick while retaining raw-counter
  precision for wall-clock seconds and milliseconds. Scheduler and AON/RTC
  cadence are hardware-proven; long-duration oscillator drift is not.
- The pinned BK7258 120 MHz AP boot setting is now an explicit
  `T5AI_CORE_CLOCK_HZ` contract instead of an unexplained constant.
  Compile-time checks require it to divide exactly into the scheduler rate and
  produce a valid 24-bit SysTick reload. Pebble does not reprogram the
  boot-owned clock.
- CPU1 is the Pebble application core; keep unused cores quiescent.
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

### 6. GPIO, EXTI, and buttons — implemented and hardware-validated

Native T5AI GPIO input, pull, push-pull output, and software open-drain output
are implemented behind Pebble's GPIO interface. GPIO-backed rising, falling,
and emulated both-edge interrupts are also implemented. The provisional P18
Back and P12 Select inputs use a 40 ms FreeRTOS-timer debounce path; absent
Up/Down buttons are represented explicitly. P18/P12 mapping, idle pull-ups,
falling/rising edge delivery, CPU1 interrupt routing, and per-pin interrupt
acknowledgement are validated. Low-power wake behavior remains to be tested.

Board signals recorded from the reference design:

- Power latch: P19
- Amplifier enable: P28; keep disabled until audio is implemented
- Back/power button: P18; Select/user button: P12
- Charge detect: P30

Verify all pins from the schematic/BSP before hardware testing.

### 7. I2C and RTC — implemented and hardware validated

- Native I2C0 uses SCL P20 / SDA P21 with controller timeouts, reset recovery,
  repeated-start reads, and Pebble locking semantics. The CPU1 interrupt route,
  register-address write, repeated START, one-byte and seven-byte reads, and
  successive write/readback transactions are proven on hardware.
- The PCF85063 at address `0x51` seeds Pebble wall-clock time after I2C0 starts
  and is updated by `rtc_set_time`; stopped, 12-hour, oscillator-stop, invalid
  BCD, and inconsistent calendar values are rejected without corrupting the
  retained AON-derived clock.
- The T5AI AON counter remains the monotonic/subsecond source so routine time
  reads do not block on I2C. Hardware samples showed the external RTC and the
  retained/AON-derived clock advancing together within one second, and a later
  unchanged-image boot restored its time from the external device.
- The external RTC alarm output is not enabled because its MCU interrupt/wake
  routing has not been confirmed. Pebble alarm timing continues to use the AON
  counter without claiming hardware wake support.

### 8. Display — native QSPI/CO5300 path hardware-proven; DMA active

- Native QSPI0 and the pinned CO5300 initialization sequence are implemented
  for the exact module parameters: 466x466, X offset 6, reset P29, mode 0 at
  80 MHz. Hardware validation proved reset/command framing, display off/on,
  full-height colour bands, pixel ordering, and successful FIFO completion.
- Pebble's 260x260 ARGB2222 contract is preserved. A bounded 932-byte scanline
  buffer converts to big-endian RGB565 and nearest-neighbor scales dirty rows
  at the display boundary, including software 180-degree rotation.
- CO5300 register commands remain synchronous, but full-frame pixel delivery now
  uses BK7258 GDMA into the QSPI memory aperture in bounded 8-panel-row staging
  chunks. Cache clean operations, a boot-time RAM-to-RAM self-test, timeouts, and
  a CPU-stream fallback are present. Hardware boot telemetry reported
  `PBLDMATEST ok=1` with matching source/destination data. DMA completion is
  still synchronously polled by the display call; interrupt completion,
  dirty-region batching, deeper sleep/wake, and teardown remain.
- Do not expose the vendor framebuffer or UI framework to Pebble applications.
- Pebble brightness control maps 0..100 onto the CO5300 `0x51` register;
  requests before initialization or during an update are cached for the next
  safe transport boundary. With no ALS fitted, the ambient-light driver
  explicitly reports unavailable/unknown rather than emulating a sensor.

### 9. Touch — interrupt-driven native path implemented; final physical check pending

- The CST9217/CST92xx controller is implemented at address `0x5a` using I2C0,
  GPIO42 falling-edge interrupt, GPIO43 reset, validated report framing, and
  Pebble's touch service. Native
  466x466 coordinates are range-checked and mapped directly into Pebble's
  260x260 space; service-level rotation remains intact. Hardware testing on
  DVT3 previously proved valid coordinate motion in both axes, edge scaling,
  clean finger-up transitions, and end-to-end launcher navigation.
- The GPIO ISR performs no I2C. It only gives a binary semaphore to the dedicated
  touch worker. The worker sleeps indefinitely while idle, reads reports after
  an interrupt, and uses a 20 ms timeout only while a contact is active as a
  lost-edge fallback for motion and finger-up. Repeated failures reset only the
  CST9217; bus bit-bang recovery is no longer invoked by this driver.
- The official Waveshare schematic identifies `TP_INT=IO42` and
  `TP_RESET=IO43`; the pinned Tuya board source omits the interrupt and appears
  to assign reset to IO42 incorrectly. The corrected image builds, flashes, and
  reaches a stable runtime without a boot loop. Confirm one physical swipe and
  idle/read-counter behavior on this exact image before calling the conversion
  hardware-validated or claiming low-power wake support.
- Normal subscribers now enable the sensor and receive reports. Add controller
  gestures only if they preserve Pebble recognizer and button-substitution behavior.
- Keep classic Pebble button behavior stable for applications.

### 10. Accelerometer — native hardware path validated

- The QMI8658 is implemented on I2C0 at address `0x6b`, with WHO_AM_I `0x05`
  validation, 4 g scaling, Pebble sampling-rate negotiation, timestamps, axis
  rotation, direct peeks, and power-down while unsubscribed. DVT3 stationary
  testing measured approximately 1 g on each positive and negative physical
  axis with stable one-shot reads. The mounting transform was measured and
  corrected to Pebble's API convention: right is sensor +Y / Pebble +X, top is
  sensor -X / Pebble +Y, and out of the watchface is +Z.
- Button debounce, touch polling, and accelerometer polling use Pebble's native
  high-priority timer service; DVT3 no longer depends on the deliberately
  disabled FreeRTOS software-timer daemon.
- No interrupt pin is identified in the module BSP, so the initial driver uses
  bounded system-task polling and delivers one sample at a time. Confirm the
  interrupt routing before adding FIFO batching, wake-on-motion, shake, or
  double-tap handling.
- Validate continuous service delivery once T5AI's common services are enabled;
  refine per-unit offset calibration only if broader production samples require
  it.

### 11. Battery measurement and charging state — blocked on fitted battery

- The CP-owned T5AI SARADC is accessed through its native mailbox service.
  P13/channel 15 uses the reference 16-sample peak strategy and the module's
  measured two-point SAR calibration plus the schematic 2 MOhm/510 kOhm divider.
- Active-low P30 feeds Pebble's charging and cable-present state. The module has
  no separate standby/full signal, so a completed charge cannot currently be
  distinguished from unplugged state. P30 is an open-drain charger-status line;
  native initialization now enables the pull-up required by the pinned vendor
  implementation.
- The available DVT3 has no battery fitted. Its ADC path completes successfully
  and reads about 4.0 V while USB-powered, but that is the unloaded charger node,
  not a cell voltage; removing USB powers the board down. Fit a battery before
  validating voltage against a meter, unplug/charge transitions, retention, and
  the state-of-charge curve. Do not treat the no-battery reading as validation.

### 12. Bluetooth/NimBLE — pairing and reversed PPoG hardware-proven

- Keep Pebble's NimBLE host and Bluetooth services.
- DVT3 enables `T5AI_CP_HCI_VALIDATED`; the controller stub is no longer the
  active path. Controller registration/init, commands, asynchronous events,
  bidirectional ACL, legacy advertising, a BlueZ connection, ATT responses, and
  GATT service resolution are proven on hardware.
- The physical CP mailbox has one native owner. It permits one ordinary
  AP-to-CP message in flight, so every non-transport-ACK send is serialized
  until physical completion. Flash, SARADC, log, and MIPC/HCI traffic share the
  dispatcher without draining one another's channels.
- HCI callbacks copy borrowed controller data before physical acknowledgement.
  An eight-entry receive FIFO preserves interleaved transport ACKs, MIPC ACKs,
  application replies, events, and ACL packets. Callback-response descriptors
  remain owned until their logical ACK.
- HCI command and ACL request descriptors contain pointers to separate packet
  buffers. The transport now cleans those nested payload bytes from the AP data
  cache before CPU0 dereferences them. This fixed the reconnect-time HCI memory
  error that surfaced as an ATT transmit assertion despite all host mbufs being
  free. Reset precedes callback registration when the transport is restarted,
  matching the controller lifecycle.
- The vendor-compatible MIPC ABI uses fixed 32-bit shared pointers and channels
  `0x17` AP-to-CP and `0x47` CP-to-AP. The CP's recurring CRC log is non-fatal
  when a valid logical reply follows and matches pinned vendor behavior.
- Ensure the vendor Bluetooth stack and Pebble NimBLE never own the controller simultaneously.
- Numeric-comparison pairing is proven. DVT3 maps confirm/decline to its two
  fitted buttons: short-press `KEY`/Select confirms and `PWR`/Back declines;
  the absent Up/Down inputs are not used.
- Encryption is proven by subscribing to reversed PPoG, whose server rejects
  unencrypted subscriptions. Encrypted GATT writes reached the watch, a full
  phone-initiated PPoG reset exchange completed, and Pebble logged an open
  reversed version-1 session with TX/RX windows of 4. Same-boot disconnect and
  reconnect also succeeded without confirmation.
- Shared-PRF persistence writes are enabled within the validated partition and
  the PRF backend enumerates the saved BLE pairing. After reboot, Bluetooth
  Settings displays the phone's correct name and reconnects without a new
  pairing. Firmware-only AP flashing preserves this partition and the bond.
- T5AI initializes the complete normal service set after its board-specific
  hardware sequence. This removed crashes from protocol endpoints reaching
  uninitialized music, AppDB, and timer state. The common service initializer
  itself is unchanged.
- GATT discovery is streamed only on T5AI so completed services are handed to
  Pebble and freed as discovery proceeds. The shared discovery implementation
  remains stock. The T5AI host mbuf pool is 35 blocks, matching the established
  nRF52 configuration and providing pairing/discovery headroom.
- The phone app reaches a stable connected state but identifies the watch as
  `connected(factory)` and reports an unknown platform. Device/platform identity
  metadata is the next functional issue. Longer reconnect cycling, cold
  power-cycle retention, sustained payload transfer, and Bluetooth low-power
  behavior remain to be measured.
- Replace polling with IRQ delivery and validate Bluetooth low-power behavior
  only after the functional data path is stable.

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

### 14. Image packaging and boot flow — AP-only path proven; product recovery pending

Status correction (2026-09-13): the later persistence work enabled bounded
runtime writes to system-resource bank 0, coredump, debug-log, shared-PRF, and
PFS regions. Older bullets below that say those individual facilities are
disabled are retained as investigation history and are superseded. Firmware
and recovery updates, raw arbitrary flash access, and OTA remain disabled.

- The pinned `tuya_app` table places 68 KiB primary bootloader, 68 KiB Tuya
  BL2, and 1088 KiB CP application partitions before the AP. The resulting AP
  physical offset is `0x132000`; 34:32 translation maps it to logical offset
  `0x120000` and XIP address `0x02120000`. Its 3808 KiB physical allocation is
  exactly `0x380000` logical bytes. Host tests enforce these relationships.
- The official outputs are not interchangeable with Pebble's artifact: QIO is
  a complete physical-flash image, UA concatenates Tuya BL2 + CP + AP, and UG
  adds a 64-byte compressed-OTA header. `pebbleos.t5ai-crc.bin` is deliberately
  an AP-only protected payload with no QIO/UA/UG header. It boots only within
  the retained vendor bootloader/CP environment and is not an update container.
- The pinned BK7258 packager's protected-block transform is implemented and
  host-tested: 32 logical bytes, `0xff` tail padding, then a big-endian Beken
  CRC-16 (polynomial `0x8005`, initial value `0xffffffff`). T5AI builds now
  reproducibly emit `pebbleos.t5ai-crc.bin` in addition to the raw binary. The
  packager now rejects a raw image larger than the `0x1f0000` experimental slot
  and verifies that the slot remains inside the pinned factory AP partition.
- The pinned SDK selects its normal bootloader and its packaging path contains
  no signing step for this configuration. That does not prove the factory
  device's OTP/fuse policy or authorize replacing its AP payload.
- The protected payload repeatedly enters Pebble's reset handler and boots the
  native runtime. Exact-range AP-only programming and recovery to the known-good
  diagnostic are proven. Two matching full-flash backups exist, but complete
  factory restoration has not needed to be exercised.
- DVT3 uses a flash-only `tyutool` runner. It creates one exact-range protected
  AP image containing firmware, the system-resource pack, and a preformatted PFS;
  the bootloader and CP partitions are not included. Reset, run, erase,
  debug-server, direct resource imaging, and recovery imaging remain gated.
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
  exactly the firmware region (`0x02120000..0x02210000`) and reject any NOR
  VMA/load address outside it. A successful check stamps
  `build/pebbleos.image-layout.ok`; `.bin`/`.hex` generation depends on it.

### 15. Host validation — current baseline complete

- Maintain the clean native compile and link with no unresolved placeholder
  drivers as further changes are made.
- T5AI host packaging enforces a 1 MiB logical system-resource pack ceiling;
  the current 1,026,537-byte pack fits narrowly. The flash-only runner installs
  it into the single protected logical `0x00210000..0x00310000` resource bank
  (physical `0x00231000..0x00341000`). Runtime writes to this bank are now
  permitted, but there is no redundant bank or validated rollback path. The
  PFS remains a raw CP-flash partition at
  physical `0x00350000..0x00450000` after a sector-alignment gap.
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

Current touch/display-focused baseline: all 44 tests in
`test_tuya_kernel`, `test_tuya_image_packaging`, and `test_tuya_image` pass.
The complete documented eight-module command runs 88 tests and all pass. The
DVT3 native firmware compiles and links without unresolved platform symbols or
internal-RAM overflow. Host-generated ELF, map, raw binary, Intel HEX,
protected-block, and checked layout artifacts are available.

### 16. Continued hardware validation

The factory image is preserved and AP-only programming/recovery are proven.
CPU1 startup, the CP handshake, dual USB roles, scheduler/timebases, buttons,
I2C/RTC, display, touch, accelerometer, SARADC, and normal NimBLE advertising,
ACL, ATT, GATT discovery, pairing, persistent bond enumeration, reconnect, and
reversed Pebble Protocol are proven. The next Bluetooth work is correcting the
`factory`/unknown-platform identity, followed by longer reconnect and
power-cycle soak testing. Common protocol-backed services now initialize before
the connection can reach them. Fit a battery before charging or state-of-charge
validation. General writable storage, resources, and OTA remain high-risk
because they require a tested corruption and full-recovery policy. Bounded
persistence/resource writes are enabled; OTA and firmware/recovery writes
remain disabled.

2026-09-12 watchdog follow-up: decoded UART captures separated two failure
modes. First, the task watchdog reported `status=0x180 mask=0x182`, with
`KernelBG` blocked in the common I2C semaphore while `prv_poll` was current.
The CST92xx transaction now runs in a dedicated worker rather than inside the
watched system task, so a delayed peripheral timeout cannot stall system
callbacks. A subsequent reset was not a watchdog trip: after a roughly
45-second CST wait, 30 once-per-second advertising-cycle callbacks filled the
undrained system-task queue. T5AI now runs the bounded KernelBG callback runner
at priority 4 alongside NewTimer. The touch worker was subsequently raised to
the same blocking priority 4 after hardware counters proved it could be
starved; KernelMain remains 3 and app/render 1. The idle watchdog also treats
`s_current_cb == NULL` as healthy;
a genuinely stuck KernelBG callback still withholds its feed. The resulting
801,890-byte AP payload has SHA-256
`fd4a8dc4f5f9e8bae4a0c0537798e1929d5dc86375b9b32eecd6cf0b2ad87781`
and occupies `0x132000..0x1f5c62`. Exact-range flashing succeeded, all 79
focused tests pass, the native build and `git diff --check` pass, and the board
completed a 120-second untouched soak with no watchdog warning, queue-full
error, or reboot. Physical-swipe stability and navigation were the immediate
hardware confirmation and are recorded below.

2026-09-12 panel/touch milestone: the native CO5300 path now boots through the
PebbleOS splash and charging animation into the launcher at forced 100-percent
brightness. CST92xx bring-up was completed by acknowledging every report with
`0xAB` at register `0xD000`, running its bounded poller at a priority that is
not starved by KernelMain, and matching the BK7258 I2C controller's transmit
and receive FIFO-level behavior. A real upward path decoded as 85 valid
reports with 48 coordinate changes (`first=132,204`, `last=151,65`). The touch
service and app event loop delivered one touchdown, 48 moves, and one liftoff;
the unified Tier-1 MenuLayer recognizer completed one pan with 46 live updates,
and the user confirmed that the launcher visibly moved. The queue drained on
the normal shared blocking queue-set path (`take=70`, all member depths zero at
the first settled sample); no T5AI polling or semaphore alternative remains in
`events.c`. A later sample reached 182 seconds of uptime without reboot. The
current 802,400-byte AP payload has SHA-256
`0fae1147d8372bf1b2fde0d53b6daf86cac108714f6d57d61ad81eb2d25df9ba`
and occupies exactly `0x132000..0x1f5e60`. Exact-range flashing succeeded, the
native build passes, `git diff --check` passes, and all 80 focused tests pass.

2026-09-13 clock/display/touch follow-up: the scheduler clock contract was
corrected to the hardware-proven 120 MHz AP clock. Runtime samples advanced by
58,997 FreeRTOS milliseconds versus 59,001 RTC milliseconds, eliminating the
earlier approximately two-times scheduler skew without re-electing the
boot-owned core clock. The display now uses cache-managed BK7258 GDMA staging
for pixel aperture writes, with a successful boot-time self-test and bounded
CPU fallback. The latest touch revision follows the board schematic rather
than the incomplete Tuya abstraction: GPIO42 is `TP_INT`, GPIO43 is
`TP_RESET`, falling-edge EXTI wakes a dedicated semaphore-blocked worker, and
only an active contact receives the 20 ms lost-edge fallback. The shared I2C
common layer is exactly stock and CST92xx bus bit-bang recovery was removed.
The current build uses 769,315 bytes of FLASH, 67,456 bytes of KERNEL_RAM,
67,614 bytes of T5AI_DISPLAY_RAM, and 112,064 bytes of LOG_STRINGS. Its
817,428-byte protected AP payload has SHA-256
`78bfd52829937518d2e74283557cc296459a35d868ec1f9592679cf22c6cb1f8`.
The combined 3,268,608-byte flash image has SHA-256
`b649ff0c6c8bf256f235038368b73cd74d9aea8ae904bfd2d5cb3cdedebd4411`.
It built, flashed, reached `Ready for communication`, and remained up through
the observation window without a reboot. Final physical swipe/navigation and
idle interrupt-counter validation are still pending for this exact image.

2026-09-13 Bluetooth checkpoint: DVT3 now pairs with a phone, persists and
enumerates the BLE bond, reconnects after reboot, completes encrypted GATT
discovery, subscribes to reversed PPoG, opens the Pebble Protocol session, and
appears connected in the Pebble phone app. The successful trace continued
through music-service connection and the switch to normal connection parameters
without a reboot. The phone currently labels it `connected(factory)` and reports
an unknown platform; that identity mismatch is the next Bluetooth issue.

The final reboot fault had two independent causes. First, the T5AI CP mailbox
request publishes a descriptor containing a pointer to the HCI packet. Cleaning
only the descriptor left CPU0 reading stale ACL bytes and returning HCI memory
capacity, which NimBLE surfaced as an ATT transmit assertion. The T5AI IPC layer
now cleans the pointed-to packet before publishing the request. Second, the
board-specific startup exposed the common protocol endpoint table without
initializing all endpoint owners. It now initializes the safe common transport
pieces in the T5AI Bluetooth sequence and calls `services_normal_init()` after
writable PFS is available. This supplies the missing music, AppDB, timer, and
other normal-service state without adding board-specific initialization to
shared service code.

Persistent shared-PRF writes are now enabled within their bounded partition,
and the PRF Bluetooth backend reports the saved pairing during enumeration. A
firmware-only AP flash leaves that partition intact. T5AI uses a local streaming
GATT discovery implementation to release completed service allocations while
preserving the stock shared implementation for other boards, and its NimBLE
host mbuf pool is sized to 35 blocks. Temporary allocator, mutex, ATT, timer,
music, transport, and serial-trace diagnostics have been removed. Persistent
coredumps and unsafe firmware/recovery writes remain disabled. Cold power-cycle
retention, long reconnect soak, sustained payload transfer, and Bluetooth
low-power behavior remain to be validated. The diagnostics-free checkpoint
build uses 873,075 bytes of FLASH, 81,536 bytes of KERNEL_RAM, 67,614 bytes of
T5AI_DISPLAY_RAM, and 120,177 bytes of LOG_STRINGS. Its 927,656-byte protected
AP payload has SHA-256
`ebd702307935cd5f04d21cc19e371bb8f83e6dd96bff2a5ca17715e5ba7b291e`
and occupies `0x132000..0x2147a8`; it was built and checked but deliberately
not reflashed after the successful phone session.

### 2026-09-15 display PSRAM cache decision

Interactive profiling by physical use showed that removing the T5AI-only
KernelMain event-loop delay and reducing CO5300 transfer overhead improved
responsiveness, but scrolling and full redraws remained visibly slow. The
260x260 ARGB2222 system framebuffer cannot fit in the available kernel SRAM and
therefore remains in the dedicated PSRAM reservation at `0x60000000`. App
rendering normally targets internal App RAM, after which the compositor writes
the dirty area into this system framebuffer and the display driver reads it
again while expanding it to the 466x466 RGB565 panel stream. PSRAM latency thus
affects both the compositor-copy and panel-conversion sides of every frame.

The selected experiment explicitly maps only the 128 KiB
`T5AI_DISPLAY_RAM` reservation as privileged, execute-never,
write-back/write-allocate memory. The mapping uses fixed MPU region 10 and the
existing Pebble `MpuRegion`/ARMv8-M MAIR policy rather than programming raw MPU
register encodings in the display driver. Region 10 was selected after checking
the objects in the actual native build: Pebble's linked ARM_CM33 FreeRTOS port
uses per-task regions 4 through 7, Pebble's global memory layout uses regions 0
through 3, and the inherited boot map can retain fixed regions 8 and 9. The
Tuya FreeRTOS bridge under `boards/getafix/tuya/kernel` is reference and test
material and is not linked into `pebbleos.elf`; its different MAIR and region
rules must not be used to infer the native image's live MPU ownership.

The range and region number are centralized in
`include/pbl/soc/t5ai/memory.h`. `board_t5ai.c` installs the fixed mapping from
`board_early_init()`, after Pebble's global MPU initialization and before the
compositor first accesses its `NOLOAD` framebuffer. The only shared-code change
is a `CONFIG_SOC_T5AI`-guarded extension to `mpu_memory_is_cachable()` so shared
cache-aware callers report the display reservation accurately. Generic MAIR
indexing, permissions, and task-region encoding were deliberately left
unchanged after the linked-port audit showed that they already own the native
configuration.

The reservation currently contains the 67,614-byte compositor framebuffer and
T5AI-local display/flash caches; the linked total is 107,680 of 131,072 bytes.
CPU code owns the framebuffer. BK7258 GDMA reads the CO5300 staging buffer from
the same reservation, so the display driver must continue cleaning the exact
staging range before every DMA launch. Any future DMA engine, CP-side access,
framebuffer capture, or external writer touching this reservation must define
the corresponding clean/invalidate ownership transition before the cached
mapping can be considered production-safe. Moving another shared buffer into
this linker region without such an audit is prohibited.

The first cached build used 874,035 bytes of FLASH, 83,072 bytes of KERNEL_RAM,
and 107,680 bytes of T5AI_DISPLAY_RAM. All 26 `test_tuya_kernel` checks passed;
the image built, flashed, reached runlevel 4 and `Ready for communication`, and
showed no MPU fault, assertion, watchdog reset, or display-DMA failure during
the captured observation window. Boot-to-runlevel was approximately 1.8
seconds shorter than the immediately preceding trace, but that comparison was
not controlled and is evidence for follow-up rather than a performance claim.
The AP core clock remains 120 MHz.

Before accepting this mapping permanently, measure `render`,
`framebuffer_prepare`, and `framebuffer_send` with the FPS test; exercise full
redraws, scrolling, rotation, screenshots, and repeated suspend/wake; inspect
region 10 and its MAIR attribute on target; and complete a longer idle and
interactive soak. Any stale rows, intermittent corruption, DMA mismatch, MPU
fault, or unexplained reset is grounds to revert the region installation and
the matching cacheability-query entry while retaining the independent display
DMA batching and scaler optimizations.

### 2026-09-15 QSPI lane-width follow-up

Confirm on hardware that CO5300 pixel data is actually transmitted over all
four QSPI data lanes. Do not treat the `0x32` pixel prefix, the command-C
`data_line` field, enabled IO2/IO3 pins, or command-A register contents as proof
of physical quad-lane operation. Capture CLK, CS, and IO0 through IO3 with a
logic analyser during a full-frame update, decode the `32 00 2c 00` preamble,
and verify that subsequent RGB565 data advances four bits per clock on all four
lanes.

Cycle-counter profiling at a 120 MHz AP clock measured a representative
434,312-byte full-frame update at approximately 116 ms: 38--40 ms in software
conversion/scaling, 34--35 ms cleaning the cached PSRAM staging data, 17--18 ms
waiting for GDMA/QSPI, and the remainder in row callbacks, duplication, and
command overhead. The isolated 17--18 ms hardware-transfer bucket corresponds
to roughly 25 MB/s, below the ideal 40 MB/s payload rate for 80 MHz quad QSPI
but above an 80 MHz single-lane ceiling. Account for GDMA setup, aperture
back-pressure, command gaps, and measurement boundaries before attributing the
difference to lane width.

As a software cross-check, Tuya's BK7258 LCD-QSPI driver programs command A as
`L=0x00002c00`, `H=0x10000100`, `CFG1=0x0000eaaa`, and
`CFG2=0x80008000`. Temporarily applying those exact values in PebbleOS did not
improve the measured frame time or transfer bucket, so that experiment was
reverted. Later review should compare the complete live QSPI register set and
pin mux against the vendor firmware, then use physical IO0--IO3 activity as the
deciding evidence. Retain 80 MHz unless signal-integrity measurements justify
another supported clock; raising the AP core clock is not a substitute for
correct lane configuration.

## Validation commands

```sh
cd /home/baz/coredevices/pebbleos
. .venv/bin/activate

python pbl configure --board getafix@dvt3
python ./waf build

python -m unittest \
  tools.tests.test_getafix_boards \
  tools.tests.test_t5ai_boot_probe \
  tools.tests.test_tuya_flash \
  tools.tests.test_tuya_hardware \
  tools.tests.test_tuya_image \
  tools.tests.test_tuya_image_packaging \
  tools.tests.test_tuya_kernel \
  tools.tests.test_tuya_memory

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
