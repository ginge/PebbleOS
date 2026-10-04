# Unhygienix dependency, build and flash tooling

Unhygienix means "The village fishmonger." The independent hardware target is
`unhygienix` (`CONFIG_BOARD_UNHYGIENIX`), with `PLATFORM_GABBRO` apps and the
existing Round 2/Getafix DVT2 companion identity. Getafix DVT/DVT2 hardware
configuration is separate.

Run commands from the PebbleOS repository root. Keep supplied build inputs and
generated validation artifacts under `build/`.

## Dependency setup

```sh
git submodule update --init third_party/hal_tuya/TuyaOpen-T5AI
python pbl configure --board unhygienix
python pbl build
```

Use the normal PebbleOS SDK/toolchain setup and Python requirements. Native AP
builds do not compile or link the Tuya application, vendor RTOS or vendor BLE
host. Native board, SoC and drivers retain ownership of the AP implementation.
The platform submodule pins `13379b63e07e78770fb4d0bffe36db2754658132`.
TuyaOpen pins `e82840dc9b629cb77fa922fb04e3f820dc2e78b4` and is optional for AP/CP
packaging. Neither submodule is upgraded implicitly.

## CP packaging

The stock CP is a generated vendor artifact, not tracked source in either SDK.
Supply the validated stock `app.bin` with SHA-256
`0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856`.
A fresh checkout needs this artifact separately to package CP. Keep the original
for rollback; unknown input images are rejected before compilation.

```sh
python pbl waf t5ai_cp --t5ai-cp-stock build/inputs/stock-cp.bin
```

This uses the configured ARM compiler, builds the board-local RF hook, and emits
`build/pebbleos.t5ai-cp.bin`, `pebbleos.t5ai-cp-crc.bin`, and
`pebbleos.t5ai-cp.json`. The manifest records SDK revisions, stock and hook
hashes, output hashes and the supported Unhygienix mailbox/HCI ABI. It describes a
source-hook integration into a pinned binary, not a full CP source rebuild.
Changes to CP ABI, addresses or radio policy require revalidation.

## Native flashing

The tyutool runner uses the Tuya UART boot protocol; generic OpenOCD/PULSE
programming is not established on this hardware. The validated backend is
`tyutool_cli` 3.2.9; install it independently and pass its executable path.
Serial ports are explicit; prefer the stable programming UART `if00` by-id path.

```sh
python pbl flash --dry-run --tyutool tyutool_cli --tyutool-tty /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00
```

Remove `--dry-run` to write. Default is AP firmware only. `--resources` includes
system resources. `--t5ai-provision --resources` also formats PFS, destroying
stored apps/data. `--t5ai-cp` includes CP from the build directory after vector,
CRC, size and manifest validation. All selected inputs are checked before the
first write. CP is written first if selected; failed writes stop the sequence.
Multiple partition writes are not atomic. Factory bootloader, configuration,
RF calibration and shared pairing storage are excluded from these operations.
Erase, recovery and OTA remain disabled. Existing dedicated guard scripts remain
usable for validation and rollback.

## Optional vendor reference build

```sh
git submodule update --init --recursive third_party/hal_tuya/TuyaOpen
python pbl waf tuya_reference --tuya-python .venv-tuya/bin/python
```

Use the pinned TuyaOpen `pyproject.toml` and `uv.lock` for its separate Python
build environment. The builder stages both SDKs under `build/unhygienix_hw/sdk`
and invokes the vendor generator there, keeping submodule source pristine.
Vendor toolchain downloads remain governed by the pinned T5AI scripts. This
reference target is optional; it does not build the native PebbleOS kernel or
flash hardware. The vendor MPU/kernel compatibility checker can consume the staged SDK after
its compilation database exists.

## Validation

Check native AP builds and existing image/map tests, then runner tests for exact
write ranges, default PFS preservation, explicit provisioning and rejection of
corrupt AP/CP inputs before any write. Hardware qualification still requires the
watchface regression, 25 reconnect cycles, cold-boot bond/face restoration and an
overnight connected-idle soak. Preserve the complete first failure trace.

