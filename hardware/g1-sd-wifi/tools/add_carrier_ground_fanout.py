#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Add short ground stubs/vias to the real In1 reference plane.

Conservative copper-envelope checks choose candidates; native refill and DRC
remain mandatory. This does not qualify current loops or mechanical fit.
"""
import argparse, hashlib, importlib.util, json, math, pathlib
import pcbnew
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('carrier_generator',ROOT/'tools/generate_board.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
CLEAR=.151
VIA_DIAMETER=.50
VIA_DRILL=.20
TRACE_WIDTH=.15

def point(v):return (pcbnew.ToMM(v.x)-g.ORIGIN[0],pcbnew.ToMM(v.y)-g.ORIGIN[1])
def box(pad):
    b=pad.GetBoundingBox()
    return (pcbnew.ToMM(b.GetX())-g.ORIGIN[0],pcbnew.ToMM(b.GetY())-g.ORIGIN[1],
            pcbnew.ToMM(b.GetRight())-g.ORIGIN[0],pcbnew.ToMM(b.GetBottom())-g.ORIGIN[1])
def point_rect(a,b):
    return math.hypot(max(b[0]-a[0],0,a[0]-b[2]),max(b[1]-a[1],0,a[1]-b[3]))
def point_line(a,b,c):
    dx,dy=c[0]-b[0],c[1]-b[1];den=dx*dx+dy*dy
    t=max(0,min(1,((a[0]-b[0])*dx+(a[1]-b[1])*dy)/den)) if den else 0
    return math.dist(a,(b[0]+t*dx,b[1]+t*dy))
def line_rect(a,c,b):
    # A closed-box slab test, followed by distances to each box edge.
    lo,hi=0,1
    for v,d,mn,mx in [(a[0],c[0]-a[0],b[0],b[2]),(a[1],c[1]-a[1],b[1],b[3])]:
        if abs(d)<1e-12:
            if v<mn or v>mx:lo,hi=1,0;break
        else:
            x,y=sorted(((mn-v)/d,(mx-v)/d));lo=max(lo,x);hi=min(hi,y)
    if lo<=hi:return 0
    corners=[(b[0],b[1]),(b[2],b[1]),(b[2],b[3]),(b[0],b[3])]
    return min(point_rect(a,b),point_rect(c,b),*(point_line(p,a,c) for p in corners))
def line_line(a,b,c,d):
    def orient(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    if orient(a,b,c)*orient(a,b,d)<0 and orient(c,d,a)*orient(c,d,b)<0:return 0
    return min(point_line(a,c,d),point_line(b,c,d),point_line(c,a,b),point_line(d,a,b))
def ground_connected(pad,conn):
    pending=[pad];seen=set()
    while pending:
        item=pending.pop();key=str(item.m_Uuid.AsString())
        if key in seen:continue
        seen.add(key)
        if item.Type() in [pcbnew.PCB_VIA_T,pcbnew.PCB_ZONE_T]:return True
        if item.Type()==pcbnew.PCB_PAD_T and pcbnew.Cast_to_PAD(item).GetDrillSize().x>0:return True
        pending.extend(conn.GetConnectedItems(item))
    return False

def add_fanout(board, only_refs=None):
    footprints=list(board.GetFootprints());allpads=[p for f in footprints for p in f.Pads()]
    smd=[p for p in allpads if p.GetNetname()=='GND' and p.IsOnLayer(pcbnew.F_Cu) and not p.GetDrillSize().x and (only_refs is None or p.GetParentFootprint().GetReference() in only_refs)]
    copperpads=[p for p in allpads if any(p.IsOnLayer(l) for l in g.LAYERS)]
    padboxes=[(p,box(p)) for p in copperpads]
    # Anonymous paste apertures have no copper and are not clearance obstacles.
    holes=[(point(p.GetPosition()),max(pcbnew.ToMM(p.GetDrillSize().x),pcbnew.ToMM(p.GetDrillSize().y))/2) for p in allpads if p.GetDrillSize().x]
    othertracks=[t for t in board.GetTracks() if t.GetNetname()!='GND']
    for t in board.GetTracks():
        if isinstance(t,pcbnew.PCB_VIA):holes.append((point(t.GetPosition()),pcbnew.ToMM(t.GetDrill())/2))
    records=[];failed=[];skipped=0;net=next(n for n in board.GetNetInfo().NetsByNetcode().values() if n.GetNetname()=='GND')
    board.BuildConnectivity();conn=board.GetConnectivity()
    for index,pad in enumerate(smd):
        if index%10==0:board.BuildConnectivity();conn=board.GetConnectivity()
        if ground_connected(pad,conn):skipped+=1;continue
        a=point(pad.GetPosition());candidate=None
        for radius in [.60,.75,1.0,1.25,1.5,2.0,2.5,3.0]:
            for degrees in [0,90,180,270,45,135,225,315]:
                rad=math.radians(degrees);v=(a[0]+radius*math.cos(rad),a[1]+radius*math.sin(rad))
                vb=(v[0]-VIA_DIAMETER/2,v[1]-VIA_DIAMETER/2,v[0]+VIA_DIAMETER/2,v[1]+VIA_DIAMETER/2)
                tb=(min(a[0],v[0])-TRACE_WIDTH/2,min(a[1],v[1])-TRACE_WIDTH/2,max(a[0],v[0])+TRACE_WIDTH/2,max(a[1],v[1])+TRACE_WIDTH/2)
                if not g.allowed(vb) or not g.allowed(tb):continue
                if any(math.dist(v,h)<VIA_DRILL/2+drill+.251 for h,drill in holes):continue
                bad=False
                for q,b in padboxes:
                    # No new via-in-pad, including same-net SMD lands.
                    if not q.GetDrillSize().x and q.IsOnLayer(pcbnew.F_Cu) and point_rect(v,b)<VIA_DIAMETER/2+.051:bad=True;break
                    if q.GetNetname()!='GND':
                        distance=max(0,math.dist(v,point(q.GetPosition()))-pcbnew.ToMM(q.GetSize().x)/2) if q.GetShape()==pcbnew.PAD_SHAPE_CIRCLE else point_rect(v,b)
                        if distance<VIA_DIAMETER/2+CLEAR:bad=True;break
                        if q.IsOnLayer(pcbnew.F_Cu) and line_rect(a,v,b)<TRACE_WIDTH/2+CLEAR:bad=True;break
                if bad:continue
                for t in othertracks:
                    if isinstance(t,pcbnew.PCB_VIA):
                        if math.dist(v,point(t.GetPosition()))<VIA_DIAMETER/2+pcbnew.ToMM(t.GetWidth(pcbnew.F_Cu))/2+CLEAR:bad=True;break
                        if point_line(point(t.GetPosition()),a,v)<pcbnew.ToMM(t.GetWidth(pcbnew.F_Cu))/2+TRACE_WIDTH/2+CLEAR:bad=True;break
                    else:
                        b,c=point(t.GetStart()),point(t.GetEnd());width=pcbnew.ToMM(t.GetWidth())
                        if point_line(v,b,c)<VIA_DIAMETER/2+width/2+CLEAR:bad=True;break
                        if t.GetLayer()==pcbnew.F_Cu and line_line(a,v,b,c)<TRACE_WIDTH/2+width/2+CLEAR:bad=True;break
                if not bad:candidate=v;break
            if candidate:break
        ref=pad.GetParentFootprint().GetReference()+'.'+pad.GetNumber()
        if candidate is None:failed.append(ref);continue
        via=pcbnew.PCB_VIA(board);via.SetPosition(g.p(*candidate));via.SetWidth(g.mm(VIA_DIAMETER));via.SetDrill(g.mm(VIA_DRILL));via.SetViaType(pcbnew.VIATYPE_THROUGH);via.SetLayerPair(pcbnew.F_Cu,pcbnew.B_Cu);via.SetNet(net);via.SetLocked(True);board.Add(via)
        track=pcbnew.PCB_TRACK(board);track.SetStart(pad.GetPosition());track.SetEnd(g.p(*candidate));track.SetWidth(g.mm(TRACE_WIDTH));track.SetLayer(pcbnew.F_Cu);track.SetNet(net);track.SetLocked(True);board.Add(track)
        holes.append((candidate,VIA_DRILL/2));records.append({'pad':ref,'xy_mm':candidate,'stub_length_mm':math.dist(a,candidate),'via_diameter_mm':VIA_DIAMETER,'via_drill_mm':VIA_DRILL})
    board.BuildConnectivity();pcbnew.ZONE_FILLER(board).Fill(board.Zones());board.BuildConnectivity();conn=board.GetConnectivity();conn.RecalculateRatsnest()
    return {'ground_stubs':records,'unroutable_ground_pads':failed,'already_serviced_ground_pads':skipped,'native_unconnected_count_after':conn.GetUnconnectedCount(False),'complete_layout_qualified':False}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('input',type=pathlib.Path);ap.add_argument('output',type=pathlib.Path);ap.add_argument('--report',type=pathlib.Path,required=True);ap.add_argument('--ref',action='append');a=ap.parse_args()
    before=hashlib.sha256(a.input.read_bytes()).hexdigest()
    board=pcbnew.LoadBoard(str(a.input));report=add_fanout(board,set(a.ref) if a.ref else None);pcbnew.SaveBoard(str(a.output),board)
    # A fresh native reload avoids stale connectivity caches after zone refill.
    checked=pcbnew.LoadBoard(str(a.output));checked.BuildConnectivity()
    conn=checked.GetConnectivity();conn.RecalculateRatsnest()
    report['native_unconnected_count_after']=conn.GetUnconnectedCount(False)
    report['input_board_sha256']=before;report['output_board_sha256']=hashlib.sha256(a.output.read_bytes()).hexdigest();a.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='ground_stubs'}))
if __name__=='__main__':main()
