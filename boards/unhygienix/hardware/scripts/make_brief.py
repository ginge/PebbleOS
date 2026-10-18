from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, Preformatted
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.units import mm
from xml.sax.saxutils import escape
from pathlib import Path
import argparse

# Keep this generator with the proposal it reproduces; see ../README.md.
_parser = argparse.ArgumentParser(description='Generate the Rev D hardware proposal snapshot.')
_parser.add_argument('--output', type=Path,
                     default=Path(__file__).resolve().parents[1] / 'pdf' / 't5ai-pebble-addon-proposal.pdf')
_output = _parser.parse_args().output.resolve()
_output.parent.mkdir(parents=True, exist_ok=True)
OUT = str(_output)
styles=getSampleStyleSheet()
styles.add(ParagraphStyle(name='BodyX',fontName='Helvetica',fontSize=9.3,leading=13,spaceAfter=7))
styles.add(ParagraphStyle(name='SmallX',fontName='Helvetica',fontSize=8,leading=10,spaceAfter=5))
styles.add(ParagraphStyle(name='LabelX',fontName='Helvetica-Bold',fontSize=11,leading=14,spaceBefore=11,spaceAfter=6,textColor=colors.HexColor('#155b63')))
styles['Title'].fontSize=25
styles['Title'].leading=29
story=[]
def p(t,style='BodyX'): story.append(Paragraph(t,styles[style]))
def title(n,t):
 p('T5-AI / PEBBLE ADD-ON     |     REV D CONCEPT     |     18 OCT 2026','SmallX')
 p(t,'Title'); p(n,'SmallX'); story.append(Spacer(1,4*mm))
def h(t): p(t,'LabelX')
def tab(rows,widths):
 data=[[Paragraph(escape(str(x)),styles['SmallX']) for x in row] for row in rows]
 t=Table(data,colWidths=[v*mm for v in widths],hAlign='LEFT')
 t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#e3eff0')),('VALIGN',(0,0),(-1,-1),'TOP'),('LINEBELOW',(0,0),(-1,0),.7,colors.HexColor('#155b63')),('LINEBELOW',(0,1),(-1,-1),.3,colors.HexColor('#d9e1e2')),('LEFTPADDING',(0,0),(-1,-1),6),('RIGHTPADDING',(0,0),(-1,-1),6),('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),5)]))
 story.append(t)
def page(): story.append(PageBreak())

title('01 / Scope and architecture','Compact Pebble hardware additions')
p('<b>Recommendation:</b> a thin, two-layer daughterboard for the confirmed Waveshare T5-E1 Touch AMOLED 1.75, adding LRA haptics, Up/Down buttons and ambient-light sensing. Reserve unpopulated pads for a separate heart-rate module. The user selected crisp LRA haptics and a custom case.')
p('<b>Status:</b> preliminary electrical design and planning BOM. PCB outline, routed CAD, switch footprints and manufacturing files remain pending case geometry. No claim of verified fit or completed electrical testing is made.')
p('<b>Revision B:</b> reserve space for a protected 3.7 V, approximately 1000 mAh battery. Pages 5-6 define the provisional pocket, two-day energy budget and required charger changes. The two-day target is conditional on measured low-power performance, with the display normally off.')
p('<b>Current scope:</b> pages 7-8 cover optional compass/barometer; page 9 adds a tiny speaker to the BOM and reserves acoustic space. The slimmer 3.5-4 mm battery is still under consideration. Pages 5-6 retain the larger-cell reference budget, not a final battery selection.')
h('Populate on revision A')
tab([['Function','Proposed implementation','Reason'],['Vibration','DRV2605LDGSR + small coin LRA','Closed-loop drive and braking; established TI device.'],['Up / Down','Two normally-open switches to ground','Direct GPIO inputs; no expander or ADC ladder.'],['Ambient light','OPT3001DNPR + local bypass','2 x 2 mm sensor; existing PebbleOS support.'],['Heart rate, optional','Five solder pads to a rear sensor module','No extra populated parts until optics and module are chosen.']],[31,69,70])
h('Construction and size targets')
p('Use 0.6 mm FR-4, two copper layers, 0402 passives and soldered wires/pads. Start with the leaded DRV2605L package to keep assembly and inspection simple. Its smaller ball-grid variant is a later option if measured space justifies the assembly cost. Avoid tall headers, breakout boards and a four-layer stack unless routing proves they are required.')
p('Allow roughly 10 x 12 mm for the central electronics as an initial placement budget, excluding switches, actuator, wiring clearance and mounting. This is an engineering estimate, not a routed-board dimension. The final PCB will likely be a narrow strip or crescent following the custom case wall; button position may dominate its size.')
h('Known hardware and firmware boundary')
p('The board already supplies touch/display, motion sensing, RTC, microphone/audio hardware and charging [1]. The checked production configuration has Back/Select but no Up/Down GPIOs or haptic configuration. OPT3001 support exists; DRV2605L support must be added. AW86225 is an existing PebbleOS alternative to compare if its supply and total BOM are attractive. All future firmware work belongs in /home/baz/coredevices/pebbleos, scoped to the board or driver.')
page()

