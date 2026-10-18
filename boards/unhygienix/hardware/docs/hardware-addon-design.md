# Unhygienix hardware add-on and enclosure brief

Status: preliminary design, 2026-10-18. This text records the hardware discussion
and is the handoff input for enclosure and PCB design. No PCB has been routed,
no manufacturing files have been released, and no combined enclosure fit or
battery runtime has been verified. Dimensions marked as reservations are design
allowances, not manufacturer drawings.

## Owner requirements and decisions

- Host: Waveshare T5-E1-Touch-AMOLED-1.75, Tuya T5AI/BK7258; PebbleOS board
  target `unhygienix`. The exact PCB revision and GPS/non-GPS variant still need
  checking against the physical unit.
- Fit a custom watch case while staying within the existing display assembly's
  footprint as far as practical. Minimise total thickness, component cost and
  PCB area. Do not claim that an unmeasured existing case has enough free volume.
- Add crisp LRA haptics, two physical Up/Down buttons, ambient light sensing and
  a tiny speaker. Keep the existing Back/Select functions, for four logical
  buttons in total.
- Scope compass/magnetometer and barometer as independent population options.
  A magnetometer plus the existing accelerometer provides the compass; these
  are not two separate added sensors.
- Optional heart-rate input: reserve a module connection and investigate a rear
  optical module only if requested. Its sensor/optics are not selected.
- Battery: rechargeable single-cell 3.7 V nominal, standard 4.2 V full-charge
  chemistry. Aim for 48 hours with the display mostly asleep. The owner prefers
  a thinner premium cell over the originally proposed thick battery.
- Current slim-cell enquiry target: approximately 45 mm shaped outline,
  3.5-4 mm cell thickness, 750-800 mAh. This is a target specification, NOT a
  verified available battery. Final capacity, pack drawing, price and MOQ remain
  unresolved. Do not freeze the case around an imaginary cell.

## Repositories, existing work and supporting documents

Production source belongs in `/home/baz/coredevices/pebbleos`.
Canonical hardware artifacts belong in
`/home/baz/coredevices/pebbleos/boards/unhygienix/hardware/`. The separate workspace
`/home/baz/Documents/ChatGPT/pebble` holds reference material and artifacts;
do not modify its source files for this work. Preserve stock common Pebble code;
prefer board-, SoC- or driver-local implementation.

Supporting illustrated proposal:
[`../pdf/t5ai-pebble-addon-proposal.pdf`](../pdf/t5ai-pebble-addon-proposal.pdf).
That PDF retains the larger battery as a comparison. This text explicitly records
the later preference for a slimmer premium cell. Neither is a released design.

See also [current board status](../../../../docs/boards/unhygienix/index.md), [battery subsystem review](../../../../docs/boards/unhygienix/battery-review.md)
and [microphone integration](../../../../docs/boards/unhygienix/microphone.md). The source tree contains unrelated
in-progress firmware changes. Read current source before relying on implementation
status below; do not revert or overwrite another agent's work.

## Host dimensions and required mechanical evidence

The Waveshare product dimension drawing gives:

| Item | Published dimension | Interpretation |
| --- | --- | --- |
| Overall assembly outline | 51 mm diameter | External assembly dimension; not usable internal cavity or a verified bare-PCB outline |
| Visible display width | 43.76 mm | Not the complete glass, module or case outline |
| Stock assembly side view | 11.70 mm, 12.10 mm overall | Existing assembly dimensions; not free height for additions |

Source: [Waveshare dimension drawing][host-dimensions]. The separate nominal
1.75-inch display designation must not replace the dimensioned mechanical drawing.

No custom case CAD or dimensioned internal cavity has been supplied. Obtain the
manufacturer 3D drawing from the [host resources][host-wiki], check the actual
unit/revision, and establish PCB, glass, connectors, USB, buttons, fasteners,
display flex and antenna positions. Manufacturer outer dimensions alone cannot
prove clearance for any added hardware.

## Candidate BOM and integration

Costs are rough GBP allowances for small batches, excluding VAT, shipping,
assembly/setup, tooling and supplier MOQ. They are not quotations.

