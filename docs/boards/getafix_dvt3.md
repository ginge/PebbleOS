# Getafix DVT3: Waveshare port baseline

`getafix@dvt3` is an independently selectable copy of the DVT2 configuration,
intended as a starting point for work on the Waveshare T5-E1-Touch-AMOLED-1.75.
It is **not a working port**. Do not flash its binaries to the Waveshare board.
The name is a local development revision, not an official Pebble Round 2 revision.

## Build the baseline

After following {doc}`../development/getting_started`:

```sh
./pbl configure --board getafix@dvt3
./pbl build
```

Recovery firmware uses `./pbl configure --board getafix@dvt3 --variant=prf`.
The target still inherits SF32LB52 startup, memory layout, bootloader, pin
assignments, peripherals, and the SiFli `sftool` runner. That runner is not
suitable for the Tuya board. No hardware operation is implied by a successful
build. The firmware hardware ID is explicitly `FirmwareMetadataPlatformUnknown`;
an ID and companion tooling support must be agreed before distributing images.
DVT3 is deliberately absent from release/publishing matrices.

## Codebase map

- `tools/waf/boards.py` discovers revisions from `boards/getafix/getafix.yml`.
- `tools/waf/kconfig.py` combines the board defconfig, revision defconfig,
  firmware project config, normal/PRF config, and command-line overrides.
- `boards/getafix/Kconfig.dvt3` defines only the DVT3 revision symbol; DVT2 is
  not enabled as an alias. Both revisions select AW86225 haptics.
- `src/fw/board/wscript_build` selects the shared Getafix board/display sources.
- `src/fw/board/boards/board_getafix.c` supplies pins and peripheral instances.
- `src/fw/board/displays/display_getafix.*` defines the 260 x 260 round display
  and row masks. Display selection and splash routing live one directory above.
- `soc/`, `src/fw/startup/`, `src/fw/linker/`, and `third_party/hal_sifli/`
  implement the current CPU, interrupt, memory, and HAL integration.
- `src/fw/drivers/` and `include/pbl/drivers/` contain peripheral implementations
  and interfaces; `src/fw/services/` consumes those drivers.
- `src/fw/applib/`, `src/fw/apps/`, `resources/`, and `sdk/` provide the UI,
  applications, resources, and application compatibility layer.
- `include/pebbleos/firmware_metadata.h` selects the on-device hardware identity.

## Hardware differences and remaining work

Waveshare's [product specifications](https://www.waveshare.com/t5-e1-touch-amoled-1.75.htm)
and [wiki](https://www.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75)
describe hardware that cannot be supported by changing Getafix pins alone.

| Area | Current DVT3 baseline (from DVT2) | Waveshare target |
| --- | --- | --- |
| MCU/HAL | SiFli SF32LB52 | Tuya T5-E1, Armv8-M STAR-MC1 |
| Display | Getafix 260 x 260, JDI interface | 466 x 466 AMOLED, CO5300 over QSPI |
| Touch | CST816 | CST9217 over I2C |
| Motion | LIS2DW12 | QMI8658 accelerometer/gyroscope |
| RTC | Current SoC integration | Onboard PCF85063 |
| Memory | Existing Getafix flash layout | Advertised 8 MB flash, 16 MB PSRAM |

Porting sequence:

1. Integrate the Tuya SDK/SoC support, startup, interrupt vectors, clocks,
   memory/linker layout, boot image format, and flashing tools. First verify
   serial output and scheduler operation on hardware.
2. Use the vendor schematic and matching SDK examples to establish safe power,
   reset, GPIO, I2C, and QSPI mappings. Do not reuse Getafix pin assignments.
3. Implement CO5300 display and CST9217 touch support. Update framebuffer size,
   round row masks, orientation, coordinate transforms, and resource/UI scaling
   together; merely changing the display dimensions is insufficient.
4. Integrate QMI8658, RTC, buttons, charging/battery, audio, and BLE; disable
   unsupported Getafix devices and services until replacements exist. Validate
   power sequencing and sleep/wake behavior before battery operation.
5. Validate normal and recovery images, assign a hardware identity, and only
   then add deployment/release support.

No Waveshare GPIO mappings, new HAL, peripheral drivers, or display geometry
changes are implemented by this baseline revision.
