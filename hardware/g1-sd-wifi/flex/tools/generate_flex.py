#!/usr/bin/env python3
"""Generate native split-arm CN503 passive flex CAD from the checked interface map.

No automatic release approval: geometry, continuity, rail budget, installed routing
and manufacturing stackup still need prototype qualification.
"""
from __future__ import annotations
import json
import math
from pathlib import Path
import uuid
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'flex'
INTERFACE = json.loads((ROOT / 'design/flex-interface.json').read_text())
NS = uuid.UUID('82798121-af87-4f4f-9d4f-778a031c29ce')

def uid(name): return str(uuid.uuid5(NS, name))
def q(s): return json.dumps(str(s))
def f(v): return f'{v:.6f}'.rstrip('0').rstrip('.') or '0'
def pt(x,y): return f'(xy {f(x)} {f(y)})'
def tr(s,x): return -x if s=='A' else x

def net_name(p): return f"/CN503_{p['cn503_contact']}_{p['signal']}"

def outline():
    # Local X grows away from the connector. Y grows north, toward the tail.
    # At Y24+ the inner edge steps away from the wider connector-end housing.
    return [(-.35,-.7),(6.4,-.7),(6.4,32),(10,40),(10,59.2),(-.5,59.2),(-.5,54.2),(-.7,53.7),(-.7,40),(1.35,32),(1.35,23),(0.4,21.8),(-.35,21.8)]


def schematic(row, pins, landing, tail):
    name=f'KUI-CN503-{row}-Flex-RevA'
    skey=uid(name+'sch')
    def symbol_def(sym,pnumbers):
        z=[f'(symbol "KUI_Flex:{sym}" (pin_names (offset 0.6)) (in_bom no) (on_board yes)',
           '(property "Reference" "J" (at 0 3.81 0) (effects (font (size 1.27 1.27))))',
           f'(property "Value" "{sym}" (at 0 1.27 0) (effects (font (size 1.27 1.27))))',
           f'(symbol "{sym}_0_1" (rectangle (start 0 0) (end 3.81 -50.8) (stroke (width 0.254) (type default)) (fill (type background))))',
           f'(symbol "{sym}_1_1"']
        for i,n in enumerate(pnumbers):
            z.append(f'(pin passive line (at -5.08 {-2.54*(i+1):.2f} 0) (length 5.08) (name "{n}" (effects (font (size 1.0 1.0)))) (number "{n}" (effects (font (size 1.0 1.0)))))')
        return '\n'.join(z)+'))'
    adef=f'CN503_{row}_Landing'; bdef=f'FPC_{row}_20'
    cn=[int(p['cn503_contact'][1:]) for p in pins]
    # Schematic row order follows CN503; FPC numbers follow actual mating
    # position, which is reversed for the mirror-shaped B arm.
    tail_numbers=[p['pin'] for p in pins]
    symbols=[]; wires=[]; labels=[]
    for ref,val,lib,x,fp,pnums in [('J1',f'CN503 row {row} solder contacts',adef,76.2,landing,cn),('J2','20-pin bottom-contact FPC tail',bdef,177.8,tail,tail_numbers)]:
        z=[f'(symbol (lib_id "KUI_Flex:{lib}") (at {x} 63.5 0) (unit 1) (in_bom no) (on_board yes) (dnp no) (uuid "{uid(name+ref)}")',
           f'(property "Reference" "{ref}" (at {x} 60.5 0) (effects (font (size 1.27 1.27))))',
           f'(property "Value" {q(val)} (at {x} 62.5 0) (effects (font (size 1.1 1.1))))',
           f'(property "Footprint" "KUI_Flex:{fp}" (at {x} 63.5 0) (effects (font (size 1.27 1.27)) hide))',
           '(property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))']
        for n in pnums: z.append(f'(pin "{n}" (uuid "{uid(name+ref+str(n))}"))')
        z.append(f'(instances (project "{name}" (path "/{skey}" (reference "{ref}") (unit 1)))) )')
        symbols.append('\n'.join(z))
        for i,p in enumerate(pins):
            yy=63.5+2.54*(i+1)
            wires.append(f'(wire (pts (xy {x-5.08} {f(yy)}) (xy {x-12.7} {f(yy)})) (stroke (width 0) (type default)) (uuid "{uid(name+ref+"wire"+str(i))}"))')
            labels.append(f'(label {q(net_name(p).lstrip('/'))} (at {x-12.7} {f(yy)} 0) (effects (font (size 1 1)) (justify right bottom)) (uuid "{uid(name+ref+"label"+str(i))}"))')
    defs=symbol_def(adef,cn)+symbol_def(bdef,tail_numbers)
    (OUT/(name+'.symbols.txt')).write_text(defs.replace('KUI_Flex:', '')+'\n')
    txt=f'''(kicad_sch (version 20231120) (generator "eeschema") (uuid "{skey}") (paper "A4")
(title_block (title "K-UI CN503 row {row} passive flex") (date "2026-10-10") (rev "A prototype") (comment 1 "Logical mapping only; motherboard continuity and mechanical fit unqualified") (comment 2 "No +12V or audio contacts; two separate flex arms"))
(lib_symbols {defs})
{chr(10).join(wires)}
{chr(10).join(labels)}
{chr(10).join(symbols)}
(sheet_instances (path "/" (page "1")))
)'''
    (OUT/(name+'.kicad_sch')).write_text(txt+'\n')