title('02 / Proposed circuit connections','Electrical design')
p('Bus selection is constrained by device addresses. Proposed seven-bit addresses: light 0x44, haptics 0x5A. The software assigns the touch device 0x5A too: <b>this is an address conflict that must be resolved before using the shared bus.</b> Therefore the recommended revision-A wiring below assigns the add-on its own GPIO I2C bus; implement and validate a board-local software I2C controller, or qualify a hardware controller route before layout release.')
tab([['Host connection','Add-on assignment','Implementation'],['H3 pin 2 / pin 3','GND / 3V3','Common power and ground; no connection to H3 pin 1 (5 V).'],['H3 pin 4 / IO14','ADD_SCL','Separate 100 kHz GPIO I2C clock.'],['H3 pin 5 / IO15','ADD_SDA','Separate open-drain data; pull up to 3V3.'],['H3 pin 6 / IO16','UP_N','Normally-open switch to GND.'],['H3 pin 7 / IO17','DOWN_N','Normally-open switch to GND.'],['H3 pin 8 / IO47','HAPTIC_EN','Default-low enable. Verify boot ownership and mux.'],['Spare IO44 solder pad','HR_INT_N, optional','Only connect for a selected module; validate GPIO interrupt.']],[44,43,83])
h('Haptics / U1 / DRV2605LDGSR [2]')
p('VDD pins 10 and 6 to 3V3; GND pin 8 to ground. SCL pin 2 and SDA pin 3 to ADD_SCL/ADD_SDA. EN pin 5 to HAPTIC_EN, with 100 kohm to ground. IN/TRIG pin 4 to ground. REG pin 1 has 1 uF to ground and supplies no external load. OUT+ pin 7 and OUT- pin 9 go only to the two actuator pads. Neither motor terminal is grounded. Fit 1 uF plus an initial 22 uF bulk capacitor at VDD; check effective capacitance after DC bias.')
h('Ambient light / U2 / OPT3001 [3]')
p('VDD to 3V3 with 100 nF bypass; GND and ADDR to ground (0x44). SDA/SCL to the separate add-on bus; leave INT unconnected for sampled readings. Sensor must face an external light window with an opaque barrier against AMOLED light. Reuse the existing PebbleOS ALS driver after bus binding and optical calibration.')
h('Buttons, bus and optional module')
p('Each switch input gets a 47 kohm pull-up to 3V3; use software debounce and qualify wake operation. Add two 4.7 kohm I2C pull-ups for the new isolated bus. HR pads: 3V3, GND, ADD_SCL, ADD_SDA, HR_INT_N. This is a 3.3 V module interface, not a direct bare-sensor connection. A module needing 1.8 V requires regulation and any level translation on its own board.')
page()

