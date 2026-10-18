# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

"""Reproducible A3 PDF + editable vector SVG views, sectioned from CAD meshes.
Section discretisation .04 mm; dimension labels use exact CAD parameters.
"""
from pathlib import Path
from xml.sax.saxutils import escape
import json
import numpy as np
from reportlab.pdfgen import canvas
from reportlab.lib.units import mm
from case import ROOT,P,COLORS,build,mesh,polar

OUT=ROOT/'reports';OUT.mkdir(exist_ok=True)
MODES=('thin_notched','full_cell_stacked')
models={m:build(m) for m in MODES}
meshes={m:{n:mesh(s) for n,s in models[m][0].items()} for m in MODES}
c=canvas.Canvas(str(OUT/'unhygienix-case-drawings.pdf'),pagesize=(420*mm,297*mm))
c.setTitle('Unhygienix enclosure concept 01 - UNVERIFIED FIT')
c.setAuthor('Unhygienix mechanical concept / CadQuery generator')

def color(rgb):return '#'+''.join(f'{int(v*255):02x}' for v in rgb)
class Sheet:
    def __init__(self,title,num):
        self.svg=['<svg xmlns="http://www.w3.org/2000/svg" width="420mm" height="297mm" viewBox="0 0 420 297">',
                  '<rect width="420" height="297" fill="white"/>','<g transform="translate(0,297) scale(1,-1)">']
        self.num=num;c.saveState();c.scale(mm,mm)
        self.text(15,280,'UNHYGIENIX / '+title,5.7,bold=True)
        self.text(15,270,'CONCEPT 01  |  UNVERIFIED COMPONENT FIT  |  BATTERY UNSELECTED',3.5,col='#ae3e25')
        self.line(15,265,405,265)
        self.line(15,18,405,18)
        self.text(15,10,'All dimensions mm. A3. Published outer dimensions are not verified bare-host geometry.',3)
        self.text(366,10,f'2026-10-18 / {num}',3)
    def line(self,x,y,X,Y,col='#344452',dash=False,w=.2):
        c.setStrokeColor(col);c.setLineWidth(w);c.setDash([2,1] if dash else [])
        c.line(x,y,X,Y)
        self.svg.append(f'<line x1="{x}" y1="{y}" x2="{X}" y2="{Y}" stroke="{col}" stroke-width="{w}"'+(' stroke-dasharray="2 1"' if dash else '')+'/>')
    def text(self,x,y,t,size=3.4,col='#243746',bold=False):
        c.setFillColor(col);c.setFont('Helvetica-Bold' if bold else 'Helvetica',size);c.drawString(x,y,t)
        self.svg.append(f'<text transform="translate({x},{y}) scale(1,-1)" fill="{col}" font-family="sans-serif" font-size="{size}"'+(' font-weight="bold"' if bold else '')+'>'+escape(t)+'</text>')
    def path(self,loops,fill,stroke='#243746',w=.17):
        if not loops:return
        path=c.beginPath();cmd=[]
        for loop in loops:
            if len(loop)<3:continue
            path.moveTo(*loop[0]);cmd.append(f'M {loop[0][0]:.4f} {loop[0][1]:.4f}')
            for x,y in loop[1:]:path.lineTo(x,y);cmd.append(f'L {x:.4f} {y:.4f}')
            path.close();cmd.append('Z')
        c.setFillColor(fill);c.setStrokeColor(stroke);c.setLineWidth(w);c.setDash([])
        c.drawPath(path,stroke=1,fill=1,fillMode=0)
        self.svg.append(f'<path d="{" ".join(cmd)}" fill="{fill}" fill-rule="evenodd" stroke="{stroke}" stroke-width="{w}"/>')
    def rect(self,x,y,w,h,fill):self.path([[(x,y),(x+w,y),(x+w,y+h),(x,y+h)]],fill)
    def dim(self,x1,y1,x2,y2,label,vertical=False):
        self.line(x1,y1,x2,y2)
        if vertical:
            for y in (y1,y2):self.line(x1-1,y-1,x1+1,y+1)
            self.text(x1+2,(y1+y2)/2,label,3)
        else:
            for x in (x1,x2):self.line(x-1,y1-1,x+1,y1+1)
            self.text((x1+x2)/2-len(label)*.8,y1+2,label,3)
    def finish(self):
        self.svg.append('</g></svg>');(OUT/f'drawing-{self.num:02}.svg').write_text('\n'.join(self.svg))
        c.restoreState();c.showPage()