| Ref/function | Candidate | Hardware allowance / notes | Cost allowance |
| --- | --- | --- | --- |
| U1, haptics | TI DRV2605LDGSR, VSSOP-10 | Driver body 3 x 3 mm; land pattern is larger; no boost inductor proposed | 1.0-2.5 |
| M1, actuator | Small coin LRA; Vybronics VG0825003D candidate | Part/drawing and installed envelope not frozen; sample before finalising its pocket | 1.5-4.0 |
| SW1/SW2 | Side-actuated normally-open SMT switches | Exact part, travel, actuator height and case seal TBD | 0.3-1.0 pair |
| U2, ambient light | TI OPT3001DNPR | 2 x 2 x 0.65 mm IC; needs external optical window and display-light barrier | 0.9-1.6 |
| U3, compass option | MEMSIC MMC5603NJ | 0.8 x 0.8 x 0.4 mm WLP; magnetic layout is more important than footprint | 1.5-3 with bypass |
| U4, pressure option | Bosch BMP390 | 2 x 2 x 0.75 mm LGA; requires pressure path to outside | 2-4 with bypass |
| Pressure vent | Hydrophobic pressure-equalisation membrane | Exact part, adhesive land and ingress requirement TBD | 0.5-2 estimate |
| SPK1, speaker | Soberton SP-1003, 8 ohm, 0.3 W, wire leads | 10 mm diameter; reserve 2.9 mm height pending current drawing | 1.5-3 |
| Speaker mounting | Gasket, retention, grille/duct | Optional acoustic membrane is additional and unquoted | 0.3-1 |
| B1 | Protected 3.7 V LiPo pack | Slim premium candidate remains unselected; see battery options | Quote required |
| HR interface | Five unpopulated solder pads | 3V3, GND, SCL, SDA, optional interrupt; module not included | No populated sensor |

Base electronics/actuator estimate was GBP 6-12. Including the speaker/mounting,
allow approximately GBP 8-16 before battery and optional sensors. Compass and
barometer/vent together add roughly GBP 4-9. Final sourcing may change these.

### PCB construction and assembly

Start with a two-layer 0.6 mm FR-4 board, 0402 passives and soldered wires/pads.
Avoid added tall headers, breakout boards, duplicate regulators and another MCU.
Keep manufacturer-required antenna clearance; do not cover it with added copper,
the metallic battery pouch, actuator or speaker.

The original electronics core placement budget was about 10 x 12 mm, excluding
switches, actuator, mounting and wiring. Compass/pressure add about 25-35 mm2
distributed around the PCB, excluding magnetic clearance and vent land. These
are estimates, not routed dimensions. A crescent/strip or small wired sensor
islands may be necessary. The final PCB outline must be coordinated with the case.

### Proposed interface assignments

These are preliminary and require actual H3 continuity, boot ownership, GPIO mux
and wake validation. GPIO numbers are SoC signals, not connector pin numbers.

| Host connection | Proposed role |
| --- | --- |
| H3 pin 1 | 5 V: not used as the add-on logic supply |
| H3 pin 2 | Ground |
| H3 pin 3 | 3V3 supply; qualify regulator headroom under radio/display/haptic load |
| H3 pin 4 / IO14 | ADD_SCL, separate GPIO I2C bus |
| H3 pin 5 / IO15 | ADD_SDA, separate GPIO I2C bus |
| H3 pin 6 / IO16 | UP_N, switch to ground |
| H3 pin 7 / IO17 | DOWN_N, switch to ground |
| H3 pin 8 / IO47 | HAPTIC_EN, default low |
| IO44 exposed pad | Optional HR interrupt; validate before assignment |

Important: the touch controller is configured at address 0x5A, the same address
as DRV2605L. Do NOT connect DRV2605L directly to the existing sensor/touch bus.
The proposal uses a separate 100 kHz GPIO I2C bus; controller implementation and
timing remain to be qualified. Proposed addresses on that bus: OPT3001 0x44,
DRV2605L 0x5A, MMC5603NJ 0x30, BMP390 0x76. No new GPIO is needed for the two
optional sensors when polled. HR module address compatibility is not yet known.

### Circuit notes

- DRV2605LDGSR: VDD pins 10/6 to 3V3, GND pin 8; SCL pin 2, SDA pin 3;
  EN pin 5 with 100 kohm pull-down; IN/TRIG pin 4 to ground. REG pin 1 has
  1 uF to ground and powers no other load. OUT+ pin 7 and OUT- pin 9 connect
  only across M1. Fit 1 uF plus initial 22 uF local supply capacitance. Neither
  actuator terminal is grounded. Qualify effective capacitance and supply sag.