title('03 / Planning bill of materials','BOM and cost choices')
p('Costs below are planning allowances in GBP for a small batch of roughly 10-100 boards, excluding VAT, shipping, assembly setup and the existing host board/battery/case. They are not supplier quotations. Passives may be substituted only with the specified ratings and footprints.')
tab([['Ref / qty','Part or specification','Allowance'],['U1 / 1','TI DRV2605LDGSR, VSSOP-10','GBP 1.0-2.5'],['U2 / 1','TI OPT3001DNPR, USON-6','GBP 0.9-1.6'],['M1 / 1','Small coin LRA; Vybronics VG0825003D candidate [4]; final fit/source TBD','GBP 1.5-4.0'],['SW1, SW2 / 2','Side-actuated, normally-open SMT switches; exact part follows case travel/height','GBP 0.3-1.0 total'],['C1, C2 / 2','1 uF, X5R/X7R, 6.3 V or higher, 0402',''],['C3 / 1','22 uF, X5R, 6.3 V or higher, 0603; check bias derating',''],['C4 / 1','100 nF, X7R, 6.3 V or higher, 0402',''],['R1 / 1','100 kohm, 0402, haptic-enable pull-down',''],['R2, R3 / 2','47 kohm, 0402, button pull-ups',''],['R4, R5 / 2','4.7 kohm, 0402, separate I2C bus pull-ups',''],['Passives total','4 capacitors + 5 resistors','GBP 0.1-0.4'],['PCB / 1','2-layer 0.6 mm FR-4, shape TBD','GBP 0.3-1.2 allocation'],['Wires / bonding','Fine wire, insulation, adhesive/strain relief','GBP 0.2-0.6'],['HR pads','Unpopulated; sensor module excluded','GBP 0 populated BOM']],[28,112,30])
p('<b>Planning total:</b> approximately GBP 4.3-11.3 per unit in parts and allocated PCB cost. Use GBP 6-12 as a practical target before assembly and delivery. A one-off professionally assembled prototype can cost considerably more because of setup/minimum charges.')
h('How cost and space are controlled')
p('No GPIO expander, second MCU, regulator, USB connector or duplicated motion/RTC hardware. The separate GPIO bus saves two difficult sensor-bus solder taps and avoids the 0x5A collision, at the cost of a small firmware addition. Do not put both haptic-driver alternatives on the first PCB. A thin rigid board generally costs less than rigid-flex; separated button islands are a fallback only if case geometry requires them.')
p('The LRA candidate specifies 1.2 Vrms AC and 235-250 Hz [4]. Configure motor-specific limits and calibrate after mounting. Do not use a generic 3 V DC motor profile or promise identical strength from a smaller actuator. Buy a sample before freezing the enclosure and BOM.')
page()

title('04 / Mechanical gates and optional features','What to settle before PCB layout')
h('Required mechanical input')
p('Provide the custom case STEP/STL and board mounting location, or dimensioned internal photographs/drawings. Required measurements: free XY volume, clearance above/below the host PCB, battery envelope including swelling allowance, screw bosses, display flex routing, antenna position, and the desired Up/Down locations, travel and sealing method. An outside case diameter alone cannot establish fit.')
p('Keep added copper and motor metal out of the host antenna clearance. Bond the LRA firmly to the case structure with the intended vibration axis; keep it off the battery pouch and avoid loading the glass/display flex. Sensor placement follows the light window, which may require a small wired sensor island. Never assume the sensor can see through the AMOLED stack.')
h('Other hardware to decide together')
tab([['Option','Value / consequence','Recommendation'],['Compass / magnetometer','Useful for compass apps; motor/speaker magnets require separation and calibration.','Most useful extra; leave unpopulated until wanted.'],['Battery gauge','Improves state-of-charge estimation compared with voltage alone.','Consider after battery runtime is measured.'],['Heart-rate sensing','Rear optics, light seal, skin contact and signal processing are substantial work.','Reserve module pads; defer populated sensor.'],['Charging contacts','Convenient for a sealed watch case; requires mechanical/contact design.','Add only if USB access is unsuitable.'],['Barometer','Adds altitude/floor-related data and needs a pressure vent.','Defer unless an intended app needs it.'],['Extra RTC / IMU / microphone','Duplicates existing hardware.','Omit.']],[37,98,35])
h('Validation before ordering a finished board')
p('Verify actual PCB revision and H3 continuity, GPIO mux/boot behavior and wake support. Scope the new bus with both ICs attached; check acknowledgements and no stuck-low recovery. Validate 3V3 rail sag during simultaneous radio, bright display and haptics at low battery. If headroom fails, redesign haptic power before release. Measure idle current, calibrate haptics, exercise independent/held/simultaneous buttons, and compare ALS readings with the display off/on. Check the assembled stack in CAD, then run schematic ERC and routed-board DRC. No Gerbers should be ordered from this concept brief.')
h('Sources and checked implementation')
for s in [
('[1] Waveshare schematic (host pinout and fitted circuits)','https://files.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75/T5-E1-Touch-AMOLED-1.75_schematic.pdf'),
('[2] TI DRV2605L datasheet','https://www.ti.com/lit/ds/symlink/drv2605l.pdf'),
('[3] TI OPT3001 product and datasheet','https://www.ti.com/product/OPT3001'),
('[4] Vybronics VG0825003D datasheet','https://www.vybronics.com/wp-content/uploads/datasheet-files/Vybronics-VG0825003D-datasheet.pdf')]:
 p('<link href="'+s[1]+'" color="#155b63">'+s[0]+'</link>','SmallX')
