#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Snapshot actual native carrier placement, without changing its copper."""
import csv,hashlib,importlib.util,json,math,pathlib
import pcbnew
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('carrier_generator',ROOT/'tools/generate_board.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
board=pcbnew.LoadBoard(str(g.BOARD));data={c['ref']:c for c in g.components(ROOT/'design/integrated-components.json')}
g.sync_native_metadata(board,ROOT/'design/integrated-components.json',ROOT/'controller/checks/netlist.xml')
records=[]
for fp in sorted(board.GetFootprints(),key=lambda f:f.GetReference()):
 ref=fp.GetReference();c=data[ref];box=g.physical_box(fp)
 records.append({'ref':ref,'value':fp.GetValue(),'block':c['block'],'populate':not fp.IsDNP(),
 'in_bom':not fp.IsExcludedFromBOM(),'origin_xy_mm':[round(pcbnew.ToMM(fp.GetPosition().x)-g.ORIGIN[0],5),round(pcbnew.ToMM(fp.GetPosition().y)-g.ORIGIN[1],5)],
 'physical_bbox_mm':[round(box[0]-g.ORIGIN[0],5),round(box[1]-g.ORIGIN[1],5),round(box[2]-g.ORIGIN[0],5),round(box[3]-g.ORIGIN[1],5)],
 'rotation_deg':fp.GetOrientationDegrees(),'side':'top' if fp.GetLayer()==pcbnew.F_Cu else 'bottom',
 'footprint':c['footprint'],'native_hierarchy_path':str(fp.GetPath().AsString()),'height_max_mm':c.get('height_max_mm'),
 'assembly_centroid_offset_mm':c.get('assembly_centroid_offset_mm',[0,0]),
 'fabricated_probe_only':c.get('mpn')=='PCB_TEST_PAD'})
height=[]
for r in records:
 if r['side']=='bottom' and r['fabricated_probe_only']:continue
 h=r['height_max_mm'];available=8 if r['physical_bbox_mm'][3]<=20 else 6
 if h is None:height.append({'ref':r['ref'],'status':'component_height_unqualified','populate':r['populate']})
 elif r['populate'] and g.MOUNT_UNDERSIDE+g.PCB_THICKNESS+h>available:
  height.append({'ref':r['ref'],'status':'stack_exceeds_nominal_clearance','assembled_top_mm':g.MOUNT_UNDERSIDE+g.PCB_THICKNESS+h,'available_mm':available})
report={'board_sha256':hashlib.sha256(g.BOARD.read_bytes()).hexdigest(),'status':'native_placement_local_routes_present_global_routing_pending',
 'physical_components':len(data),'placed':len(records),'failed':[],
 'mechanical_fit_qualified':False,'routing_qualified':False,'power_layout_qualified':False,
 'provisional_geometry':{'outline_mm':[g.WIDTH,g.HEIGHT],'bios_cutout_mm':g.CUTOUT,'cn503_open_notch_mm':g.NOTCH,'north_south_registration':'unqualified'},
 'height_assumption':{'carrier_underside_above_motherboard_mm':g.MOUNT_UNDERSIDE,'pcb_thickness_mm':g.PCB_THICKNESS,'upper_region_height_mm':8,'lower_region_height_mm':6},
 'height_findings':height,'parts':records,'reviewed_power_sense_routes':json.loads((ROOT/'design/carrier-critical-routes.json').read_text()),
 'bare_bottom_probe_access':'Bench access with carrier removed; underside insulation required after installation; no fitted bottom component'}
(ROOT/'design/carrier-placement.json').write_text(json.dumps(report,indent=2)+'\n')
with (ROOT/'design/carrier-placement-draft.csv').open('w',newline='') as stream:
 writer=csv.writer(stream,lineterminator='\n');writer.writerow(['Designator','Mid X','Mid Y','Rotation','Layer','Populate','Qualified'])
 for r in records:
  fp=next(fp for fp in board.GetFootprints() if fp.GetReference()==r['ref'])
  if not r['populate'] or fp.IsExcludedFromPosFiles():continue
  dx,dy=r['assembly_centroid_offset_mm'];angle=math.radians(r['rotation_deg']);x,y=r['origin_xy_mm']
  writer.writerow([r['ref'],f'{x+dx*math.cos(angle)+dy*math.sin(angle):.5f}mm',f'{y-dx*math.sin(angle)+dy*math.cos(angle):.5f}mm',r['rotation_deg'],r['side'],'yes','NO'])
print(json.dumps({'snapshot_parts':len(records),'board_sha256':report['board_sha256'],'height_findings':len(height)}))
