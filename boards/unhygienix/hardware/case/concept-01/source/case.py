# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

"""Unhygienix concept CAD; editable CadQuery source, mm, no firmware actions.
PH = placeholder; KO = unverified keepout, excluded from physical assembly.
Run python case.py to rebuild both alternatives, STEP/STL/DXF and numeric checks.
"""
import argparse
import itertools
import json
import math
from pathlib import Path
import cadquery as cq
import trimesh
import ezdxf

ROOT = Path(__file__).resolve().parent.parent
P = json.loads((ROOT / 'source/parameters.json').read_text())
COLORS = {'case':(.23,.30,.37),'back':(.4,.46,.51),'host':(.42,.64,.81),
          'cell':(.98,.67,.17),'pcb':(.14,.57,.37),'speaker':(.72,.32,.25),
          'lra':(.64,.39,.72),'sensor':(.16,.69,.73),'liner':(.84,.83,.73)}

def box(w,d,h,x=0,y=0,z=0):
    return cq.Workplane('XY').box(w,d,h,centered=(True,True,False)).translate((x,y,z))
def cyl(r,h,x=0,y=0,z=0):
    return cq.Workplane('XY').circle(r).extrude(h).translate((x,y,z))
def ring(ro,ri,h,x=0,y=0,z=0):
    return cq.Workplane('XY').circle(ro).circle(ri).extrude(h).translate((x,y,z))
def polar(a,r):
    return (r*math.cos(math.radians(a)),r*math.sin(math.radians(a)))
def rad(shape,a,r):
    return shape.translate((r,0,0)).rotate((0,0,0),(0,0,1),a)
def island(a,w,d,z,h,r=None):
    return rad(box(d,w,h,z=z),a,r or P['sensor_radius'])
def keybox(a,w,d,h,r,z):
    return rad(box(d,w,h,z=z),a,r)
def lra_zone(pad=0,z=0,h=1):
    a=math.degrees(math.atan2(P['lra_xy'][1],P['lra_xy'][0]))-90
    return box(P['lra_flex_envelope_width_RESERVED']+2*pad,
               P['lra_flex_envelope_length_RESERVED']+2*pad,h,y=-2.4,z=z).rotate(
                   (0,0,0),(0,0,1),a).translate((*P['lra_xy'],0))
def cell(h,z,notched):
    result=cyl(P['battery_diameter_TARGET']/2,h,z=z)
    if notched:
        pad=P['battery_edge_gap_ASSUMED'];sx,sy=P['speaker_xy'];px,py=P['pcb_xy']
        cuts=[box(8+2*pad,6+2*pad,h+2,-13,-8,z-1),box(14.4+2*pad,14.4+2*pad,h+2,sx,sy,z-1),lra_zone(pad+.5,z-1,h+2),
              box(P['pcb_width']+2*(pad+1),P['pcb_depth']+2*(pad+1),h+2,px,py,z-1)]
        for a in (P['compass_angle'],P['pressure_angle'],P['light_angle']):
            cuts.append(island(a,5+2*pad,5+2*pad,z-1,h+2))
        for a in P['button_angles']:
            cuts.append(keybox(a,5+2*pad,4+2*pad,h+2,23.9,z-1))
        for tool in cuts: result=result.cut(tool)
        # Discard isolated slivers: a battery must be one contiguous outline.
        result=cq.Workplane(obj=max(result.solids().vals(),key=lambda s:s.Volume()))
    return result