def slice_loops(m,axis,value):
    normal=np.zeros(3);normal[axis]=1;origin=np.zeros(3);origin[axis]=value
    section=m.section(plane_origin=origin,plane_normal=normal)
    if section is None:return []
    dims=[i for i in range(3) if i!=axis]
    return [np.asarray(loop)[:,dims] for loop in section.discrete]
def plot_part(sh,mode,name,axis,value,cx,cy,scale):
    loops=slice_loops(meshes[mode][name],axis,value)
    if loops:sh.path([[(cx+q[0]*scale,cy+q[1]*scale) for q in loop] for loop in loops],color(COLORS[models[mode][2][name]]))
def section(sh,mode,axis,value,cx,cy,scale):
    for n in meshes[mode]:plot_part(sh,mode,n,axis,value,cx,cy,scale)
def plan(sh,mode,cx,cy,scale):
    # Overlay component footprints from each part's mid-plane. This is a layer projection.
    skip=('PH_host','PRINT_host_retainer','PRINT_accessory_deck','CUT_','PRINT_rear_cover','PH_core_','PH_speaker_gasket','PH_speaker_edge','PH_compass_IC','PH_barometer_IC','PH_ALS_IC')
    names=['PRINT_upper_housing','PH_UNSELECTED_battery']+[n for n in meshes[mode] if n not in ('PRINT_upper_housing','PH_UNSELECTED_battery')]
    for n in names:
        if n.startswith(skip):continue
        bb=models[mode][0][n].val().BoundingBox();z=(bb.zmin+bb.zmax)/2
        if n=='PRINT_upper_housing':z=4.5
        plot_part(sh,mode,n,2,z,cx,cy,scale)
    for label,x,y in [('S',15,-5),('L',13.4,13),('D',-2,-16),('P',-13,-8),('M',*polar(160,23.1)),('B',*polar(130,23.1)),('A',*polar(70,23.1))]:
        sh.text(cx+x*scale-1,cy+y*scale-1,label,3.2,bold=True)
    sh.line(cx-30*scale,cy-5*scale,cx+30*scale,cy-5*scale,dash=True)
    sh.text(cx-32*scale,cy-5*scale,'A',3);sh.text(cx+31*scale,cy-5*scale,'A',3)
    sh.line(cx+13.4*scale,cy-29*scale,cx+13.4*scale,cy+29*scale,dash=True)
    sh.text(cx+13.4*scale+1,cy+29*scale,'B',3)
    sh.dim(cx-26.9*scale,cy-36*scale,cx+26.9*scale,cy-36*scale,'BODY DIA 53.8')
    sh.dim(cx-35*scale,cy-32*scale,cx-35*scale,cy+32*scale,'64 lugs',True)
    sh.line(cx-3,cy,cx+3,cy,dash=True);sh.line(cx,cy-3,cx,cy+3,dash=True)

s=Sheet('LAYOUT COMPARISON',1)
for mode,cx,title in zip(MODES,(109,312),('A / minimum-thickness study','B / full-cell stacked concept')):
    meta=models[mode][3];s.text(cx-75,252,title,4.2,bold=True)
    s.text(cx-75,245,f'D53.8 x H{meta["height"]:.1f}; assumes bare host H6.0',3.5)
    plan(s,mode,cx,160,2.35)
    s.text(cx-75,61,f'Cell outline area {meta["battery_area_mm2"]:.0f} mm2 ({meta["battery_disk_fraction"]*100:.1f}% of D45)',3.4)
    s.text(cx-75,54,'Host hidden; footprints at different Z overlaid.',3.1)
s.text(15,40,'S speaker   L LRA   D daughterboard   P pack protection   M compass   B barometer   A ambient light',3.6)
s.text(15,31,'A has no demonstrated 750-800 mAh pack. B preserves cell area at a 5.6 mm thickness penalty; pack remains unselected.',3.4)
s.finish()

