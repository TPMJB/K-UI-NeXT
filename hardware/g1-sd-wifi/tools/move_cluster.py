#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Move a part with its local cluster as one rigid body.

The owner footprint, the named member footprints (typically its bypass
capacitors) and every copper island that touches only those footprints'
pads (bypass feeds, ground stubs and their vias) are rotated about the
owner's origin and translated together, so reviewed local copper keeps its
exact geometry. Copper that also reaches a pad outside the cluster cannot
move rigidly; it is deleted and listed so the router can redo it. Members
given with --lift are moved as part of the cluster but their copper is
dropped too, for parts that will be re-placed afterwards. Run with KiCad
10's Python (pcbnew); native DRC follows.
"""
import argparse
import json
import math
import pathlib

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
ORIGIN = (70.0, 45.0)


def key(v):
    return (v.x, v.y)


def islands(board):
    """Copper islands: tracks and vias joined end to end on one net."""
    items = list(board.GetTracks())
    parent = list(range(len(items)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    ends = {}
    for i, t in enumerate(items):
        points = [t.GetPosition()] if t.Type() == pcbnew.PCB_VIA_T else [t.GetStart(), t.GetEnd()]
        for p in points:
            ends.setdefault((t.GetNetCode(), key(p)), []).append(i)
    for group in ends.values():
        for j in group[1:]:
            parent[find(j)] = find(group[0])
    # Via-to-track joins where the track end falls inside the via annulus.
    vias = [i for i, t in enumerate(items) if t.Type() == pcbnew.PCB_VIA_T]
    for i, t in enumerate(items):
        if t.Type() == pcbnew.PCB_VIA_T:
            continue
        for v in vias:
            via = items[v]
            if via.GetNetCode() == t.GetNetCode() and (via.HitTest(t.GetStart()) or via.HitTest(t.GetEnd())):
                parent[find(i)] = find(v)
    groups = {}
    for i in range(len(items)):
        groups.setdefault(find(i), []).append(items[i])
    return list(groups.values())


def touched_refs(island, pads):
    refs = set()
    for t in island:
        points = [t.GetPosition()] if t.Type() == pcbnew.PCB_VIA_T else [t.GetStart(), t.GetEnd()]
        for ref, pad in pads:
            if pad.GetNetCode() == t.GetNetCode() and any(pad.HitTest(p) for p in points):
                refs.add(ref)
    return refs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--owner', required=True)
    ap.add_argument('--member', action='append', default=[])
    ap.add_argument('--lift', action='append', default=[], help='Member whose copper is dropped')
    ap.add_argument('--to', required=True, help='x,y,rotation_deg: new owner origin (local mm) and added rotation')
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    x, y, rot = (float(v) for v in args.to.split(','))
    board = pcbnew.LoadBoard(str(args.board))
    fps = {f.GetReference(): f for f in list(board.GetFootprints())}
    cluster = [args.owner] + args.member + args.lift
    missing = [r for r in cluster if r not in fps]
    if missing:
        raise ValueError('Unknown references: %s' % missing)
    pads = [(f.GetReference(), p) for f in fps.values() for p in f.Pads()]
    owner = fps[args.owner]
    centre = owner.GetPosition()
    target = pcbnew.VECTOR2I(pcbnew.FromMM(ORIGIN[0] + x), pcbnew.FromMM(ORIGIN[1] + y))
    angle = pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T)
    a = math.radians(rot)

    def move(p):
        dx, dy = p.x - centre.x, p.y - centre.y
        # KiCad's y axis points down, so a positive angle turns clockwise
        # on screen exactly as footprint Rotate() does.
        nx = dx * math.cos(a) + dy * math.sin(a)
        ny = -dx * math.sin(a) + dy * math.cos(a)
        return pcbnew.VECTOR2I(int(round(target.x + nx)), int(round(target.y + ny)))

    moved, dropped, graveyard = [], [], []
    rigid = set([args.owner] + args.member)
    for island in islands(board):
        refs = touched_refs(island, pads)
        if not refs & set(cluster):
            continue
        if refs <= rigid:
            for t in island:
                if t.Type() == pcbnew.PCB_VIA_T:
                    t.SetPosition(move(t.GetPosition()))
                else:
                    t.SetStart(move(t.GetStart()))
                    t.SetEnd(move(t.GetEnd()))
            moved.append({'net': island[0].GetNetname(), 'items': len(island), 'refs': sorted(refs)})
        else:
            for t in island:
                board.Remove(t)
                graveyard.append(t)
            dropped.append({'net': island[0].GetNetname(), 'items': len(island), 'refs': sorted(refs)})
    for ref in cluster:
        fp = fps[ref]
        fp.SetPosition(move(fp.GetPosition()))
        fp.Rotate(fp.GetPosition(), angle)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    report = {'owner': args.owner, 'to': [x, y, rot], 'members': args.member, 'lifted': args.lift,
              'moved_islands': moved, 'dropped_islands': dropped}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'moved_islands': len(moved), 'dropped_islands': len(dropped),
                      'dropped': [d['refs'] for d in dropped]}))


if __name__ == '__main__':
    main()
