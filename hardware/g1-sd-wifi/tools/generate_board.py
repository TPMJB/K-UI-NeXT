#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create/re-route a reviewable carrier draft from the integrated pin contracts.

Placement is deterministic and constrained to the provisional owner-derived
outline. It is a routing starting point, not a qualified physical fit or a
high-speed/power layout sign-off. No DRC exclusions are generated.
Run with /usr/bin/python3 (KiCad's pcbnew module).
"""
import argparse
import collections
import csv
import json
import math
import pathlib
import re
import subprocess
import xml.etree.ElementTree as ET

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAD = ROOT / 'controller'
DESIGN = ROOT / 'design'
NAME = 'KUI-G1-Bridge-RevA'
BOARD = CAD / (NAME + '.kicad_pcb')
ORIGIN = (70.0, 45.0)
WIDTH, HEIGHT = 59.0, 105.0
CUTOUT = (6.0, 51.0, 35.0, 70.0)
NOTCH = (43.8, 42.0, 59.0, 89.0)
C5_ORIGIN = (14.0, 10.0)
C5_BOX = (3.5733, 1.0973, 24.5537, 18.9027)
EDGE_MARGIN = 0.30
PART_GAP = 0.15
TRACK = 0.15
CLEARANCE = 0.15
VIA = 0.50
DRILL = 0.20
MOUNT_UNDERSIDE = 2.5
PCB_THICKNESS = 0.8
LAYERS = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
ANCHORS = {
    'U10': (16.0, 34.0, 0), 'U201': (39.0, 34.0, 0),
    'U202': (35.0, 21.5, 0), 'U203': (36.5, 26.5, 0),
    'J201': (47.0, 10.0, 0), 'U206': (14.0, 10.0, 0),
    'U35': (36.0, 45.0, 90), 'U36': (52.0, 32.0, 0),
    'J50': (39.4, 54.8, 90), 'J51': (39.4, 78.0, 90),
    'U40': (17.0, 91.0, 0), 'J20': (54.0, 101.0, 0),
    'U20': (50.0, 100.0, 0), 'U21': (48.0, 92.0, 0),
    'U22': (38.0, 100.0, 0), 'L20': (52.5, 92.0, 180),
    'L21': (42.0, 100.0, 180), 'L201': (45.0, 27.0, 180),
    'C2001': (40.5, 27.0, 90), 'C2002': (46.5, 24.5, 0),
    'C2003': (46.0, 30.0, 90), 'C2004': (42.5, 26.5, 90),
    'R2001': (41.5, 24.5, 0),
    'X201': (39.5, 42.0, 270), 'X10': (7.0, 23.0, 0),
    'R2002': (42.5, 41.0, 0), 'C2020': (36.0, 41.0, 90),
    'C2021': (42.5, 43.0, 90),
    'R2003': (39.0, 27.0, 90),
    'C206': (44.5, 92.0, 180), 'C207': (56.0, 92.0, 90),
    'C208': (53.0, 96.0, 0), 'C209': (34.5, 100.0, 180),
    'C210': (45.0, 100.0, 90), 'C211': (42.0, 104.0, 0),
}

def mm(v): return pcbnew.FromMM(v)
def p(x, y): return pcbnew.VECTOR2I(mm(ORIGIN[0]+x), mm(ORIGIN[1]+y))
def rect_intersects(a,b,gap=0):
    return not (a[2]+gap <= b[0] or b[2]+gap <= a[0] or a[3]+gap <= b[1] or b[3]+gap <= a[1])
def inflate(a,g): return (a[0]-g,a[1]-g,a[2]+g,a[3]+g)
def moved(a,x,y): return (a[0]+x,a[1]+y,a[2]+x,a[3]+y)
def center(a): return ((a[0]+a[2])/2,(a[1]+a[3])/2)

def physical_box(fp):
    """Physical pads and body/courtyard lines, excluding labels and fields."""
    objects=list(fp.Pads())
    allowed={pcbnew.F_CrtYd,pcbnew.B_CrtYd,pcbnew.F_Fab,pcbnew.B_Fab,pcbnew.F_SilkS,pcbnew.B_SilkS}
    objects += [g for g in fp.GraphicalItems() if g.GetLayer() in allowed and not isinstance(g, pcbnew.PCB_TEXT)]
    boxes=[g.GetBoundingBox() for g in objects]
    if not boxes: raise ValueError('Footprint has no physical geometry: '+fp.GetReference())
    return (min(pcbnew.ToMM(b.GetX()) for b in boxes),min(pcbnew.ToMM(b.GetY()) for b in boxes),
            max(pcbnew.ToMM(b.GetRight()) for b in boxes),max(pcbnew.ToMM(b.GetBottom()) for b in boxes))

class Occupancy:
    def __init__(self): self.cells=collections.defaultdict(set); self.boxes={}
    def cellkeys(self,b):
        return [(x,y) for x in range(math.floor(b[0]),math.ceil(b[2])+1) for y in range(math.floor(b[1]),math.ceil(b[3])+1)]
    def add(self,ref,b):
        self.boxes[ref]=b
        for key in self.cellkeys(inflate(b,PART_GAP)): self.cells[key].add(ref)
    def collisions(self,b,ignore=()):
        refs=set()
        for key in self.cellkeys(inflate(b,PART_GAP)): refs.update(self.cells.get(key,()))
        return [r for r in refs if r not in ignore and rect_intersects(b,self.boxes[r],PART_GAP)]

def allowed(b):
    if b[0]<EDGE_MARGIN or b[1]<EDGE_MARGIN or b[2]>WIDTH-EDGE_MARGIN or b[3]>HEIGHT-EDGE_MARGIN: return False
    return not any(rect_intersects(b,inflate(r,EDGE_MARGIN)) for r in (CUTOUT,NOTCH))

def read_netlist(path):
    tree=ET.parse(path).getroot(); comps={}; nets={}
    for c in tree.findall('./components/comp'):
        stamp=c.findtext('tstamps','').strip().split()[0]
        sheet=c.find('sheetpath')
        prefix=sheet.attrib.get('tstamps','/') if sheet is not None else '/'
        fields={f.attrib['name']:f.text or '' for f in c.findall('./fields/field') if f.attrib['name'] not in ('Footprint','Value','Reference')}
        comps[c.attrib['ref']]={'path': prefix.rstrip('/')+'/'+stamp,'value':c.findtext('value',''),'footprint':c.findtext('footprint',''),'fields':fields}
    for n in tree.findall('./nets/net'):
        for node in n.findall('node'): nets[(node.attrib['ref'],node.attrib['pin'])]=n.attrib['name']
    return comps,nets

def footprint(c):
    nick,name=c['footprint'].split(':',1)
    library=CAD/(nick+'.pretty') if nick.startswith('KUI') else pathlib.Path('/usr/share/kicad/footprints')/(nick+'.pretty')
    fp=pcbnew.FootprintLoad(str(library),name)
    if fp is None: raise ValueError('Cannot load '+c['footprint'])
    fp.SetFPID(pcbnew.LIB_ID(nick,name)); fp.SetReference(c['ref']); fp.SetValue(c['value'])
    fp.SetDNP(not c.get('populate',True))
    fp.SetExcludedFromBOM(not c.get('in_bom',True))
    # Explanatory labels belong to the assembly drawing; dense parts otherwise
    # mask these labels without affecting electrical geometry.
    for g in fp.GraphicalItems():
        if isinstance(g,pcbnew.PCB_TEXT) and g.GetLayer()==pcbnew.F_SilkS:g.SetLayer(pcbnew.F_Fab)
    fp.Value().SetVisible(False); fp.Reference().SetVisible(False)
    return fp

def pad_centers(fp):
    return {pad.GetNumber(): (pcbnew.ToMM(pad.GetPosition().x)-ORIGIN[0],pcbnew.ToMM(pad.GetPosition().y)-ORIGIN[1]) for pad in fp.Pads() if pad.GetNumber()}

def add_line(board,a,b,layer=pcbnew.Edge_Cuts,width=.1):
    s=pcbnew.PCB_SHAPE(board);s.SetShape(pcbnew.SHAPE_T_SEGMENT);s.SetStart(p(*a));s.SetEnd(p(*b));s.SetWidth(mm(width));s.SetLayer(layer);board.Add(s)

def add_text(board,text,x,y,layer=pcbnew.Dwgs_User,size=1.0):
    t=pcbnew.PCB_TEXT(board);t.SetText(text);t.SetPosition(p(x,y));t.SetTextSize(pcbnew.VECTOR2I(mm(size),mm(size)));t.SetTextThickness(mm(.15));t.SetLayer(layer);board.Add(t)

def outline(board):
    pts=[(0,0),(WIDTH,0),(WIDTH,NOTCH[1]),(NOTCH[0],NOTCH[1]),(NOTCH[0],NOTCH[3]),(WIDTH,NOTCH[3]),(WIDTH,HEIGHT),(0,HEIGHT)]
    for a,b in zip(pts,pts[1:]+pts[:1]): add_line(board,a,b)
    x0,y0,x1,y1=CUTOUT;pts=[(x0,y0),(x1,y0),(x1,y1),(x0,y1)]
    for a,b in zip(pts,pts[1:]+pts[:1]):add_line(board,a,b)
    add_text(board,'K-UI G1 Rev A — CIRCUIT / ROUTING DRAFT',WIDTH/2,-4)
    add_text(board,'59 x 105 mm provisional envelope; mechanical fit NOT QUALIFIED',WIDTH/2,-2)
    add_text(board,'BIOS cutout — measured west-east long axis',20,60)
    add_text(board,'CN503 intact housing / shield aperture',51,65)
    add_text(board,'C5 removable module / compression fixture pending',14,10)
    x0,y0,x1,y1=C5_BOX
    for a,b in zip([(x0,y0),(x1,y0),(x1,y1),(x0,y1)],[(x1,y0),(x1,y1),(x0,y1),(x0,y0)]):add_line(board,a,b,pcbnew.Dwgs_User,.15)
    add_line(board,(0,HEIGHT+7),(50,HEIGHT+7),pcbnew.Dwgs_User,.25)
    for x in range(0,51,10):add_line(board,(x,HEIGHT+6),(x,HEIGHT+8),pcbnew.Dwgs_User,.25)
    add_text(board,'50 mm print calibration',25,HEIGHT+10)


def ground_plane(board,net):
    existing={z.GetZoneName():z for z in board.Zones()}
    zone=existing.get('GND reference plane')
    new_zone=zone is None
    if new_zone:zone=pcbnew.ZONE(board)
    zone.UnFill();zone.SetLayer(pcbnew.In1_Cu);zone.SetNet(net);zone.SetZoneName('GND reference plane')
    zone.SetLocalClearance(mm(CLEARANCE));zone.SetMinThickness(mm(.15));zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    pts=[(0,0),(WIDTH,0),(WIDTH,NOTCH[1]),(NOTCH[0],NOTCH[1]),(NOTCH[0],NOTCH[3]),(WIDTH,NOTCH[3]),(WIDTH,HEIGHT),(0,HEIGHT)]
    poly=zone.Outline();poly.RemoveAllContours();poly.NewOutline()
    for x,y in pts:poly.Append(mm(ORIGIN[0]+x),mm(ORIGIN[1]+y))
    poly.NewHole();x0,y0,x1,y1=CUTOUT
    for x,y in [(x0,y0),(x0,y1),(x1,y1),(x1,y0)]:poly.Append(mm(ORIGIN[0]+x),mm(ORIGIN[1]+y),0,0)
    if new_zone:board.Add(zone)
    # Preserve a continuous return plane: vias may pass through In1, but signal
    # traces must use the other three copper layers. Native DRC enforces this.
    rule=existing.get('In1 reference plane — no routed traces')
    new_rule=rule is None
    if new_rule:rule=pcbnew.ZONE(board)
    rule.SetLayer(pcbnew.In1_Cu);rule.SetIsRuleArea(True)
    rule.SetZoneName('In1 reference plane — no routed traces')
    rule.SetDoNotAllowTracks(True);rule.SetDoNotAllowVias(False)
    rule.SetDoNotAllowPads(False);rule.SetDoNotAllowZoneFills(False);rule.SetDoNotAllowFootprints(False)
    poly=rule.Outline();poly.RemoveAllContours();poly.NewOutline()
    for x,y in pts:poly.Append(mm(ORIGIN[0]+x),mm(ORIGIN[1]+y))
    poly.NewHole()
    for x,y in [(x0,y0),(x0,y1),(x1,y1),(x1,y0)]:poly.Append(mm(ORIGIN[0]+x),mm(ORIGIN[1]+y),0,0)
    if new_rule:board.Add(rule)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def edge_routing_obstacles(board):
    """Export physical edge clearance as explicit router obstacles.

    Native DSN's default track clearance alone does not carry KiCad's copper
    edge constraint. Rectangular rule areas protect every outline segment,
    including the BIOS aperture and CN503 notch, on all four copper layers.
    """
    prefix='Routing edge clearance / '
    existing={z.GetZoneName():z for z in board.Zones()}
    layers=pcbnew.LSET()
    for layer in LAYERS:layers.AddLayer(layer)
    for index,edge in enumerate(g for g in board.GetDrawings() if g.GetLayer()==pcbnew.Edge_Cuts):
        a=edge.GetStart();c=edge.GetEnd()
        x0,x1=sorted([pcbnew.ToMM(a.x),pcbnew.ToMM(c.x)])
        y0,y1=sorted([pcbnew.ToMM(a.y),pcbnew.ToMM(c.y)])
        if x0!=x1 and y0!=y1:raise ValueError('Edge obstacle generator requires orthogonal outline')
        name=prefix+str(index);zone=existing.get(name);is_new=zone is None
        if is_new:zone=pcbnew.ZONE(board)
        zone.SetLayerSet(layers);zone.SetIsRuleArea(True)
        zone.SetZoneName(name);zone.SetDoNotAllowTracks(True)
        zone.SetDoNotAllowVias(True);zone.SetDoNotAllowPads(True)
        zone.SetDoNotAllowZoneFills(True);zone.SetDoNotAllowFootprints(False)
        poly=zone.Outline();poly.RemoveAllContours();poly.NewOutline()
        for x,y in [(x0-EDGE_MARGIN,y0-EDGE_MARGIN),(x1+EDGE_MARGIN,y0-EDGE_MARGIN),
                    (x1+EDGE_MARGIN,y1+EDGE_MARGIN),(x0-EDGE_MARGIN,y1+EDGE_MARGIN)]:
            poly.Append(mm(x),mm(y))
        if is_new:board.Add(zone)


def critical_switch_routes(board,fps):
    """Protect short switch-node paths before the general routing pass.

    A 0.15 mm neck leaves fine-pitch IC lands, then immediately widens to
    0.40 mm. These three local necks are explicitly reported for review.
    """
    records=[]
    for ic,pin,ind,escape in [('U201','63','L201',(0,-.65)),
                              ('U21','7','L20',(.60,0)),
                              ('U22','7','L21',(.60,0))]:
        source=next(pad for pad in fps[ic].Pads() if pad.GetNumber()==pin)
        target=next(pad for pad in fps[ind].Pads() if pad.GetNumber()=='1')
        a=(pcbnew.ToMM(source.GetPosition().x)-ORIGIN[0],pcbnew.ToMM(source.GetPosition().y)-ORIGIN[1])
        b=(a[0]+escape[0],a[1]+escape[1])
        c=(pcbnew.ToMM(target.GetPosition().x)-ORIGIN[0],pcbnew.ToMM(target.GetPosition().y)-ORIGIN[1])
        for start,end,width in [(a,b,.15),(b,c,.40)]:
            track=pcbnew.PCB_TRACK(board);track.SetStart(p(*start));track.SetEnd(p(*end));track.SetWidth(mm(width));track.SetLayer(pcbnew.F_Cu)
            track.SetNet(source.GetNet());track.SetLocked(True);board.Add(track)
        records.append({'net':source.GetNetname(),'from':ic+'.'+pin,'to':ind+'.1',
                        'polyline_xy_mm':[a,b,c], 'routed_length_mm':round(math.dist(a,b)+math.dist(b,c),4),
                        'neck_width_mm':.15,'neck_length_mm':round(math.dist(a,b),4),'main_width_mm':.40,
                        'electrical_layout_qualified':False})
    return records


def reviewed_local_routes(board,fps,path):
    """Install geometry-checked local power/sense paths without guessing pins."""
    source=json.loads(path.read_text())
    for ref,rotation in source['rotate_input_capacitors_deg'].items():
        fps[ref].SetOrientationDegrees(rotation)
    nets={n.GetNetname():n for n in board.GetNetInfo().NetsByNetcode().values()}
    result=[]
    for record in source['tracks']:
        if record['type'] in ['ground-via','power-via']:
            via=pcbnew.PCB_VIA(board);via.SetPosition(p(*record['xy_mm']))
            via.SetWidth(mm(record['diameter_mm']));via.SetDrill(mm(record['drill_mm']))
            via.SetViaType(pcbnew.VIATYPE_THROUGH);via.SetLayerPair(pcbnew.F_Cu,pcbnew.B_Cu)
            via.SetNet(nets[record.get('net','GND')]);via.SetLocked(True);board.Add(via)
        else:
            if 'from' in record:
                ref,pin=record['from'].rsplit('.',1)
                pad=next(q for q in fps[ref].Pads() if q.GetNumber()==pin)
                start=pad_centers(fps[ref])[pin]
                if math.dist(start,record['xy_mm'][0])>.001 or pad.GetNetname()!=record['net']:
                    raise ValueError('Reviewed local-route anchor no longer matches '+record['from'])
            if '.' in record.get('to',''):
                ref,pin=record['to'].rsplit('.',1)
                end=pad_centers(fps[ref])[pin]
                if math.dist(end,record['xy_mm'][-1])>.001:
                    raise ValueError('Reviewed local-route endpoint no longer matches '+record['to'])
            for a,b in zip(record['xy_mm'],record['xy_mm'][1:]):
                t=pcbnew.PCB_TRACK(board);t.SetStart(p(*a));t.SetEnd(p(*b));t.SetWidth(mm(record['width_mm']))
                t.SetLayer(board.GetLayerID(record.get('layer','F.Cu')));t.SetNet(nets[record['net']]);t.SetLocked(True);board.Add(t)
        result.append(record)
    return result


def components(path):
    return [c for c in json.loads(path.read_text()) if c.get('on_board',True) and c.get('footprint') and not c.get('off_board',False)]

def place(args):
    data=components(args.components); byref={c['ref']:c for c in data}
    net_comps,padnets=read_netlist(args.netlist)
    missing=set(byref)-set(net_comps)
    if missing: raise ValueError('Physical parts missing in native netlist: '+str(sorted(missing)))
    board=pcbnew.BOARD();board.SetFileName(str(BOARD));board.SetCopperLayerCount(4)
    enabled=pcbnew.LSET.AllNonCuMask()
    for layer in LAYERS: enabled.AddLayer(layer)
    board.SetEnabledLayers(enabled)
    ds=board.GetDesignSettings();ds.SetBoardThickness(mm(PCB_THICKNESS));ds.m_MinClearance=mm(CLEARANCE)
    ds.SetAuxOrigin(p(0,HEIGHT));board.GetPlotOptions().SetUseAuxOrigin(True)
    ds.m_TrackMinWidth=mm(TRACK);ds.m_ViasMinSize=mm(VIA);ds.m_MinThroughDrill=mm(0.20);ds.m_CopperEdgeClearance=mm(EDGE_MARGIN)
    nc=ds.m_NetSettings.GetDefaultNetclass();nc.SetTrackWidth(mm(TRACK));nc.SetClearance(mm(CLEARANCE));nc.SetViaDiameter(mm(VIA));nc.SetViaDrill(mm(DRILL))
    power_nc=pcbnew.NETCLASS('POWER');power_nc.SetTrackWidth(mm(.40));power_nc.SetClearance(mm(CLEARANCE));power_nc.SetViaDiameter(mm(.60));power_nc.SetViaDrill(mm(.30))
    ds.m_NetSettings.SetNetclass('POWER',power_nc)
    nets={}
    power_nets=[]
    for name in sorted(set(padnets.values())):
        n=pcbnew.NETINFO_ITEM(board,name);board.Add(n);nets[name]=n
        if name.startswith('+') or name in ('CONSOLE_5V','MCU_VREG_LX','RESERVE_5V',
                                            'FUSED_5V','PRIMARY_5V','SW_LOGIC','SW_STORAGE','MCU_VREG_AVDD'):
            n.SetNetClass(power_nc);assigned=pcbnew.STRINGSET();assigned.insert('POWER');ds.m_NetSettings.AppendNetclassLabelAssignment(name,assigned);power_nets.append(name)
    fps={c['ref']:footprint(c) for c in data}
    dimensions={r:physical_box(fp) for r,fp in fps.items()}
    placed={};occupied=Occupancy();occupied.add('C5_MODULE_BODY',C5_BOX)
    records=[]
    # Supplies alone do not identify which local capacitor serves an IC.
    netrefs=collections.defaultdict(list)
    for c in data:
        for pin in c['pins'].values():
            n=pin.get('net')
            if n:netrefs[n].append(c['ref'])
    owners={};last={}
    for c in data:
        block=c['block']
        if c['ref'].startswith(('U','J','X')):last[block]=c['ref']
        mentioned=re.findall(r'\b[UJLX]\d+\b',c.get('description',''))
        owners[c['ref']]=next((r for r in mentioned if r!=c['ref'] and r in byref),last.get(block))
    def target(c):
        ref=c['ref']
        if ref in ANCHORS:return ANCHORS[ref][:2]
        owner=owners.get(ref)
        if owner in placed:
            pinmatch=re.search(r'\bpin\s*(\d+)',c.get('description',''))
            explicit={'C2001':('U201','64'),'C2002':('U201','65'),'C2003':('U201','65'),'C2004':('U201','61')}
            o,pin=explicit.get(ref,(owner,pinmatch.group(1) if pinmatch else None))
            if o in placed and pin:
                at=pad_centers(fps[o]).get(pin)
                if at:return at
            return placed[owner][:2]
        neighbors=[]
        for pi in c['pins'].values():
            n=pi.get('net')
            if not n or n=='GND' or n.startswith('+') or len(netrefs[n])>12:continue
            for nr in netrefs[n]:
                if nr in placed and nr!=ref:neighbors.append(placed[nr][:2])
        if neighbors:return (sum(x for x,y in neighbors)/len(neighbors),sum(y for x,y in neighbors)/len(neighbors))
        return {'frontend':(16,34),'controller':(39,34),'power-bus':(29,80),'bios':(17,91),'cn503-interface':(39,64)}.get(c['block'],(29,80))
    priority=[]
    for c in data:
        b=dimensions[c['ref']];area=(b[2]-b[0])*(b[3]-b[1])
        bypass=bool(re.search(r'bypass|decoupl|buck .*cap|VREG|Native buck',c.get('description',''),re.I))
        critical=(c['ref'].startswith('C') and c['block']=='frontend') or c['ref'] in {'C2001','C2002','C2003','C2004','C2005','C2006','C2007','C2008','C2009','C2010','C2011','C2012','C2013','C2014','C2015','C2016','C2017','C2018','C2019','R2001','C206','C207','C208','C209','C210','C211'}
        tier=0 if 'placement_offset_mm' in c else 1 if c['ref'] in ANCHORS else 1.5 if critical else 2 if c['ref'].startswith(('U','J','L','X','F')) else 2.5 if bypass else 3
        priority.append((tier,-area,c['ref'],c))
    priority.sort(key=lambda e:e[:3])
    failures=[]
    for _,_,ref,c in priority:
        fp=fps[ref];tx,ty=target(c)
        if 'placement_offset_mm' in c:
            dx,dy=c['placement_offset_mm'];angle=c.get('placement_rotation_deg',0)
            fp.SetOrientationDegrees(angle);fp.SetPosition(p(C5_ORIGIN[0]+dx,C5_ORIGIN[1]+dy))
            actual=physical_box(fp);b=(actual[0]-ORIGIN[0],actual[1]-ORIGIN[1],actual[2]-ORIGIN[0],actual[3]-ORIGIN[1])
            if not allowed(b) or occupied.collisions(b,ignore=('C5_MODULE_BODY',)):
                failures.append({'ref':ref,'reason':'C5 rigid-contact geometry collides or crosses outline','bbox_mm':b})
            point=(C5_ORIGIN[0]+dx,C5_ORIGIN[1]+dy);placed[ref]=(*point,angle);occupied.add(ref,b)
        else:
            base_angle=ANCHORS.get(ref,(0,0,0))[2];angles=[base_angle] if ref in ANCHORS else [0,90]
            candidates=[]
            for angle in angles:
                fp.SetPosition(pcbnew.VECTOR2I(0,0));fp.SetOrientationDegrees(angle)
                box=physical_box(fp);cx,cy=center(box);box=moved(box,-cx,-cy)
                # All remaining part centers lie on a deterministic half-mm grid.
                for iy in range(1,int(HEIGHT*2)):
                    y=iy/2
                    if c.get('height_max_mm',0)>2.4 and y+box[3]>20:continue
                    for ix in range(1,int(WIDTH*2)):
                        x=ix/2;b=moved(box,x,y)
                        if not allowed(b):continue
                        score=(x-tx)**2+(y-ty)**2
                        candidates.append((score,angle,x,y,box,cx,cy))
            candidates.sort(key=lambda a:a[:4]);choice=None
            for candidate in candidates:
                _,angle,x,y,box,cx,cy=candidate;b=moved(box,x,y)
                if not occupied.collisions(b):choice=(angle,x,y,cx,cy,b);break
            if choice is None:
                failures.append({'ref':ref,'reason':'No nonoverlapping physical placement in provisional outline'})
                continue
            angle,x,y,cx,cy,b=choice;fp.SetOrientationDegrees(angle);fp.SetPosition(p(x-cx,y-cy));placed[ref]=(x,y,angle);occupied.add(ref,b)
        board.Add(fp);fp.SetPath(pcbnew.KIID_PATH(net_comps[ref]['path']))
        fp.SetValue(net_comps[ref]['value'])
        for key,value in net_comps[ref]['fields'].items():
            fp.SetField(key,value);fp.GetField(key).SetVisible(False)
        for pad in fp.Pads():
            n=padnets.get((ref,pad.GetNumber()))
            if n:pad.SetNet(nets[n])
        fp.Reference().SetVisible(False);fp.Value().SetVisible(False)
        actual=physical_box(fp);b=occupied.boxes[ref]
        records.append({'ref':ref,'value':c['value'],'block':c['block'],'populate':c.get('populate',True),
                        'origin_xy_mm':[round(pcbnew.ToMM(fp.GetPosition().x)-ORIGIN[0],5),round(pcbnew.ToMM(fp.GetPosition().y)-ORIGIN[1],5)],
                        'physical_bbox_mm':list(b),'rotation_deg':placed[ref][2],'footprint':c['footprint'],
                        'native_hierarchy_path':net_comps[ref]['path'],'height_max_mm':c.get('height_max_mm'),
                        'assembly_centroid_offset_mm':c.get('assembly_centroid_offset_mm',[0,0])})
    outline(board);edge_routing_obstacles(board);local_routes=critical_switch_routes(board,fps)
    reviewed_routes=reviewed_local_routes(board,fps,DESIGN/'carrier-critical-routes.json')
    board.BuildConnectivity();ground_plane(board,nets['GND']);pcbnew.SaveBoard(str(BOARD),board)
    (DESIGN/'carrier-netclass-contract.json').write_text(json.dumps({'signal_track_width_mm':TRACK,'signal_clearance_mm':CLEARANCE,'preferred_power_track_width_mm':.40,'power_net_names':power_nets,'narrow_pad_escape_review_required':True,'ground_plane_layer':'In1.Cu'},indent=2)+'\n')
    report={'status':'placement_complete_routing_pending' if not failures else 'placement_incomplete','physical_components':len(data),'placed':len(records),
            'failed':failures,'mechanical_fit_qualified':False,'routing_qualified':False,
            'provisional_geometry':{'outline_mm':[WIDTH,HEIGHT],'bios_cutout_mm':CUTOUT,'cn503_open_notch_mm':NOTCH,'north_south_registration':'unqualified'},
            'height_assumption':{'carrier_underside_above_motherboard_mm':MOUNT_UNDERSIDE,'pcb_thickness_mm':PCB_THICKNESS,'upper_region_height_mm':8,'lower_region_height_mm':6},
            'critical_local_routes':local_routes,'reviewed_power_sense_routes':reviewed_routes,'parts':records}
    height=[]
    for r in records:
        h=r['height_max_mm'];available=8 if r['physical_bbox_mm'][3]<=20 else 6
        if h is None:height.append({'ref':r['ref'],'status':'component_height_unqualified','populate':r['populate']})
        elif r['populate'] and MOUNT_UNDERSIDE+PCB_THICKNESS+h>available:
            height.append({'ref':r['ref'],'status':'stack_exceeds_nominal_clearance','assembled_top_mm':MOUNT_UNDERSIDE+PCB_THICKNESS+h,'available_mm':available})
    report['height_findings']=height
    (DESIGN/'carrier-placement.json').write_text(json.dumps(report,indent=2)+'\n')
    # Draft part centroids remain separate from manufacturing release inputs.
    with (DESIGN/'carrier-placement-draft.csv').open('w',newline='') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(['Designator','Mid X','Mid Y','Rotation','Layer','Populate','Qualified'])
        for r in records:
            if not r['populate']:continue
            dx,dy=r['assembly_centroid_offset_mm'];angle=math.radians(r['rotation_deg']);x,y=r['origin_xy_mm']
            ax=x+dx*math.cos(angle)+dy*math.sin(angle);ay=y-dx*math.sin(angle)+dy*math.cos(angle)
            w.writerow([r['ref'],f'{ax:.5f}mm',f'{ay:.5f}mm',r['rotation_deg'],'top','yes','NO'])
    if not pcbnew.ExportSpecctraDSN(board,str(args.dsn)):raise ValueError('Native DSN export failed')
    print(json.dumps({'placed':len(records),'physical_parts':len(data),'failures':failures,'board':str(BOARD),'dsn':str(args.dsn)}))
    if failures:raise SystemExit(1)



# All16 official C5 body-centroid positions are explicit: a prefix selection
# must never silently repack recovery contacts or the second castellated row.
C5_NATIVE_CONTACT_POSITIONS = {
    'K201':(21.62,2.672,270),'K202':(19.08,2.672,270),
    'K203':(16.54,2.672,270),'K204':(14.00,2.672,270),
    'K205':(11.46,2.672,270),'K206':(8.92,2.672,270),
    'K207':(6.38,2.672,270),'K208':(6.38,17.328,90),
    'K209':(8.92,17.328,90),'K210':(11.46,17.328,90),
    'K211':(14.00,17.328,90),'K212':(16.54,17.328,90),
    'K213':(19.08,17.328,90),'K214':(21.62,17.328,90),
    'K215':(15.0255,8.2044,90),'K216':(20.1055,8.2044,90),
}

def validate_c5_contact_group(board):
    """Check the complete rigid group against explicit origins and tip lands."""
    fps={fp.GetReference():fp for fp in board.GetFootprints()}
    source=json.loads((DESIGN/'sources/xiao-c5-spring-contact-placement.json').read_text())
    official={c['ref']:c for c in source['contacts']}
    if set(official)!=set(C5_NATIVE_CONTACT_POSITIONS):
        raise ValueError('Official C5 spring contract must contain the exact16 reviewed refs')
    result=[]
    for ref,(x,y,rotation) in C5_NATIVE_CONTACT_POSITIONS.items():
        if ref not in fps:raise ValueError('Missing rigid C5 contact '+ref)
        fp=fps[ref];actual=(pcbnew.ToMM(fp.GetPosition().x)-ORIGIN[0],pcbnew.ToMM(fp.GetPosition().y)-ORIGIN[1])
        angle=fp.GetOrientationDegrees();error=abs((angle-rotation+180)%360-180)
        c=official[ref];dx,dy=c['body_centroid_offset_mm']
        if math.dist((x,y),(C5_ORIGIN[0]+dx,C5_ORIGIN[1]+dy))>.00001:
            raise ValueError('Explicit C5 position differs from official underside geometry at '+ref)
        if math.dist(actual,(x,y))>.00001 or error>.00001:
            raise ValueError('C5 rigid contact displaced: '+ref+' actual='+str((*actual,angle))+' expected='+str((x,y,rotation)))
        rad=math.radians(angle);lx,ly=source['contact_tip_local_mm']
        tip=(actual[0]+lx*math.cos(rad)+ly*math.sin(rad),actual[1]-lx*math.sin(rad)+ly*math.cos(rad))
        tx,ty=c['tip_offset_mm'];expected=(C5_ORIGIN[0]+tx,C5_ORIGIN[1]+ty)
        if math.dist(tip,expected)>.00001:raise ValueError('C5 spring tip misses official module land at '+ref)
        result.append({'ref':ref,'origin_xy_mm':actual,'rotation_deg':angle,'tip_xy_mm':tip,'tip_error_mm':math.dist(tip,expected)})
    return {'checked_contacts':len(result),'all_origins_orientations_and_tips_match':True,'tolerance_mm':.00001,'positions':result}


def sync_native_metadata(board,components_file,netlist_file):
    """Refresh sourcing/attributes in place and refuse circuit/footprint changes."""
    validate_c5_contact_group(board)
    data={c['ref']:c for c in components(components_file)}
    net_comps,padnets=read_netlist(netlist_file)
    fps={f.GetReference():f for f in board.GetFootprints()}
    if set(fps)!=set(data):raise ValueError('Physical component set changed; metadata sync cannot change placement')
    for ref,fp in fps.items():
        native=net_comps[ref]
        ident=fp.GetFPID();name=str(ident.GetLibNickname())+':'+str(ident.GetLibItemName())
        if name!=native['footprint']:
            raise ValueError('Footprint changed at '+ref+'; routed geometry requires review')
        for pad in fp.Pads():
            n=padnets.get((ref,pad.GetNumber()))
            if n is not None and pad.GetNetname()!=n:
                raise ValueError('Net changed at '+ref+'.'+pad.GetNumber()+'; routed circuit requires review')
        fp.SetPath(pcbnew.KIID_PATH(native['path']));fp.SetValue(native['value'])
        fp.SetDNP(not data[ref].get('populate',True));fp.SetExcludedFromBOM(not data[ref].get('in_bom',True))
        for key,value in native['fields'].items():
            fp.SetField(key,value);fp.GetField(key).SetVisible(False)
    return len(fps)


def route(args):
    b=pcbnew.LoadBoard(str(BOARD))
    if not pcbnew.ImportSpecctraSES(b,str(args.ses)):raise ValueError('Native session import failed')
    count=sync_native_metadata(b,args.components,args.netlist)
    b.BuildConnectivity();pcbnew.ZONE_FILLER(b).Fill(b.Zones());pcbnew.SaveBoard(str(BOARD),b)
    print(json.dumps({'native_board':str(BOARD),'tracks_and_vias':len(list(b.GetTracks())),'metadata_synced':count,'routing_qualified':False}))


def sync_fields(args):
    b=pcbnew.LoadBoard(str(BOARD));before=len(list(b.GetTracks()))
    count=sync_native_metadata(b,args.components,args.netlist);pcbnew.SaveBoard(str(BOARD),b)
    print(json.dumps({'native_board':str(BOARD),'metadata_synced':count,'tracks_and_vias_preserved':before}))


def refresh_unrouted_nets(args):
    """Apply reviewed connector pin changes without disturbing placement.

    Copper already present on any affected net makes this operation unsafe;
    those changes require an explicit reroute rather than relabelled tracks.
    """
    b=pcbnew.LoadBoard(str(BOARD));_,padnets=read_netlist(args.netlist)
    allowed_refs=set(args.allow_ref);changes=[]
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            new_name=padnets.get((fp.GetReference(),pad.GetNumber()))
            if new_name is None or new_name==pad.GetNetname():continue
            if fp.GetReference() not in allowed_refs:
                raise ValueError('Unapproved pad-net change at '+fp.GetReference()+'.'+pad.GetNumber())
            changes.append((pad,pad.GetNetname(),new_name,fp.GetReference()))
    affected={name for _,old,new,_ in changes for name in (old,new)}
    routed=[t for t in b.GetTracks() if t.GetNetname() in affected]
    if routed:
        raise ValueError('Affected connector nets already contain copper; explicitly reroute them first')
    nets={n.GetNetname():n for n in b.GetNetInfo().NetsByNetcode().values()}
    for _,_,name,_ in changes:
        if name not in nets:
            net=pcbnew.NETINFO_ITEM(b,name);b.Add(net);nets[name]=net
    report=[]
    for pad,old,new,ref in changes:
        pad.SetNet(nets[new]);report.append({'ref':ref,'pin':pad.GetNumber(),'old_net':old,'new_net':new})
    count=sync_native_metadata(b,args.components,args.netlist)
    edge_routing_obstacles(b)
    b.BuildConnectivity();pcbnew.ZONE_FILLER(b).Fill(b.Zones());pcbnew.SaveBoard(str(BOARD),b)
    if not pcbnew.ExportSpecctraDSN(b,str(args.dsn)):raise ValueError('Native DSN export failed')
    print(json.dumps({'native_board':str(BOARD),'metadata_synced':count,'pad_net_changes':report,
                      'tracks_and_vias_preserved':len(list(b.GetTracks())),'dsn':str(args.dsn)}))


def export_dsn(args):
    b=pcbnew.LoadBoard(str(BOARD));sync_native_metadata(b,args.components,args.netlist)
    edge_routing_obstacles(b);ground_plane(b,b.FindNet('GND'));pcbnew.SaveBoard(str(BOARD),b)
    if not pcbnew.ExportSpecctraDSN(b,str(args.dsn)):raise ValueError('Native DSN export failed')
    # The In1 "no routed traces" rule area exports as a board-wide
    # wire_keepout. Freerouting 1.9 treats it as an obstacle for vias too
    # (every via has an In1 annulus), so no via could ever be placed and all
    # routing collapsed onto F.Cu. Headless19Ripup already deactivates In1 for
    # traces, and native KiCad DRC still enforces the rule after import.
    content=args.dsn.read_text();start=content.find('(wire_keepout "" (polygon In1.Cu')
    if start<0:raise ValueError('In1 wire_keepout not found; check the DSN exporter output')
    depth=0
    for end in range(start,len(content)):
        depth+=content[end]=='(';depth-=content[end]==')'
        if depth==0:break
    content=content[:start]+content[end+1:]
    if 'wire_keepout' in content:raise ValueError('Unexpected extra wire_keepout in DSN')
    args.dsn.write_text(content)
    if args.signal_starter:
        # Escape-width trial only. The native POWER netclass remains 0.40 mm;
        # any imported power copper needs explicit widening/current review.
        content=args.dsn.read_text()
        content,count=re.subn(r'(\(class POWER\b[\s\S]*?\(rule\s*\(width )400(\))',
                              r'\g<1>150\2',content,count=1)
        if count!=1:raise ValueError('POWER routing rule not found exactly once')
        args.signal_starter.write_text(content)
    print(json.dumps({'native_dsn':str(args.dsn),'signal_starter_dsn':str(args.signal_starter) if args.signal_starter else None,
                      'power_widths_qualified':False}))


def install_local_routes(args):
    b=pcbnew.LoadBoard(str(BOARD));sync_native_metadata(b,args.components,args.netlist)
    tracks=list(b.GetTracks())
    if len(tracks)!=6 or any(not t.IsLocked() for t in tracks):
        raise ValueError('Installing local starter routes requires only the six original locked SW segments; preserve routed copper')
    fps={f.GetReference():f for f in b.GetFootprints()}
    records=reviewed_local_routes(b,fps,args.source)
    edge_routing_obstacles(b);b.BuildConnectivity();pcbnew.ZONE_FILLER(b).Fill(b.Zones());pcbnew.SaveBoard(str(BOARD),b)
    report_path=DESIGN/'carrier-placement.json'
    report=json.loads(report_path.read_text());report['reviewed_power_sense_routes']=records
    for row in report['parts']:
        row['rotation_deg']=fps[row['ref']].GetOrientationDegrees()
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'native_board':str(BOARD),'installed_local_records':len(records),
                      'tracks_and_vias':len(list(b.GetTracks())),'complete_layout_qualified':False}))


def main():
    ap=argparse.ArgumentParser();sub=ap.add_subparsers(dest='mode',required=True)
    q=sub.add_parser('place');q.add_argument('--components',type=pathlib.Path,default=DESIGN/'integrated-components.json')
    q.add_argument('--netlist',type=pathlib.Path,default=CAD/'checks/netlist.xml');q.add_argument('--dsn',type=pathlib.Path,default=DESIGN/'carrier-draft.dsn')
    q=sub.add_parser('import-session');q.add_argument('ses',type=pathlib.Path)
    q.add_argument('--components',type=pathlib.Path,default=DESIGN/'integrated-components.json')
    q.add_argument('--netlist',type=pathlib.Path,default=CAD/'checks/netlist.xml')
    q=sub.add_parser('sync-fields');q.add_argument('--components',type=pathlib.Path,default=DESIGN/'integrated-components.json')
    q.add_argument('--netlist',type=pathlib.Path,default=CAD/'checks/netlist.xml')
    q=sub.add_parser('refresh-unrouted-nets');q.add_argument('--allow-ref',action='append',required=True)
    q.add_argument('--components',type=pathlib.Path,default=DESIGN/'integrated-components.json')
    q.add_argument('--netlist',type=pathlib.Path,default=CAD/'checks/netlist.xml')
    q.add_argument('--dsn',type=pathlib.Path,default=DESIGN/'carrier-draft.dsn')
    q=sub.add_parser('export-dsn');q.add_argument('--components',type=pathlib.Path,default=DESIGN/'integrated-components.json')
    q.add_argument('--netlist',type=pathlib.Path,default=CAD/'checks/netlist.xml')
    q.add_argument('--dsn',type=pathlib.Path,default=DESIGN/'carrier-draft.dsn')
    q.add_argument('--signal-starter',type=pathlib.Path)
    q=sub.add_parser('install-local-routes');q.add_argument('--components',type=pathlib.Path,default=DESIGN/'integrated-components.json')
    q.add_argument('--netlist',type=pathlib.Path,default=CAD/'checks/netlist.xml')
    q.add_argument('--source',type=pathlib.Path,default=DESIGN/'carrier-critical-routes.json')
    args=ap.parse_args();{'place':place,'import-session':route,'sync-fields':sync_fields,
                         'refresh-unrouted-nets':refresh_unrouted_nets,'export-dsn':export_dsn,
                         'install-local-routes':install_local_routes}[args.mode](args)
if __name__=='__main__':main()