def build(mode):
    thin=mode=='thin_notched';ro=P['body_diameter']/2;ri=P['cavity_diameter']/2
    bt=P['battery_zone_bottom']+P['battery_installed_height_RESERVATION']
    shift=0 if thin else bt+.6+.8-P['battery_zone_bottom']
    hz=bt+.3 if thin else P['speaker_cup_top']+shift+.3
    H=hz+P['host_height_ASSUMED']+2*P['host_axial_gap_each']+P['bezel_thickness']
    parts={};cats={};zones={}
    def add(n,s,c):parts[n]=s;cats[n]=c
    shell=ring(ro,ri,H-1,z=1).union(ring(ro,P['bezel_aperture']/2,.6,z=H-.6))
    bosses=[polar(a,25.0) for a in (90,235,275)]
    for x,y in bosses:
        shell=shell.union(cyl(1.7,hz-1.6,x,y,1)).cut(cyl(.68,4.5,x,y,.9))
    bz=min(4.2+shift,hz-2.8)
    for i,a in enumerate(P['button_angles'],1):
        shell=shell.cut(keybox(a,4.4,7,1.8,ro,bz-.2))
        flange=keybox(a,5.4,.65,2.4,ri-.325,bz-.5).intersect(cyl(ri-.05,H))
        key=keybox(a,4,3.7,1.4,ro-.35,bz).union(flange)
        key=key.union(keybox(a,5.4,1.15,2.4,ro+.925,bz-.5))
        add(f'PRINT_key_{i}',key,'case')
        sw=keybox(a,4,2.6,1.7,23,bz-.15).union(keybox(a,1,.3,1,24.45,bz+.2))
        add(f'PH_switch_{i}',sw,'pcb')
        zones[f'KO_key_{i}_swept']=key.union(key.translate((*polar(a,-P['button_stop']),0)))
    # Ports remain explicitly unregistered to actual PCB features.
    shell=shell.cut(rad(box(7,14,4.6,z=hz+1),270,ro))
    shell=shell.cut(rad(box(6,2,2,z=hz+2),250,ro))
    pa=P['pressure_angle'];la=P['light_angle'];vz=3.5+shift
    shell=shell.cut(rad(box(3,5,5,z=vz-2.5),pa,27.85))
    shell=shell.cut(cq.Workplane('YZ').circle(.6).extrude(8).translate((21,0,vz)).rotate((0,0,0),(0,0,1),pa))
    shell=shell.cut(cq.Workplane('YZ').rect(2,2).extrude(8).translate((21,0,3.6+shift)).rotate((0,0,0),(0,0,1),la))
    sx,sy=P['speaker_xy'];shell=shell.cut(box(18,2.4,.7,sx+8,sy,1.25+shift))
    for side in (-1,1):
        for sign in (-1,1):
            lug=box(3.2,9.5,5,sign*(P['strap_width_ASSUMED']/2+1.6),side*27.25,2)
            hole=cq.Workplane('YZ').center(side*29.5,4.5).circle(P['springbar_bore_ASSUMED']/2).extrude(40,both=True)
            shell=shell.union(lug.cut(hole).cut(cyl(ri,H,z=hz-.7)))
    rear=cyl(ro,1)
    for x,y in bosses:
        rear=rear.cut(cyl(.95,2,x,y,-.1)).cut(cyl(1.65,.55,x,y,0))
    for x in (-7,3):
        rear=rear.cut(box(.8,1.5,1.2,x,-24,-.1))
    add('PRINT_rear_cover',rear,'back')
    add('PRINT_host_retainer',ring(25.45,23,.6,z=hz-.6),'case')
    if not thin:
        deck=cyl(25.45,.8,z=bt+.6)
        for x,y in bosses:
            deck=deck.cut(cyl(2.35,1,x,y,bt+.5))
            shell=shell.union(cyl(3,.3,x,y,bt+.3))
        deck=deck.cut(box(7,3,2,-3,-22.5,bt))
        add('PRINT_accessory_deck',deck,'case')
    floor=1.2+shift
    cup=ring(P['speaker_cup_outer']/2,P['speaker_bore']/2,P['speaker_cup_top']+shift-floor,sx,sy,floor)
    duct=box(14,4,1.1,sx+7,sy,floor).cut(box(16,2.4,.7,sx+7,sy,floor+.05))
    duct=duct.cut(cyl(5.4,2,sx,sy,floor-.1))
    cup=cup.union(duct).cut(box(5,2.4,.7,sx+5.5,sy,floor+.05))
    add('PRINT_speaker_cup',cup.intersect(cyl(ri-.2,10,z=shift)),'case')
    add('PH_speaker_SP1003_RESERVED',cyl(P['speaker_diameter']/2,P['speaker_height_RESERVED'],sx,sy,1.95+shift),'speaker')
    add('PH_speaker_gasket',ring(5.3,4.8,.25,sx,sy,1.7+shift),'liner')
    add('PH_speaker_edge_retainer',ring(5.35,4.6,.4,sx,sy,4.9+shift),'liner')
    lx,ly=P['lra_xy']
    add('CUT_LRA_rigid_shim',cyl(4.5,.3,lx,ly,1.2+shift),'case')
    add('PH_LRA_VG0825003D',cyl(P['lra_diameter']/2,P['lra_height'],lx,ly,1.6+shift),'lra')
    zones['KO_LRA_flex']=lra_zone(0,1.2+shift,3.3)
    px,py=P['pcb_xy'];board=box(P['pcb_width'],P['pcb_depth'],P['pcb_thickness'],px,py,1.8+shift)
    for x,y in ((px-4,py-3),(px+4,py+3)):
        board=board.cut(cyl(.8,1,x,y,1.7+shift))
        add(f'PRINT_PCB_post_{x}_{y}',cyl(1.35,.6,x,y,1.2+shift).cut(cyl(.55,.7,x,y,1.15+shift)),'case')
    add('PH_daughterboard',board,'pcb')
    add('PH_core_components',box(8,8,P['component_height_ASSUMED'],px,py,2.4+shift),'pcb')
    for name,a,w,d,iw,ih in [('compass',P['compass_angle'],3,4,.8,.4),('barometer',pa,4,5,2,.75),('ALS',la,3,4,2,.65)]:
        add(f'PH_{name}_island',island(a,w,d,2.9+shift,.6),'sensor')
        add(f'PH_{name}_IC',island(a,iw,iw,3.5+shift,ih),'sensor')
    add('PH_pressure_membrane_UNSELECTED',rad(box(.15,5,5,z=vz-2.5),pa,26.435),'sensor')
    window=rad(box(.3,1.9,1.9,z=2.65+shift),la,26.7)
    add('PH_ALS_window',window,'sensor');shell=shell.cut(window)
    well=island(la,4.6,5.2,2.7+shift,2.1).cut(island(la,3.4,4.8,2.6+shift,2.3))
    well=well.cut(rad(box(6,2.2,2.2,z=2.65+shift),la,25))
    add('PRINT_ALS_blackout',well,'case');shell=shell.cut(well)
    add('PH_UNSELECTED_battery',cell(P['battery_thickness_TARGET'],P['battery_bottom'],thin),'cell')
    zones['KO_battery_installed']=cell(P['battery_installed_height_RESERVATION'],P['battery_zone_bottom'],thin)
    add('CUT_dielectric_liner',cell(.2,1.05,thin),'liner')
    for a in (110,290):add(f'PRINT_cell_guide_{a}',rad(box(1,4,3,z=1.2),a,23.8),'case')
    add('PH_host_UNREGISTERED',cyl(P['host_diameter_RESERVATION']/2,P['host_height_ASSUMED'],z=hz+P['host_axial_gap_each']),'host')
    add('PRINT_upper_housing',shell,'case')
    zones['REF_visible_display_43_76']=cyl(43.76/2,.1,z=H-.6)
    zones['KO_FPC_route_UNREGISTERED']=box(8,5,hz-1,-10,19,1)
    zones['KO_RF_sector_UNREGISTERED']=island(45,13,6,1,H-2,r=22.5)
    zones['KO_USB_plug_UNREGISTERED']=rad(box(23,14,7,z=hz),270,31)
    zones['KO_reset_tool_UNREGISTERED']=rad(box(15,2,2,z=hz+2),250,31)
    zones['KO_protection_leads_UNSELECTED']=box(8,6,3,-13,-8,1.8+shift)
    add('PH_pack_protection_UNSELECTED',zones['KO_protection_leads_UNSELECTED'],'pcb')
    zones['KO_HR_UNALLOCATED_EXTERNAL_ONLY']=cyl(8,3,z=-3)
    area=parts['PH_UNSELECTED_battery'].val().Volume()/P['battery_thickness_TARGET']
    meta={'mode':mode,'diameter':2*ro,'height':round(H,4),'host_base_datum':hz,'accessory_shift':shift,
          'battery_area_mm2':area,'battery_disk_fraction':area/(math.pi*(P['battery_diameter_TARGET']/2)**2),
          'strap_tip_span':64,'key_span':56.8,'host_height_assumed':P['host_height_ASSUMED']}
    return parts,zones,cats,meta

