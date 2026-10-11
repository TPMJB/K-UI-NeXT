#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Lay a supply connection the gap closer could not fit, moving signals aside.

A supply net that must stay on the 1 oz outer layers (generate_board's
OUTER_ONLY_NETS) can be left open when the autorouter has filled F.Cu and
B.Cu with signals along every way between its ends. This lays each of the
net's open connections at a supply width on F.Cu/B.Cu (through vias at the
POWER netclass size), letting the grid search cross other nets' unlocked
signal copper at a price per cell. The signal tracks it crosses are cut back
and the vias it crosses removed; supply copper and locked copper are never
touched. The new copper is locked. The signal connections it cut are left
open for tools/reroute_finished.sh to put back (Freerouting rips up and
reroutes around the locked supply copper).

Native DRC (zones refilled) must show no new violation other than the
dangling leftovers of cut tracks, which tools/remove_dangling_copper.py
clears; otherwise the board is left unchanged. Needs numpy and Pillow
alongside pcbnew (as close_gaps.py).
"""
import argparse
import json
import pathlib
import shutil
import tempfile

import numpy as np
import pcbnew

import close_gaps as cg


def crossed(path, net, soft, soft_items, width):
    """Unlocked signal items the path runs through, each with the points."""
    foreign = [(soft[0][l] != 0) & (soft[0][l] != net) for l in cg.ROUTE_LAYERS]
    vforeign = (soft[1] != 0) & (soft[1] != net)
    hits = []
    for a, b in zip(path, path[1:] + [None]):
        l, i, j = a
        if foreign[l][j, i]:
            hits.append((cg.ROUTE_LAYERS[l], cg.to_nm(i, j), cg.CLEAR + width / 2 + cg.SLACK + cg.G))
        if b is not None and b[0] != l and vforeign[j, i]:
            hits.append((None, cg.to_nm(i, j), cg.CLEAR + cg.POWER_VIA[0] / 2 + cg.SLACK + cg.G))
    found = {}
    for layer, pt, acc in hits:
        for it in soft_items:
            if it.GetNetCode() == net or (layer is not None and not it.IsOnLayer(layer)):
                continue
            box = it.GetBoundingBox()
            box.Inflate(pcbnew.FromMM(acc))
            if box.Contains(pt) and it.HitTest(pt, pcbnew.FromMM(acc)):
                found.setdefault(it.m_Uuid.AsString(), (it, []))[1].append(pt)
    return list(found.values())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=cg.BOARD)
    ap.add_argument('--net', required=True)
    ap.add_argument('--width', type=float, action='append', help='mm, widest first (default 0.4 then 0.3)')
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    widths = args.width or [0.4, 0.3]
    cg.load_rules(args.board)
    pro = args.board.with_suffix('.kicad_pro')
    rules_before = pro.read_bytes()
    work = pathlib.Path(tempfile.mkdtemp())
    for extra in (pro, args.board.parent / 'fp-lib-table'):
        shutil.copy(extra, work / extra.name)
    if (args.board.parent / 'KUI_Footprints.pretty').exists():
        shutil.copytree(args.board.parent / 'KUI_Footprints.pretty', work / 'KUI_Footprints.pretty')
    scratch = work / args.board.name
    board = pcbnew.LoadBoard(str(args.board))
    pcbnew.SaveBoard(str(scratch), board)
    before = cg.drc(scratch, work)
    baseline = cg.hard_violations(before)
    nets = {n.GetNetname(): n.GetNetCode() for n in board.GetNetInfo().NetsByNetcode().values()}
    net = nets[args.net]
    protected = cg.protected_nets(args.board)
    pairs = [p for p in cg.open_pairs(before) if p[1] == args.net]
    if not pairs:
        raise SystemExit('%s has no open connection.' % args.net)
    rippable = lambda it: (it.Type() in (pcbnew.PCB_TRACE_T, pcbnew.PCB_VIA_T) and not it.IsLocked()
                           and it.GetNetname() not in protected)
    records = []
    for dist, name, pa, pb in pairs:
        path = None
        for width in widths:
            track, via, soft_track, soft_via, soft_items = cg.build(board, rippable, width, cg.POWER_VIA)
            sa = {c for c in cg.item_cells(board, pa, net) if c[0] in cg.OUTER_INDICES}
            sb = {c for c in cg.item_cells(board, pb, net) if c[0] in cg.OUTER_INDICES}
            if not sa or not sb:
                continue
            hist = [np.zeros((cg.NY, cg.NX), np.int32) for _ in cg.ROUTE_LAYERS]
            path = cg.astar(track, via, net, sa, sb, (0, 0, cg.NX - 1, cg.NY - 1), cg.OUTER_INDICES,
                            (soft_track, soft_via), hist)
            if path is not None:
                break
        if path is None:
            raise SystemExit('%s: no way between %s and %s even through signal copper.' % (name, pa, pb))
        victims = crossed(path, net, (soft_track, soft_via), soft_items, width)
        added = []
        cg.stub(board, pcbnew.VECTOR2I(pcbnew.FromMM(pa[0]), pcbnew.FromMM(pa[1])), path[0], path[0][0], net, added, width)
        cg.add_path(board, path, net, added, width, cg.POWER_VIA)
        cg.stub(board, pcbnew.VECTOR2I(pcbnew.FromMM(pb[0]), pcbnew.FromMM(pb[1])), path[-1], path[-1][0], net, added, width)
        for it in added:
            it.SetLocked(True)
        for it, pts in victims:
            if it.Type() == pcbnew.PCB_TRACE_T:
                cg.cut(board, it, pts)
            else:
                board.Remove(it)
        rec = {'net': name, 'from': pa, 'to': pb, 'width_mm': width,
               'length_mm': round(sum(pcbnew.ToMM(t.GetLength()) for t in added if t.Type() == pcbnew.PCB_TRACE_T), 1),
               'vias': sum(1 for t in added if t.Type() == pcbnew.PCB_VIA_T),
               'cut': sorted({it.GetNetname() for it, _ in victims}), 'uuids': [t.m_Uuid.AsString() for t in added]}
        records.append(rec)
        print(json.dumps({k: v for k, v in rec.items() if k != 'uuids'}), flush=True)
    pcbnew.SaveBoard(str(scratch), board)
    after = cg.drc(scratch, work)
    ours = {u for r in records for u in r['uuids']}
    bad = [v for v in after['violations'] if v['type'] not in cg.CLEANUP_TYPES
           and any(i['uuid'] in ours for i in v['items'])]
    if bad or cg.hard_violations(after) > baseline:
        shutil.rmtree(work)
        raise SystemExit('Supply route fails native DRC; board left unchanged: %s' % json.dumps(
            [(v['type'], v['description']) for v in bad][:10]))
    shutil.rmtree(work)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    if pro.read_bytes() != rules_before:
        pro.write_bytes(rules_before)
        raise SystemExit('Saving changed the project rules file; restored it.')
    summary = {'routes': [{k: v for k, v in r.items() if k != 'uuids'} for r in records],
               'open_before': len(before['unconnected_items']), 'open_after': len(after['unconnected_items'])}
    print(json.dumps({'open_before': summary['open_before'], 'open_after': summary['open_after']}))
    if args.report:
        args.report.write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
