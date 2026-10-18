# Unhygienix hardware design workspace

Canonical location for the T5-E1 Touch AMOLED 1.75 add-on hardware, enclosure
design, supporting PDFs, design generators and mechanical references.

Start with the [hardware/enclosure brief](docs/hardware-addon-design.md) and
[case-design handoff prompt](docs/case-design-handoff.md).

| Path | Contents |
| --- | --- |
| `docs/` | Text design brief, BOM/constraints and handoff prompt |
| `pdf/` | Reviewed illustrated proposal (Rev D discussion snapshot) |
| `scripts/` | Python generator for that proposal |
| `references/` | Original manufacturer schematic and dimension drawing, with provenance/hashes |
| `case/` | Future editable enclosure CAD, STEP assemblies, print exports and fit reports |
| `pcb/` | Future schematic/layout CAD, BOM exports and fabrication outputs |
| `measurements/` | Future physical dimensions, power measurements and validation evidence |

The text brief records the latest decisions and unresolved choices. The PDF is
a historical proposal snapshot and retains the larger battery comparison. Its
generator reproduces that snapshot; it does not automatically synchronise with
the Markdown brief. Update both deliberately when issuing a new design revision.

No case CAD, routed PCB or verified-fit manufacturing files existed when this
directory was created on 2026-10-18. Directory reservations are not completed
designs. The slim premium battery remains a sourcing target, not a selected part.

## Regenerate the proposal

Use Python 3 with `reportlab==4.4.9` (the version used for the saved proposal):

```sh
python3 boards/unhygienix/hardware/scripts/make_brief.py
```

The default output is `pdf/t5ai-pebble-addon-proposal.pdf` relative to this
hardware directory, independent of the current working directory. For a preview:

```sh
python3 boards/unhygienix/hardware/scripts/make_brief.py --output /tmp/t5ai-proposal.pdf
```

Render and visually check every changed PDF page before publishing a revision.
Keep editable originals and final exports together in this tree; do not rely on
`/tmp` or the reference workspace for canonical source files. The old workspace
PDF remains a historical copy, not the canonical location.

Related firmware documentation remains in its existing location:
[board status](../../../docs/boards/unhygienix/index.md),
[battery review](../../../docs/boards/unhygienix/battery-review.md), and
[microphone integration](../../../docs/boards/unhygienix/microphone.md).
Follow the repository and parent board AGENTS.md. Preserve unrelated firmware work.
