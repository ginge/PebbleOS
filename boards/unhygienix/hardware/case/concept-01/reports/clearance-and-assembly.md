# Clearance and assembly report - concept 01

**Status: nominal CAD feasibility only. Battery unselected; component fit unverified.**
This report accompanies the editable CadQuery model, two STEP assemblies, case
STLs, five A3 drawing sheets, vector SVGs and proposed cell/PCB DXFs. No physical
prototype, fitted battery or combined electrical/mechanical qualification exists.
No firmware changes, hardware tests, purchases or supplier contacts were made.

## Evidence and confidence

Read the canonical hardware brief and handoff first. Manufacturer references and
the original schematic are archived in `../../../references/`. Their provenance
is recorded there. Case-specific downloads and SHA-256 values are in
`../evidence/provenance.json`.

| Geometry/input | Evidence | What is and is not established |
| --- | --- | --- |
| Host D51; visible width 43.76; stock height 11.70 / overall 12.10 | Archived Waveshare dimension image | Published assembly dimensions only; no usable cavity, glass OD, PCB outline or internal Z stack established |
| Host mechanical resource | Official T5 wiki download, `T5-E1-Touch-AMOLED-1.75-3D.zip` | Contains DWG and PDF, no STEP. PDF is labelled **ESP32-S3-Touch-AMOLED-1.75-B**. Outer numbers agree, but internal mounting/connector coordinates are not accepted for the T5 unit |
| Actual PCB revision/GPS variant | Not physically inspected | Open. No model is registered to the user's watch |
| Bare host D51 x H6 | Conservative lateral reservation and **assumed** height | PH cylinder, deliberately not a manufactured board model or an assertion that 6 mm is achievable |
| SP-1003 | Soberton product page and linked 2019 drawing | Product/Drawing D10 x H2.4; drawing general tolerance +/-0.5 mm and dimensions for reference. Retain H2.9 per brief. No detailed speaker solid verified |
| VG0825003D | Manufacturer datasheet, outline page 12, dated 2021-06-24 | Candidate body D8.0 +/-0.1 x H2.55 +/-0.05. FPC overall 13.265 +/-0.3 x 8.535 +/-0.3. 9.2 x 14.2 installed reservation is designer allowance; orientation/routing and final selection open |
| OPT3001 / MMC5603NJ / BMP390 | Hardware brief package dimensions | IC envelopes only. Islands, wiring, mounting, vent and light-pipe mechanics are provisional |
| Battery | Owner target only | D45 shaped, 3.5-4 mm, 750-800 mAh is NOT an available part. 4 mm body, 5.5 mm installed zone and 8 x 6 x 3 protection bay are explicit reservations |
| Switches, seals, strap, HR | No chosen parts | Placeholder geometry and bench prototype assumptions only; no waterproof rating or HR volume commitment |

