#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Connectivity-driven re-placement of signal passives on the carrier.

The original deterministic placer packed parts without regard to what they
connect to, leaving series resistors and pull-ups tens of millimetres from
both of their partners. This tool keeps every IC, connector, probe pad,
locked route and the power section where they are, lifts the movable
two-pad resistors/capacitors that carry at least one signal net, and puts
each one back at the free top-side spot that minimises its wire length to
the fixed pads it connects to. Fast nets (clocks, USB, SD, QSPI, C5 SPI)
are placed first so they get the best spots. Run with KiCad 10's Python;
routing and native DRC follow.
"""
import argparse
import json
import math
import pathlib
import re

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
ORIGIN = (70.0, 45.0)
WIDTH, HEIGHT = 59.0, 105.0
CUTOUT = (6.0, 51.0, 35.0, 70.0)
NOTCH = (43.8, 42.0, 59.0, 89.0)
C5_BOX = (3.5733, 1.0973, 24.5537, 18.9027)
EDGE_MARGIN = 0.30
PART_GAP = 0.20
TRACK_GAP = 0.20
GRID = 0.25
POWER = {'GND', '+3V3_LOGIC', 'CONSOLE_3V3', '+3V3_STORAGE', '+1V1_MCU', 'PRIMARY_5V', '+5V_HOLD',
         'RESERVE_5V', '+5V_C5', '+3V3_C5_IO', 'FUSED_5V', 'CONSOLE_5V', 'FLEX_5V', 'MCU_VREG_AVDD'}
FAST = re.compile(r'CLK|USB|SD_|QSPI|PSRAM|C5_(SCLK|MOSI|MISO|CSn)|XIN|XOUT|LINK_')


def mm(v):
    return pcbnew.FromMM(v)


def local(v):
    return (pcbnew.ToMM(v.x) - ORIGIN[0], pcbnew.ToMM(v.y) - ORIGIN[1])


def bbox(b):
    return (pcbnew.ToMM(b.GetLeft()) - ORIGIN[0], pcbnew.ToMM(b.GetTop()) - ORIGIN[1],
            pcbnew.ToMM(b.GetRight()) - ORIGIN[0], pcbnew.ToMM(b.GetBottom()) - ORIGIN[1])


def overlap(a, b, gap):
    return not (a[2] + gap <= b[0] or b[2] + gap <= a[0] or a[3] + gap <= b[1] or b[3] + gap <= a[1])


def inside_outline(b):
    m = EDGE_MARGIN
    if b[0] < m or b[1] < m or b[2] > WIDTH - m or b[3] > HEIGHT - m:
        return False
    return not any(overlap(b, k, m) for k in (CUTOUT, NOTCH, C5_BOX))


def bottom_only(fp):
    pads = list(fp.Pads())
    return bool(pads) and all(not p.IsOnLayer(pcbnew.F_Cu) for p in pads)


def signal_nets(fp):
    return [p.GetNetname() for p in fp.Pads()
            if p.GetNetname() and p.GetNetname() not in POWER and not p.GetNetname().startswith('unconnected')]


def search(fp, targets, ax, ay, radius, obstacles, copper):
    candidates = []
    steps = int(radius / GRID)
    for rot in (0, 90, 180, 270):
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(pcbnew.VECTOR2I(mm(ORIGIN[0] + ax), mm(ORIGIN[1] + ay)))
        rel = {p.GetNumber(): (local(p.GetPosition())[0] - ax, local(p.GetPosition())[1] - ay) for p in fp.Pads()}
        for i in range(-steps, steps + 1):
            for j in range(-steps, steps + 1):
                if math.hypot(i * GRID, j * GRID) > radius:
                    continue
                x, y = ax + i * GRID, ay + j * GRID
                cost = sum(min(math.dist((x + rel[n][0], y + rel[n][1]), q) for q in targets[n])
                           for n in targets)
                candidates.append((cost, x, y, rot))
    candidates.sort()
    reach = radius + 4
    near = (ax - reach, ay - reach, ax + reach, ay + reach)
    obs = [o for o in obstacles if overlap(o, near, 0)]
    cop = [c for c in copper if overlap(c, near, 0)]
    for cost, x, y, rot in candidates:
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(pcbnew.VECTOR2I(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y)))
        b = bbox(fp.GetBoundingBox(False))
        if not inside_outline(b) or any(overlap(b, o, PART_GAP) for o in obs) \
                or any(overlap(b, c, TRACK_GAP) for c in cop):
            continue
        return (cost, x, y, rot, b)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--report', type=pathlib.Path)
    ap.add_argument('--radius', type=float, default=10.0)
    ap.add_argument('--keep', action='append', default=[], help='Reference that must not move')
    args = ap.parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    fps = {f.GetReference(): f for f in list(board.GetFootprints())}
    tracks = list(board.GetTracks())
    locked_refs = set()
    for f in fps.values():
        for p in f.Pads():
            for t in tracks:
                if t.IsLocked() and t.GetNetCode() == p.GetNetCode() and (
                        (t.Type() == pcbnew.PCB_VIA_T and p.HitTest(t.GetPosition())) or
                        (t.Type() != pcbnew.PCB_VIA_T and (p.HitTest(t.GetStart()) or p.HitTest(t.GetEnd())))):
                    locked_refs.add(f.GetReference())
    keep = set(args.keep)
    movable = [r for r, f in fps.items()
               if re.fullmatch(r'[RC]\d+', r) and len(list(f.Pads())) == 2 and signal_nets(f)
               and r not in locked_refs and r not in keep and not re.fullmatch(r'[RC]4\d\d', r)]
    graveyard = []
    removed = 0
    # Lift movable parts: drop their unlocked stubs (and vias only they fed).
    for r in movable:
        own = [t for p in fps[r].Pads() for t in board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T
               and not t.IsLocked() and t.GetNetCode() == p.GetNetCode()
               and (p.HitTest(t.GetStart()) or p.HitTest(t.GetEnd()))]
        ends = {(e.x, e.y) for t in own for e in (t.GetStart(), t.GetEnd())}
        for v in [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T and not v.IsLocked()
                  and (v.GetPosition().x, v.GetPosition().y) in ends]:
            if not [t for t in board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T and t not in own
                    and t.GetNetCode() == v.GetNetCode() and (v.HitTest(t.GetStart()) or v.HitTest(t.GetEnd()))]:
                own.append(v)
        for t in own:
            board.Remove(t)
            graveyard.append(t)
            removed += 1
    movable_set = set(movable)
    fixed_pads = {}
    for r, f in fps.items():
        if r in movable_set or r.startswith('TP'):
            continue
        for p in f.Pads():
            fixed_pads.setdefault(p.GetNetname(), []).append(local(p.GetPosition()))
    obstacles = [bbox(f.GetBoundingBox(False)) for r, f in fps.items()
                 if r not in movable_set and not bottom_only(f)]
    copper = [bbox(t.GetBoundingBox()) for t in board.GetTracks()
              if t.Type() == pcbnew.PCB_VIA_T or t.GetLayer() == pcbnew.F_Cu]

    def priority(r):
        nets = signal_nets(fps[r])
        fast = any(FAST.search(n) for n in nets)
        series = len(nets) == 2
        return (0 if fast else 1, 0 if series else 1, r)

    results = []
    for r in sorted(movable, key=priority):
        fp = fps[r]
        pads = list(fp.Pads())
        targets = {}
        for p in pads:
            n = p.GetNetname()
            if n in fixed_pads:
                targets[p.GetNumber()] = fixed_pads[n]
        if not targets:
            results.append({'ref': r, 'placed': False, 'reason': 'no fixed partner'})
            continue
        # Target point: centroid of each pad's nearest-to-centroid fixed partner.
        pts = [pt for v in targets.values() for pt in v]
        cx = sum(x for x, _ in pts) / len(pts)
        cy = sum(y for _, y in pts) / len(pts)
        anchors = {num: min(v, key=lambda q: math.dist(q, (cx, cy))) for num, v in targets.items()}
        ax = sum(x for x, _ in anchors.values()) / len(anchors)
        ay = sum(y for _, y in anchors.values()) / len(anchors)
        old = (fp.GetPosition(), fp.GetOrientationDegrees())
        chosen = None
        for radius in (args.radius, args.radius * 2.5, 60.0):
            chosen = search(fp, targets, ax, ay, radius, obstacles, copper)
            if chosen is not None:
                break
        if chosen is None:
            fp.SetPosition(old[0])
            fp.SetOrientationDegrees(old[1])
            obstacles.append(bbox(fp.GetBoundingBox(False)))
            results.append({'ref': r, 'placed': False, 'reason': 'no free spot', 'nets': signal_nets(fp)})
            continue
        cost, x, y, rot, b = chosen
        obstacles.append(b)
        results.append({'ref': r, 'placed': True, 'xy_mm': [round(x, 3), round(y, 3)], 'rotation_deg': rot,
                        'wire_mm': round(cost, 2), 'nets': signal_nets(fp)})
    pcbnew.SaveBoard(str(args.board), board)
    summary = {'movable': len(movable), 'placed': sum(1 for x in results if x['placed']),
               'unplaced': [x['ref'] for x in results if not x['placed']], 'removed_copper_items': removed}
    if args.report:
        args.report.write_text(json.dumps({'summary': summary, 'parts': results}, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
