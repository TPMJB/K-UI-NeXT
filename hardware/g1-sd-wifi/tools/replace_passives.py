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
PLANE_NETS = {'GND', '+3V3_LOGIC'}
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


def plane_fanout(board, fps):
    """Locked plane-net stubs that join one pad to one via and nothing else."""
    tracks = list(board.GetTracks())
    vias = [t for t in tracks if t.Type() == pcbnew.PCB_VIA_T]
    segs = [t for t in tracks if t.Type() != pcbnew.PCB_VIA_T]
    result = {}
    for r, f in fps.items():
        for p in f.Pads():
            if p.GetNetname() not in PLANE_NETS:
                continue
            for t in segs:
                if not t.IsLocked() or t.GetNetCode() != p.GetNetCode():
                    continue
                if p.HitTest(t.GetStart()):
                    far = t.GetEnd()
                elif p.HitTest(t.GetEnd()):
                    far = t.GetStart()
                else:
                    continue
                via = [v for v in vias if v.GetNetCode() == t.GetNetCode() and v.HitTest(far)]
                if len(via) != 1:
                    continue
                others = [u for u in segs if u is not t and u.GetNetCode() == t.GetNetCode()
                          and (via[0].HitTest(u.GetStart()) or via[0].HitTest(u.GetEnd()))]
                if not others:
                    result.setdefault(r, []).extend([t, via[0]])
    return result


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
    ap.add_argument('--part', action='append', default=[],
                    help='Also move this part (any pad count, e.g. a small IC); all copper on its '
                         'pads is dropped. Re-place its bypass capacitors afterwards.')
    ap.add_argument('--bypass', action='append', default=[],
                    help='CAP=OWNER: place this supply capacitor early, next to the owner pin on '
                         'its supply net (priority over ordinary parts)')
    ap.add_argument('--clear-plane-fanout', action='store_true',
                    help='Before placing, remove every narrow (0.15 mm) GND/+3V3_LOGIC pad-to-via '
                         'fanout stub on the board so it does not block placement; reviewed wider '
                         'local stubs stay. Implies --lift-plane-fanout.')
    ap.add_argument('--lift-plane-fanout', action='store_true',
                    help='Also lift locked GND/+3V3_LOGIC fanout stubs (pad-to-via only); rerun '
                         'add_carrier_ground_fanout.py for both nets afterwards')
    args = ap.parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    fps = {f.GetReference(): f for f in list(board.GetFootprints())}
    tracks = list(board.GetTracks())
    cleared = 0
    if args.clear_plane_fanout:
        args.lift_plane_fanout = True
        keep_graveyard = []
        for items in plane_fanout(board, fps).values():
            for i in range(0, len(items), 2):
                stub, via = items[i], items[i + 1]
                if pcbnew.ToMM(stub.GetWidth()) <= 0.16:
                    for t in (stub, via):
                        board.Remove(t)
                        keep_graveyard.append(t)
                        cleared += 1
        tracks = list(board.GetTracks())
    fanout = plane_fanout(board, fps) if args.lift_plane_fanout else {}
    fanout_items = {id(t) for items in fanout.values() for t in items}
    locked_refs = set()
    for f in fps.values():
        for p in f.Pads():
            for t in tracks:
                if id(t) in fanout_items:
                    continue
                if t.IsLocked() and t.GetNetCode() == p.GetNetCode() and (
                        (t.Type() == pcbnew.PCB_VIA_T and p.HitTest(t.GetPosition())) or
                        (t.Type() != pcbnew.PCB_VIA_T and (p.HitTest(t.GetStart()) or p.HitTest(t.GetEnd())))):
                    locked_refs.add(f.GetReference())
    keep = set(args.keep)
    movable = [r for r, f in fps.items()
               if re.fullmatch(r'[RC]\d+', r) and len(list(f.Pads())) == 2 and signal_nets(f)
               and r not in locked_refs and r not in keep and not re.fullmatch(r'[RC]4\d\d', r)]
    unknown = [r for r in args.part if r not in fps]
    if unknown:
        raise ValueError('Unknown --part references: %s' % unknown)
    movable += [r for r in args.part if r not in movable]
    bypass = dict(item.split('=') for item in args.bypass)
    unknown = [r for r in list(bypass) + list(bypass.values()) if r not in fps]
    if unknown:
        raise ValueError('Unknown --bypass references: %s' % unknown)
    movable += [r for r in bypass if r not in movable]
    forced = set(args.part) | set(bypass)
    graveyard = []
    removed = 0
    # Lift movable parts: drop their unlocked stubs (and vias only they fed).
    for r in movable:
        own = [t for p in fps[r].Pads() for t in board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T
               and (r in forced or not t.IsLocked()) and t.GetNetCode() == p.GetNetCode()
               and (p.HitTest(t.GetStart()) or p.HitTest(t.GetEnd()))]
        if r in forced:
            own += [v for p in fps[r].Pads() for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T
                    and v.GetNetCode() == p.GetNetCode() and p.HitTest(v.GetPosition())]
        ends = {(e.x, e.y) for t in own for e in (t.GetStart(), t.GetEnd())}
        for v in [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T
                  and (r in forced or not v.IsLocked())
                  and (v.GetPosition().x, v.GetPosition().y) in ends]:
            if not [t for t in board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T and t not in own
                    and t.GetNetCode() == v.GetNetCode() and (v.HitTest(t.GetStart()) or v.HitTest(t.GetEnd()))]:
                own.append(v)
        own += fanout.get(r, [])
        # Separate SWIG proxies of one item are not equal; dedupe by UUID.
        unique = {}
        for t in own:
            unique.setdefault(t.m_Uuid.AsString(), t)
        for t in unique.values():
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
        # Named parts (ICs) are the hardest to fit, so they choose first,
        # larger ones before smaller.
        size = -len(list(fps[r].Pads())) if r in forced else 0
        return (0 if r in forced else 1, size, 0 if fast else 1, 0 if series else 1, r)

    def signal_targets(fp):
        if fp.GetReference() in bypass:
            # A bypass capacitor aims its supply pad at the owner's pins on
            # the same supply net.
            owner = fps[bypass[fp.GetReference()]]
            supply = [p for p in fp.Pads() if p.GetNetname() != 'GND']
            return {p.GetNumber(): [local(q.GetPosition()) for q in owner.Pads()
                                    if q.GetNetname() == p.GetNetname()] for p in supply}
        # Supply pins drop to a plane via or a local rail; only signal
        # partners decide where the part belongs.
        direct = {p.GetNumber(): fixed_pads[p.GetNetname()] for p in fp.Pads()
                  if p.GetNetname() in fixed_pads and p.GetNetname() not in POWER}
        if direct:
            return direct
        # Two hops: a part whose only partner is a movable series part (an
        # oscillator behind its damping resistor) aims at what that part
        # connects to, so it can claim its spot early instead of last.
        hop = {}
        for p in fp.Pads():
            n = p.GetNetname()
            if n in POWER or not n:
                continue
            for r2 in movable:
                if r2 == fp.GetReference():
                    continue
                q = list(fps[r2].Pads())
                if len(q) != 2 or n not in {x.GetNetname() for x in q}:
                    continue
                other = next(x.GetNetname() for x in q if x.GetNetname() != n)
                if other in fixed_pads and other not in POWER:
                    hop.setdefault(p.GetNumber(), []).extend(fixed_pads[other])
        return hop

    def place(r):
        fp = fps[r]
        targets = signal_targets(fp)
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
            return {'ref': r, 'placed': False, 'reason': 'no free spot', 'nets': signal_nets(fp)}
        cost, x, y, rot, b = chosen
        obstacles.append(b)
        return {'ref': r, 'placed': True, 'xy_mm': [round(x, 3), round(y, 3)], 'rotation_deg': rot,
                'wire_mm': round(cost, 2), 'nets': signal_nets(fp)}

    # Parts whose signal partners are all movable wait where they are,
    # reserved, until those partners have been placed.
    deferred = [r for r in movable if not signal_targets(fps[r])]
    reserved = {r: bbox(fps[r].GetBoundingBox(False)) for r in deferred}
    obstacles.extend(reserved.values())
    results = [place(r) for r in sorted(set(movable) - set(deferred), key=priority)]
    for r in [r for r in movable if r not in deferred]:
        for p in fps[r].Pads():
            fixed_pads.setdefault(p.GetNetname(), []).append(local(p.GetPosition()))
    for r in sorted(deferred, key=priority):
        obstacles.remove(reserved[r])
        if signal_targets(fps[r]):
            results.append(place(r))
        else:
            obstacles.append(reserved[r])
            results.append({'ref': r, 'placed': False, 'reason': 'no fixed partner'})
    # A part that found no spot stays where it was, but parts placed before
    # it may already sit there; move those again around it.
    stuck = [fps[x['ref']] for x in results if not x['placed'] and 'reason' in x and x['reason'] == 'no free spot']
    placed_rows = {x['ref']: x for x in results if x['placed']}
    for blocker in stuck:
        bb = bbox(blocker.GetBoundingBox(False))
        for r, row in list(placed_rows.items()):
            fp = fps[r]
            mine = bbox(fp.GetBoundingBox(False))
            if not overlap(mine, bb, PART_GAP):
                continue
            obstacles.remove(next(o for o in obstacles if o == mine))
            targets = {p.GetNumber(): fixed_pads[p.GetNetname()] for p in fp.Pads()
                       if p.GetNetname() in fixed_pads and p.GetNetname() not in POWER}
            pts = [pt for v in targets.values() for pt in v]
            cx = sum(x for x, _ in pts) / len(pts)
            cy = sum(y for _, y in pts) / len(pts)
            chosen = None
            for radius in (args.radius, args.radius * 2.5, 60.0):
                chosen = search(fp, targets, cx, cy, radius, obstacles, copper)
                if chosen is not None:
                    break
            if chosen is None:
                raise ValueError('%s overlaps unplaced %s and cannot move' % (r, blocker.GetReference()))
            cost, x, y, rot, nb = chosen
            obstacles.append(nb)
            row.update({'xy_mm': [round(x, 3), round(y, 3)], 'rotation_deg': rot, 'wire_mm': round(cost, 2),
                        'moved_off': blocker.GetReference()})
    pcbnew.SaveBoard(str(args.board), board)
    summary = {'movable': len(movable), 'placed': sum(1 for x in results if x['placed']),
               'unplaced': [x['ref'] for x in results if not x['placed']], 'removed_copper_items': removed,
               'cleared_fanout_items': cleared}
    if args.report:
        args.report.write_text(json.dumps({'summary': summary, 'parts': results}, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
