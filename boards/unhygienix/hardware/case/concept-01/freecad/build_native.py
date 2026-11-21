# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Native FreeCAD sketch/CSG rebuild of the Unhygienix case concept.
Run inside FreeCAD's Python environment. No CadQuery dependency.
"""
import json
import math
import re
from pathlib import Path
import FreeCAD as App
import Part
import Sketcher
import Import
import MeshPart

class Dim:
    """Carry a numerical value and a FreeCAD spreadsheet expression together."""
    def __init__(self, value, expr=None):
        self.value = float(value)
        self.expr = str(value) if expr is None else expr
    def calc(self, other, op, fn):
        other = dim(other)
        return Dim(fn(self.value, other.value), f'({self.expr} {op} {other.expr})')
    def __add__(self, x): return self.calc(x, '+', lambda a,b:a+b)
    __radd__ = __add__
    def __sub__(self, x): return self.calc(x, '-', lambda a,b:a-b)
    def __rsub__(self, x): return dim(x).__sub__(self)
    def __mul__(self, x): return self.calc(x, '*', lambda a,b:a*b)
    __rmul__ = __mul__
    def __truediv__(self, x): return self.calc(x, '/', lambda a,b:a/b)
    def __neg__(self): return Dim(-self.value, f'(-{self.expr})')
    def __lt__(self, x): return self.value < dim(x).value
    def __bool__(self): return bool(self.value)
    def __format__(self, spec): return format(self.value,spec)
    def __str__(self): return str(self.value)
    def __round__(self, n=0): return round(self.value,n)
    def __pow__(self, x): return self.calc(x,"^", lambda a,b:a**b)
    def __rtruediv__(self, x): return dim(x).__truediv__(self)

def dim(x): return x if isinstance(x, Dim) else Dim(x)
def number(x): return dim(x).value

def bind(obj, prop, value):
    value=dim(value)
    obj.setExpression(prop, value.expr)

def vector(values): return App.Vector(*(number(v) for v in values))

def profile(plane, circles=None, rectangle=None, center=(0,0)):
    sk = DOC.addObject('Sketcher::SketchObject','Profile')
    sk.Label='Circle/ring profile' if circles else 'Constrained rectangular profile'
    if plane == 'YZ':
        sk.Placement.Rotation=App.Rotation(App.Vector(1,1,1),120)
    if circles:
        for r in circles:
            i=sk.addGeometry(Part.Circle(App.Vector(0,0,0),App.Vector(0,0,1),number(r)),False)
            sk.addConstraint(Sketcher.Constraint('Coincident',i,3,-1,1))
            c=sk.addConstraint(Sketcher.Constraint('Radius',i,number(r)))
            bind(sk,f'Constraints[{c}]',r)
    else:
        w,h=rectangle
        vertices=[(-w/2,-h/2),(w/2,-h/2),(w/2,h/2),(-w/2,h/2)]
        for i in range(4):
            a,b=vertices[i],vertices[(i+1)%4]
            sk.addGeometry(Part.LineSegment(vector((*a,0)),vector((*b,0))),False)
        for i in range(4):
            sk.addConstraint(Sketcher.Constraint('Coincident',i,2,(i+1)%4,1))
            sk.addConstraint(Sketcher.Constraint('Horizontal' if i%2==0 else 'Vertical',i))
        for typ,idx,point,value in [('DistanceX',0,1,-w/2),('DistanceY',0,1,-h/2)]:
            c=sk.addConstraint(Sketcher.Constraint(typ,idx,point,number(value)))
            bind(sk,f'Constraints[{c}]',value)
        for idx,value in [(0,w),(1,h)]:
            c=sk.addConstraint(Sketcher.Constraint('Distance',idx,number(value)))
            bind(sk,f'Constraints[{c}]',value)
    if center!=(0,0):
        # Only the spring-bar YZ profile uses a shifted sketch origin.
        if plane=='YZ':
            bind(sk,'Placement.Base.y',center[0]);bind(sk,'Placement.Base.z',center[1])
        else:
            bind(sk,'Placement.Base.x',center[0]);bind(sk,'Placement.Base.y',center[1])
    return sk

class Solid:
    def __init__(self,obj): self.obj=obj
    def boolean(self,other,kind):
        obj=DOC.addObject('Part::'+kind,kind)
        obj.Base=self.obj;obj.Tool=other.obj;obj.Refine=True
        return Solid(obj)
    def union(self,other): return self.boolean(other,'Fuse')
    def cut(self,other): return self.boolean(other,'Cut')
    def intersect(self,other): return self.boolean(other,'Common')
    def translate(self,xyz):
        obj=DOC.addObject('Part::Compound','Position');obj.Links=[self.obj]
        for axis,val in zip('xyz',xyz): bind(obj,'Placement.Base.'+axis,val)
        return Solid(obj)
    def rotate(self,origin,axis,angle):
        assert origin==(0,0,0)
        obj=DOC.addObject('Part::Compound','Orientation');obj.Links=[self.obj]
        obj.Placement.Rotation=App.Rotation(vector(axis),number(angle))
        bind(obj,'Placement.Rotation.Angle',angle)
        return Solid(obj)

class Workplane:
    def __init__(self,plane='XY',**unused):
        self.plane=plane;self.circles=[];self.rectangle=None;self.offset=(0,0)
    def circle(self,r):self.circles.append(r);return self
    def rect(self,w,h):self.rectangle=(w,h);return self
    def center(self,x,y):self.offset=(x,y);return self
    def extrude(self,length,both=False):
        sk=profile(self.plane,self.circles,self.rectangle,self.offset)
        obj=DOC.addObject('Part::Extrusion','Extrude');obj.Base=sk
        obj.DirMode='Normal';obj.Solid=True
        bind(obj,'LengthFwd',length)
        if both:bind(obj,'LengthRev',length)
        return Solid(obj)
    def box(self,w,d,h,centered=(True,True,False)):
        assert centered==(True,True,False)
        return self.rect(w,d).extrude(h)

class Native:
    Workplane=Workplane
cq=Native()

def polar(a,r):
    a=dim(a);r=dim(r)
    return (Dim(r.value*math.cos(math.radians(a.value)),f'({r.expr} * cos({a.expr}))'),
            Dim(r.value*math.sin(math.radians(a.value)),f'({r.expr} * sin({a.expr}))'))


def box(w,d,h,x=0,y=0,z=0):
    return cq.Workplane('XY').box(w,d,h,centered=(True,True,False)).translate((x,y,z))

def cyl(r,h,x=0,y=0,z=0):
    return cq.Workplane('XY').circle(r).extrude(h).translate((x,y,z))

def ring(ro,ri,h,x=0,y=0,z=0):
    return cq.Workplane('XY').circle(ro).circle(ri).extrude(h).translate((x,y,z))

def rad(shape,a,r):
    return shape.translate((r,0,0)).rotate((0,0,0),(0,0,1),a)

def island(a,w,d,z,h,r=None):
    return rad(box(d,w,h,z=z),a,r or P['sensor_radius'])

def keybox(a,w,d,h,r,z):
    return rad(box(d,w,h,z=z),a,r)

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
    area=0
    meta={'mode':mode,'diameter':2*ro,'height':round(H,4),'host_base_datum':hz,'accessory_shift':shift,
          'battery_area_mm2':area,'battery_disk_fraction':area/(math.pi*(P['battery_diameter_TARGET']/2)**2),
          'strap_tip_span':64,'key_span':56.8,'host_height_assumed':P['host_height_ASSUMED']}
    return parts,zones,cats,meta

def lra_zone(pad=0,z=0,h=1):
    return box(P['lra_flex_envelope_width_RESERVED']+2*pad,
               P['lra_flex_envelope_length_RESERVED']+2*pad,h,y=-2.4,z=z).rotate(
                   (0,0,0),(0,0,1),P['lra_orientation']).translate((*P['lra_xy'],0))

def cell(h,z,notched):
    # Reference components are loaded from the matching STEP, never exported
    # from this temporary bounding primitive. All PRINT geometry is native.
    return cyl(P['battery_diameter_TARGET']/2,h,z=z)


def dimensions(doc, original):
    sheet=doc.addObject('Spreadsheet::Sheet','Dimensions')
    sheet.Label='Dimensions (mm / degrees) - edit column B'
    sheet.set('A1','Parameter');sheet.set('B1','Value');sheet.set('C1','Status / unit')
    data={};row=2
    flat={}
    for key,val in original.items():
        if isinstance(val,(int,float)):flat[key]=val
        elif isinstance(val,list):
            for i,n in enumerate(val):flat[f'{key}_{i}']=n
    flat['lra_orientation']=math.degrees(math.atan2(original['lra_xy'][1],original['lra_xy'][0]))-90
    for key,val in flat.items():
        sheet.set(f'A{row}',key);sheet.set(f'B{row}',str(val));sheet.setAlias(f'B{row}',key)
        status='degrees' if ('angle' in key or key=='lra_orientation') else 'mm'
        if any(s in key for s in ('ASSUMED','TARGET','RESERV','UNSELECTED')):status+='; UNVERIFIED'
        sheet.set(f'C{row}',status)
        data[key]=Dim(val,'Dimensions.'+key);row+=1
    for key,val in original.items():
        if isinstance(val,list):data[key]=[data[f'{key}_{i}'] for i in range(len(val))]
        elif not isinstance(val,(int,float)):data[key]=val
    # Derived assembly heights follow the original concept's stack equations.
    for key,expr in [('installed_top','battery_zone_bottom + battery_installed_height_RESERVATION'),
                     ('stacked_shift','installed_top + 1.4 - battery_zone_bottom'),
                     ('thin_host_datum','installed_top + 0.3'),
                     ('stacked_host_datum','speaker_cup_top + stacked_shift + 0.3'),
                     ('thin_height','thin_host_datum + host_height_ASSUMED + 2 * host_axial_gap_each + bezel_thickness'),
                     ('stacked_height','stacked_host_datum + host_height_ASSUMED + 2 * host_axial_gap_each + bezel_thickness')]:
        sheet.set(f'A{row}',key);sheet.set(f'B{row}','='+expr);sheet.setAlias(f'B{row}',key)
        sheet.set(f'C{row}','derived; mm');row+=1
    sheet.setColumnWidth('A',340);sheet.setColumnWidth('B',110);sheet.setColumnWidth('C',220)
    return data


def configure_view(doc):
    if not App.GuiUp:
        return
    import FreeCADGui as Gui
    for obj in doc.Objects:
        if obj.ViewObject:
            obj.ViewObject.Visibility = False
    for obj in doc.getObject('PrintableParts').Group:
        obj.ViewObject.Visibility = True
        obj.ViewObject.ShapeColor = (0.72, 0.76, 0.82)
        obj.ViewObject.LineColor = (0.12, 0.15, 0.2)
        obj.ViewObject.DisplayMode = 'Flat Lines'
    doc.getObject('PrintableParts').ViewObject.Visibility = True
    Gui.activeDocument().activeView().viewAxonometric()
    Gui.activeDocument().activeView().fitAll()


def main():
    global DOC,P
    import argparse
    parser=argparse.ArgumentParser()
    parser.add_argument('--concept',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    root=args.concept;out=args.out or root/'freecad';out.mkdir(parents=True,exist_ok=True)
    original=json.loads((root/'source/parameters.json').read_text())
    results=[]
    for mode in ('full_cell_stacked','thin_notched'):
        DOC=App.newDocument('Unhygienix_'+mode)
        P=dimensions(DOC,original)
        parts,_,_,meta=build(mode)
        DOC.recompute()
        printable={n:s.obj for n,s in parts.items() if n.startswith('PRINT_')}
        # Keep only native construction histories that feed actual print parts.
        keep={DOC.getObject('Dimensions')}
        def visit(o):
            if o in keep:return
            keep.add(o)
            for dep in o.OutList:visit(dep)
        for o in printable.values():visit(o)
        for o in reversed(DOC.Objects):
            if o not in keep:DOC.removeObject(o.Name)
        group=DOC.addObject('App::DocumentObjectGroup','PrintableParts');group.Label='PRINTABLE native parts'
        for name,obj in printable.items():
            obj.Label=name;group.addObject(obj)
            obj.addProperty('App::PropertyString','DesignStatus','Documentation')
            obj.DesignStatus='Concept 01; unverified fit; MJF/SLS PA12 prototype'
        refdoc=App.newDocument('OriginalSTEP')
        Import.insert(str(root/'exports'/mode/'assembly.step'),refdoc.Name)
        imported=list(refdoc.Objects)
        originals={o.Label:o for o in imported if o.isDerivedFrom('Part::Feature')}
        report=[]
        for name,obj in printable.items():
            ref=originals[name].Shape
            shape=obj.Shape
            delta=shape.cut(ref).Volume+ref.cut(shape).Volume
            bounds=['XMin','XMax','YMin','YMax','ZMin','ZMax']
            err=max(abs(getattr(shape.BoundBox,k)-getattr(ref.BoundBox,k)) for k in bounds)
            row={'part':name,'valid':shape.isValid(),'solids':len(shape.Solids),
                 'volume_mm3':shape.Volume,'symmetric_difference_mm3':delta,'max_bounds_error_mm':err}
            report.append(row)
            assert row['valid'] and row['solids']==1 and delta<0.001 and err<0.00001,row
        refs=DOC.addObject('App::DocumentObjectGroup','References')
        refs.Label='REFERENCE ONLY - frozen STEP placeholders; not print parts'
        for obj in imported:
            if obj.isDerivedFrom('Part::Feature') and not obj.Label.startswith('PRINT_'):
                copy=DOC.addObject('Part::Feature','Reference')
                copy.Label=obj.Label;copy.Shape=obj.Shape.copy();refs.addObject(copy)
        App.closeDocument(refdoc.Name)
        # Validate spreadsheet editing, recompute and restoration.
        sheet=DOC.getObject('Dimensions');alias=sheet.getCellFromAlias('body_diameter')
        shell=printable['PRINT_upper_housing'];before=shell.Shape.Volume
        sheet.set(alias,str(original['body_diameter']+0.4));DOC.recompute()
        changed=shell.Shape.Volume
        assert abs(changed-before)>0.1,'Body diameter did not drive geometry'
        assert shell.Shape.isValid(),'Edited shell invalid'
        sheet.set(alias,str(original['body_diameter']));DOC.recompute()
        assert abs(shell.Shape.Volume-before)<1e-5,'Restored dimensions differ'
        # Export each part centered on the build plate, without assembly offsets.
        stldir=out/mode/'stl';stldir.mkdir(parents=True,exist_ok=True)
        for name,obj in printable.items():
            shape=obj.Shape.copy();bb=shape.BoundBox
            shape.translate(App.Vector(-(bb.XMin+bb.XMax)/2,-(bb.YMin+bb.YMax)/2,-bb.ZMin))
            mesh=MeshPart.meshFromShape(Shape=shape,LinearDeflection=0.04,AngularDeflection=0.12,Relative=False)
            assert mesh.isSolid(),name+' mesh is not solid'
            mesh.write(str(stldir/(name+'.stl')))
        DOC.recompute()
        configure_view(DOC)
        path=out/('unhygienix-'+mode+'.FCStd');DOC.saveAs(str(path))
        name=DOC.Name;App.closeDocument(name)
        DOC=App.openDocument(str(path));DOC.recompute()
        for objname in [o.Name for o in DOC.Objects if o.Label.startswith('PRINT_')]:
            obj=DOC.getObject(objname)
            assert obj.Shape.isValid() and len(obj.Shape.Solids)==1,obj.Label
        App.closeDocument(DOC.Name)
        results.append({'mode':mode,'parts':report,'native_file':path.name,
                        'spreadsheet_edit_restore':'passed','save_reopen':'passed'})
        print('PASSED',mode,len(report),'parts',flush=True)
    (out/'validation.json').write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__':main()
