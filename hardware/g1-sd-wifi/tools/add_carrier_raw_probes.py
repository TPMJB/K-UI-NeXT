#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Add native bare bottom probe lands and short endpoint taps, preserving PCB.

The carrier must be removed for bench access. No bottom assembled component,
header or paste aperture is introduced. Native DRC remains mandatory.
"""
import argparse, hashlib, importlib.util, json, math, pathlib
import pcbnew
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ground_fanout',ROOT/'tools/add_carrier_ground_fanout.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f);g=f.g
CLEAR=.151
ORDER=1

def add_probes(board):
    data={c['ref']:c for c in g.components(ROOT/'design/integrated-components.json')}
    native,padnets=g.read_netlist(ROOT/'controller/checks/netlist.xml')
    fps={q.GetReference():q for q in board.GetFootprints()}
    new=sorted(set(data)-set(fps),key=lambda r:int(r[2:]) if r.startswith('TP') else -1)
    if any(not (r.startswith('TP') and 100<=int(r[2:])<=138) for r in new):raise ValueError('Only reviewed raw-probe additions are accepted')
    if len(new)!=39:raise ValueError('Expected exactly39 new raw probe refs, got '+str(len(new)))
    nets={q.GetNetname():q for q in board.GetNetInfo().NetsByNetcode().values()}
    pads=[q for fp in fps.values() for q in fp.Pads() if any(q.IsOnLayer(l) for l in g.LAYERS)]
    records=[];failed=[]
    def check(a,v,net):
        # Bottom land, top via, and top short branch use different envelopes.
        if not g.allowed((v[0]-.5,v[1]-.5,v[0]+.5,v[1]+.5)):return False
        for q in pads:
            if q.GetNetname()==net:continue
            b=f.box(q)
            distance=max(0,math.dist(v,f.point(q.GetPosition()))-pcbnew.ToMM(q.GetSize().x)/2) if q.GetShape()==pcbnew.PAD_SHAPE_CIRCLE else f.point_rect(v,b)
            if q.IsOnLayer(pcbnew.B_Cu) and distance<.5+CLEAR:return False
            if distance<.25+CLEAR:return False
            if q.IsOnLayer(pcbnew.F_Cu) and f.line_rect(a,v,b)<.075+CLEAR:return False
        for t in board.GetTracks():
            if t.GetNetname()==net:continue
            if isinstance(t,pcbnew.PCB_VIA):
                pos=f.point(t.GetPosition());w=pcbnew.ToMM(t.GetWidth(pcbnew.F_Cu))
                if math.dist(v,pos)<.5+w/2+CLEAR:return False
                if f.point_line(pos,a,v)<w/2+.075+CLEAR:return False
            else:
                b,c=f.point(t.GetStart()),f.point(t.GetEnd());w=pcbnew.ToMM(t.GetWidth())
                if t.GetLayer()==pcbnew.B_Cu and f.point_line(v,b,c)<.5+w/2+CLEAR:return False
                if f.point_line(v,b,c)<.25+w/2+CLEAR:return False
                if t.GetLayer()==pcbnew.F_Cu and f.line_line(a,v,b,c)<.075+w/2+CLEAR:return False
        for q in pads:
            if q.GetDrillSize().x:
                if math.dist(v,f.point(q.GetPosition()))<.1+pcbnew.ToMM(q.GetDrillSize().x)/2+.251:return False
        for t in board.GetTracks():
            if isinstance(t,pcbnew.PCB_VIA) and math.dist(v,f.point(t.GetPosition()))<.1+pcbnew.ToMM(t.GetDrill())/2+.251:return False
        return True
    # A three-column stagger prevents bare1mm bottom lands crowding .5pitch pins.
    endpoints=[]
    for ref in new:
        c=data[ref];meta=c['probe_access'];src=next(q for q in fps[meta['endpoint_reference']].Pads() if q.GetNumber()==meta['endpoint_pin'])
        endpoints.append((ref,c,src))
    endpoints.sort(key=lambda z:(z[1]['probe_access']['endpoint_reference'],ORDER*f.point(z[2].GetPosition())[1],z[0]))
    bankindex={}
    for ref,c,src in endpoints:
        a=f.point(src.GetPosition());net=src.GetNetname();owner=c['probe_access']['endpoint_reference'];candidates=[]
        if owner in ['U35','U36']:
            box=g.physical_box(fps[owner]);cx=(box[0]+box[2])/2-g.ORIGIN[0]
            direction=1 if a[0]<cx else -1
            index=bankindex.get(owner,0);bankindex[owner]=index+1
            col=index%3;candidates.append((a[0]+direction*[1.0,1.995,2.99][col],a[1]+ORDER*[0,.1,.2][col]))
        for radius in [1.0,1.2,1.4,1.6,1.8,2.0,2.2,2.4,2.6,2.8,3.0]:
            for angle in range(0,360,15):
                r=math.radians(angle);candidates.append((a[0]+radius*math.cos(r),a[1]+radius*math.sin(r)))
        candidate=next((v for v in candidates if math.dist(a,v)<=3.00001 and check(a,v,net)),None)
        if candidate is None:
            fine=[]
            for step in range(18,61):
                radius=step*.05
                for angle in range(0,360,5):
                    rad=math.radians(angle);fine.append((a[0]+radius*math.cos(rad),a[1]+radius*math.sin(rad)))
            candidate=next((v for v in fine if check(a,v,net)),None)
        if candidate is None:failed.append(ref);continue
        fp=g.footprint(c);board.Add(fp);fp.SetPosition(g.p(*candidate));fp.Flip(fp.GetPosition(),False)
        fp.SetPath(pcbnew.KIID_PATH(native[ref]['path']));fp.SetValue(native[ref]['value'])
        for key,value in native[ref]['fields'].items():fp.SetField(key,value);fp.GetField(key).SetVisible(False)
        for q in fp.Pads():q.SetNet(nets[padnets[(ref,q.GetNumber())]])
        fps[ref]=fp;pads.extend(fp.Pads())
        via=pcbnew.PCB_VIA(board);via.SetPosition(g.p(*candidate));via.SetWidth(g.mm(.5));via.SetDrill(g.mm(.2));via.SetViaType(pcbnew.VIATYPE_THROUGH);via.SetLayerPair(pcbnew.F_Cu,pcbnew.B_Cu);via.SetNet(nets[net]);via.SetLocked(True);board.Add(via)
        t=pcbnew.PCB_TRACK(board);t.SetStart(src.GetPosition());t.SetEnd(g.p(*candidate));t.SetWidth(g.mm(.15));t.SetLayer(pcbnew.F_Cu);t.SetNet(nets[net]);t.SetLocked(True);board.Add(t)
        records.append({'ref':ref,'net':net,'endpoint':owner+'.'+src.GetNumber(),'xy_mm':candidate,'side':'B.Cu bare copper','added_stub_length_mm':math.dist(a,candidate),'maximum_added_stub_mm':3.0,'native_hierarchy_path':native[ref]['path'],'bench_access':'carrier removed; insulate underside after installation','route_loading_qualified':False})
    if failed:raise ValueError('No compliant short probe placement for '+','.join(failed))
    g.sync_native_metadata(board,ROOT/'design/integrated-components.json',ROOT/'controller/checks/netlist.xml')
    return records

def main():
    global ORDER
    ap=argparse.ArgumentParser();ap.add_argument('--reverse-bank-order',action='store_true');ap.add_argument('input',type=pathlib.Path);ap.add_argument('output',type=pathlib.Path);ap.add_argument('--report',type=pathlib.Path,default=ROOT/'design/raw-g1-probe-placement.json');a=ap.parse_args()
    ORDER=-1 if a.reverse_bank_order else 1
    board=pcbnew.LoadBoard(str(a.input));records=add_probes(board)
    board.BuildConnectivity();pcbnew.ZONE_FILLER(board).Fill(board.Zones());pcbnew.SaveBoard(str(a.output),board)
    report={'board_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'fitted_bottom_components':0,'probe_loading_qualified':False,'pads':records}
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'probes':len(records),'output':str(a.output),'max_added_stub_mm':max(q['added_stub_length_mm'] for q in records)}))
if __name__=='__main__':main()