- The VG0825003D candidate uses 1.2 Vrms AC at approximately 235-250 Hz.
  Configure limits/calibration for the actual selected LRA; not a generic DC motor.
- OPT3001: 3V3 with 100 nF bypass, ADDR to GND, INT unused initially.
- Buttons: independent switch-to-GND inputs with 47 kohm pull-ups; software
  debounce. Maintain room for simultaneous presses and qualify wake behaviour.
- Separate bus: one pair of 4.7 kohm pull-ups to 3V3; check rise times before
  adding parallel pull-ups for every device.
- MMC5603NJ: 3V3 with at least 2.2 uF effective local bypass; inspect DC bias.
- BMP390: VDD/VDDIO to 3V3, 100 nF bypass at each, CSB to VDDIO, SDO to GND;
  INT unused. Keep the pressure port clear of coating, adhesive and potting.
- HR pads describe a 3.3 V module interface. A bare optical sensor requiring
  1.8 V needs its own regulation/level compatibility; do not attach it directly.

## Mechanical placement requirements

| Item | Provisional enclosure reservation | Critical dependency |
| --- | --- | --- |
| Slim preferred battery | Roughly 45 mm shaped footprint, 3.5-4 mm cell; investigate 5-5.5 mm installed height | Not a selected pack; supplier protection, leads, tolerances and expansion requirements govern |
| Larger reference battery | 48 x 46 x 7.5 mm bounding envelope, plus adjacent 8 x 6 x 3 mm protection/lead bay | Rounded pocket required; a 48 x 46 rectangle does NOT fit a 51 mm circle |
| Speaker | 12 x 12 x 4 mm local envelope | Includes provisional gasket/diaphragm/cavity space; acoustic tuning required |
| Magnetometer | Around 3 x 4 mm electronics area at perimeter | Initial 15-20 mm separation goal from speaker/LRA, not a guaranteed magnetic clearance |
| Barometer | Around 4 x 5 mm electronics area | Short pressure path; initial 5 x 5 mm flat vent land, exact vent TBD |
| Light sensor | Optical access at case/display perimeter | External window/light pipe, opaque isolation from AMOLED |
| LRA | Do not freeze a pocket without its actual drawing | Rigid case coupling; no force on battery pouch, glass or flex |
| HR option | Unallocated rear skin-facing volume | May compete with battery; optics/window/seal are not designed |

General enclosure rules:

1. Keep the external diameter near the existing 51 mm assembly footprint, but
   prove the actual case wall, board retention and tolerance stack in CAD. If
   this cannot work, show the dimensional tradeoff rather than silently enlarging.
2. Prefer placing additions beside available host features or in existing height
   voids. Do not sum every component height into a stack unnecessarily.
3. A 10 mm speaker cannot be assumed to fit in the narrow annulus around a
   45 mm battery in a 51 mm case. Find a real pocket/shaped-cell cutout, use a
   different height region, or propose a revised battery/case arrangement.
4. Speaker SP-1003 information differs: current product material says 2.4 mm,
   older drawing/distributor data 2.9 mm. Reserve 2.9 mm until a current drawing
   resolves the discrepancy. Keep diaphragm clearance; do not clamp its moving face.
5. Mount the speaker to a gasketed acoustic path with a short side-facing outlet,
   clear of the wrist. No final cavity volume/loudness has been established.
   Do not assume a plain speaker is waterproof. Acoustic and pressure membranes
   serve different functions; share a path only after validating both functions.
6. Keep speaker/LRA/inductors, magnetic charging hardware, magnetic screws and
   high-current loops away from the compass. Do not locate it beneath magnetised
   metalwork or the battery merely because there is PCB area available there.
   Validate final assembled geometry; calibration cannot cure saturation or all
   changing interference. A wired perimeter sensor island is an acceptable fallback.
7. Vent the barometer to outside through a suitable hydrophobic membrane; do
   not rely on a sealed air pocket. Keep its vent away from sweat pooling,
   wrist blockage, direct sound pressure and mounting stress. A vent specification
   alone does not establish a water-resistance rating for the assembled watch.
8. Provide two real Up/Down case actuators with travel limits, retaining features
   and tolerance allowance. Preserve access to existing buttons and USB/reset
   needed for development/recovery. Strap width, attachment method, sealing target
   and manufacturing process are not yet chosen; keep them explicit parameters.
