#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Move owner-assigned bypass capacitors next to their owner's supply pin.

Each capacitor is searched over rings around the owner's pad on the shared
supply net, in all four orientations. A candidate is accepted only when its
physical box stays inside the outline (with the edge margin), clears the
BIOS cutout, CN503 notch and C5 module box, and clears every other
footprint, track and via by PART_GAP. The candidate with the shortest
capacitor-supply-pad to owner-pad distance wins. Run with KiCad 10's
/usr/bin/python3 (pcbnew). Routing and DRC still follow.
"""
import argparse
import json
import math
import pathlib

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

# Owner-assigned groups from CARRIER-LAYOUT-REVIEW.md "Whole-board decoupling".
OWNERS = {
    'C119': 'X10', 'C400': 'U40', 'C401': 'U40',
    'C212': 'U23', 'C213': 'U24', 'C214': 'U25', 'C215': 'U26',
    'C216': 'U27', 'C217': 'U28', 'C218': 'U29', 'C219': 'U30',
    'C220': 'U31', 'C221': 'U32', 'C222': 'U33', 'C223': 'U34',
    'C229': 'U37', 'C230': 'U72', 'C231': 'U38', 'C232': 'U73',
    'C233': 'U39', 'C234': 'U70',
    'C402': 'U41', 'C403': 'U42', 'C404': 'U43', 'C405': 'U44', 'C406': 'U45',
    'C235': 'U71', 'C236': 'U71', 'C2036': 'K214',
    'C2025': 'J201', 'C2026': 'J201',
}


def mm(v):
    return pcbnew.FromMM(v)


def local(v):
    return (pcbnew.ToMM(v.x) - ORIGIN[0], pcbnew.ToMM(v.y) - ORIGIN[1])


def box(item):
    b = item.GetBoundingBox()
    return (pcbnew.ToMM(b.GetLeft()) - ORIGIN[0], pcbnew.ToMM(b.GetTop()) - ORIGIN[1],
            pcbnew.ToMM(b.GetRight()) - ORIGIN[0], pcbnew.ToMM(b.GetBottom()) - ORIGIN[1])


def fp_box(fp):
    # Pads plus courtyard/fab body; silkscreen text is excluded.
    b = fp.GetBoundingBox(False)
    return (pcbnew.ToMM(b.GetLeft()) - ORIGIN[0], pcbnew.ToMM(b.GetTop()) - ORIGIN[1],
            pcbnew.ToMM(b.GetRight()) - ORIGIN[0], pcbnew.ToMM(b.GetBottom()) - ORIGIN[1])


def overlap(a, b, gap):
    return not (a[2] + gap <= b[0] or b[2] + gap <= a[0] or a[3] + gap <= b[1] or b[3] + gap <= a[1])


def inside_outline(b):
    m = EDGE_MARGIN
    if b[0] < m or b[1] < m or b[2] > WIDTH - m or b[3] > HEIGHT - m:
        return False
    for keep in (CUTOUT, NOTCH, C5_BOX):
        if overlap(b, keep, m):
            return False
    return True


def top_copper(board):
    # Only F.Cu tracks and through vias can collide with a top-side SMT part.
    return [box(t) for t in board.GetTracks()
            if t.Type() == pcbnew.PCB_VIA_T or t.GetLayer() == pcbnew.F_Cu]


def bottom_only(fp):
    pads = list(fp.Pads())
    return bool(pads) and all(not p.IsOnLayer(pcbnew.F_Cu) for p in pads)


def supply_pad(fp, net):
    pads = [p for p in fp.Pads() if p.GetNetname() == net]
    if not pads:
        raise ValueError('%s has no pad on %s' % (fp.GetReference(), net))
    return pads


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--report', type=pathlib.Path)
    ap.add_argument('--max-radius', type=float, default=6.0)
    ap.add_argument('--pair', action='append', default=[],
                    help='REF=OWNER[:NET], placed in the order given instead of the default groups')
    args = ap.parse_args()
    pairs = []
    for item in args.pair:
        ref, owner = item.split('=')
        owner, _, net_name = owner.partition(':')
        pairs.append((ref, owner, net_name or None))
    if not pairs:
        pairs = [(c, o, None) for c, o in OWNERS.items()]
    board = pcbnew.LoadBoard(str(args.board))
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    tracks = top_copper(board)
    results = []
    graveyard = []
    for cap_ref, owner_ref, want_net in pairs:
        cap, owner = fps[cap_ref], fps[owner_ref]
        cap_pads = list(cap.Pads())
        supply = next(p for p in cap_pads if (p.GetNetname() == want_net if want_net else p.GetNetname() != 'GND'))
        net = supply.GetNetname()
        targets = supply_pad(owner, net)
        # Bare B.Cu probe pads (no top copper) do not block top-side parts.
        others = [fp_box(f) for r, f in fps.items() if r != cap_ref and not bottom_only(f)]
        # Stubs attached to this part (and vias they alone fed) are removed and
        # reported; the router reconnects the part at its new position.
        own = [t for p in cap_pads for t in board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T
               and t.GetNetCode() == p.GetNetCode() and (p.HitTest(t.GetStart()) or p.HitTest(t.GetEnd()))]
        ends = {(e.x, e.y) for t in own for e in (t.GetStart(), t.GetEnd())}
        for v in [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T
                  and (v.GetPosition().x, v.GetPosition().y) in ends]:
            feeders = [t for t in board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T and t not in own
                       and t.GetNetCode() == v.GetNetCode() and (v.HitTest(t.GetStart()) or v.HitTest(t.GetEnd()))]
            if not feeders:
                own.append(v)
        removed_copper = len(own)
        for t in own:
            board.Remove(t)
            # Removed items stay referenced until save; collecting their
            # proxies early leaves later container iteration untyped.
            graveyard.append(t)
        tracks = top_copper(board)
        old_pos, old_rot = cap.GetPosition(), cap.GetOrientationDegrees()
        before = min(math.dist(local(supply.GetPosition()), local(t.GetPosition())) for t in targets)
        best = None
        for target in targets:
            tx, ty = local(target.GetPosition())
            reach = args.max_radius + 4.0
            near = (tx - reach, ty - reach, tx + reach, ty + reach)
            others_near = [o for o in others if overlap(o, near, 0)]
            tracks_near = [t for t in tracks if overlap(t, near, 0)]
            r = 0.9
            while r <= args.max_radius and (best is None or r <= best[0] + 0.5):
                steps = max(8, int(2 * math.pi * r / 0.25))
                for k in range(steps):
                    ang = 2 * math.pi * k / steps
                    x, y = tx + r * math.cos(ang), ty + r * math.sin(ang)
                    for rot in (0, 90, 180, 270):
                        cap.SetOrientationDegrees(rot)
                        cap.SetPosition(pcbnew.VECTOR2I(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y)))
                        b = fp_box(cap)
                        if not inside_outline(b):
                            continue
                        if any(overlap(b, o, PART_GAP) for o in others_near):
                            continue
                        if any(overlap(b, t, TRACK_GAP) for t in tracks_near):
                            continue
                        d = math.dist(local(supply.GetPosition()), (tx, ty))
                        if best is None or d < best[0]:
                            best = (d, x, y, rot, target.GetNumber())
                r += 0.25
        if best is None:
            cap.SetPosition(old_pos)
            cap.SetOrientationDegrees(old_rot)
            results.append({'cap': cap_ref, 'owner': owner_ref, 'net': net, 'placed': False,
                            'before_mm': round(before, 3), 'removed_copper_items': removed_copper})
            continue
        d, x, y, rot, pin = best
        cap.SetOrientationDegrees(rot)
        cap.SetPosition(pcbnew.VECTOR2I(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y)))
        results.append({'cap': cap_ref, 'owner': owner_ref + '.' + pin, 'net': net, 'placed': True,
                        'before_mm': round(before, 3), 'after_mm': round(d, 3),
                        'xy_mm': [round(x, 3), round(y, 3)], 'rotation_deg': rot,
                        'removed_copper_items': removed_copper})
    pcbnew.SaveBoard(str(args.board), board)
    text = json.dumps(results, indent=2)
    if args.report:
        args.report.write_text(text + '\n')
    print(text)


if __name__ == '__main__':
    main()