Primary links: [Waveshare host wiki](https://www.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75),
[host mechanical download](https://files.waveshare.com/wiki/T5-E1-Touch-AMOLED-1.75/T5-E1-Touch-AMOLED-1.75-3D.zip),
[LRA drawing](https://www.vybronics.com/wp-content/uploads/datasheet-files/Vybronics-VG0825003D-datasheet.pdf),
[Soberton product](https://www.soberton.com/sp-1003/),
[Soberton drawing](https://www.soberton.com/wp-content/uploads/2019/10/SP-1003_2D.pdf).
These were inspected during the design session; local archives retain the host/LRA evidence.

## Layout and stack

O is the display center, Z0 the external rear face, +Y the 12 o'clock lug.
Angles are counter-clockwise from +X, looking down from the display. Four keys
are proposed at 0, 320, 180 and 210 degrees. They represent Up/Down/Back/Select
functions, not proven engagement coordinates for the two existing switches.
Stock button transfer geometry or wired extenders must be established after
registering the PCB; no host modifications are prescribed or performed now.

Both bodies are D53.8, cavity D51.6, nominal wall 1.1. Bezel aperture D44.3 gives
only 0.27 mm radial margin over the published active area. The actual glass/frame
edge must be measured before relying on bezel retention. Cosmetic frame overlap,
adhesive thickness and active-area alignment remain open. The host reservation
has 0.30 radial clearance and 0.15 axial gap on each side. This is a clearance
budget, not a complete supplier tolerance stack.

A puts all additions beside a notched cell within one installed-height zone.
The largest connected battery region is retained; disconnected slivers are
removed. With protection and sensor/switch allowances, the result has 760.9 mm2,
47.8% of a D45 disk. Its concave outline may be unsuitable for a pouch maker.
Capacity cannot be inferred from area, and 750-800 mAh must not be associated
with this pocket. This arrangement is a thin feasibility study, not a viable
sourced pack solution.

B retains a D45 disk and places additions on a rigid 0.8 mm accessory deck.
Protection is remote in the upper layer at (-13,-8), requiring a supplier-approved
protected-pack construction and lead route. It does not sit on the pouch.
The accessory deck bears on case collars, not the cell. B adds 5.6 mm versus A.
It is the more credible volume baseline for the requested capacity, still
unverified and potentially reducible once real host voids are known.

| Stack datum | A notched | B full cell |
| --- | --- | --- |
| Rear floor | Z0-1.00 | Z0-1.00 |
| Dielectric sheet | Z1.05-1.25, H0.20 | Same |
| Cell target | Z1.40-5.40, H4.00 | Same |
| Installed cell reservation | Z1.20-6.70, H5.50 | Same |
| Add-on deck | None | Z7.30-8.10, H0.80; collar starts Z7.00, 0.30 above installed cell zone |
| Core board | Z1.80-2.40; components to 3.60 | Z8.70-9.30; components to 10.50 |
| Protection bay | Z1.80-4.80 | Z8.70-11.70 |
| Speaker body | Z1.95-4.85; cup to 5.40 | Z8.85-11.75; cup to 12.30 |
| LRA body | Z1.60-4.15; rigid shim beneath | Z8.50-11.05 |
| Host lower datum / actual reserved solid | 7.00 / Z7.15-13.15 | 12.60 / Z12.75-18.75 |
| Bezel | Z13.30-13.90 | Z18.90-19.50 |
| Height equation | 7.9 + actual bare-host height | 13.5 + actual bare-host height |

The 5.5 mm installed cell zone allows 1.3 mm above the 4 mm placeholder body,
but that is not a supplier-approved swelling allowance. The 0.2 mm lower zone
allowance and separate dielectric sheet are bookkeeping envelopes, not additional
safe cell capacity. Do not compress the pack to hold these heights. Any change in
actual pack thickness, growth, protection dimensions or wire exit can invalidate
both alternatives. The older 5.3 mm cell and 48 x 46 x 7.5 reference budget have
not been silently substituted; no rectangular 48 x 46 cavity is assumed.

## Daughterboard, controls and access

The proposed core PCB is 12 x 10 x 0.6 mm, global extents X[-8,4], Y[-21,-11].
Two D1.60 holes are centered at (-6,-19) and (2,-13); their centers are 8 x 6 mm
apart. The 8 x 8 x 1.2 component reservation is not a routed circuit and may be
too small. No tracks, copper, switch supports or connected sensor flexes are
claimed. The scope is coordinated outline/datum CAD, not PCB fabrication files.
Separate perimeter islands are M: 3 x 4, B: 4 x 5, ALS: 3 x 4 mm. The optional
HR module connection is electrical scope in the hardware brief; its PCB pad layout
and optical module are not routed here.

Keys have a captured inner flange, a radial guide and an outer hard-stop shoulder.
Rod cross-section 4 x 1.4 runs in 4.4 x 1.8: 0.2 clearance each side. A 0.1 rest
gap to a placeholder switch nose plus 0.15 assumed switch movement gives 0.25
actuation travel. The shoulder limits inward motion to 0.35. The extra 0.10 is
only an overtravel budget, not permission to overload a selected switch. The
switch must provide return force and an actual carrier/load path. The drawing
and motion audit test shell interference at rest, 0.25, 0.35 and 0.40 mm. They
cannot prove force, crispness, wear, electrical actuation or seal performance.

A provisional 14 x 4.6 USB opening at 270 degrees is accompanied by a 23 x 14 x 7
plug keepout. A 2 x 2 reset-tool opening is proposed at 250 degrees. These are
**unregistered angular positions**: no claim is made that the watch can actually
be plugged in or reset in these case exports. Access must be registered and tested
with the real cable, boot/recovery buttons and header before finalizing walls.
Rear cover removal remains the prototype service route. Avoid changing firmware
or removing recovery hardware to make a case fit.

## Battery retention, acoustics, optics and magnetic layout

The pouch rests on an insulated rear surface and uses removable battery-rated
adhesive retention, not compression by the host, PCB, LRA or speaker. Two side
guides provide 0.8 nominal radial gap to a full D45 cell. Rounded/deburred edges,
insulated screw ends, supplier growth limits and a qualified pull-tab adhesive
are required. Rear strain-relief tie slots are outside the nominal pouch outline;
ties secure wires only, never cinch the pouch. Liner and adhesives remain materials
reservations and must be accounted for in a fitted stack.

The LRA mounts through a 0.30 rigid **cut shim** and 0.10 assumed bond to the rear
or deck. It has a direct case load path; the pouch is not a support. The FPC tail
points inward with a separate 9.2 x 14.2 reservation. Do not bend its bracket or
bond over moving/unsupported flex features. Shim material/bond and stiffness need
an actuator sample; the thin shim is not supplied as a structural PA12 print.

SP-1003 is centered at (15,-5). Its cup has OD12.8, bore10.8 and top height4.2 above
the zone floor, a separate edge gasket and back retainer. Gasket ID9.6 clears the
nominal D9.1 internal face feature in the old drawing, but a static landing rim
and its tolerance are **not verified**. Confirm the face/vent drawing before
selecting gasket ID or preload; never load the diaphragm. The model reserves a
0.5 face plenum, a 0.25 gasket and a short 2.4 x 0.7 side duct to the +X wall.
The duct is not a tuned acoustic result; enclosure leakage, cavity compliance,
membrane loading, outlet grille and SPL must be measured. The plain speaker is
not assumed waterproof. Acoustic membrane choice is unallocated and may increase
height or require an outlet revision.

The ALS window at 70 degrees is 1.9 x 1.9 with an opaque U-shaped light well.
It faces outward, avoiding intentional direct AMOLED illumination, but real glass,
light-pipe material, alignment and display leakage still need optical calibration.
The BMP390 option at 130 degrees uses a separate D1.2 radial pressure hole and a
5 x 5 flattened membrane land. Membrane material, adhesive annulus and sealing
are unselected; the entire 5 x 5 plate is a placeholder. Pressure sampling must
be qualified against sound, sweat, mounting stress and wrist obstruction. No
shared speaker/pressure membrane is assumed. A sensor isolation hood and membrane
bond detail may be needed after parts are selected.

The compass is a perimeter island at 160 degrees. Speaker and LRA centers are
about 39.0 and 35.5 mm from it respectively; exact placeholder surface gaps are
saved in `validation.json`. These exceed the brief's initial 15-20 mm placement
goal, but are not magnetic qualification. Account for actual magnet moment,
charging current loops, host inductors, fasteners, antenna and battery foils.
Use non-magnetic structural hardware where practical and test field magnitude,
heading and saturation with speaker/LRA off and driven, USB/charging modes,
several orientations and the final strap/cover. Calibration does not guarantee
removal of variable or saturating interference.

The rear HR ghost is a D16 x H3 cylinder **outside** the rear surface, in
`keepouts.step` only. It is an illustration of a decision, not allocated internal
volume, a selected module, an optical seal or a promise that a 3 mm bulge works.
A real module needs a separately budgeted rear cover or smaller/relocated cell.

## Numerical clearance checks and limits

`validation.json` summarizes validity, topology, STEP reimport, mesh and key-motion
checks. Each physical body is a valid single solid. Each exported printable STL
is one watertight connected mesh; STEP reimport retains the expected solid count.
Physical part intersections are zero at nominal rest geometry. Mesh tessellation
uses 0.04 mm linear / 0.12 rad angular settings; dimensions derive from BREP CAD.
STL dimensions are in mm; STL itself has no unit metadata.

`exports/*/battery-clearances.csv` and JSON compare every component with the
**installed cell envelope**, excluding the modeled cell itself and intentional
liner overlap. Nominal minimum gap is 0.20 to the rear floor; other important
budgets include 0.80 radial to full-cell guides/bosses, 0.30 to B support collars,
0.60 to the B deck, and 0.45 to A host lower face. These are geometric budgets,
not certified tolerances. The point-to-point result cannot establish wire routing,
thermal expansion, cable bends, magnetic performance or assembly tool clearance.

Known unregistered keepouts are deliberately separate and visibly red. Their
intersections are listed in `unregistered-keepout-intersections.json`:

- The illustrative FPC corridor intersects cell/retainer, optional pressure island
  and, for B, the deck. No measured minimum bend radius or actual FPC route exists.
  This is an **open fit gate**. Register the real flex, cut a retainer/deck relief
  or move the island/cell only after that evidence exists. Do not crease the flex.
- The illustrative antenna sector intersects the LRA and case furniture, and B's
  full cell/deck. Because the actual antenna position/required clearance is unknown,
  this proves neither a real collision nor a clear design. Obtain the antenna
  keepout and reposition additions before RF release. The present arrangement
  must not be called antenna-safe.
- The separate protection reservation is now physically allocated without overlap
  with the nominal battery/core PCB. Supplier lead exit and strain relief remain
  unverified; no selected protection circuit or routed wiring is represented.

These gates mean **neither assembly is a verified-fit arrangement**, despite the
nominal solids pass. There is no defensible guaranteed minimum finished thickness
until bare host, pack, flex and antenna data are registered.

## Prototype process and tolerance budget

Proposed first case process is MJF/SLS PA12, dry bench fit only. Exported print
parts are the upper housing with integral lugs, rear cover, host retainer, four
captured keys, speaker cup, two PCB posts, two battery guides, ALS well and B's
accessory deck. Small furniture is individually printed/bonded to its rigid datum;
all glue/bond strength is unqualified. The LRA shim and dielectric liner are cut
parts, and windows/gaskets/membranes are purchased or cut parts represented only
by placeholders. Optional switch/sensor carriers still require detailing.

Assume +/-0.20 mm dimensional print error for discussion, not an accepted bureau
capability. The 0.20 per-side key running gap can be consumed by worst-case error;
use a clearance coupon, measure/ream guide faces and tune the parameter before
functional testing. Do not use tight printer-dependent friction as a return spring.
The 0.60 bezel and retainer are thin, process-sensitive trial features: bureau
minimum feature/flatness approval is required, and thickening them changes the
stack. Do not treat these print files as injection-molding rules or a strength
qualification. Powder clearance and finishing of the 0.7-high duct/1.2 pressure
hole need inspection; do not trap cleaning residue against optics or the cell.

Three proposed rear screws have D1.36 blind pilot bores, D1.90 cover clearances
and D3.30 x 0.55 recesses. Screw form, length, torque, pullout and non-magnetic
material are not selected. The rear joint is a dry butt joint; there is no proven
water seal. Host retainer rests on body bosses with removable, frame-compatible
adhesive retention proposed. Actual glass/PCB load surfaces and adhesive datums
are open; never bond to a flex or take clamp reaction through the pouch.

Lugs have 20.0 clear width, D1.80 bar bores, centers 59.0 apart and tips64.0 apart.
Bars, width, wrist comfort and tensile/fatigue/drop strength remain assumptions.
No rear screw or spring bar has been fitted or load-tested.

## Intended assembly sequence - after fit gates are closed

1. Register the measured host/pack and review all red keepouts. Print empty-case
   coupons and trial the empty housing with an inert dimensioned pack gauge;
   confirm cable, reset and recovery access before fitting powered hardware.
2. Finish/debur holes and bonding faces. Inspect the speaker duct/vent, remove all
   powder/residue and install the qualified optical window/pressure membrane.
3. Insert the host from the rear against frame-compatible optical/adhesive pads.
   Install the retainer on its three rigid body supports, with the measured flex
   route relieved. Check no load on glass active area, flex or battery.
4. Install sensor islands, switch carriers and captured keys from inside. Test
   return and hard stop on actual switches; align existing Back/Select transfer.
5. A: mount core/protection/speaker/LRA furniture on the rigid rear datum around
   the shaped inert cell gauge. B: preassemble them on the accessory deck, bond
   the deck to case collar lands, and route pack leads through its edge notch.
6. Connect short, strain-relieved wires per the hardware brief. Install the
   qualified cell on the insulated rear cover with a removable adhesive tab.
   Verify supplier growth clearance and screw lengths using gauges, not pouch
   compression. Close the three rear screws to a qualified low torque.
7. Verify four buttons, host USB/reset/recovery, display/FPC, optical response,
   LRA mounting and sound/pressure behavior. Then perform fitted-cell charging
   current/temperature review, RF and magnetic tests, and mechanical tests.
   Sealing/immersion qualification would require a separate design and test plan.

## Smallest useful next evidence set

To turn this into fitted CAD, obtain: (1) actual unit revision/GPS marking and
registered bare display/PCB/glass geometry, Z stack, flex bend/antenna keepouts,
USB and stock switch positions; (2) a selected slim protected pack drawing with
all tolerances, growth and lead/protection details; (3) selected LRA/FPC and side
switch drawings including force, electrical travel and maximum overtravel.
A labeled caliper/photo survey or a correct manufacturer STEP/PCB assembly can
supply (1). Strap and ingress targets may remain open for a bench prototype.

With that evidence, nest additions in measured host voids before accepting B's
separate layer, rerun all clearances and update the section drawings. This offers
real thickness reduction without pretending the 51 mm assembly is free cavity
or promising capacity/runtime from an unselected cell.
