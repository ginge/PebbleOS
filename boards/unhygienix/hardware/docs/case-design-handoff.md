# Handoff prompt: Unhygienix custom watch case

Design a compact custom watch enclosure for the Waveshare
T5-E1-Touch-AMOLED-1.75 (PebbleOS target `unhygienix`) and its proposed add-on
hardware. Work in `/home/baz/coredevices/pebbleos` and follow its AGENTS.md.

Save case CAD and reports in the hardware workspace `case/` directory.
Use `references/` for the archived manufacturer inputs and `pdf/` for the
illustrated proposal. Read the workspace README for snapshot limitations.

First read:

- `/home/baz/coredevices/pebbleos/boards/unhygienix/hardware/docs/hardware-addon-design.md`
- `/home/baz/coredevices/pebbleos/docs/boards/unhygienix/index.md`
- `/home/baz/coredevices/pebbleos/docs/boards/unhygienix/battery-review.md`

The hardware brief is the design input. Preserve its distinction between
manufacturer dimensions, provisional reservations and unselected components.
The owner's latest preference is a slim premium battery, not the older 5.3 mm
cell retained in the comparison budget. Do not treat the target 750-800 mAh,
3.5-4 mm shaped battery as a sourced part or guarantee 48-hour runtime.

Keep the external footprint close to the existing 51 mm assembly and minimise
total thickness. Include four logical buttons (existing Back/Select plus physical
Up/Down), a small daughterboard, LRA haptics, ambient-light window and the tiny
Soberton SP-1003 speaker. Reserve optional compass/magnetometer and vented BMP390
barometer arrangements. Keep optional rear heart-rate optics explicit; do not
silently take their volume from the battery. Strap dimensions, manufacturing
process and water-resistance target remain open.

Obtain and verify the host mechanical model/drawing and the actual PCB revision.
Search existing project artifacts for case geometry before asking for it. No
custom-case CAD was supplied in the originating discussion. Proceed with a
clearly labelled parametric concept while identifying the dimensions or supplier
drawings needed to establish fit. Do not manufacture missing dimensions.

Explore a thin assembly stack, with protected battery retention, display-flex
bend clearance, non-magnetic/perimeter compass placement, rigid LRA coupling,
speaker gasket/cavity/outlet, and separate or qualified shared pressure vent.
Check USB/reset/recovery access and button travel. A 10 mm speaker does not
automatically fit beside a 45 mm battery in a 51 mm circle. A 48 x 46 mm battery
bounding envelope requires a shaped pocket, not a rectangular cavity. Do not
claim fit from bounding widths alone or use the battery pouch as a structural
support. Verify speaker/LRA magnetic effects and antenna clearance in the layout.

Create editable parametric CAD, a STEP assembly, printable case STL/3MF parts,
dimensioned top/section drawings and a clearance/assembly report. Clearly mark
placeholder parts and unverified-fit regions. Coordinate a proposed daughterboard
outline and mounting datums; no routed PCB currently exists. Show the external
diameter and total thickness of each viable arrangement, and explain any tradeoff
needed to accommodate the battery and speaker. Save new CAD/artifacts under an
`/home/baz/coredevices/pebbleos/boards/unhygienix/hardware/case/` directory and document paths
in the handoff notes. Do not overwrite existing unrelated changes or alter
firmware as part of enclosure work.

Do useful concept/layout work before requesting missing input. Ask only for
information that blocks a concrete decision. Report unresolved battery/LRA/switch
selection and physical measurements, fit limitations and proposed next steps.
Do not order parts, fabricate, or contact suppliers without user instruction.

## Concept 01 checkpoint - 2026-10-18

Editable CadQuery source, named STEP assemblies, printable case STLs, PCB/cell
DXFs, five A3 top/section drawing sheets, SVGs and clearance/assembly report:
[`../case/concept-01/README.md`](../case/concept-01/README.md).

A notched-cell study is D53.8 x H13.9; the full-cell stack is D53.8 x H19.5,
assuming an explicitly unmeasured 6.0 mm bare host. Both include separate
protection, speaker, LRA, four keys and sensor island envelopes. A retains 47.8%
of a D45 disk area, so the 750-800 mAh target cannot be assumed.
Nominal CAD checks do not establish actual component fit. The downloaded host
DWG/PDF resource is labelled ESP32-S3; actual T5 internals/revision remain open.
Register host FPC/antenna/USB/switch geometry and selected battery/LRA/switch
drawings before releasing fitted CAD. No firmware changed for this enclosure work.
