#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply reviewed, explicit track edits to the carrier.

For the few places where the gap closer joins a route to existing copper at
a shallow angle or doubles back along it. The edits are a JSON list, applied
in order; coordinates are native KiCad board millimetres (as DRC reports and
the PCB file show them), tracks are matched by net, layer and both end
points (either order, within 1 um), and every edit must match exactly one
item or the board is left unchanged:

  {"op": "delete", "net": "C5_PWR_EN", "layer": "In2.Cu", "start": [x, y], "end": [x, y]}
  {"op": "add", "net": "C5_PWR_EN", "layer": "In2.Cu", "points": [[x, y], [x, y], ...]}
  {"op": "width", "net": "+3V3_LOGIC", "layer": "F.Cu", "start": [x, y], "end": [x, y], "width": 0.2}

"width" changes one track's width in place (same ends, same ID).

Added tracks use the Default netclass width unless "width" (mm) is given;
an added track needs at least two points, none repeated consecutively. The
layer must be one of the board's copper layers. Native DRC must be run
afterwards. Run with KiCad 10's Python.
"""
import argparse
import json
import pathlib

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
TOL = pcbnew.FromMM(0.001)


def pt(xy):
    return pcbnew.VECTOR2I(pcbnew.FromMM(xy[0]), pcbnew.FromMM(xy[1]))


def near(a, b):
    return abs(a.x - b.x) <= TOL and abs(a.y - b.y) <= TOL


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--edits', type=pathlib.Path, required=True)
    args = ap.parse_args()
    edits = json.loads(args.edits.read_text())
    pro = args.board.with_suffix('.kicad_pro')
    rules_before = pro.read_bytes()
    board = pcbnew.LoadBoard(str(args.board))
    nets = {n.GetNetname(): n for n in board.GetNetInfo().NetsByNetcode().values()}
    width = board.GetDesignSettings().m_NetSettings.GetDefaultNetclass().GetTrackWidth()
    copper = {board.GetLayerName(l): l for l in board.GetEnabledLayers().CuStack()}
    graveyard, done = [], []
    for e in edits:
        if e.get('layer') not in copper:
            raise SystemExit('Not a copper layer of this board: %s' % json.dumps(e))
        layer = copper[e['layer']]
        if e['op'] in ('delete', 'width'):
            a, b = pt(e['start']), pt(e['end'])
            hits = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == e['net']
                    and t.GetLayer() == layer and ((near(t.GetStart(), a) and near(t.GetEnd(), b))
                                                   or (near(t.GetStart(), b) and near(t.GetEnd(), a)))]
            if len(hits) != 1:
                raise SystemExit('Edit matches %d tracks, not 1: %s' % (len(hits), json.dumps(e)))
            if e['op'] == 'width':
                if not isinstance(e.get('width'), (int, float)) or not 0.05 <= e['width'] <= 5:
                    raise SystemExit('A width edit needs "width" in mm (0.05-5): ' + json.dumps(e))
                hits[0].SetWidth(pcbnew.FromMM(e['width']))
            else:
                board.Remove(hits[0])
                graveyard.append(hits[0])
        elif e['op'] == 'add':
            if e['net'] not in nets:
                raise SystemExit('No such net: ' + e['net'])
            points = [pt(p) for p in e['points']]
            if len(points) < 2 or any(a == b for a, b in zip(points, points[1:])):
                raise SystemExit('An added track needs two or more distinct points: ' + json.dumps(e))
            for a, b in zip(points, points[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(a)
                t.SetEnd(b)
                t.SetWidth(pcbnew.FromMM(e['width']) if 'width' in e else width)
                t.SetLayer(layer)
                t.SetNet(nets[e['net']])
                board.Add(t)
        else:
            raise SystemExit('Unknown edit: ' + json.dumps(e))
        done.append(e)
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(args.board), board)
    if pro.read_bytes() != rules_before:
        pro.write_bytes(rules_before)
        raise SystemExit('Saving changed the project rules file; restored it.')
    print(json.dumps({'applied': len(done), 'edits': done}))


if __name__ == '__main__':
    main()
