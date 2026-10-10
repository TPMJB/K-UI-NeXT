#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Route the last open connections that the autorouter left behind.

A small grid router for the handful of connections Freerouting gives up on.
Every other-net pad, track, via, hole and the board edge is rasterised from
KiCad's own clearance-inflated outlines onto a 0.05 mm grid per routing layer
(F.Cu, In2.Cu, In3.Cu, B.Cu; In1 is the ground reference and In4 the 3.3 V
plane, both kept free of tracks). Each open pair reported by native DRC is
then joined by an A* search with 45-degree moves and through vias, shortest
pairs first. New copper is added unlocked; the caller must rerun native DRC,
and any route DRC rejects is removed again by --verify. Needs numpy and
Pillow in KiCad 10's Python environment.
"""
import argparse
import heapq
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageDraw
import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
ORIGIN = (70.0, 45.0)
WIDTH, HEIGHT = 59.0, 115.0
G = 0.05                      # grid pitch, mm
TRACK = 0.15                  # routed width, mm
CLEAR = 0.15                  # copper clearance, mm
EDGE = 0.30                   # copper-to-edge clearance, mm
VIA_D, VIA_DRILL = 0.50, 0.20
HOLE_GAP = 0.25               # drilled-hole separation, mm
VIA_COST = 30                 # in grid steps
WEIGHT = 1.6                  # weighted A*: faster, at most 60% longer than optimal
MAX_EXPANSIONS = 8_000_000
SLACK = 0.03                  # quantisation margin, mm
ROUTE_LAYERS = [pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.B_Cu]
NX, NY = int(round(WIDTH / G)), int(round(HEIGHT / G))


def to_cell(x_nm, y_nm):
    return (int(round((pcbnew.ToMM(x_nm) - ORIGIN[0]) / G)), int(round((pcbnew.ToMM(y_nm) - ORIGIN[1]) / G)))


def to_nm(i, j):
    return pcbnew.VECTOR2I(pcbnew.FromMM(ORIGIN[0] + i * G), pcbnew.FromMM(ORIGIN[1] + j * G))


def polys(item, layer, grow):
    ps = pcbnew.SHAPE_POLY_SET()
    item.TransformShapeToPolygon(ps, layer, pcbnew.FromMM(grow), pcbnew.FromMM(0.005), pcbnew.ERROR_OUTSIDE)
    out = []
    for k in range(ps.OutlineCount()):
        o = ps.Outline(k)
        out.append([((pcbnew.ToMM(o.CPoint(n).x) - ORIGIN[0]) / G, (pcbnew.ToMM(o.CPoint(n).y) - ORIGIN[1]) / G)
                    for n in range(o.PointCount())])
    return out


def paint(grid, outlines, net):
    """Mark cells covered by outlines with net; a cell two nets claim is blocked (-1)."""
    for pts in outlines:
        if len(pts) < 3:
            continue
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        x0, y0 = max(0, int(math.floor(min(xs)))), max(0, int(math.floor(min(ys))))
        x1, y1 = min(NX - 1, int(math.ceil(max(xs)))), min(NY - 1, int(math.ceil(max(ys))))
        if x1 < x0 or y1 < y0:
            continue
        img = Image.new('1', (x1 - x0 + 1, y1 - y0 + 1), 0)
        ImageDraw.Draw(img).polygon([(x - x0, y - y0) for x, y in pts], fill=1, outline=1)
        m = np.array(img, dtype=bool)
        region = grid[y0:y1 + 1, x0:x1 + 1]
        clash = m & (region != 0) & (region != net)
        region[m & (region == 0)] = net
        region[clash] = -1


def build(board):
    """Track-blocking grids per routing layer and one via-blocking grid."""
    track = {l: np.zeros((NY, NX), np.int32) for l in ROUTE_LAYERS}
    via = np.zeros((NY, NX), np.int32)
    t_grow = CLEAR + TRACK / 2 + SLACK
    v_grow = CLEAR + VIA_D / 2 + SLACK
    items = [p for f in board.GetFootprints() for p in f.Pads()] + list(board.GetTracks())
    for it in items:
        net = it.GetNetCode() or -1
        for l in ROUTE_LAYERS:
            if not it.IsOnLayer(l):
                continue
            paint(track[l], polys(it, l, t_grow), net)
            paint(via, polys(it, l, v_grow), net)
        if it.Type() == pcbnew.PCB_PAD_T and not it.GetDrillSize().x:
            # No via in an SMD land, whatever its net (solder wicking).
            for l in (pcbnew.F_Cu, pcbnew.B_Cu):
                if it.IsOnLayer(l):
                    paint(via, polys(it, l, VIA_D / 2 + 0.05), -1)
        drill = 0.0
        if it.Type() == pcbnew.PCB_VIA_T:
            drill = pcbnew.ToMM(it.GetDrill())
        elif it.Type() == pcbnew.PCB_PAD_T and it.GetDrillSize().x:
            drill = pcbnew.ToMM(min(it.GetDrillSize().x, it.GetDrillSize().y))
        if drill:
            c = it.GetPosition()
            r = (drill / 2 + HOLE_GAP + VIA_DRILL / 2 + SLACK) / G
            cx, cy = (pcbnew.ToMM(c.x) - ORIGIN[0]) / G, (pcbnew.ToMM(c.y) - ORIGIN[1]) / G
            paint(via, [[(cx + r * math.cos(a / 16 * math.pi), cy + r * math.sin(a / 16 * math.pi))
                         for a in range(32)]], -1)
    # Board outline, aperture and notch (and every rule-area keepout).
    edge = np.zeros((NY, NX), bool)
    yy, xx = np.mgrid[0:NY, 0:NX]
    m = (EDGE + TRACK / 2 + SLACK) / G
    edge |= (xx < m) | (yy < m) | (xx > NX - m) | (yy > NY - m)
    for x0, y0, x1, y1 in [(6.0, 61.0, 35.0, 80.0), (43.8, 52.0, 59.0, 99.0)]:
        edge |= (xx > x0 / G - m) & (xx < x1 / G + m) & (yy > y0 / G - m) & (yy < y1 / G + m)
    vm = (EDGE + VIA_D / 2 + SLACK) / G
    vedge = (xx < vm) | (yy < vm) | (xx > NX - vm) | (yy > NY - vm)
    for x0, y0, x1, y1 in [(6.0, 61.0, 35.0, 80.0), (43.8, 52.0, 59.0, 99.0)]:
        vedge |= (xx > x0 / G - vm) & (xx < x1 / G + vm) & (yy > y0 / G - vm) & (yy < y1 / G + vm)
    for l in ROUTE_LAYERS:
        track[l][edge] = -1
    via[vedge] = -1
    return track, via


def astar(track, via, net, starts, goals, box):
    """starts/goals: sets of (layer_index, i, j)."""
    x0, y0, x1, y1 = box
    free = [(t == 0) | (t == net) for t in (track[l] for l in ROUTE_LAYERS)]
    vfree = (via == 0) | (via == net)
    gx = [g[1] for g in goals]
    gy = [g[2] for g in goals]
    gcx, gcy = sum(gx) / len(gx), sum(gy) / len(gy)
    open_, best, came = [], {}, {}
    for s in starts:
        best[s] = 0.0
        heapq.heappush(open_, (WEIGHT * math.hypot(s[1] - gcx, s[2] - gcy), 0.0, s))
    steps = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
             (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
    expanded = 0
    while open_:
        _, g, s = heapq.heappop(open_)
        if g > best.get(s, 1e18):
            continue
        if s in goals:
            path = [s]
            while path[-1] in came:
                path.append(came[path[-1]])
            return path[::-1]
        expanded += 1
        if expanded > MAX_EXPANSIONS:
            return None
        l, i, j = s
        moves = []
        for di, dj, c in steps:
            ni, nj = i + di, j + dj
            if x0 <= ni <= x1 and y0 <= nj <= y1 and free[l][nj, ni]:
                moves.append(((l, ni, nj), c))
        if vfree[j, i]:
            for nl in range(len(ROUTE_LAYERS)):
                if nl != l and free[nl][j, i]:
                    moves.append(((nl, i, j), VIA_COST))
        for n, c in moves:
            ng = g + c
            if ng < best.get(n, 1e18):
                best[n] = ng
                came[n] = s
                heapq.heappush(open_, (ng + WEIGHT * math.hypot(n[1] - gcx, n[2] - gcy), ng, n))
    return None


def item_cells(board, desc_pos, net):
    """Grid states for the copper item DRC named at this position."""
    x, y = desc_pos
    p = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
    i, j = to_cell(p.x, p.y)
    cells = set()
    for li, l in enumerate(ROUTE_LAYERS):
        for it in [q for f in board.GetFootprints() for q in f.Pads()] + list(board.GetTracks()):
            if it.GetNetCode() == net and it.IsOnLayer(l) and it.HitTest(p):
                cells.add((li, i, j))
                break
    return cells


def add_path(board, path, net, records):
    nets = board.GetNetInfo().GetNetItem(net)
    def heading(a, b):
        di, dj = b[1] - a[1], b[2] - a[2]
        n = max(abs(di), abs(dj)) or 1
        return (a[0] == b[0], di // n if di % n == 0 else di, dj // n if dj % n == 0 else dj)

    pts = []
    for s in path:
        # Extend the current straight run while layer and unit heading hold.
        if len(pts) >= 2 and pts[-1][0] == s[0] == pts[-2][0] and \
                heading(pts[-2], pts[-1]) == heading(pts[-1], s):
            pts[-1] = s
            continue
        pts.append(s)
    for a, b in zip(pts, pts[1:]):
        if a[0] != b[0]:
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(to_nm(a[1], a[2]))
            v.SetWidth(pcbnew.FromMM(VIA_D))
            v.SetDrill(pcbnew.FromMM(VIA_DRILL))
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(nets)
            board.Add(v)
            records.append(v)
        elif (a[1], a[2]) != (b[1], b[2]):
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(to_nm(a[1], a[2]))
            t.SetEnd(to_nm(b[1], b[2]))
            t.SetWidth(pcbnew.FromMM(TRACK))
            t.SetLayer(ROUTE_LAYERS[a[0]])
            t.SetNet(nets)
            board.Add(t)
            records.append(t)


def stub(board, at, cell, layer_index, net, records):
    """Join an item's real position to its grid point."""
    end = to_nm(cell[1], cell[2])
    if end == at:
        return
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(at)
    t.SetEnd(end)
    t.SetWidth(pcbnew.FromMM(TRACK))
    t.SetLayer(ROUTE_LAYERS[layer_index])
    t.SetNet(board.GetNetInfo().GetNetItem(net))
    board.Add(t)
    records.append(t)