p('Local evidence: fw/board/boards/board_unhygienix.c (bus, touch address, existing buttons); drivers/ambient/opt3001.c; drivers/vibe/Kconfig. Inspected 18 October 2026. No production or reference source was modified.','SmallX')
page()

title('05 / Battery selection and reserved volume','Reserve a 1000 mAh battery bay')
p('<b>Recommended candidate:</b> a protected 1S pack built around Grepow GRP5545043, standard 3.7 V nominal / 4.2 V maximum chemistry. The current manufacturer list gives 1040 mAh and a 45 x 43 x 5.3 mm cell; its older individual listing gives 960 mAh and 45.1 x 43 x 5.35 mm [5]. Budget runtime using 960 mAh until the supplier supplies a current signed drawing, minimum capacity and pack specification. This is a sourcing candidate; stock, price and minimum order quantity are not verified.')
h('Mechanical reservation added to the concept')
tab([['Item','Provisional allowance / constraint'],['Battery pocket','48 x 46 x 7.5 mm clear space, below host board. This is a bounding box for the shaped cell, not a claim that the cell is rectangular.'],['Protection and leads','Additional adjacent 8 x 6 x 3 mm bay for protection PCB/lead exit, plus cable bend path. Replace with the actual protected-pack drawing.'],['Insulation / swelling','7.5 mm height provisionally includes cell thickness tolerance, insulation and expansion allowance. Supplier pack limits govern final clearance; no compression fit.'],['Add-on PCB','Place alongside the pocket/perimeter where possible. Do not add component or motor height into the battery clearance.'],['Layer order','Display and host PCB, insulating separator, battery pocket, rear cover. Keep mounting screws, sharp solder joints and actuator loads away from the pouch.'],['Heart-rate option','A rear optical module needs a separate skin-facing volume/window. Do not assume it can occupy the reserved battery bay.']],[39,131])
p('These reservations are design requirements for the custom case, not a completed fit check. Keep the cell and its metallic pouch out of the antenna keep-out. If the existing case cannot provide this volume, change capacity, thickness or enclosure geometry only after checking CAD. Do not force the cell into a smaller space.')
h('Battery and charging BOM additions')
tab([['Ref','Specification','Cost status'],['B1','Protected GRP5545043-based pack; minimum accepted capacity 960 mAh; supplier-qualified discharge current for measured peaks','Supplier quote required'],['J_BAT / leads','Mate with existing host battery connector after verifying pitch, orientation and polarity; avoid a second connector if unnecessary','Quote with pack'],['Host R8 rework','Reduce charger current to match the selected pack; value must be qualified, see page 6','One resistor / board rework'],['Insulation / carrier','Thin insulating separator and non-compressive retention, with lead strain relief','Mechanical BOM']],[26,106,38])
p('The earlier GBP 6-12 add-on target excludes this battery pack and charger rework. A shaped pack may cost more or have a higher minimum order than a rectangular cell. Compare a standard rectangular pack only after checking its complete protected dimensions and corner clearance in the round case.')
page()

