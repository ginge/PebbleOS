"""Geometry/export audit only, never a claim of physical component fit."""
import csv,json,math
from case import ROOT,P,build,polar
results=[]
for mode in ('thin_notched','full_cell_stacked'):
    out=ROOT/'exports'/mode
    checks=json.loads((out/'geometry-validation.json').read_text())
    info=json.loads((out/'layout.json').read_text())
    inter=json.loads((out/'interferences.json').read_text())
    gaps=json.loads((out/'battery-clearances.json').read_text())
    assert {x.name for x in (out/'stl').glob('*.stl')} == {r['name']+'.stl' for r in checks if 'watertight' in r}
    assert all(r['valid'] and r['solids']==1 for r in checks)
    assert all(r.get('watertight',True) and r.get('mesh_components',1)==1 for r in checks)
    assert info['step_valid'] and info['step_solids']==info['part_count']
    assert not inter
    assert all(r['overlap_mm3']<1e-5 and r['gap_mm']>=.1999 for r in gaps)
    parts,zones,cats,meta=build(mode);shell=parts['PRINT_upper_housing']
    motion=[]
    for i,a in enumerate(P['button_angles'],1):
        key=parts[f'PRINT_key_{i}']
        row={'key':i}
        for travel in (0,.25,.35,.4):
            moved=key.translate((*polar(a,-travel),0))
            vol=moved.intersect(shell).val().Volume()
            row[str(travel)]=round(vol,6)
        assert row['0']<1e-5 and row['0.25']<1e-5 and row['0.35']<1e-5
        assert row['0.4']>1e-5
        motion.append(row)
    with (out/'battery-clearances.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=gaps[0].keys());w.writeheader();w.writerows(gaps)
    (out/'key-motion-audit.json').write_text(json.dumps(motion,indent=2)+'\n')
    mag=parts['PH_compass_IC'].val()
    dists={n:mag.distance(parts[n].val()) for n in ('PH_LRA_VG0825003D','PH_speaker_SP1003_RESERVED')}
    minimum=min(gaps,key=lambda r:r['gap_mm'])
    results.append({'mode':mode,'physical_parts':info['part_count'],
                    'printable_parts':sum('watertight' in r for r in checks),
                    'min_installed_cell_gap_mm':minimum['gap_mm'],
                    'magnetic_body_surface_gaps_mm':dists,
                    'status':'CAD_GEOMETRY_PASS_ONLY; PHYSICAL_FIT_UNVERIFIED'})
(ROOT/'reports/validation.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
