#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare a changed carrier with an earlier one: what survived, what went.

Footprints: every footprint of the base board must keep its library identity,
position, orientation, side and pad geometry, and its pad nets apart from
the pads named with --pad-net-change. Zones (outline, net, layers, priority,
lock, rule-area flags; not their fill) and board drawings (Edge.Cuts,
silkscreen, documentation) must be unchanged.

Tracks and vias are matched by UUID. A matched item must keep its net, width,
lock state and layer (via: type, layer pair and drill), and its position;
a track may instead have been trimmed or split on its own line, where new
copper joins it. On nets outside --allow-net the pieces on that line must
still cover the whole original track; on --allow-net nets, which the change
lifts and reroutes, trimmed copper is reported as shortened. Removed items
are listed by net and fail the check outside --allow-net. An item named
with --allow-change (by UUID) may differ in one reviewed property only: a
track's width or a drawing's text. New copper is summarised by net and
layer. Run with KiCad 10's Python.

  tools/check_preservation.py --base-rev fedc570 --allow-net LINK_A1 ... \\
      --pad-net-change U201:21 ... --report design/.../preservation-check.json
"""
import argparse
import collections
import hashlib
import json
import pathlib
import subprocess
import tempfile

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'


def items(board):
    out = {}
    for t in board.GetTracks():
        rec = {'net': t.GetNetname(), 'via': t.Type() == pcbnew.PCB_VIA_T, 'locked': t.IsLocked()}
        if rec['via']:
            p = t.GetPosition()
            rec.update(at=(p.x, p.y), width=t.GetWidth(pcbnew.F_Cu), drill=t.GetDrillValue(),
                       via_type=int(t.GetViaType()),
                       layers=(board.GetLayerName(t.TopLayer()), board.GetLayerName(t.BottomLayer())))
        else:
            a, b = t.GetStart(), t.GetEnd()
            rec.update(at=(a.x, a.y, b.x, b.y), width=t.GetWidth(), layer=board.GetLayerName(t.GetLayer()))
        out[t.m_Uuid.AsString()] = rec
    return out


def footprints(board):
    out = {}
    for f in board.GetFootprints():
        p = f.GetPosition()
        pads = []
        for pd in f.Pads():
            q, s = pd.GetPosition(), pd.GetSize(pcbnew.F_Cu)
            pads.append((pd.GetNumber(), q.x, q.y, s.x, s.y, int(pd.GetShape(pcbnew.F_Cu)),
                         round(pd.GetOrientationDegrees(), 4), pd.GetNetname()))
        out[f.GetReference()] = {'fpid': str(f.GetFPID().GetUniStringLibId()), 'at': (p.x, p.y),
                                 'rot': round(f.GetOrientationDegrees(), 4), 'layer': f.GetLayer(),
                                 'pads': sorted(pads)}
    return out


def zones(board):
    out = {}
    for z in board.Zones():
        outline = z.Outline()
        pts = [(outline.CVertex(i).x, outline.CVertex(i).y) for i in range(outline.TotalVertices())]
        out[z.m_Uuid.AsString()] = {
            'net': z.GetNetname(), 'layers': sorted(board.GetLayerName(l) for l in z.GetLayerSet().Seq()),
            'priority': z.GetAssignedPriority(), 'locked': z.IsLocked(), 'rule_area': z.GetIsRuleArea(),
            'keepouts': (z.GetDoNotAllowTracks(), z.GetDoNotAllowVias(), z.GetDoNotAllowPads(),
                         z.GetDoNotAllowZoneFills(), z.GetDoNotAllowFootprints()),
            'outline': pts}
    return out


def drawings(board):
    out = {}
    for d in board.GetDrawings():
        bb = d.GetBoundingBox()
        rec = {'class': d.GetClass(), 'layer': board.GetLayerName(d.GetLayer()),
               'bbox': (bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom())}
        if hasattr(d, 'GetText'):
            q, size = d.GetTextPos(), d.GetTextSize()
            rec.update(text=d.GetText(), text_pos=(q.x, q.y), text_size=(size.x, size.y),
                       text_angle=round(d.GetTextAngleDegrees(), 4), text_thickness=d.GetTextThickness(),
                       text_justify=(int(d.GetHorizJustify()), int(d.GetVertJustify())),
                       text_mirrored=d.IsMirrored())
        elif hasattr(d, 'GetStart'):
            a, b = d.GetStart(), d.GetEnd()
            rec['ends'] = (a.x, a.y, b.x, b.y)
        out[d.m_Uuid.AsString()] = rec
    return out


def length(rec):
    x0, y0, x1, y1 = rec['at']
    return ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5 / 1e6


def is_piece(old, new, tol=1000):
    """True when track `new` lies on track `old` (within tol nm): a split or trim."""
    if old['via'] or new['via'] or any(old[k] != new[k] for k in ('net', 'layer', 'width', 'locked')):
        return False
    ax, ay, bx, by = old['at']
    length2 = (bx - ax) ** 2 + (by - ay) ** 2
    for px, py in (new['at'][:2], new['at'][2:]):
        if length2 == 0:
            if abs(px - ax) > tol or abs(py - ay) > tol:
                return False
            continue
        t = ((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / length2
        cx, cy = ax + t * (bx - ax), ay + t * (by - ay)
        if not -1e-6 <= t <= 1 + 1e-6 or ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5 > tol:
            return False
    return True


def covered(old, pieces, tol=1000):
    """True when the pieces (all lying on old) span the whole of old."""
    ax, ay, bx, by = old['at']
    full = ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5
    if full <= tol:
        return True
    spans = []
    for p in pieces:
        ts = [((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / full ** 2 for px, py in (p['at'][:2], p['at'][2:])]
        spans.append((min(ts), max(ts)))
    reach = 0.0
    for t0, t1 in sorted(spans):
        if t0 * full > reach * full + tol:
            return False
        reach = max(reach, t1)
    return reach * full >= full - tol


def compare(name, base, new, problems):
    for key, rec in base.items():
        if key not in new:
            problems.append('%s removed: %s' % (name, key))
        elif new[key] != rec:
            problems.append('%s changed: %s' % (name, key))
    problems += ['%s added: %s' % (name, key) for key in new if key not in base]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--base-rev', required=True, help='git revision holding the base board')
    ap.add_argument('--allow-net', action='append', default=[], help='net whose base copper may be removed or shortened')
    ap.add_argument('--pad-net-change', action='append', default=[], help='REF:PAD whose net may change')
    ap.add_argument('--allow-change', action='append', default=[],
                    help='UUID of a track whose width, or a drawing whose text, may change')
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    rel = args.board.resolve().relative_to(ROOT.parents[1])
    text = subprocess.run(['git', '-C', str(ROOT), 'show', '%s:%s' % (args.base_rev, rel)],
                          capture_output=True, check=True).stdout
    with tempfile.TemporaryDirectory() as tmp:
        base_path = pathlib.Path(tmp) / args.board.name
        base_path.write_bytes(text)
        base = pcbnew.LoadBoard(str(base_path))
        new = pcbnew.LoadBoard(str(args.board))
        bi, ni = items(base), items(new)
        bf, nf = footprints(base), footprints(new)
        bz, nz = zones(base), zones(new)
        bd, nd = drawings(base), drawings(new)
    problems = []
    allowed_pads = {tuple(s.split(':')) for s in args.pad_net_change}
    for ref, f in bf.items():
        g = nf.get(ref)
        if g is None:
            problems.append('footprint removed: ' + ref)
            continue
        if (f['fpid'], f['at'], f['rot'], f['layer']) != (g['fpid'], g['at'], g['rot'], g['layer']):
            problems.append('footprint moved or replaced: ' + ref)
        mask = lambda pads: sorted(p[:-1] + (('*',) if (ref, p[0]) in allowed_pads else (p[-1],)) for p in pads)
        if mask(f['pads']) != mask(g['pads']):
            problems.append('pad geometry or net changed: ' + ref)
    compare('zone', bz, nz, problems)
    allowed = set(args.allow_change)
    reviewed = {}
    for u in sorted(allowed):
        # A text's bounding box follows its string; everything else that
        # places or sizes it must be unchanged. Only texts may be allowed.
        if u in bd and u in nd and 'text' in bd[u] and \
                {k: v for k, v in bd[u].items() if k not in ('text', 'bbox')} == \
                {k: v for k, v in nd[u].items() if k not in ('text', 'bbox')}:
            reviewed[u] = {'drawing_text': [bd[u].get('text'), nd[u].get('text')]}
            nd[u] = bd[u]
        elif u in bi and u in ni and not bi[u]['via'] and \
                {k: v for k, v in bi[u].items() if k != 'width'} == {k: v for k, v in ni[u].items() if k != 'width'}:
            reviewed[u] = {'net': bi[u]['net'], 'width_mm': [bi[u]['width'] / 1e6, ni[u]['width'] / 1e6]}
            ni[u] = bi[u]
        else:
            problems.append('--allow-change item missing or changed beyond width/text: ' + u)
    compare('drawing', bd, nd, problems)
    changed = [u for u in bi if u in ni and bi[u] != ni[u]]
    # The gap closer joins a new route to a track by splitting it at the
    # junction: the original keeps its identity but becomes a piece of
    # itself, on the same line, layer, width and net.
    trimmed = [u for u in changed if is_piece(bi[u], ni[u])]
    problems += ['copper changed in place: %s %s' % (u, bi[u]['net']) for u in changed if u not in trimmed]
    fresh = [r for u, r in ni.items() if u not in bi]
    split, shortened = {}, {}
    for u in trimmed:
        pieces = [ni[u]] + [r for r in fresh if is_piece(bi[u], r)]
        if covered(bi[u], pieces):
            split[u] = bi[u]['net']
        else:
            shortened[u] = {'net': bi[u]['net'], 'base_mm': round(length(bi[u]), 4),
                            'kept_mm': round(length(ni[u]), 4)}
            if bi[u]['net'] not in args.allow_net:
                problems.append('copper shortened on a protected net: %s %s' % (u, bi[u]['net']))
    removed = [u for u in bi if u not in ni]
    removed_by_net = collections.Counter(bi[u]['net'] for u in removed)
    problems += ['copper removed from a protected net: %s (%d items)' % (n, c)
                 for n, c in sorted(removed_by_net.items()) if n not in args.allow_net]
    added = [u for u in ni if u not in bi]
    added_by_net = collections.defaultdict(collections.Counter)
    added_length = collections.Counter()
    for u in added:
        r = ni[u]
        added_by_net[r['net']]['via' if r['via'] else r['layer']] += 1
        if not r['via']:
            added_length[r['net']] += length(r)
    report = {
        'base_rev': args.base_rev,
        'board_sha256': hashlib.sha256(args.board.read_bytes()).hexdigest(),
        'allow_net': sorted(args.allow_net), 'pad_net_change': sorted(args.pad_net_change),
        **({'reviewed_changes': reviewed} if args.allow_change else {}),
        'base_footprints': len(bf), 'footprints': len(nf), 'added_footprints': sorted(set(nf) - set(bf)),
        'zones_compared': len(bz), 'drawings_compared': len(bd),
        'base_tracks_and_vias': len(bi), 'tracks_and_vias': len(ni),
        'base_items_kept_unchanged': len(bi) - len(removed) - len(changed)
                                     - sum(1 for u in reviewed if u in bi),
        **({'base_items_changed_reviewed': sum(1 for u in reviewed if u in bi)} if args.allow_change else {}),
        'base_items_split_and_covered': dict(sorted(split.items())),
        'base_items_shortened': dict(sorted(shortened.items())),
        'base_items_removed': len(removed), 'base_items_removed_by_net': dict(sorted(removed_by_net.items())),
        'base_removed_and_shortened_mm_by_net': {
            n: round(sum(length(bi[u]) for u in removed if bi[u]['net'] == n and not bi[u]['via'])
                     + sum(s['base_mm'] - s['kept_mm'] for s in shortened.values() if s['net'] == n), 3)
            for n in sorted(set(removed_by_net) | {s['net'] for s in shortened.values()})},
        'items_added': len(added),
        'added_by_net': {n: dict(c) for n, c in sorted(added_by_net.items())},
        'added_track_length_mm': {n: round(v, 2) for n, v in sorted(added_length.items())},
        'problems': problems, 'pass': not problems,
    }
    text = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.write_text(text)
    print(text)
    raise SystemExit(0 if report['pass'] else 1)


if __name__ == '__main__':
    main()