title('06 / Two-day budget and charger qualification','48 hours is a power target')
p('<b>These are engineering assumptions, not measured panel specifications.</b> No verified brightness-versus-current curve for this exact assembled panel was found. Use black-background watchfaces, moderate indoor brightness and 30 minutes of display-on time per day (for example 120 interactions of 15 seconds). The panel must enter display sleep between interactions; displaying a black image alone is not assumed to power it down.')
tab([['State / contribution','Assumption at battery terminals'],['Display-off connected idle','10 mA total, including host SoC, memory, BLE, regulators and panel sleep. This is a target to achieve and measure.'],['Display-on / active host','55 mA excluding the panel; 25 mA incremental display allowance, giving 80 mA total. The panel allowance is about 93 mW at 3.7 V, inclusive of its supply losses.'],['Other use','0.1 mA daily average allowance for light sensing and short haptic alerts. Wi-Fi/GPS off; no speaker playback, continuous microphone or HR sampling.'],['Usable capacity','80% of 960 mAh = 768 mAh, allowing a combined reserve for cutoff, aging and operating conditions. This is an estimate, not a guaranteed aging curve.']],[51,119])
p('<b>Average current:</b> (10 x 23.5 + 80 x 0.5) / 24 + 0.1 = <b>11.56 mA</b>.<br/><b>Estimated runtime:</b> 768 / 11.56 = <b>66 hours</b>.<br/><b>Minimum nameplate capacity for this model:</b> 48 x 11.56 / 0.8 = <b>694 mAh</b>. The recommended 960-1040 mAh class leaves useful margin.')
tab([['Scenario (assumed)','Estimated runtime'],['30 min/day screen use; 10 mA idle / 80 mA active','66 h'],['60 min/day screen use; same currents','59 h'],['30 min/day brighter/heavier use; 130 mA active','62 h'],['Unoptimised: 35 mA idle / 100 mA active; 30 min/day','21 h'],['Display continuously on at 80 mA total','9.6 h']],[132,38])
h('Required charging change')
p('The published host schematic has R8 = 82 kohm at ETA6098 ISET [1]. Its datasheet associates that setting with 2 A fast charging; precharge is 200 mA and termination is 130 mA [6]. Qualify a reduced charge target of approximately 0.5 A only if the pack permits it, including precharge, temperature limits and thermal behaviour. Do not retain 2 A by default or assume firmware can change the fixed resistor. The reduced-current resistor value needs manufacturer guidance and bench validation; no unverified value is prescribed here. Connect B1 to the battery port, never to the 3V3 or 5V expansion rail.')
h('Acceptance measurement before promising two days')
p('The current WFI idle implementation does not establish deep sleep or tick suppression. With USB disconnected, measure a whole-device average of <b>16 mA or less</b> for 48 h on the assumed 768 mAh usable capacity. At the baseline duty, idle must be at most about 14.5 mA. Test panel brightness and black/mixed/white content, then run a representative 48 h workload. At 35 mA idle, power optimisation is required.')
for label,url in [('[5] Grepow current round-cell list and linked individual specifications','https://www.grepow.com/shaped-battery/round-lipo-battery.html'),('[6] ETA6098 manufacturer datasheet','https://www.eta-semi.com/wp-content/uploads/2022/03/ETA6098_V1.1.pdf')]:
 p('<link href="'+url+'" color="#155b63">'+label+'</link>','SmallX')
page()

title('07 / Optional compass and magnetometer','One sensor provides the compass')
p('A magnetometer measures magnetic field in three axes. Combined with the existing QMI8658 accelerometer, calibration and the PebbleOS e-compass service, it can provide tilt-compensated magnetic heading. There is no separate compass IC to buy. True-north heading additionally needs magnetic-declination information; it does not follow from adding the sensor alone.')
h('Recommended hardware / reserve as an assembly option')
tab([['Item','Proposed scope'],['U3','MEMSIC MMC5603NJ, 0.8 x 0.8 x 0.4 mm WLP; existing PebbleOS driver.'],['Interface','3V3, GND, ADD_SCL and ADD_SDA; address 0x30. No additional host GPIO required.'],['Bypass','C5: at least 2.2 uF effective local supply bypass, with value/package selected for DC-bias derating.'],['Placement budget','Approximately 3 x 4 mm including bypass and routing, before magnetic clearance. Estimate only.'],['Cost allowance','GBP 1.5-3.0 for sensor and bypass in a small batch; assembly excluded, supplier quote required.']],[39,131])
p('Package, supply and layout requirements follow [7]. Use the existing add-on bus pull-ups; do not fit another pair simply because another sensor is added. Leave U3/C5 unpopulated for a compass-free assembly and make that firmware variant tolerate absent hardware.')
h('The important constraint is magnetic placement')
p('Place the sensor at the perimeter, as far as practical from the LRA, speaker, charger inductors, charging magnets, magnetic screws and high-current loops. A 15-20 mm separation from the LRA/speaker is an initial placement goal, not a guaranteed clearance. Measure the fully assembled watch. If the battery covers the useful placement region, use a small wired sensor island instead of placing the magnetometer directly beneath the pouch or display metalwork.')
p('Calibrate after the final battery, fasteners, case and actuator are installed. Fixed offsets can often be corrected; saturation and changing interference cannot be assumed removable by calibration. Reject or suspend heading samples while haptics or speaker playback disturb them, and qualify recovery. If retaining a speaker prevents useful heading, reconsider its position or whether to fit it.')
h('Firmware and power scope')
p('Reuse CONFIG_MAG_MMC5603NJ, bind MAG with the final axis map, enable the existing e-compass service and verify the QMI8658 acceleration feed used for tilt compensation. Audit startup calibration and subscriber-driven sampling. Current shared calibration estimates hard-iron offsets using a sphere fit; it must not be described as a complete soft-iron correction. Preserve common Pebble code unless measurements show a scoped fix cannot solve a problem.')
p('Start with 10-25 Hz while a compass app is subscribed, powered down otherwise. The current driver selects a bandwidth whose 100 Hz datasheet consumption is 2.4 mA typical at 1.8 V; scaling by measurement rate suggests 0.24-0.60 mA while active, before rail/host overhead. Budget conservatively 0.3-1 mA active on the proposed rail and measure it. At 30 minutes/day this is roughly 0.006-0.021 mA daily average; calibration and extra accelerometer wakeups are additional.')
p('<link href="https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5603NJDatasheetRev.B.pdf" color="#155b63">[7] MEMSIC MMC5603NJ datasheet</link>','SmallX')
page()

