#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Lay the high-current supply trunks before signal routing, and lock them.

The autorouter gives every supply connection the same narrow escape width,
and the widening pass afterwards can only grow copper where signals left room.
On the carrier that left the 1.3 A input path (J20 -> F20 -> Q20) at
0.25-0.30 mm, about 25 C over ambient at the normal load. This lays each
trunk below with close_gaps.py's grid search on the placed, not yet routed
board: widest width first, on the given layers only, with POWER-class vias
where a layer change is allowed. The trunk input path changes no layer, so it
carries no via. Each trunk ends inside its pads; at a fine-pitch pin where no
trunk width fits, the trunk stops within NECK_MAX_MM and a NECK_MM-wide neck
on the pad's layer finishes the connection (rated by check_power_copper.py
as a pad escape). Paths are straightened wherever the grid shows the
straight line clear. Trunk copper
is locked so the autorouter treats it as fixed; native DRC (zones refilled)
must report no violation on it, otherwise the tool fails. Needs numpy and
Pillow alongside pcbnew (as close_gaps.py).
"""
import argparse
import json
import pathlib
import shutil
import tempfile

import numpy as np
import pcbnew

import close_gaps as cg

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
F, B = 0, 3   # close_gaps.ROUTE_LAYERS indices of F.Cu and B.Cu
NECK_MM = 0.25
NECK_MAX_MM = 1.5
# net, from, to, widths (mm, widest first), allowed layer indices, why.
TRUNKS = [
    ('CONSOLE_5V', 'J20.1', 'F20.1', (1.0, 0.8, 0.6), {F}, '1.3 A input to the fuse'),
    ('FUSED_5V', 'F20.2', 'Q20.3', (1.0, 0.8, 0.6), {F}, '1.3 A fuse to reverse-polarity FET'),
    ('FUSED_5V', 'F20.2', 'D21.1', (0.8, 0.6, 0.5), {F}, 'TVS clamp path, fault current until F20 opens'),
    ('PRIMARY_5V', 'Q20.2', 'U20.3', (0.5, 0.4), {F}, '0.75 A into the power mux'),
    ('PRIMARY_5V', 'Q20.2', 'C235.1', (0.6, 0.5, 0.4), {F, B}, '0.6 A towards the C5 load switch'),
    ('PRIMARY_5V', 'C235.1', 'U71.1', (0.6, 0.5, 0.4), {F, B}, 'C5 load switch input, through its capacitor'),
    ('PRIMARY_5V', 'Q20.2', 'C200.1', (0.8, 0.6, 0.5), {F}, 'input bulk capacitor'),
]


# Local mm (x0, y0, x1, y1) no trunk may enter: U36's pin rows and their
# escapes towards J51. Every pin of that 0.65 mm-pitch buffer must leave
# straight out and drop through a via beside it; a supply trunk under it
# (even on B.Cu, which blocks those vias) cut off the FPGA control lines.
KEEPOUT = [(25.0, 83.0, 36.3, 95.0)]


# Plane feeds: net, pad, number of POWER vias beside the pad (never inside
# an SMD land, where an open via would draw solder from the joint), stub
# width, why. Each via joins the pad by its own short stub on the pad layer.
FEEDS = [
    ('+3V3_LOGIC', 'L20.2', 3, 0.6, 'converter output into the In4 plane; the vias share its short-circuit current'),
]
FEED_RADIUS_MM = 2.5


def pads_of(board, spec):
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    out = []
    for part in spec.split('+'):
        ref, num = part.split('.')
        matches = [p for p in fps[ref].Pads() if p.GetNumber() == num]
        if len(matches) != 1:
            raise ValueError('%s: expected one pad %s' % (spec, num))
        out.append(matches[0])
    return out


def end_cells(grid_track, pads, net, allowed):
    """Free grid states inside the pads (or, for a shared end, on the line
    between their centres), on allowed copper layers the pads are on."""
    cells = set()
    for li in allowed:
        layer = cg.ROUTE_LAYERS[li]
        if not all(p.IsOnLayer(layer) for p in pads):
            continue
        grid = grid_track[layer]
        if len(pads) == 1:
            mask = np.zeros_like(grid)
            cg.paint(mask, cg.polys(pads[0], layer, 0.0), 1)
            js, is_ = np.nonzero((mask == 1) & ((grid == 0) | (grid == net)))
            cells |= {(li, int(i), int(j)) for i, j in zip(is_, js)}
        else:
            a, b = pads[0].GetPosition(), pads[-1].GetPosition()
            for k in range(11):
                x = a.x + (b.x - a.x) * k // 10
                y = a.y + (b.y - a.y) * k // 10
                i, j = cg.to_cell(x, y)
                if grid[j, i] in (0, net):
                    cells.add((li, i, j))
    return cells


def neck(board, pads, net, allowed, wide_track):
    """Grid path on the pad's layer from inside the pad to the nearest cell
    where the wide trunk fits, or None."""
    narrow_track, narrow_via = cg.build(board, None, NECK_MM, cg.POWER_VIA)
    layers = {li for li in allowed if all(p.IsOnLayer(cg.ROUTE_LAYERS[li]) for p in pads)}
    start = end_cells(narrow_track, pads, net, layers)
    if not start:
        return None, None
    c = pads[0].GetPosition()
    ci, cj = cg.to_cell(c.x, c.y)
    r = int(NECK_MAX_MM / cg.G)
    goals = set()
    for li in layers:
        grid = wide_track[cg.ROUTE_LAYERS[li]]
        js, is_ = np.nonzero((grid[cj - r:cj + r + 1, ci - r:ci + r + 1] == 0) |
                             (grid[cj - r:cj + r + 1, ci - r:ci + r + 1] == net))
        goals |= {(li, int(i) + ci - r, int(j) + cj - r) for i, j in zip(is_, js)
                  if (i - r) ** 2 + (j - r) ** 2 <= r * r}
    if not goals:
        return None, None
    box = (max(0, ci - r), max(0, cj - r), min(cg.NX - 1, ci + r), min(cg.NY - 1, cj + r))
    path = cg.astar(narrow_track, narrow_via, net, start, goals, box, layers)
    return (cg.simplify(path, narrow_track, net) if path else None), narrow_track


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    cg.load_rules(args.board)
    board = pcbnew.LoadBoard(str(args.board))
    pro = args.board.with_suffix('.kicad_pro')
    rules_before = pro.read_bytes()
    nets = {n.GetNetname(): n.GetNetCode() for n in board.GetNetInfo().NetsByNetcode().values()}
    records = []
    for name, a, b, widths, allowed, why in TRUNKS:
        net = nets[name]
        pa, pb = pads_of(board, a), pads_of(board, b)
        for p in pa + pb:
            if p.GetNetname() != name:
                raise ValueError('%s is on %s, not %s' % (p.GetParent().GetReference(), p.GetNetname(), name))
        done = None
        for width in widths:
            track, via = cg.build(board, None, width, cg.POWER_VIA)
            for x0, y0, x1, y1 in KEEPOUT:
                win = (slice(int(y0 / cg.G), int(y1 / cg.G) + 1), slice(int(x0 / cg.G), int(x1 / cg.G) + 1))
                via[win] = -1
                for layer in cg.ROUTE_LAYERS:
                    track[layer][win] = -1
            sa, sb = end_cells(track, pa, net, allowed), end_cells(track, pb, net, allowed)
            necks, reachable = [], True
            for pads, cells in ((pa, sa), (pb, sb)):
                if not cells:
                    approach, _ = neck(board, pads, net, allowed, track)
                    if approach is None:
                        reachable = False
                        break
                    cells.add(approach[-1])
                    necks.append(approach)
            if not reachable:
                continue
            xs = [c[1] for c in sa | sb]
            ys = [c[2] for c in sa | sb]
            m = int(12.0 / cg.G)
            box = (max(0, min(xs) - m), max(0, min(ys) - m), min(cg.NX - 1, max(xs) + m), min(cg.NY - 1, max(ys) + m))
            path = cg.astar(track, via, net, sa, sb, box, allowed)
            if path is None:
                continue
            added = []
            cg.add_path(board, cg.simplify(path, track, net), net, added, width, cg.POWER_VIA)
            for n in necks:
                cg.add_path(board, n, net, added, NECK_MM, cg.POWER_VIA)
            for it in added:
                it.SetLocked(True)
            done = {'net': name, 'from': a, 'to': b, 'width_mm': width, 'why': why, 'necks': len(necks),
                    'segments': sum(1 for it in added if it.Type() == pcbnew.PCB_TRACE_T),
                    'vias': sum(1 for it in added if it.Type() == pcbnew.PCB_VIA_T),
                    'length_mm': round(sum(pcbnew.ToMM(it.GetLength()) for it in added
                                           if it.Type() == pcbnew.PCB_TRACE_T), 2),
                    'uuids': [it.m_Uuid.AsString() for it in added]}
            break
        if done is None:
            raise SystemExit('%s %s -> %s: no route at any of %s mm' % (name, a, b, widths))
        records.append(done)
        print(json.dumps({k: v for k, v in done.items() if k != 'uuids'}), flush=True)
    for name, spec, count, width, why in FEEDS:
        net = nets[name]
        pads = pads_of(board, spec)
        placed = []
        for _ in range(count):
            track, via = cg.build(board, None, width, cg.POWER_VIA)
            for x0, y0, x1, y1 in KEEPOUT:
                win = (slice(int(y0 / cg.G), int(y1 / cg.G) + 1), slice(int(x0 / cg.G), int(x1 / cg.G) + 1))
                via[win] = -1
                for layer in cg.ROUTE_LAYERS:
                    track[layer][win] = -1
            start = end_cells(track, pads, net, {F})
            c = pads[0].GetPosition()
            ci, cj = cg.to_cell(c.x, c.y)
            r = int(FEED_RADIUS_MM / cg.G)
            window = (slice(max(0, cj - r), cj + r + 1), slice(max(0, ci - r), ci + r + 1))
            ok = (((via[window] == 0) | (via[window] == net)) &
                  ((track[cg.ROUTE_LAYERS[F]][window] == 0) | (track[cg.ROUTE_LAYERS[F]][window] == net)))
            js, is_ = np.nonzero(ok)
            goals = {(F, int(i) + window[1].start, int(j) + window[0].start) for i, j in zip(is_, js)
                     if (i + window[1].start - ci) ** 2 + (j + window[0].start - cj) ** 2 <= r * r}
            if not start or not goals:
                break
            box = (max(0, ci - r), max(0, cj - r), min(cg.NX - 1, ci + r), min(cg.NY - 1, cj + r))
            path = cg.astar(track, via, net, start, goals, box, {F})
            if path is None:
                break
            added = []
            cg.add_path(board, cg.simplify(path, track, net), net, added, width, cg.POWER_VIA)
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(cg.to_nm(path[-1][1], path[-1][2]))
            v.SetWidth(pcbnew.FromMM(cg.POWER_VIA[0]))
            v.SetDrill(pcbnew.FromMM(cg.POWER_VIA[1]))
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(board.GetNetInfo().GetNetItem(net))
            board.Add(v)
            added.append(v)
            for it in added:
                it.SetLocked(True)
            placed.extend(added)
        if sum(1 for it in placed if it.Type() == pcbnew.PCB_VIA_T) < count:
            raise SystemExit('%s %s: placed %d of %d feed vias' % (name, spec, sum(1 for it in placed if it.Type() == pcbnew.PCB_VIA_T), count))
        records.append({'net': name, 'from': spec, 'to': 'plane', 'width_mm': width, 'why': why,
                        'vias': count, 'uuids': [it.m_Uuid.AsString() for it in placed]})
        print(json.dumps({'net': name, 'feed': spec, 'vias': count, 'stub_width_mm': width}), flush=True)
    # Native DRC with refilled zones must accept every trunk item.
    work = pathlib.Path(tempfile.mkdtemp())
    for extra in (pro, args.board.parent / 'fp-lib-table'):
        shutil.copy(extra, work / extra.name)
    if (args.board.parent / 'KUI_Footprints.pretty').exists():
        shutil.copytree(args.board.parent / 'KUI_Footprints.pretty', work / 'KUI_Footprints.pretty')
    scratch = work / args.board.name
    pcbnew.SaveBoard(str(scratch), board)
    check = cg.drc(scratch, work)
    ours = {u for r in records for u in r['uuids']}
    bad = [v for v in check['violations'] if any(i['uuid'] in ours for i in v['items'])
           and v['type'] not in cg.CLEANUP_TYPES]
    shutil.rmtree(work)
    if bad:
        raise SystemExit('Trunk copper fails native DRC: %s' % json.dumps(
            [(v['type'], v['description']) for v in bad][:10]))
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    if pro.read_bytes() != rules_before:
        pro.write_bytes(rules_before)
        raise SystemExit('Saving changed the project rules file; restored it.')
    if args.report:
        args.report.write_text(json.dumps({'trunks': [{k: v for k, v in r.items() if k != 'uuids'} for r in records]},
                                          indent=2) + '\n')


if __name__ == '__main__':
    main()
