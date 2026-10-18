# Unhygienix enclosure concept 01

**Editable mechanical study, not verified-fit or released manufacturing CAD.**
Battery, exact host revision/internal geometry, switches and installed fit remain
unverified. No firmware was modified, hardware measured, parts ordered or parts
fabricated for this design. Date: 2026-10-18.

| Arrangement | Body diameter | Total height with assumed 6.0 mm bare host | Cell outline area | Assessment |
| --- | --- | --- | --- | --- |
| A: `thin_notched` | 53.8 mm | 13.9 mm | 760.9 mm2 / 47.8% of D45 disk | Minimum-thickness study; severe capacity/manufacturability compromise |
| B: `full_cell_stacked` | 53.8 mm | 19.5 mm | 1590.4 mm2 / 100% | Full-cell feasibility baseline; 5.6 mm thicker |

Neither is a sourced 750-800 mAh pack. Thickness is **7.9 + bare-host height**
for A and **13.5 + bare-host height** for B, in mm. A 6.0 mm host height is an
explicit design assumption, not a measurement. The stock 11.70/12.10 mm height
includes its case and cannot establish that bare-host height. For example,
a hypothetical 8 mm bare host would produce 15.9/21.5 mm cases; a hypothetical
11.7 mm bare host would produce 19.6/25.2 mm cases.

The footprint includes a 53.8 mm round body, a 56.8 mm span across the opposed
button caps, and 64.0 mm lug-to-lug length. A provisional 20 mm strap is shown.
The body is 2.8 mm wider than the stock assembly. With a conservative D51 host
reservation, 0.3 mm radial insertion clearance and 1.1 mm wall, a D51 outer case
is impossible: 51 + 2(0.3 + 1.1) = 53.8 mm. A D51 case would need an actual
host/frame outline no larger than D48.2 with those allowances. Measure it before
reducing the case diameter.

## Files

- `source/case.py`, `source/parameters.json`: editable CadQuery parametric CAD.
- `exports/{thin_notched,full_cell_stacked}/assembly.step`: named, colored physical
  assembly solids. `PH_` components are placeholders, not supplier CAD.
- `exports/*/keepouts.step`: separate red reference/keepout assembly. Includes
  **unregistered** host FPC/RF/recovery regions and an external HR ghost.
- `exports/*/stl/`: manifold printable case parts, millimetres, each centered in
  XY and placed at Z0. STEP preserves assembled coordinates.
- `exports/*/PROPOSED-daughterboard.dxf`: proposed outline and mounting datums.
- `exports/*/UNSELECTED-cell-outline.dxf`: feasibility outline only, not a pack drawing.
- `reports/unhygienix-case-drawings.pdf`: five A3 sheets: top plans, sections of
  both alternatives, button/PCB detail and clearance/evidence gates.
- `reports/drawing-01.svg` through `drawing-05.svg`: editable vector sheets.
- `reports/clearance-and-assembly.md`: engineering interpretation, uncertainties,
  assembly sequence, print assumptions and the measurements that matter next.
- `exports/*/*clearances*`, `*interferences*`, `key-motion-audit.json` and
  `geometry-validation.json`: machine-readable checks; `reports/validation.json`
  summarizes checks on the supplied geometry only.
- `evidence/`: official host drawing download, LRA drawing and URL/SHA-256 provenance.
  Existing manufacturer image/schematic remain in `../../references/`.

## Rebuild

Use Python 3 and the pinned direct dependencies in `source/requirements.txt`:

```sh
python3 -m venv /tmp/unhygienix-cad-venv
/tmp/unhygienix-cad-venv/bin/pip install -r source/requirements.txt
/tmp/unhygienix-cad-venv/bin/python source/case.py
/tmp/unhygienix-cad-venv/bin/python source/validate.py
/tmp/unhygienix-cad-venv/bin/python source/drawings.py
```

Run from this directory. No FreeCAD installation is required; the authoritative
editable model is the CadQuery construction script plus parameters. STEP is the
neutral exchange format, not a parametric feature-history replacement.
The generator operates only on its own exports/reports. Re-review dimensions,
process minima and tolerances after parameter changes; assertions do not validate
new supplier geometry. Direct dependency versions and the generation log are saved.

## Decision

Carry B as the full-cell packaging baseline while obtaining the actual host model
and cell drawings. Keep A as a quantified thickness/capacity trade study, not a
recommendation to source a concave 750-800 mAh cell. The highest-value thickness
reduction next is to register the real bare board/display and identify existing
voids for additions; only measured voids can justify removing a layer. No such
internal void is claimed here.