for num,mode in enumerate(MODES,2):
    parts,zones,cats,meta=models[mode];shift=meta['accessory_shift'];hz=meta['host_base_datum'];H=meta['height']
    s=Sheet(('A / NOTCHED CELL' if num==2 else 'B / FULL CELL')+' SECTIONS',num)
    s.text(15,252,'A-A / y = -5.0, looking toward +Y / speaker face and side duct',4,bold=True)
    section(s,mode,1,-5,120,180,3.5)
    s.dim(120-26.9*3.5,170,120+26.9*3.5,170,'53.8 BODY')
    s.dim(222,180,222,180+H*3.5,f'{H:.1f} TOTAL',True)
    s.text(250,237,f'Height equation: H = {H-6:.1f} + measured host height',3.8,bold=True)
    for j,t in enumerate([
      'Host solid is a D51 x H6.0 reservation, not a bare-PCB model.',
      f'Host lower datum Z {hz:.2f}; solid starts Z {hz+.15:.2f}.',
      '0.15 axial gap each side; 0.30 nominal radial host clearance.',
      '0.60 bezel; D44.30 aperture over published 43.76 active area.',
      'Cell: Z1.40-5.40, 4.00 target; no selected part/tolerance.',
      'Installed cell reservation: Z1.20-6.70, including growth/liners.',
      f'Speaker body Z{1.95+shift:.2f}-{4.85+shift:.2f}; reserved H2.90.',
      f'Cup top Z{5.4+shift:.2f}; gasket H0.25; face plenum H0.50.',
      'Outlet duct 2.4 x 0.7; cavity and grille not acoustically qualified.',
      'Rear floor 1.00; dielectric liner H0.20; no pouch support loads.'
    ]):s.text(250,226-j*7,t,3.25)
    s.text(15,144,'B-B / x = 13.4, looking toward -X / LRA and battery zone',4,bold=True)
    section(s,mode,0,13.4,120,70,3.5)
    s.dim(222,70,222,70+H*3.5,f'{H:.1f} TOTAL',True)
    for j,t in enumerate([
      f'LRA body Z{1.6+shift:.2f}-{4.15+shift:.2f}; D8.0 x H2.55 nominal.',
      'Full FPC reservation 9.2 x 14.2, tail directed inward.',
      'Rigid 0.30 shoe bonds to rear/deck, separately from cell.',
      '0.10 adhesive under LRA body is an assumed stack allowance.',
      'Optional rear HR: external ghost only, zero internal allocation.',
      'No optical window or HR sensor is silently fitted in the cell volume.'
    ]):s.text(250,128-j*8,t,3.3)
    if num==3:
        s.text(250,68,'B: deck Z7.30-8.10; supported on case boss collars.',3.3)
        s.text(250,60,'Remote protection bay Z8.70-11.70 above deck.',3.3)
        s.text(250,52,'Pack lead length and construction require supplier approval.',3.3)
    else:
        s.text(250,68,'A: concave cell outline is a feasibility envelope only.',3.3)
        s.text(250,60,'Supplier manufacturability/capacity is unknown.',3.3)
        s.text(250,52,'No rectangular 48 x 46 mm pocket is assumed.',3.3)
    s.text(15,30,'Section boundaries tessellated from CAD at 0.04 mm; dimensions are exact model parameters. Colors identify placeholder components.',3.2)
    s.finish()

s=Sheet('BUTTON / PCB / DATUM DETAILS',4)
s.text(15,251,'C / radial button section at angle 0 deg, A layout / scale 12:1',4,bold=True)
# Local zoom; render slice only button, switch and housing, crop in the graphics state.
s.svg.append('<defs><clipPath id="buttonZoom"><rect x="15" y="180" width="160" height="60"/></clipPath></defs><g clip-path="url(#buttonZoom)">')
c.saveState();clip=c.beginPath();clip.rect(15,180,160,60);c.clipPath(clip,stroke=0)
for n in ['PRINT_upper_housing','PRINT_key_1','PH_switch_1']:
    plot_part(s,'thin_notched',n,1,0,-190,140,12)
c.restoreState();s.svg.append('</g>')
s.text(15,173,'Actual section of housing, captured key and switch reservation.',3)
for j,t in enumerate([
 'Rod 4.00 x 1.40 in 4.40 x 1.80 guide: 0.20 running gap per side.',
 'Inside flange 5.40 x 2.40 retained by aperture; radius trimmed to clear wall.',
 'Outside shoulder limits inward travel to 0.35 nominal.',
 'Rest rod-to-switch-tip gap 0.10. Assumed electrical travel 0.15:',
 '0.25 total to actuate, leaving 0.10 to hard stop. Actual switch TBD.',
 'Housing supplies retention/stop, selected switch must supply return force.',
 'Back/Select positions are proposals; engagement with stock switches unverified.',
 'Switch carrier, transfer geometry, seals and PCB routing are not released.'
]):s.text(195,241-j*8,t,3.4)
s.text(15,146,'D / proposed daughterboard outline / scale 5:1',4,bold=True)
# Dedicated outline with global XY labels; STEP/DXF share datums.
s.rect(45,72,60,50,'#6bb18c')
for x,y in [(55,82),(95,112)]:
    theta=np.linspace(0,2*np.pi,60);s.path([[(x+4*np.cos(t),y+4*np.sin(t)) for t in theta]],'#ffffff')