9. Insulate the battery from solder joints/screws, provide non-compressive retention
   and lead strain relief, and allow supplier-specified expansion. Never use the
   pouch as a structural support for speaker or LRA. Preserve display flex bend radius.

## Battery choices and honest runtime budget

The larger candidate is a protected pack around Grepow GRP5545043. Current
manufacturer list: 1040 mAh, 45 x 43 x 5.3 mm; older individual listing: 960 mAh,
45.1 x 43 x 5.35 mm. Runtime was calculated conservatively from 960 mAh. The owner
then requested a thinner option. Neither current stock nor a protected-pack
drawing or minimum capacity has been confirmed.

The current preference is to investigate a premium 750-800 mAh, 3.5-4 mm shaped
cell. Do not present that target as an orderable part. A verified catalogue example
of a smaller thin cell is PowerStream GMT353533-PCB: nominal 35 mm round,
3.5 mm cell thickness, 350 mAh with protection option. Complete pack dimensions
still need checking; that capacity does not meet two days under the original model.
Higher-voltage 4.35/4.4 V cells are not assumed interchangeable with the present
4.2 V charging system.

Original engineering assumptions, not measured panel specifications:

- 30 minutes/day display on, moderate brightness, mostly black watchface;
  true panel sleep between interactions, not simply drawing a black screen.
- Battery-terminal current: 10 mA whole-device display-off connected idle;
  80 mA whole-device display-on, comprising 55 mA active host plus 25 mA display
  allowance (about 93 mW at 3.7 V, including display supply losses).
- Additional 0.1 mA daily average for light sensing/short haptics.
- BLE connected, Wi-Fi/GPS off, no continuous microphone, no HR or speaker playback
  in the original model. All optional sensor/host wake costs must be added.
- Use 80% of nameplate capacity for combined cutoff/aging/operating reserve.

Average current = (10 * 23.5 + 80 * 0.5) / 24 + 0.1 = 11.56 mA.

| Nameplate capacity | Estimated runtime under that model |
| --- | --- |
| 350 mAh | 24 h |
| 500 mAh | 35 h |
| 700 mAh | 48 h, essentially no extra workload margin |
| 750 mAh | 52 h |
| 800 mAh | 55 h |
| 960 mAh | 66 h |

For 960 mAh and 80% usable capacity, 48 h requires average current <=16 mA.
At the original screen duty and 80 mA active current, idle must be <=14.5 mA.
For a slim 750 mAh cell, the equivalent average limit is 12.5 mA. The board's
current runtime/power has not been qualified. A 35 mA idle scenario reduces the
960 mAh example to around 21 h; current WFI support alone is not deep-sleep proof.

Compass: start with 10-25 Hz only while subscribed, off otherwise; allow roughly
0.3-1 mA active at the proposed supply pending measurements. BMP390 manufacturer
figure is 3.2 uA at 1 Hz, excluding processor and bus overhead. At 0.3 W speaker
output and assumed 80% battery-to-speaker efficiency, playback adds about 101 mA
at 3.7 V: two minutes/day adds about 3.4 mAh/day, plus CPU/codec overhead.

Measure at the battery with USB disconnected. Sweep panel brightness/content,
include radio/haptic/audio peaks and wake costs, then run a representative
48-hour workload before claiming two-day life.

## Charging and audio integration gates

The published host schematic shows ETA6098 with R8=82 kohm at ISET; its datasheet
associates this setting with about 2 A fast charge. Verify actual assembly/revision
before charging a small cell. Reduce the target to the chosen pack's allowed rate
(approximately 0.5 A was considered for the larger pack, not a universal setting).
The smaller 350 mAh option requires its own charge assessment. Qualify the
200 mA precharge, 130 mA termination, temperature limits and enclosure heating.
Do not prescribe a resistor through unverified extrapolation or assume firmware
can control a fixed hardware setting. Battery connects to the battery port, not
the expansion 3V3/5V rails. A protection board is not a substitute for a correctly
configured charger.

Speaker connects across the existing H2 output pair PA_OUTL+/PA_OUTL-. Both are
bridge-driven; neither goes to ground. Reuse fitted NS4150B/analog DAC hardware;
do not add a second amplifier or parallel the stock large speaker. Start at 0.1 W,
target about 0.2 W maximum intended output into 8 ohms (1.26 Vrms differential
for a sine), and measure signal/transients against the actual speaker rating.
Firmware playback and pop-free volume/enable control remain necessary; current
board code holds the amplifier disabled until a native playback path owns it.