title('08 / Optional pressure and altitude','Barometer: small IC, real vent work')
p('<b>Recommended part:</b> Bosch BMP390. Use it for ambient pressure, weather trends and relative elevation changes. Absolute altitude needs a reference pressure or known-height calibration and drifts with weather. Floor counting needs an algorithm and validation. Its internal temperature channel compensates the sensor; it is not a reliable outdoor-air thermometer inside a warm watch.')
tab([['Item','Proposed scope'],['U4','BMP390, 2 x 2 x 0.75 mm LGA. Reserve approximately 4 x 5 mm for sensor, bypass and routing.'],['Interface','VDD/VDDIO to 3V3; shared ADD_SCL/ADD_SDA; CSB to VDDIO; SDO to GND selects 0x76. INT unused initially.'],['Bypass','C6/C7: 100 nF each at VDD and VDDIO. No new regulator or GPIO required.'],['Power','Bosch quotes 3.2 uA typical at 1 Hz. System wake and software costs are additional. Use 0.1-1 Hz background; faster sampling only for an active feature.'],['Cost allowance','GBP 2-4 for sensor and bypass; GBP 0.5-2 planning allowance for a vent/seal, subject to an actual qualified part.']],[39,131])
h('Pressure path and case work')
p('Provide a short air path from the sensor port to outside air through a hydrophobic pressure-equalisation vent. Start by reserving roughly a 5 x 5 mm flat adhesive/sealing land; final size, hole and adhesive follow the selected vent drawing. This land is an estimate, not an orderable vent specification. Keep it away from wrist contact, sweat pooling and direct speaker airflow. A suitable existing case vent may be shared only after pressure response is measured.')
p('Keep the sensor opening free of adhesive, potting and conformal coating. Isolate it from PCB bending and heat sources. Verify pressure lag after wetting, case squeezing and changes in temperature. A membrane does not establish a water-resistance rating for the whole watch. Bosch integration requirements and a qualified vent supplier govern the mechanical details [8,9].')
h('Firmware scope is larger than compass integration')
p('The existing BMP390 driver only probes the chip and enters low power; its interface exposes initialisation only. Add factory trim readout, pressure/temperature acquisition, Bosch compensation, bounded sampling, error recovery and diagnostics. App-visible data needs a reviewed service/API path. Keep acquisition driver-local; any public interface must follow repository SDK rules.')
h('Incremental scope, validation and recommendation')
p('Reserve both footprints now with independent population options. Estimated incremental parts/vent allowance: <b>GBP 4-9 total</b>, excluding assembly, PCB changes and tooling. Combined electronics occupy roughly 25-35 mm2 distributed around the board, plus magnetic separation and a vent land; they need not increase battery stack height. These are placement/cost estimates, not routed results.')
p('Acceptance: check shared-bus operation and power, heading across rotation/tilt with haptics and charging, reference pressure and elevation changes, wet/dry vent response and sleep recovery. Allow several days of firmware/bench work after hardware arrives, plus possible layout/sealing iteration. No firmware was changed.')
for label,url in [('[8] Bosch BMP390 product specifications and datasheet','https://www.bosch-sensortec.com/en/products/environmental-sensors/pressure-sensors/bmp390'),('[9] Gore pressure vents for portable electronics','https://www.gore.com/products/pressure-vents-portable-electronics')]:
 p('<link href="'+url+'" color="#155b63">'+label+'</link>','SmallX')
