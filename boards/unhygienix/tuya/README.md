# Optional Tuya application and CP inputs

`src/`, `tests/`, `app_default.config` and this directory's `CMakeLists.txt`
belong to a separate TuyaOpen peripheral bring-up application. It is built by
[`tools/build_tuya_hardware.py`](../../../tools/build_tuya_hardware.py) in a
private SDK staging tree. Its startup banner explicitly says the PebbleOS kernel
is not running. It is not the `unhygienix` PebbleOS application or a replacement
for production drivers, scheduler, storage, touch class or Bluetooth services.
Do not flash that application as part of a PebbleOS source-cleanup check.

The production AP board is [`unhygienix.c`](../unhygienix.c), with drivers under
`drivers/` and SoC code under `soc/t5ai/`. Production vendor inputs are pinned
under `third_party/hal_tuya/`. The workspace
`/home/baz/Documents/ChatGPT/pebble` is external reference/evidence material;
it is not a second production source checkout.

`cp/` has a different role: its RF/latency sources, contracts, overlays and
reviewed manifests are inputs to the production CP tooling. They must retain
pinned hashes, exact ABI checks and ownership/relocation guards. Do not dismiss
them as unused bring-up code or modify vendor submodules to match this app.

## Compared configuration — 22 November 2026

| Property | Optional application | Production AP | Decision |
| --- | --- | --- | --- |
| Power latch, Back, Select | GPIO19, 18, 12 | GPIO19, 18, 12 | Same wiring |
| Amplifier disable, charge status | GPIO28 high disables; GPIO30 active low | Same | Preserve polarity |
| I2C bus | I2C0, SCL20, SDA21 | Same, shared 100 kHz | Preserve shared RTC/IMU/touch bus |
| RTC | `0x51` | `0x51` | Same address |
| Touch reset/interrupt | Reset corrected from GPIO42 to **43** | Reset43, interrupt42 | Schematic supports production wiring; GPIO42 must not be driven as reset |
| Panel | 466×466 RGB565, QSPI0 at 80 MHz, reset29, X offset6 | Same | Preserve full rectangular 260×260 ARGB2222 logical canvas |
| Touch orientation | Mirror native coordinates, then scale | Scale, then native touch class mirrors both axes | Same direction; preserve existing one-pixel rounding differences |
| Battery acquisition | Channel15/P13, peak of16 samples | Same | Same ADC/divider tap, not interchangeable service lifetimes |
| Battery calibration | Caller-supplied measured two-point calibration; main passes NULL and logs raw | Board low2716/span2854, divider2510/510 | Intentionally distinct; do not borrow coefficients or invent volts from raw reference readings |

The reset correction uses existing hardware evidence, not a guessed agreement
with the vendor app. [Touch review](../../../docs/boards/unhygienix/touch-power-review.md)
records the manufacturer's schematic pin table (GPIO42 TP_INT, GPIO43 TP_RESET),
the fitted controller identity and prior working native touch/disable-enable
checks. Archived schematic SHA-256:
`8ce9c02095ac6f903ef6036a18a15e692957860788b2734e20b48415070d04b9`.
The pinned upstream TuyaOpen board file still calls GPIO42 reset. Leave that
pinned vendor file unchanged; it is a known reference discrepancy, not authority
to change the working AP wiring. This source correction has not been tested by
flashing the optional application.

The native touch driver scales each axis with integer truncation before class
orientation (`259 - floor(native * 260 / 466)`). The optional core mirrors first
(`floor((465 - native) * 260 / 466)`). Their outputs differ by zero or one logical
pixel, including some interior coordinates. Neither direction nor the qualified
production rounding is changed to force equality. The optional app also lacks
the production driver's ACK/palm/recovery behavior; it is not a behavioral
replacement for that driver.

[Battery review](../../../docs/boards/unhygienix/battery-review.md) records the
production calibration provenance and remaining actual-cell limits. The current
bench remains battery-disconnected. Configuration comparisons do not qualify
voltage accuracy, charging transitions or current consumption.

`tools.tests.test_unhygienix_reference` checks the source declarations for pins,
bus/geometry/format and calibration policy. These static checks fail closed when
an expected declaration is missing or duplicated; they are not C layout models
or substitutes for compiling the complete board. Its behavioral comparison links
the complete reference `hardware_core.c` with the actual production touch driver
and native touch class under the existing real-header sanitizer harness. All 466
axis values retain their documented rounding relationship and out-of-range
rejection. Existing reference core/runtime and production touch/battery suites
provide complementary coverage. No new optional full SDK build or hardware
qualification follows from these host tests.