def drc(board_path, work):
    cli = os.environ.get('KICAD10_CLI', 'kicad-cli')
    out = work / 'drc.json'
    subprocess.run([cli, 'pcb', 'drc', '--format', 'json', '-o', str(out), str(board_path)],
                   check=True, capture_output=True)
    return json.loads(out.read_text())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--margin', type=float, default=20.0, help='Search box margin around a pair, mm')
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    work = pathlib.Path(tempfile.mkdtemp())
    scratch = work / args.board.name
    for extra in (args.board.with_suffix('.kicad_pro'), args.board.parent / 'fp-lib-table'):
        if extra.exists():
            shutil.copy(extra, work / extra.name)
    if (args.board.parent / 'KUI_Footprints.pretty').exists():
        shutil.copytree(args.board.parent / 'KUI_Footprints.pretty', work / 'KUI_Footprints.pretty')
    board = pcbnew.LoadBoard(str(args.board))
    pcbnew.SaveBoard(str(scratch), board)
    report = drc(scratch, work)
    nets = {n.GetNetname(): n.GetNetCode() for n in board.GetNetInfo().NetsByNetcode().values()}
    pairs = []
    for u in report['unconnected_items']:
        a, b = u['items'][:2]
        name = re.search(r'\[([^\]]+)\]', a['description']).group(1)
        pa, pb = (a['pos']['x'], a['pos']['y']), (b['pos']['x'], b['pos']['y'])
        pairs.append((math.dist(pa, pb), name, pa, pb))
    pairs.sort()
    track, via = build(board)
    results, graveyard = [], []
    for dist, name, pa, pb in pairs:
        net = nets[name]
        sa, sb = item_cells(board, pa, net), item_cells(board, pb, net)
        if not sa or not sb:
            results.append({'net': name, 'routed': False, 'reason': 'endpoint not found'})
            continue
        ia, ja = to_cell(pcbnew.FromMM(pa[0]), pcbnew.FromMM(pa[1]))
        ib, jb = to_cell(pcbnew.FromMM(pb[0]), pcbnew.FromMM(pb[1]))
        m = int(args.margin / G)
        box = (max(0, min(ia, ib) - m), max(0, min(ja, jb) - m), min(NX - 1, max(ia, ib) + m), min(NY - 1, max(ja, jb) + m))
        path = astar(track, via, net, sa, sb, box)
        if path is None:
            results.append({'net': name, 'routed': False, 'reason': 'no path', 'length_mm': round(dist, 2)})
            continue
        added = []
        stub(board, pcbnew.VECTOR2I(pcbnew.FromMM(pa[0]), pcbnew.FromMM(pa[1])), path[0], path[0][0], net, added)
        add_path(board, path, net, added)
        stub(board, pcbnew.VECTOR2I(pcbnew.FromMM(pb[0]), pcbnew.FromMM(pb[1])), path[-1], path[-1][0], net, added)
        for it in added:
            for l in ROUTE_LAYERS:
                if it.IsOnLayer(l):
                    paint(track[l], polys(it, l, CLEAR + TRACK / 2 + SLACK), net)
                    paint(via, polys(it, l, CLEAR + VIA_D / 2 + SLACK), net)
        results.append({'net': name, 'routed': True, 'items': len(added), 'length_mm': round(dist, 2),
                        'uuids': [it.m_Uuid.AsString() for it in added]})
        print(json.dumps({'net': name, 'routed': True, 'items': len(added)}), flush=True)
    # Verify with native DRC; remove any route that introduced a violation.
    pcbnew.SaveBoard(str(scratch), board)
    check = drc(scratch, work)
    bad = {i['uuid'] for v in check['violations'] for i in v['items']}
    by_uuid = {t.m_Uuid.AsString(): t for t in board.GetTracks()}
    for r in results:
        if r.get('routed') and bad & set(r['uuids']):
            for u in r['uuids']:
                if u in by_uuid:
                    board.Remove(by_uuid[u])
                    graveyard.append(by_uuid[u])
            r['routed'] = False
            r['reason'] = 'rejected by DRC'
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    summary = {'pairs': len(pairs), 'routed': sum(1 for r in results if r.get('routed')),
               'failed': [(r['net'], r.get('reason')) for r in results if not r.get('routed')]}
    if args.report:
        args.report.write_text(json.dumps({'summary': summary, 'routes': results}, indent=2) + '\n')
    print(json.dumps(summary))
    shutil.rmtree(work)


if __name__ == '__main__':
    main()