## Firmware scope and optional feature limits

- OPT3001 has an existing driver. Bind it to the new bus and calibrate optical
  placement; hardware alone does not implement brightness behaviour.
- DRV2605L needs integration; AW86225 is an existing-driver alternative only
  if sourcing and total BOM justify redesign. Do not populate both by default.
- MMC5603NJ has a working driver implementation to reuse. Configure MAG axis
  mapping and verify the existing compass service with QMI8658 tilt data.
  Current common correction is a hard-iron sphere fit, not full soft-iron
  correction. Calibrate after final assembly and test haptics/speaker/charging.
  True-north heading needs declination data in addition to magnetic heading.
- BMP390 code currently probes and powers down. Pressure acquisition, trim
  coefficients/compensation, bounded sampling, diagnostics and app/service access
  remain work; do not call the current init-only interface complete support.
  Absolute altitude needs reference pressure, weather causes drift, and floor
  counting is a separate feature. Internal die temperature is not ambient air
  temperature inside a warm case.
- Preserve common Pebble code unless a scoped implementation is impossible.
  New public app APIs must follow `docs/sdk/exporting.md`.
- No production firmware changes or hardware tests were performed for this design.

## Required enclosure deliverables and acceptance

1. Parametric editable CAD and neutral STEP assembly, with separate host,
   battery, add-on PCB, LRA, speaker, buttons, sensors and case bodies.
2. STL/3MF case pieces for a stated prototype process, with explicit tolerances;
   do not silently treat print clearances as injection-moulding rules.
3. Dimensioned top and section views showing external diameter/total thickness,
   battery height/retention, speaker cavity/outlet, sensor positions, pressure
   vent and light aperture, fasteners and flex/lead routing.
4. Interference/clearance report and assembly sequence. Mark unavailable geometry
   with explicit placeholder bodies. Distinguish a concept from verified-fit CAD.
5. Unresolved decisions list: actual host/custom case CAD, chosen slim battery and
   LRA drawings, switch part/travel, strap attachment, sealing target, vent part,
   optional sensor population and whether HR requires an alternative rear cover.
6. Show any unavoidable diameter/thickness tradeoff before freezing manufacturing
   files. Do not compress a battery or remove recovery access to make CAD fit.

## Primary sources

- [Waveshare product and outline drawing][host-product]; [resources][host-wiki].
- [Host schematic][host-schematic].
- [DRV2605L datasheet][haptic-driver]; [LRA candidate datasheet][lra].
- [OPT3001 specifications][als].
- [MEMSIC MMC5603NJ datasheet][mag].
- [Bosch BMP390 specifications/datasheet][barometer].
- [Gore pressure vent integration][pressure-vent].
- [Grepow round cell list][battery-list]; [older GRP5545043 listing][battery-old].
- [PowerStream thin round cell example][thin-battery].
- [ETA6098 datasheet][charger].
- [Soberton SP-1003 specifications/drawings][speaker].

[host-product]: https://www.waveshare.com/t5-e1-touch-amoled-1.75.htm
[host-dimensions]: https://www.waveshare.com/img/devkit/T5-E1-Touch-AMOLED-1.75/T5-E1-Touch-AMOLED-1.75-details-size2.jpg
[host-wiki]: https://www.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75
[host-schematic]: https://files.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75/T5-E1-Touch-AMOLED-1.75_schematic.pdf
[haptic-driver]: https://www.ti.com/lit/ds/symlink/drv2605l.pdf
[lra]: https://www.vybronics.com/wp-content/uploads/datasheet-files/Vybronics-VG0825003D-datasheet.pdf
[als]: https://www.ti.com/product/OPT3001
[mag]: https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5603NJDatasheetRev.B.pdf
[barometer]: https://www.bosch-sensortec.com/en/products/environmental-sensors/pressure-sensors/bmp390
[pressure-vent]: https://www.gore.com/products/pressure-vents-portable-electronics
[battery-list]: https://www.grepow.com/shaped-battery/round-lipo-battery.html
[battery-old]: https://www.grepow.com/battery-news/grepow-3-7v-960mah-lipo-round-shaped-battery-5545043.html
[thin-battery]: https://www.powerstream.com/round-li-polymer.htm
[charger]: https://www.eta-semi.com/wp-content/uploads/2022/03/ETA6098_V1.1.pdf
[speaker]: https://www.soberton.com/sp-1003/
