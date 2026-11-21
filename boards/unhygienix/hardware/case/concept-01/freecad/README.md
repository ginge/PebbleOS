# Unhygienix concept 01 — native FreeCAD models

Start with **unhygienix-full_cell_stacked.FCStd**. The second file,
**unhygienix-thin_notched.FCStd**, preserves the thinner battery-outline study.
Both reproduce the existing concept; neither establishes fit to the actual watch.

## Editing in FreeCAD

1. Open the `.FCStd` file and double-click **Dimensions (mm / degrees) — edit
   column B** in the tree. Edit the values in column B, then recompute (F5).
2. `body_diameter`, `cavity_diameter`, `bezel_aperture`,
   `host_height_ASSUMED`, `strap_width_ASSUMED`, and `springbar_bore_ASSUMED`
   are useful starting points. Angle entries are degrees; other entries are mm.
   Derived stack heights appear below the input dimensions. Some entries inherited
   from the source brief describe reference components only, or are unused by
   the original construction (for example `rear_floor` and
   `button_running_gap_each`); they do not resize a corresponding print feature.
   Use that feature’s sketch/extrusion properties for its local dimensions.
3. Expand **PRINTABLE native parts** to inspect the parts. Each has its own
   editable history of constrained Sketcher profiles, native Part extrusions,
   positioned compounds and Boolean operations. This is a native Part/Sketcher
   workflow, rather than a Part Design Body/Pad/Pocket feature tree.
4. Sketch dimensions with an expression are driven by the spreadsheet. Edit the
   spreadsheet to retain that relationship. Other local details can be changed
   through sketch constraints or extrusion properties in the tree.
5. The source concept has some fixed offsets and manufacturing allowances.
   Changing one spreadsheet value does not guarantee that every related detail
   stays appropriate. In particular, some mounting offsets, walls, port locations,
   bezel details and button dimensions are local feature dimensions. Recheck the
   complete assembly after edits.

No external Python module, CadQuery installation, macro, or source STEP file is
needed to open or recompute the saved native models. The construction features
use standard FreeCAD types. Frozen hardware placeholders are explicitly
separated under **REFERENCE ONLY** and hidden initially. These references do not
follow edits to the native parts. They remain evidence of the original concept,
not newly registered hardware geometry.

## Printing

Pre-exported meshes are under `full_cell_stacked/stl/` and `thin_notched/stl/`.
They are in millimetres, centered in XY and resting on Z0. Import at 100% scale.
Do not combine parts from the two variants.

For your edited model, select only the desired final `PRINT_` object in the tree
and use File → Export → STL. Do not select the construction history or the whole
document. Parts are stored at their assembly coordinates; use the slicer's arrange
and drop-to-bed commands after export. These commands position the parts but do
not select a process-appropriate print orientation.

The existing concept assumes MJF/SLS PA12. FDM needs process-specific review of
the 0.6 mm bezel/retainer, small ducts, spring-bar holes and button clearances.
Begin with an empty housing and rear-cover fit trial. The speaker, sensors,
windows, switches, battery, dielectric liner and LRA shim are not all printable
parts and are not supplied as printable component models.

## Verification and limits

`validation.json` records solid validity, one-solid topology, comparison with the
original STEP print parts, a spreadsheet diameter edit and restoration, and a
save/reopen/recompute check. Each generated STL is also checked as a solid mesh.
These checks establish faithful CAD reconstruction, not hardware fit or strength.

The full-cell baseline remains nominally D53.8 × H19.5 mm. The thin variant is
nominally H13.9 mm and needs an unselected concave battery. Both heights assume a
6 mm bare host. USB/reset positions, FPC/antenna clearance, switches, real pack
geometry and fasteners remain unverified. Read the parent concept README and
`../reports/clearance-and-assembly.md` before installing hardware.

## Rebuilding the deliverables

`build_native.py` runs with FreeCAD's Python modules; it has no CadQuery dependency.
It reads the original concept's `source/parameters.json` and matching
`exports/*/assembly.step` for comparison and frozen references. From the FreeCAD
Python console, for example (substitute your full path):

```python
import runpy, sys
sys.argv = ['build_native.py']
runpy.run_path('/full/path/to/concept-01/freecad/build_native.py', run_name='__main__')
```

The generator rebuilds both files, validates them and regenerates the STLs. It
replaces files in this FreeCAD output directory; save personal edits under a new
name before rebuilding. The original CadQuery exports are not overwritten.