def mesh(shape):
    vs,fs=shape.val().tessellate(.04,.12)
    return trimesh.Trimesh(vertices=[v.toTuple() for v in vs],faces=fs,process=True)

def export(mode,out):
    out.mkdir(parents=True,exist_ok=True);parts,zones,cats,meta=build(mode)
    asm=cq.Assembly(name='UNVERIFIED_'+mode)
    for n,s in parts.items():asm.add(s,name=n,color=cq.Color(*COLORS[cats[n]]))
    asm.export(str(out/'assembly.step'))
    ko=cq.Assembly(name='UNREGISTERED_KEEPOUTS_NOT_COMPONENTS')
    for n,s in zones.items():ko.add(s,name=n,color=cq.Color(1,.2,.1,.35))
    ko.export(str(out/'keepouts.step'))
    stl=out/'stl';stl.mkdir(exist_ok=True);checks=[]
    for n,s in parts.items():
        b=s.val().BoundingBox();row={'name':n,'valid':s.val().isValid(),'solids':len(s.solids().vals()),'volume':s.val().Volume()}
        row['bounds']=[b.xmin,b.xmax,b.ymin,b.ymax,b.zmin,b.zmax]
        if n.startswith('PRINT_'):
            local=s.translate((-b.center.x,-b.center.y,-b.zmin));path=stl/(n+'.stl')
            cq.exporters.export(local,str(path),tolerance=.04,angularTolerance=.12)
            m=trimesh.load_mesh(path);row.update(watertight=bool(m.is_watertight),mesh_components=len(m.split()),mesh_volume=float(m.volume))
        checks.append(row)
    collisions=[]
    for (na,a),(nb,b) in itertools.combinations(parts.items(),2):
        aa=a.val().BoundingBox();bb=b.val().BoundingBox()
        if any(getattr(aa,k+'max')<=getattr(bb,k+'min')+1e-6 or getattr(bb,k+'max')<=getattr(aa,k+'min')+1e-6 for k in 'xyz'):continue
        v=a.intersect(b).val().Volume()
        if v>1e-5:collisions.append({'a':na,'b':nb,'overlap_mm3':round(v,6)})
    clear=[];bat=zones['KO_battery_installed']
    for n,s in parts.items():
        if n.startswith(('PH_UNSELECTED','CUT_')):continue
        clear.append({'part':n,'gap_mm':round(bat.val().distance(s.val()),4),'overlap_mm3':round(bat.intersect(s).val().Volume(),6)})
    # Keepout intersections are unresolved evidence, deliberately not hidden by the physical-parts pass.
    unresolved=[]
    for zn in ('KO_FPC_route_UNREGISTERED','KO_RF_sector_UNREGISTERED','KO_protection_leads_UNSELECTED'):
        for n,s in parts.items():
            if n in ('PH_host_UNREGISTERED','PH_pack_protection_UNSELECTED'):continue
            v=zones[zn].intersect(s).val().Volume()
            if v>1e-4:unresolved.append({'keepout':zn,'part':n,'overlap_mm3':round(v,6)})
    for name,data in [('geometry-validation',checks),('interferences',collisions),('battery-clearances',clear),('unregistered-keepout-intersections',unresolved)]:
        (out/(name+'.json')).write_text(json.dumps(data,indent=2)+'\n')
    imp=cq.importers.importStep(str(out/'assembly.step'))
    meta.update(step_valid=imp.val().isValid(),step_solids=len(imp.solids().vals()),part_count=len(parts),physical_intersections=len(collisions))
    (out/'layout.json').write_text(json.dumps(meta,indent=2)+'\n')
    doc=ezdxf.new();doc.units=4;m=doc.modelspace();x,y=P['pcb_xy'];w=P['pcb_width']/2;d=P['pcb_depth']/2
    m.add_lwpolyline([(x-w,y-d),(x+w,y-d),(x+w,y+d),(x-w,y+d)],close=True)
    for xx,yy in ((x-4,y-3),(x+4,y+3)):m.add_circle((xx,yy),.8)
    doc.saveas(out/'PROPOSED-daughterboard.dxf')
    shim=ezdxf.new();shim.units=4;shim.modelspace().add_circle(tuple(P['lra_xy']),4.5)
    shim.saveas(out/'CUT-LRA-shim-0.30mm.dxf')
    cq.exporters.export(cell(.1,0,mode=='thin_notched').faces('<Z').wires(),str(out/'UNSELECTED-cell-outline.dxf'))
    return meta

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,default=ROOT/'exports');args=ap.parse_args()
    results=[export(mode,args.out/mode) for mode in ('thin_notched','full_cell_stacked')]
    (args.out/'comparison.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