def pcb(row,pins):
    name=f'KUI-CN503-{row}-Flex-RevA'
    # Board coordinates are mirrored about the physical CN503 solder-row center.
    ox,oy=100.0,100.0
    def xy(x,y): return (ox+tr(row,x),oy-y)
    def pad_xy(x,y): return (tr(row,x),-y)
    defs=['(net 0 "")']+[f'(net {i+1} {q(net_name(p))})' for i,p in enumerate(pins)]
    footprint_dir=OUT/'KUI_Flex.pretty'; footprint_dir.mkdir(exist_ok=True)
    landing=f'CN503_{row}_20_SolderLanding_1.00mm'; tail=f'FPC_{row}_20_Bottom_0.50mm_10.50mm'
    route=[]; allpads=[]; draw=[]; via=[]; lengths=[]
    def seg(i,layer,a,b,width):
        if a==b: return
        ax,ay=xy(*a);bx,by=xy(*b)
        route.append(f'(segment (start {f(ax)} {f(ay)}) (end {f(bx)} {f(by)}) (width {f(width)}) (layer "{layer}") (net {i+1}) (uuid "{uid(name+str(i)+layer+str(a)+str(b))}"))')
    def footprint(ref,fn,pads):
        stem=f'(footprint "KUI_Flex:{fn}" (layer "F.Cu") (at {ox} {oy}) (uuid "{uid(name+ref)}") (attr board_only exclude_from_pos_files exclude_from_bom)\n'
        stem+=f'(property "Reference" "{ref}" (at 0 0) (layer "F.Fab") (effects (font (size .8 .8)) hide))\n'
        val=f'CN503 row {row} solder contacts' if ref=='J1' else '20-pin bottom-contact FPC tail'
        stem+=f'(property "Value" {q(val)} (at 0 0) (layer "F.Fab") (effects (font (size .8 .8)) hide))\n'
        stem+='\n'.join(pads)+'\n)'
        module=stem.replace(f'"KUI_Flex:{fn}"',f'"{fn}"',1).replace(f'(at {ox} {oy})','',1)
        (footprint_dir/(fn+'.kicad_mod')).write_text(module+'\n')
        return stem
    landingpads=[];tailpads=[]
    for i,p in enumerate(pins):
        n=int(p['cn503_contact'][1:]); y=n-1
        x=.70
        nx,ny=pad_xy(x,y)
        for layer in ['F','B']:
            landingpads.append(f'(pad "{n}" smd rect (at {f(nx)} {f(ny)}) (size 1.4 .44) (layers "{layer}.Cu" "{layer}.Mask") (net {i+1} {q(net_name(p))}) (solder_mask_margin .29) (uuid "{uid(name+"land"+layer+str(n))}"))')
        # Via island is in the static solder landing region, not the bend span.
        vx,vy=xy(.9,y)
        via.append(f'(via (at {f(vx)} {f(vy)}) (size .55) (drill .3) (layers "F.Cu" "B.Cu") (net {i+1}) (uuid "{uid(name+"via-land"+str(i))}"))')
        seg(i,'F.Cu',(.70,y),(.9,y),.15)
        seg(i,'B.Cu',(.70,y),(.9,y),.15)
        layer='B.Cu' if i%2==0 else 'F.Cu'
        lane=5.95-(i//2)*.45
        fx=9.5-i*.5
        # 0.30 mm power, 0.25 mm dedicated return, 0.15 mm signal traces.
        w=.28 if p['signal'].startswith('CONSOLE_') else (.25 if p['signal']=='GND' else .15)
        path=[(.9,y),(lane-.25,y),(lane,y+.25),(lane,33),(fx,52),(fx,53.5)]
        for j,(a,b) in enumerate(zip(path,path[1:])): seg(i,layer,a,b,.15 if j==4 else w)
        if layer=='F.Cu':
            vx,vy=xy(fx,53.5)
            via.append(f'(via (at {f(vx)} {f(vy)}) (size .55) (drill .3) (layers "F.Cu" "B.Cu") (net {i+1}) (uuid "{uid(name+"via-tail"+str(i))}"))')
        seg(i,'B.Cu',(fx,53.5),(fx,57.25),.15)
        tx,ty=pad_xy(fx,57.25)
        # Keep the route/net unchanged. Number the mirrored B tail from its
        # physical left edge, as required by the same bottom-contact socket.
        tailpads.append(f'(pad "{p["pin"]}" smd rect (at {f(tx)} {f(ty)}) (size .35 3.5) (layers "B.Cu" "B.Mask") (net {i+1} {q(net_name(p))}) (solder_mask_margin .10) (uuid "{uid(name+"tail"+str(i))}"))')
        lengths.append({'cn503_contact':p['cn503_contact'],'fpc_pin':p['pin'],'net':net_name(p),'main_layer':layer,'width_mm':w,'route_centerline_length_mm':round(sum(math.dist(a,b) for a,b in zip(path,path[1:]))+3.75+.2,3)})
    polys=outline()
    for a,b in zip(polys,polys[1:]+polys[:1]):
        ax,ay=xy(*a);bx,by=xy(*b)
        draw.append(f'(gr_line (start {f(ax)} {f(ay)}) (end {f(bx)} {f(by)}) (stroke (width .05) (type default)) (layer "Edge.Cuts") (uuid "{uid(name+"edge"+str(a))}"))')
    # Common windows deliberately remove sub-minimum coverlay webs.
    for layer in []:
        points=[xy(a,b) for a,b in [(-.1,-.36),(1.5,-.36),(1.5,20.36),(-.1,20.36)]]
        draw.append('(gr_poly (pts '+' '.join(pt(*p) for p in points)+f') (stroke (width 0) (type default)) (fill solid) (layer "{layer}") (uuid "{uid(name+layer+"row-window")}"))')
    for layer,rect in [('User.1',(-.5,54.2,10,59.2))]:
        x1,y1,x2,y2=rect
        pts=[xy(x1,y1),xy(x2,y1),xy(x2,y2),xy(x1,y2)]
        draw.append('(gr_poly (pts '+' '.join(pt(*p) for p in pts)+f') (stroke (width .05) (type default)) (fill {'solid'}) (layer "{layer}") (uuid "{uid(name+layer+"tail-window")}"))')
    for area,rect in [('ROW_COMMON_COVERLAY',(-.12,-.37,1.52,20.37)),('TAIL_COMMON_COVERLAY',(-.26,55.35,9.76,59.05))]:
        x1,y1,x2,y2=rect
        p=[xy(x1,y1),xy(x2,y1),xy(x2,y2),xy(x1,y2)]
        draw.append('(zone (net 0) (net_name "") (layers "F.Cu" "B.Cu") (name '+q(area)+') (hatch edge .5) (keepout (tracks allowed) (vias allowed) (pads allowed) (copperpour allowed) (footprints allowed)) (polygon (pts '+' '.join(pt(*z) for z in p)+')))')
    # No copper, holes or sharp crease permitted in Y24..51.5 span.
    for text,x,y,layer,size in [(f'{row}',3.3,26,'F.SilkS',1),(f'{row}1 SOUTH',3.5,-2,'Dwgs.User',.8),('NORTH / AV',3.5,22,'Dwgs.User',.7),('PI TOP 0.20; FINISHED TAIL 0.30+/-0.05',5,62,'Dwgs.User',.8),('PROTOTYPE: FIT AND CONTINUITY UNQUALIFIED',5,65,'Dwgs.User',.8),('CONTACTS BOTTOM',4.7,53,'Dwgs.User',.8)]:
        xx,yy=xy(x,y)
        draw.append(f'(gr_text {q(text)} (at {f(xx)} {f(yy)} 0) (layer "{layer}") (uuid "{uid(name+text)}") (effects (font (size {size} {size}) (thickness .15))))')
    txt=f'''(kicad_pcb (version 20240108) (generator "pcbnew")
(general (thickness .12)) (paper "A4")
(title_block (title "K-UI CN503 {row} passive flex") (date "2026-10-10") (rev "A prototype") (comment 1 "No +12V/audio; map and installed fit unqualified"))
(layers (0 "F.Cu" signal) (31 "B.Cu" signal) (35 "F.Paste" user) (34 "B.Paste" user) (36 "B.SilkS" user) (37 "F.SilkS" user) (38 "B.Mask" user) (39 "F.Mask" user) (40 "Dwgs.User" user "User.Drawings") (44 "Edge.Cuts" user) (46 "B.CrtYd" user) (47 "F.CrtYd" user) (48 "B.Fab" user) (49 "F.Fab" user) (50 "User.1" user "PI_TOP_0.20"))
(setup (pad_to_mask_clearance 0) (allow_soldermask_bridges_in_footprints yes))
{chr(10).join(defs)}
{footprint('J1',landing,landingpads)}
{footprint('J2',tail,tailpads)}
{chr(10).join(route)}
{chr(10).join(via)}
{chr(10).join(draw)}
)'''
    (OUT/(name+'.kicad_pcb')).write_text(txt+'\n')
    pro={'board':{'design_settings':{'rules':{'min_clearance':.15,'min_copper_edge_clearance':.3,'min_hole_clearance':.20,'min_track_width':.15,'min_via_annular_width':.125,'min_via_diameter':.55,'min_through_hole_diameter':.30}}},'net_settings':{'classes':[{'name':'Default','clearance':.15,'track_width':.15,'via_diameter':.55,'via_drill':.3,'wire_width':6,'bus_width':12,'line_style':0,'microvia_diameter':.45,'microvia_drill':.2,'diff_pair_width':.15,'diff_pair_gap':.15,'diff_pair_via_gap':.15,'pcb_color':'rgba(0, 0, 0, 0.000)','schematic_color':'rgba(0, 0, 0, 0.000)'}],'meta':{'version':3}},'meta':{'filename':name+'.kicad_pro','version':1}}
    (OUT/(name+'.kicad_dru')).write_text('''(version 1)\n# JLC requires0.30mm copper-edge clearance generally but allows0.20mm\n# for exposed mating gold fingers. This is only the J2 matingtail footprint.\n(rule "Goldfinger boardedge clearance"\n (condition "A.memberOfFootprint('J2')")\n (constraint edge_clearance (min 0.20mm)))\n''')
    (OUT/(name+'.kicad_pro')).write_text(json.dumps(pro,indent=2)+'\n')
    (OUT/(name+'-routing.json')).write_text(json.dumps(lengths,indent=2)+'\n')
    schematic(row,pins,landing,tail)

if __name__=='__main__':
    for c in INTERFACE['connectors']:
        pcb(c['flex_row'],c['pins'])
    (OUT/'KUI_Flex.kicad_sym').write_text('(kicad_symbol_lib (version 20231120) (generator \"kicad_symbol_editor\")\n'+''.join((OUT/(f'KUI-CN503-{row}-Flex-RevA.symbols.txt')).read_text() for row in ['A','B'])+')\n')
    for row in ['A','B']: (OUT/(f'KUI-CN503-{row}-Flex-RevA.symbols.txt')).unlink()
    (OUT/'sym-lib-table').write_text('(sym_lib_table\n (lib (name \"KUI_Flex\") (type \"KiCad\") (uri \"${KIPRJMOD}/KUI_Flex.kicad_sym\") (options \"\") (descr \"CN503 passive split flex symbols\"))\n)\n')
    (OUT/'fp-lib-table').write_text('(fp_lib_table\n (lib (name "KUI_Flex") (type "KiCad") (uri "${KIPRJMOD}/KUI_Flex.pretty") (options "") (descr "CN503 split-arm flex contacts and mating fingers"))\n)\n')
    print('Generated A/B native PCB, schematic, footprint and routing files. Release is not approved.')