page()

title('09 / Speaker BOM and case reservation','Tiny speaker, shared amplifier')
p('<b>Add SPK1 to the BOM:</b> Soberton SP-1003, 8 ohm dynamic micro-speaker, rated 0.3 W with wire leads [10]. Suitable for short alerts and limited speech; evaluate actual loudness and intelligibility in the finished case. This replaces the larger development-kit speaker. Do not fit both speakers in parallel.')
tab([['BOM item','Specification','Planning allowance'],['SPK1 / 1','Soberton SP-1003; 10 mm diameter; reserve 2.9 mm speaker height conservatively. Manufacturer information lists 2.4 mm, while older drawings/distributor data list 2.9 mm. Confirm current drawing.','GBP 1.5-3'],['Acoustic mounting','Compliant perimeter gasket, retention feature, short outlet/duct and protected grille','GBP 0.3-1'],['Acoustic membrane','Optional if water resistance is required; select for airflow/acoustic loss and intended ingress protection','Quote required'],['Speaker lead / mate','Fine twisted pair to existing host speaker output; match actual connector. No separate speaker PCB required.','Included in wiring allowance'],['Amplifier / 0 new','Reuse fitted NS4150B and existing analog DAC path. No second amplifier or new host GPIO.','GBP 0 new ICs']],[35,101,34])
h('Electrical connection and output limit')
p('Connect only across the existing H2 speaker output pair (PA_OUTL+ and PA_OUTL-). Both are driven bridge outputs; neither goes to ground. Verify connector pinout and lead strain relief. Start at 0.1 W and cap intended output near 0.2 W into 8 ohms (about 1.26 Vrms differential for a sine wave). Measure output under representative audio and startup/shutdown transients; do not exceed the speaker rating. The fitted amplifier can overdrive this smaller unit.')
p('Playback firmware remains required: codec/clock/transport setup, volume limiting and pop-free amplifier control. Keep the fitted amplifier disabled between sounds.')
h('Fit requirement added to the custom case')
p('Reserve a <b>12 x 12 x 4 mm local acoustic envelope</b> for speaker, gasket, diaphragm clearance and cavity/duct. Tune the cavity and side-facing outlet in a prototype. Keep it clear of wrist contact and pouch loads. This reservation is not a verified enclosure design.')
p('A 10 mm speaker cannot be assumed to fit beside a 45 mm battery in a 51 mm case. Use an existing pocket at another height or the shaped-cell cutout. <b>Fit requires a combined CAD check</b> with battery, host PCB and LRA. Keep its magnet away from the compass; test silent/playing heading and barometer interference.')
h('Cost and battery update')
p('Add approximately <b>GBP 1.8-4</b> to the previous base BOM, excluding an optional acoustic membrane. Base add-on plus speaker planning target becomes approximately <b>GBP 8-16</b>, before battery, optional compass/barometer, assembly and delivery. The page 3 table is the original electronics subtotal; add the speaker rows on this page.')
p('At 0.3 W output and assumed 80% efficiency, incremental battery draw is about 101 mA at 3.7 V: two minutes/day adds 3.4 mAh/day, plus CPU/codec overhead. Adjust the earlier runtime model for playback. Validate loudness, clipping, rattles, magnetic disturbance and fit.')
p('<link href="https://www.soberton.com/sp-1003/" color="#155b63">[10] Soberton SP-1003 product specifications and linked drawings</link>','SmallX')

def footer(c,d):
 c.setStrokeColor(colors.HexColor('#cbd9da')); c.line(20*mm,16*mm,190*mm,16*mm)
 c.setFont('Helvetica',8); c.setFillColor(colors.HexColor('#4a6267'))
 c.drawString(20*mm,11*mm,'PRELIMINARY DESIGN - FIT AND ELECTRICAL VALIDATION PENDING')
 c.drawRightString(190*mm,11*mm,str(d.page))
SimpleDocTemplate(OUT,pagesize=(210*mm,297*mm),rightMargin=20*mm,leftMargin=20*mm,topMargin=17*mm,bottomMargin=22*mm,title='T5-AI Pebble add-on PCB - preliminary design',author='Codex').build(story,onFirstPage=footer,onLaterPages=footer)
print(OUT)