s.dim(45,62,105,62,'12.00');s.dim(115,72,115,122,'10.00',True)
s.text(15,49,'Global board extents X[-8,4], Y[-21,-11].',3.4)
s.text(15,41,'Holes D1.60 at (-6,-19), (2,-13). Z1.80 A / Z8.70 B.',3.4)
s.text(195,142,'Case datums and development access',4,bold=True)
for j,t in enumerate([
 'O = display center; Z0 = external rear surface; +Y = 12 o\'clock lug.',
 'Three rear fasteners on R25 at 90, 235, 275 deg.',
 'Pilot D1.36 x 4.5; rear clearance D1.90; head recess D3.30 x 0.55.',
 'Candidate small polymer-thread screws; length/pullout not qualified.',
 'USB at 270 deg: 14 x 4.6 case opening, 23 x 14 x 7 plug keepout.',
 'Reset-tool opening at 250 deg, 2 x 2. Actual USB/reset positions TBD.',
 'Lugs: 20.0 clear width, 59.0 bar spacing, 64.0 tip span, D1.80 bores.',
 'Strap sizes, fit, pull strength and wrist clearance are prototype assumptions.',
 'Printed PA12 nominal +/-0.20 on dimensions; calibrate a coupon first.',
 'Dry bench prototype only. No water-resistance or final fastening claim.'
]):s.text(195,131-j*8,t,3.35)
s.finish()

s=Sheet('CLEARANCE / EVIDENCE / RELEASE GATES',5)
s.text(15,251,'What is established',4,bold=True)
for j,t in enumerate([
 'Manufacturer image: D51 assembly, 43.76 visible width, stock side 11.70 / 12.10.',
 'Downloaded host resource contains DWG + PDF, no STEP solid. PDF label is ESP32-S3.',
 'It cannot establish actual T5 PCB revision, internal heights or mounting datums.',
 'LRA drawing: D8.0 +/-0.1, H2.55 +/-0.05; FPC overall 13.265 +/-0.3 x 8.535 +/-0.3.',
 'Soberton product and 2019 drawing show D10 x H2.4; this model retains H2.9 reserve.',
 'Candidate dimensions are not installed-fit verification or final component selection.'
]):s.text(15,239-j*8,t,3.6)
s.text(15,180,'Numerical CAD checks',4,bold=True)
for k,mode in enumerate(MODES):
    meta=json.loads((ROOT/'exports'/mode/'layout.json').read_text())
    val=json.loads((ROOT/'exports'/mode/'geometry-validation.json').read_text())
    rows=json.loads((ROOT/'exports'/mode/'battery-clearances.json').read_text())
    minrow=min(rows,key=lambda r:r['gap_mm']);nprint=sum('watertight' in a for a in val)
    s.text(15,166-k*19,f'{mode}: {meta["part_count"]} physical parts; {meta["physical_intersections"]} positive-volume intersections; STEP reimport valid = {meta["step_valid"]}.',3.5)
    s.text(15,159-k*19,f'{nprint} printable meshes; watertight = {all(a.get("watertight",True) for a in val)}. Minimum installed-cell gap {minrow["gap_mm"]:.2f} to {minrow["part"]}.',3.5)
s.text(15,112,'Open release gates - a clear placeholder assembly is not proof of component fit',4,bold=True,col='#ae3e25')
for j,t in enumerate([
 'FPC, RF and actual PCB geometry remain unregistered. Red keepout STEP and JSON record their intersections.',
 'Register actual flex before routing: current illustrative FPC intersects battery/retainer and optional pressure island.',
 'Illustrative RF sector intersects LRA. Obtain antenna drawing and rearrange magnetic/metal parts before fit release.',
 'Battery pack, lead exit, growth, protection and capacity must be supplier-drawn; A removes about half the cell area.',
 'Magnetic center distances are geometric only; test speaker/LRA drive, charging, screws and compass saturation.',
 'Qualify button return/travel, carrier, USB plug and recovery access, vent adhesive, speaker response and strap loads.',
 'HR requires a separately budgeted rear cover/stack. Charging current and enclosure temperature need fitted-cell review.'
]):s.text(15,100-j*9,t,3.5)
s.text(15,27,'All STEP/STL outputs are concept fit-study artifacts. No parts fabricated, ordered or fitted; firmware untouched by this design.',3.4)
s.finish();c.save()
print(OUT/'unhygienix-case-drawings.pdf')
