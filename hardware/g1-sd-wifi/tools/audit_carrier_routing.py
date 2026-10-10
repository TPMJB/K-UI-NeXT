#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Report native routing evidence without treating connectivity as sign-off.

Use system /usr/bin/python3, which provides KiCad's pcbnew module. Distances
between component lands are geometric lower bounds, not routed path lengths.
Power/clock return loops and Kelvin branches still require layout review.
"""
import collections
import hashlib
import importlib.util
import json
import math
import pathlib

import pcbnew

ROOT=pathlib.Path(__file__).resolve().parents[1]
BOARD=ROOT/'controller/KUI-G1-Bridge-RevA.kicad_pcb'
OUT=ROOT/'controller/checks/carrier-routing-audit.json'
COMPONENTS=ROOT/'design/integrated-components.json'
NETCLASS_CONTRACT=ROOT/'design/carrier-netclass-contract.json'
PROBE_PLACEMENT=ROOT/'design/raw-g1-probe-placement.json'
FABRICATED_MPNS={
    'CUSTOM_C5_SERVICE_PADS','CUSTOM_SWD_SERVICE_PADS','CUSTOM_USB_SERVICE_PADS',
    'PCB solder pads; no fitted connector','PCB test point','PCB_SOLDER_BRIDGE','PCB_TEST_PAD',
}
ADDITIONAL_POWER_NETS={'CONSOLE_3V3','FLEX_5V','USB_SERVICE_VBUS','USB_VBUS_SENSE'}
PAIR_LIST=[
    ('RP core pin10 decap','U201','10','C2006','1'),
    ('RP core pin32 decap','U201','32','C2010','1'),
    ('RP core pin51 decap','U201','51','C2013','1'),
    ('RP VREG input','U201','64','C2001','1'),
    ('RP VREG analog filter','U201','61','C2004','1'),
    ('RP VREG output','L201','2','C2002','1'),
    ('RP Kelvin feedback','U201','65','C2002','1'),
    ('Logic buck input','U21','2','C206','1'),
    ('Logic input ground','U21','9','C206','2'),
    ('Logic buck output','L20','2','C207','1'),
    ('Logic Kelvin feedback','U21','6','C207','1'),
    ('Storage buck input','U22','2','C209','1'),
    ('Storage input ground','U22','9','C209','2'),
    ('Storage buck output','L21','2','C210','1'),
    ('Storage Kelvin feedback','U22','6','C210','1'),
    ('RP crystal input','U201','30','X201','1'),
    ('RP crystal damping','U201','31','R2002','1'),
    ('RP crystal output','R2002','2','X201','3'),
    ('QMI clock source','U201','71','R2003','1'),
    ('QMI PSRAM branch','R2003','2','U203','6'),
    ('QMI boot flash branch','R2003','2','U202','6'),
]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def uncapped_category_counts(board_sha256,total):
    """Partition native opens using independent freshly loaded board copies.

    Removing other categories' net assignments in memory keeps the native
    connectivity calculation exact and avoids KiCad DRC's 499-entry cap.
    The original PCB and its saved filled-zone copper are never written.
    """
    contract=json.loads(NETCLASS_CONTRACT.read_text())
    power_names=set(contract['power_net_names'])|ADDITIONAL_POWER_NETS
    categories={
        'GND':lambda name:name=='GND',
        'power':lambda name:name in power_names and name!='GND',
        'other_signal':lambda name:bool(name) and name!='GND' and name not in power_names,
    }
    counts={};actual_names={}
    for category,keep in categories.items():
        if sha(BOARD)!=board_sha256:
            raise RuntimeError('Carrier changed during audit; rerun after placement/routing freeze')
        fresh=pcbnew.LoadBoard(str(BOARD))
        items=[p for footprint in fresh.GetFootprints() for p in footprint.Pads()]
        items+=list(fresh.GetTracks())+list(fresh.Zones())
        names=set()
        for item in items:
            name=item.GetNetname()
            if keep(name):
                names.add(name)
            else:
                item.SetNetCode(0)
        fresh.BuildConnectivity()
        connectivity=fresh.GetConnectivity();connectivity.RecalculateRatsnest()
        counts[category]=connectivity.GetUnconnectedCount(False)
        actual_names[category]=sorted(names)
    if sha(BOARD)!=board_sha256:
        raise RuntimeError('Carrier changed during audit; rerun after placement/routing freeze')
    if sum(counts.values())!=total:
        raise RuntimeError('Native opens category sum differs from full-board native connectivity count')
    return {
        'counts':counts,'sum':sum(counts.values()),'matches_full_board_total':True,
        'counts_are_uncapped':True,'board_sha256':board_sha256,
        'method':'Independent fresh LoadBoard for each category; assign netcode0 to all pads/tracks/vias/zones outside that category; BuildConnectivity/RecalculateRatsnest/GetUnconnectedCount(False)',
        'power_net_classification':sorted(power_names),
        'category_net_names_present':actual_names,
        'netclass_contract_sha256':sha(NETCLASS_CONTRACT),
    }

def footprint_fitment_report(footprints):
    """Separate actual copper footprints from BASE populated purchased SMT."""
    components={c['ref']:c for c in json.loads(COMPONENTS.read_text())}
    fabricated={ref:fp for ref,fp in footprints.items()
                if components.get(ref,{}).get('mpn') in FABRICATED_MPNS}
    fitted={ref:fp for ref,fp in footprints.items()
            if ref in components and components[ref].get('populate',True)
            and components[ref].get('on_board',True)
            and components[ref].get('in_bom',True)
            and components[ref].get('mpn') not in FABRICATED_MPNS
            and components[ref].get('variant','BASE') in {'BASE','ALL','BASE_ONLY'}}
    return {
        'physical_footprint_layer_counts':dict(sorted(collections.Counter(
            'F.Cu' if fp.GetLayer()==pcbnew.F_Cu else 'B.Cu' if fp.GetLayer()==pcbnew.B_Cu
            else str(fp.GetLayer()) for fp in footprints.values()).items())),
        'fitted_smt_components':len(fitted),
        'all_fitted_smt_components_on_top':all(fp.GetLayer()==pcbnew.F_Cu for fp in fitted.values()),
        'fitted_smt_bottom_refs':sorted(ref for ref,fp in fitted.items() if fp.GetLayer()==pcbnew.B_Cu),
        'fitted_smt_refs':sorted(fitted),
        'fitment_scope':'BASE population declared by integrated component contract; fabricated copper and DNP/offboard parts excluded',
        'fitment_contract_sha256':sha(COMPONENTS),
        'physical_refs_missing_component_contract':sorted(set(footprints)-set(components)),
        'fabricated_copper_footprints':len(fabricated),
        'fabricated_copper_bottom_refs':sorted(ref for ref,fp in fabricated.items() if fp.GetLayer()==pcbnew.B_Cu),
        'bare_raw_G1_probe_refs':sorted(ref for ref in fabricated
                                      if ref.startswith('TP') and 100<=int(ref[2:])<=138),
    }

def main():
    board_sha256=sha(BOARD)
    board=pcbnew.LoadBoard(str(BOARD));board.BuildConnectivity()
    spec=importlib.util.spec_from_file_location('carrier_generator',ROOT/'tools/generate_board.py')
    generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
    c5_group=generator.validate_c5_contact_group(board)
    connectivity=board.GetConnectivity();connectivity.RecalculateRatsnest()
    unconnected=connectivity.GetUnconnectedCount(False)
    category_counts=uncapped_category_counts(board_sha256,unconnected)
    footprints={f.GetReference():f for f in board.GetFootprints()}
    fitment=footprint_fitment_report(footprints)
    tracks=list(board.GetTracks());by_net=collections.defaultdict(list)
    vias=[]
    for t in tracks:
        by_net[t.GetNetname()].append(t)
        if isinstance(t,pcbnew.PCB_VIA):vias.append(t)
    pads={(r,p.GetNumber()):p for r,f in footprints.items() for p in f.Pads() if p.GetNumber()}
    pairs=[]
    for label,a,pa,b,pb in PAIR_LIST:
        aa=pads[(a,pa)];bb=pads[(b,pb)]
        distance=math.hypot(pcbnew.ToMM(aa.GetPosition().x-bb.GetPosition().x),pcbnew.ToMM(aa.GetPosition().y-bb.GetPosition().y))
        pairs.append({'purpose':label,'from':a+'.'+pa,'to':b+'.'+pb,
                      'net':aa.GetNetname(),'endpoint_distance_lower_bound_mm':round(distance,4),
                      'same_net':aa.GetNetCode()==bb.GetNetCode(),'route_topology_qualified':False})
    nets={}
    for n,items in by_net.items():
        wires=[t for t in items if not isinstance(t,pcbnew.PCB_VIA)]
        nets[n]={'total_trace_length_mm':round(sum(pcbnew.ToMM(t.GetLength()) for t in wires),4),
                 'trace_widths_mm':sorted(set(round(pcbnew.ToMM(t.GetWidth()),5) for t in wires)),
                 'copper_layers':sorted(set(board.GetLayerName(t.GetLayer()) for t in wires)),
                 'segments':len(wires),'vias':len(items)-len(wires)}
    grounds=[]
    through_ground=[p for p in pads.values() if p.GetNetname()=='GND' and p.GetDrillSize().x>0]
    through_ground.extend(v for v in vias if v.GetNetname()=='GND')
    for r in ['C2001','C2002','C2003','C2004','C206','C207','C208','C209','C210','C211']:
        pad=pads[(r,'2')]
        nearest=min((math.hypot(pcbnew.ToMM(pad.GetPosition().x-v.GetPosition().x),pcbnew.ToMM(pad.GetPosition().y-v.GetPosition().y)) for v in through_ground),default=None)
        grounds.append({'ref':r,'nearest_GND_through_copper_distance_mm':round(nearest,4) if nearest is not None else None,
                        'return_loop_qualified':False})
    ds=board.GetDesignSettings();aux=ds.GetAuxOrigin()
    report={'board_sha256':board_sha256,
            'status':'routing_present_qualification_pending' if len(tracks)>6 else 'critical_switch_paths_only_general_routing_pending',
            'routing_qualified':False,'signal_integrity_qualified':False,'power_layout_qualified':False,
            'native_connectivity_unconnected_count':unconnected,
            'native_connectivity_count_is_uncapped':True,
            'native_connectivity_unconnected_category_breakdown':category_counts,
            'kicad_drc_unconnected_report_limit':499,
            'kicad_drc_unconnected_report_at_limit_status':'capped_at_499',
            'drc_count_interpretation':'A DRC report showing 499 means capped_at_499; use uncapped native connectivity for progress.',
            'physical_footprints':len(footprints),'tracks_and_vias':len(tracks),'through_vias':len(vias),
            'C5_rigid_contact_group':c5_group,
            'all_physical_components_on_top':all(f.GetLayer()==pcbnew.F_Cu for f in footprints.values()),
            **fitment,
            'tracks_on_reference_layer_In1_Cu':sum(not isinstance(t,pcbnew.PCB_VIA) and t.GetLayer()==pcbnew.In1_Cu for t in tracks),
            'aux_origin_mm':[pcbnew.ToMM(aux.x),pcbnew.ToMM(aux.y)],
            'via_geometry_mm':sorted(set((round(pcbnew.ToMM(v.GetWidth(pcbnew.F_Cu)),5),round(pcbnew.ToMM(v.GetDrill()),5)) for v in vias)),
            'local_endpoint_distances':pairs,'capacitor_ground_via_proximity':grounds,
            'routed_nets':nets,
            'remaining_reviews':['Kelvin feedback branching at the output capacitor positive land',
                                 'Input/switch/output current loops and capacitor/EP ground returns',
                                 'Clock/QMI/SD stubs, return reference and timing',
                                 'Full native DRC including unconnected items and schematic parity',
                                 'Motherboard obstacles, shield registration and C5 compression fixture']}
    if PROBE_PLACEMENT.exists():
        report['raw_G1_probe_placement_evidence']={
            'source':str(PROBE_PLACEMENT.relative_to(ROOT)),
            'source_sha256':sha(PROBE_PLACEMENT),
            'evidence':json.loads(PROBE_PLACEMENT.read_text()),
            'qualification':'Placement/route evidence only; probe loading and signal integrity remain unqualified',
        }
    if sha(BOARD)!=board_sha256:
        raise RuntimeError('Carrier changed during audit; rerun after placement/routing freeze')
    OUT.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['status','native_connectivity_unconnected_count','tracks_and_vias','through_vias','tracks_on_reference_layer_In1_Cu','all_physical_components_on_top','all_fitted_smt_components_on_top','fitted_smt_components','board_sha256']}))

if __name__=='__main__':main()
