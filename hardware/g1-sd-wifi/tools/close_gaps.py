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

With --rip the search may also cross other nets' unlocked signal copper at a
price per grid cell (never pads, locked copper, ground or POWER-class nets).
The crossed segments are cut back on both sides of the new route and their
two ends become open pairs for the next iteration; a per-cell history price
grows wherever copper is ripped, so repeated fights over one spot get
expensive. Each iteration ends with native DRC: any route that DRC rejects
is removed, and the best DRC-clean state (fewest open pairs) is the one
saved.
"""
import argparse
import ctypes
import hashlib
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

from generate_board import OUTER_ONLY_NETS as OUTER_ONLY

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
ORIGIN = (70.0, 45.0)
WIDTH, HEIGHT = 59.0, 115.0
G = 0.05                      # grid pitch, mm
TRACK = 0.15                  # routed width, mm (load_rules: Default class)
POWER_TRACKS = (0.4, 0.3)     # supply nets: these widths only, widest first
CLEAR = 0.15                  # copper clearance, mm (load_rules: board minimum)
EDGE = 0.30                   # copper-to-edge clearance, mm
VIA_D, VIA_DRILL = 0.50, 0.20
POWER_VIA = (0.60, 0.30)      # supply nets: the POWER netclass via
HOLE_GAP = 0.25               # drilled-hole separation, mm
VIA_COST = 30                 # in grid steps
WEIGHT = 1.6                  # weighted A*: faster, at most 60% longer than optimal
MAX_EXPANSIONS = 8_000_000
SLACK = 0.05                  # quantisation margin, mm (one cell: raster
                              # error <= 0.43 cell, plus 45-degree steps that
                              # clip a pad corner)
RIP_PENALTY = 30              # --rip: price per cell of another net's copper
HIST_WEIGHT = 15              # --rip: added per earlier rip at that cell
RIP_MARGIN = 0.65             # --rip: cut-back beyond the crossing, mm
MAX_VICTIMS = 4               # --rip: most tracks and vias one route may cut
ROUTE_LAYERS = [pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.B_Cu]
OUTER_INDICES = {0, 3}
NX, NY = int(round(WIDTH / G)), int(round(HEIGHT / G))


# The same search in C, compiled on first use when a C compiler is present
# (about a hundred times faster than the Python loop below, which remains
# the fallback). Results are identical up to tie-breaking.
C_ASTAR = r"""
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float f, g; int32_t s; } node;
typedef struct { node *a; long n, cap; } heap;

static int push(heap *h, node x) {
    if (h->n == h->cap) {
        long cap = h->cap ? h->cap * 2 : 1 << 16;
        node *a = realloc(h->a, cap * sizeof(node));
        if (!a) return -1;
        h->a = a; h->cap = cap;
    }
    long i = h->n++;
    while (i > 0) {
        long p = (i - 1) / 2;
        if (h->a[p].f <= x.f) break;
        h->a[i] = h->a[p]; i = p;
    }
    h->a[i] = x;
    return 0;
}

static node pop(heap *h) {
    node top = h->a[0], x = h->a[--h->n];
    long i = 0;
    for (;;) {
        long c = 2 * i + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && h->a[c + 1].f < h->a[c].f) c++;
        if (x.f <= h->a[c].f) break;
        h->a[i] = h->a[c]; i = c;
    }
    if (h->n) h->a[i] = x;
    return top;
}

/* Grids are [layer][y][x] (free, price) and [y][x] (vfree, vprice); price
   and vprice may be NULL. starts/goals are (layer, i, j) triples. Returns
   the path length written to out (triples, start first), -1 if no path,
   -2 if out is too small, -3 on allocation failure. */
long astar(int nl, int nx, int ny, const uint8_t *freeg, const uint8_t *vfree,
           const float *price, const float *vprice, int x0, int y0, int x1, int y1,
           int ns, const int32_t *starts, int ng, const int32_t *goals,
           float via_cost, float weight, long max_exp, int32_t *out, long out_cap) {
    int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    long N = (long)nl * bw * bh;
    float *best = malloc(N * sizeof(float));
    int32_t *came = malloc(N * sizeof(int32_t));
    uint8_t *goal = calloc(N, 1);
    heap h = {0};
    long result = -1;
    if (!best || !came || !goal) { result = -3; goto done; }
    for (long k = 0; k < N; k++) best[k] = INFINITY;
    double gcx = 0, gcy = 0;
    for (int k = 0; k < ng; k++) {
        int l = goals[3 * k], i = goals[3 * k + 1], j = goals[3 * k + 2];
        gcx += i; gcy += j;
        if (i >= x0 && i <= x1 && j >= y0 && j <= y1)
            goal[((long)l * bh + (j - y0)) * bw + (i - x0)] = 1;
    }
    gcx /= ng; gcy /= ng;
    for (int k = 0; k < ns; k++) {
        int l = starts[3 * k], i = starts[3 * k + 1], j = starts[3 * k + 2];
        if (i < x0 || i > x1 || j < y0 || j > y1) continue;
        int32_t s = (int32_t)(((long)l * bh + (j - y0)) * bw + (i - x0));
        best[s] = 0; came[s] = -1;
        node n0 = {(float)(weight * hypot(i - gcx, j - gcy)), 0.f, s};
        if (push(&h, n0)) { result = -3; goto done; }
    }
    static const int di[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dj[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float dc[8] = {1, 1, 1, 1, 1.414f, 1.414f, 1.414f, 1.414f};
    long expanded = 0;
    long plane = (long)ny * nx;
    while (h.n) {
        node cur = pop(&h);
        if (cur.g > best[cur.s]) continue;
        if (goal[cur.s]) {
            long len = 0;
            for (int32_t s = cur.s; s != -1; s = came[s]) len++;
            if (len > out_cap) { result = -2; goto done; }
            long k = len;
            for (int32_t s = cur.s; s != -1; s = came[s]) {
                k--;
                int l = s / (bw * bh), r = s % (bw * bh);
                out[3 * k] = l; out[3 * k + 1] = r % bw + x0; out[3 * k + 2] = r / bw + y0;
            }
            result = len;
            goto done;
        }
        if (++expanded > max_exp) break;
        int l = cur.s / (bw * bh), r = cur.s % (bw * bh);
        int i = r % bw + x0, j = r / bw + y0;
        for (int d = 0; d < 8; d++) {
            int ni = i + di[d], nj = j + dj[d];
            if (ni < x0 || ni > x1 || nj < y0 || nj > y1) continue;
            long gi = l * plane + (long)nj * nx + ni;
            if (!freeg[gi]) continue;
            float g = cur.g + dc[d] + (price ? price[gi] : 0.f);
            int32_t s = (int32_t)(((long)l * bh + (nj - y0)) * bw + (ni - x0));
            if (g < best[s]) {
                best[s] = g; came[s] = cur.s;
                node nn = {g + (float)(weight * hypot(ni - gcx, nj - gcy)), g, s};
                if (push(&h, nn)) { result = -3; goto done; }
            }
        }
        long vi = (long)j * nx + i;
        if (vfree[vi]) {
            float vc = via_cost + (vprice ? vprice[vi] : 0.f);
            for (int m = 0; m < nl; m++) {
                if (m == l || !freeg[m * plane + vi]) continue;
                float g = cur.g + vc;
                int32_t s = (int32_t)(((long)m * bh + (j - y0)) * bw + (i - x0));
                if (g < best[s]) {
                    best[s] = g; came[s] = cur.s;
                    node nn = {g + (float)(weight * hypot(i - gcx, j - gcy)), g, s};
                    if (push(&h, nn)) { result = -3; goto done; }
                }
            }
        }
    }
done:
    free(best); free(came); free(goal); free(h.a);
    return result;
}
"""
_LIB = []


def c_astar():
    if not _LIB:
        lib = None
        cache = pathlib.Path(tempfile.gettempdir()) / ('close_gaps_%s.so' % hashlib.sha256(C_ASTAR.encode()).hexdigest()[:16])
        try:
            if not cache.exists():
                src = cache.with_suffix('.c')
                src.write_text(C_ASTAR)
                subprocess.run([os.environ.get('CC', 'cc'), '-O2', '-shared', '-fPIC', '-o', str(cache), str(src), '-lm'],
                               check=True, capture_output=True)
            lib = ctypes.CDLL(str(cache))
            P = ctypes.c_void_p
            lib.astar.restype = ctypes.c_long
            lib.astar.argtypes = [ctypes.c_int] * 3 + [P] * 4 + [ctypes.c_int] * 4 + [ctypes.c_int, P, ctypes.c_int, P,
                                  ctypes.c_float, ctypes.c_float, ctypes.c_long, P, ctypes.c_long]
        except (OSError, subprocess.CalledProcessError):
            lib = None
        _LIB.append(lib)
    return _LIB[0]


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
        # Pillow samples pixel p at p + 0.5; grid cell i is centred on i.
        ImageDraw.Draw(img).polygon([(x - x0 + 0.5, y - y0 + 0.5) for x, y in pts], fill=1, outline=1)
        m = np.array(img, dtype=bool)
        region = grid[y0:y1 + 1, x0:x1 + 1]
        clash = m & (region != 0) & (region != net)
        region[m & (region == 0)] = net
        region[clash] = -1


def build(board, rippable=None, width=TRACK, via_size=(VIA_D, VIA_DRILL)):
    """Track-blocking grids per routing layer and one via-blocking grid.

    Items for which rippable(item) holds are painted into separate soft
    grids instead (returned with the list of those items); without the
    predicate the soft grids are the hard ones.
    """
    track = {l: np.zeros((NY, NX), np.int32) for l in ROUTE_LAYERS}
    via = np.zeros((NY, NX), np.int32)
    soft_track = {l: np.zeros((NY, NX), np.int32) for l in ROUTE_LAYERS} if rippable else track
    soft_via = np.zeros((NY, NX), np.int32) if rippable else via
    soft_items = []
    t_grow = CLEAR + width / 2 + SLACK
    v_grow = CLEAR + via_size[0] / 2 + SLACK
    items = [p for f in board.GetFootprints() for p in f.Pads()] + list(board.GetTracks())
    for it in items:
        net = it.GetNetCode() or -1
        soft = rippable is not None and rippable(it)
        tg, vg = (soft_track, soft_via) if soft else (track, via)
        if soft:
            soft_items.append(it)
        for l in ROUTE_LAYERS:
            if not it.IsOnLayer(l):
                continue
            paint(tg[l], polys(it, l, t_grow), net)
            paint(vg, polys(it, l, v_grow), net)
        if it.Type() == pcbnew.PCB_PAD_T and not it.GetDrillSize().x:
            # No via in or touching an SMD land, whatever its net: an open
            # via there draws solder from the joint.
            for l in (pcbnew.F_Cu, pcbnew.B_Cu):
                if it.IsOnLayer(l):
                    paint(via, polys(it, l, via_size[0] / 2 + 0.1), -1)
        drill = 0.0
        if it.Type() == pcbnew.PCB_VIA_T:
            drill = pcbnew.ToMM(it.GetDrill())
        elif it.Type() == pcbnew.PCB_PAD_T and it.GetDrillSize().x:
            drill = pcbnew.ToMM(min(it.GetDrillSize().x, it.GetDrillSize().y))
        if drill:
            c = it.GetPosition()
            r = (drill / 2 + HOLE_GAP + via_size[1] / 2 + SLACK) / G
            cx, cy = (pcbnew.ToMM(c.x) - ORIGIN[0]) / G, (pcbnew.ToMM(c.y) - ORIGIN[1]) / G
            # Hole-to-hole spacing holds between any two holes, same net or
            # not, so every hole stays a hard obstacle for new vias.
            paint(via, [[(cx + r * math.cos(a / 16 * math.pi), cy + r * math.sin(a / 16 * math.pi))
                         for a in range(32)]], -1)
    # Board outline, aperture and notch (and every rule-area keepout).
    edge = np.zeros((NY, NX), bool)
    yy, xx = np.mgrid[0:NY, 0:NX]
    m = (EDGE + width / 2 + SLACK) / G
    edge |= (xx < m) | (yy < m) | (xx > NX - m) | (yy > NY - m)
    for x0, y0, x1, y1 in [(6.0, 61.0, 35.0, 80.0), (43.8, 52.0, 59.0, 99.0)]:
        edge |= (xx > x0 / G - m) & (xx < x1 / G + m) & (yy > y0 / G - m) & (yy < y1 / G + m)
    vm = (EDGE + via_size[0] / 2 + SLACK) / G
    vedge = (xx < vm) | (yy < vm) | (xx > NX - vm) | (yy > NY - vm)
    for x0, y0, x1, y1 in [(6.0, 61.0, 35.0, 80.0), (43.8, 52.0, 59.0, 99.0)]:
        vedge |= (xx > x0 / G - vm) & (xx < x1 / G + vm) & (yy > y0 / G - vm) & (yy < y1 / G + vm)
    for l in ROUTE_LAYERS:
        track[l][edge] = -1
    via[vedge] = -1
    if rippable:
        return track, via, soft_track, soft_via, soft_items
    return track, via


def astar(track, via, net, starts, goals, box, allowed=None, soft=None, hist=None):
    """starts/goals: sets of (layer_index, i, j); allowed: layer indices.

    soft: (soft_track, soft_via) of rippable copper. Without hist it is an
    obstacle; with hist (per-layer rip counts) it may be crossed at
    RIP_PENALTY + HIST_WEIGHT * count per cell.
    """
    x0, y0, x1, y1 = box
    free = [((t == 0) | (t == net)) if allowed is None or k in allowed else np.zeros_like(t, dtype=bool)
            for k, t in enumerate(track[l] for l in ROUTE_LAYERS)]
    vfree = (via == 0) | (via == net)
    price = vprice = None
    if soft is not None:
        foreign = [(soft[0][l] != 0) & (soft[0][l] != net) for l in ROUTE_LAYERS]
        vforeign = (soft[1] != 0) & (soft[1] != net)
        if hist is None:
            free = [f & ~s for f, s in zip(free, foreign)]
            vfree = vfree & ~vforeign
        else:
            price = [np.where(s, RIP_PENALTY + HIST_WEIGHT * h, 0).astype(np.float32) for s, h in zip(foreign, hist)]
            vprice = np.where(vforeign, RIP_PENALTY * 4, 0).astype(np.float32)
    lib = c_astar()
    if lib is not None:
        ptr = lambda a: a.ctypes.data_as(ctypes.c_void_p) if a is not None else None
        fg = np.ascontiguousarray(np.stack(free).astype(np.uint8))
        vf = np.ascontiguousarray(vfree.astype(np.uint8))
        pr = np.ascontiguousarray(np.stack(price)) if price else None
        vp = np.ascontiguousarray(vprice) if price else None
        st = np.array(sorted(starts), np.int32).ravel()
        gl = np.array(sorted(goals), np.int32).ravel()
        cap = 400000
        out = np.zeros(3 * cap, np.int32)
        n = lib.astar(len(ROUTE_LAYERS), NX, NY, ptr(fg), ptr(vf), ptr(pr), ptr(vp), x0, y0, x1, y1,
                      len(starts), ptr(st), len(goals), ptr(gl), float(VIA_COST), float(WEIGHT),
                      MAX_EXPANSIONS * 4, ptr(out), cap)
        if n < -1:
            raise RuntimeError('C search failed (%d)' % n)
        return None if n < 0 else [tuple(int(v) for v in out[3 * k:3 * k + 3]) for k in range(n)]
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
                moves.append(((l, ni, nj), c + float(price[l][nj, ni]) if price else c))
        if vfree[j, i]:
            vc = VIA_COST + float(vprice[j, i]) if price else VIA_COST
            for nl in range(len(ROUTE_LAYERS)):
                if nl != l and free[nl][j, i]:
                    moves.append(((nl, i, j), vc))
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


def line_free(grid, net, a, b):
    """Every cell next to the straight line between two grid points is free
    for the net (conservative: the four cells around each half-cell sample)."""
    n = int(max(abs(b[0] - a[0]), abs(b[1] - a[1])) * 2) + 1
    for k in range(n + 1):
        x = a[0] + (b[0] - a[0]) * k / n
        y = a[1] + (b[1] - a[1]) * k / n
        for i in {int(math.floor(x)), int(math.ceil(x))}:
            for j in {int(math.floor(y)), int(math.ceil(y))}:
                v = grid[j, i]
                if v != 0 and v != net:
                    return False
    return True


def simplify(path, track, net):
    """Straighten each single-layer run of a grid path: keep a point only
    where the straight line from the last kept point would hit copper."""
    out = [path[0]]
    k = 0
    while k < len(path) - 1:
        layer = path[k][0]
        far = k + 1
        grid = track[ROUTE_LAYERS[layer]]
        j = k + 1
        while j < len(path) and path[j][0] == layer:
            if line_free(grid, net, path[k][1:], path[j][1:]):
                far = j
            j += 1
        out.append(path[far])
        k = far
    return out


def add_path(board, path, net, records, width=TRACK, via_size=(VIA_D, VIA_DRILL)):
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
            v.SetWidth(pcbnew.FromMM(via_size[0]))
            v.SetDrill(pcbnew.FromMM(via_size[1]))
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(nets)
            board.Add(v)
            records.append(v)
        elif (a[1], a[2]) != (b[1], b[2]):
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(to_nm(a[1], a[2]))
            t.SetEnd(to_nm(b[1], b[2]))
            t.SetWidth(pcbnew.FromMM(width))
            t.SetLayer(ROUTE_LAYERS[a[0]])
            t.SetNet(nets)
            board.Add(t)
            records.append(t)


def stub(board, at, cell, layer_index, net, records, width=TRACK):
    """Join an item's real position to its grid point."""
    end = to_nm(cell[1], cell[2])
    if end == at:
        return
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(at)
    t.SetEnd(end)
    t.SetWidth(pcbnew.FromMM(width))
    t.SetLayer(ROUTE_LAYERS[layer_index])
    t.SetNet(board.GetNetInfo().GetNetItem(net))
    board.Add(t)
    records.append(t)


def drc(board_path, work):
    cli = os.environ.get('KICAD10_CLI', 'kicad-cli')
    out = work / 'drc.json'
    # Zones are refilled so new vias are judged against the planes as they
    # will be filled, not against the stale fill.
    subprocess.run([cli, 'pcb', 'drc', '--refill-zones', '--format', 'json', '-o', str(out), str(board_path)],
                   check=True, capture_output=True)
    return json.loads(out.read_text())


def load_rules(board_path):
    """Take the routed width and clearance from the board's project: the
    Default netclass track width, and the larger of the board minimum and
    every netclass clearance."""
    global TRACK, CLEAR
    pro = board_path.with_suffix('.kicad_pro')
    if not pro.exists():
        raise SystemExit('%s: no project file beside the board; its rules are unknown' % board_path)
    project = json.loads(pro.read_text())
    classes = project['net_settings']['classes']
    TRACK = next(c['track_width'] for c in classes if c['name'] == 'Default')
    CLEAR = max([project['board']['design_settings']['rules']['min_clearance']] + [c['clearance'] for c in classes])
    return TRACK, CLEAR


def protected_nets(board_path):
    """Nets whose copper is never ripped: ground and the POWER netclass."""
    names = {'GND'} | set(OUTER_ONLY)
    pro = board_path.with_suffix('.kicad_pro')
    if pro.exists():
        assigned = json.loads(pro.read_text())['net_settings'].get('netclass_assignments') or {}
        names |= {n for n, classes in assigned.items() if 'POWER' in classes}
    return names


def victims(path, net, soft, soft_items):
    """Rippable track segments the path crosses, each with the crossing points."""
    foreign = [(soft[0][l] != 0) & (soft[0][l] != net) for l in ROUTE_LAYERS]
    vforeign = (soft[1] != 0) & (soft[1] != net)
    hits = []
    for a, b in zip(path, path[1:] + [None]):
        l, i, j = a
        if foreign[l][j, i]:
            hits.append((ROUTE_LAYERS[l], to_nm(i, j), CLEAR + TRACK / 2 + SLACK + G))
        if b is not None and b[0] != l and vforeign[j, i]:
            hits.append((None, to_nm(i, j), CLEAR + VIA_D / 2 + SLACK + G))
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


def cut(board, item, points):
    """Remove the part of a track within RIP_MARGIN of the points. Returns
    the gap left behind as (layer, net code, width, end a, end b)."""
    s, e = item.GetStart(), item.GetEnd()
    dx, dy = e.x - s.x, e.y - s.y
    length = math.hypot(dx, dy)
    a, b = s, e
    if length > 0:
        ts = [((p.x - s.x) * dx + (p.y - s.y) * dy) / length ** 2 for p in points]
        m = pcbnew.FromMM(RIP_MARGIN) / length
        lo, hi = min(ts) - m, max(ts) + m
        tiny = pcbnew.FromMM(0.05) / length
        at = lambda t: pcbnew.VECTOR2I(int(round(s.x + dx * t)), int(round(s.y + dy * t)))
        for p0, p1, keep in ((s, at(lo), lo > tiny), (at(hi), e, hi < 1 - tiny)):
            if keep:
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(p0)
                t.SetEnd(p1)
                t.SetWidth(item.GetWidth())
                t.SetLayer(item.GetLayer())
                t.SetNet(item.GetNet())
                board.Add(t)
        a = at(lo) if lo > tiny else s
        b = at(hi) if hi < 1 - tiny else e
    gap = (item.GetLayer(), item.GetNetCode(), pcbnew.ToMM(item.GetWidth()), a, b)
    board.Remove(item)
    return gap


# Leftover pieces of cut tracks; remove_dangling_copper.py clears them.
CLEANUP_TYPES = {'track_dangling', 'via_dangling'}


def hard_violations(report):
    return sum(1 for v in report['violations'] if v['type'] not in CLEANUP_TYPES)


def open_pairs(report):
    pairs = []
    for u in report['unconnected_items']:
        a, b = u['items'][:2]
        name = re.search(r'\[([^\]]+)\]', a['description']).group(1)
        pa, pb = (a['pos']['x'], a['pos']['y']), (b['pos']['x'], b['pos']['y'])
        pairs.append((math.dist(pa, pb), name, pa, pb))
    return sorted(pairs)


def route_pairs(board, pairs, nets, margin, protected):
    """One sweep over the open pairs without ripping. Returns per-pair results."""
    grids = build(board)
    results = []
    for dist, name, pa, pb in pairs:
        net = nets[name]
        sa, sb = item_cells(board, pa, net), item_cells(board, pb, net)
        if not sa or not sb:
            results.append({'net': name, 'routed': False, 'reason': 'endpoint not found'})
            continue
        ia, ja = to_cell(pcbnew.FromMM(pa[0]), pcbnew.FromMM(pa[1]))
        ib, jb = to_cell(pcbnew.FromMM(pb[0]), pcbnew.FromMM(pb[1]))
        m = int(margin / G)
        box = (max(0, min(ia, ib) - m), max(0, min(ja, jb) - m), min(NX - 1, max(ia, ib) + m), min(NY - 1, max(ja, jb) + m))
        # Supply nets that must stay on the 1 oz outer layers (see
        # generate_board.OUTER_ONLY_NETS) may only use F.Cu and B.Cu.
        allowed = OUTER_INDICES if name in OUTER_ONLY else None
        if allowed is not None:
            sa = {c for c in sa if c[0] in allowed}
            sb = {c for c in sb if c[0] in allowed}
        width = TRACK
        # The margin box keeps searches short; a pair it cannot join is tried
        # once more over the whole board (a detour round a full region).
        boxes = (box, (0, 0, NX - 1, NY - 1))
        path = None
        if name in protected:
            # Supply copper is routed at a supply width or not at all.
            for width in POWER_TRACKS:
                wide = build(board, None, width, POWER_VIA)
                used = wide[0]
                for b in boxes:
                    path = astar(wide[0], wide[1], net, sa, sb, b, allowed)
                    if path is not None:
                        break
                if path is not None:
                    break
        else:
            used = grids[0]
            for b in boxes:
                path = astar(grids[0], grids[1], net, sa, sb, b, allowed)
                if path is not None:
                    break
        if path is None:
            results.append({'net': name, 'routed': False, 'reason': 'no path', 'length_mm': round(dist, 2)})
            continue
        added = []
        stub(board, pcbnew.VECTOR2I(pcbnew.FromMM(pa[0]), pcbnew.FromMM(pa[1])), path[0], path[0][0], net, added, width)
        # Straight runs where the grid allows (fewer, any-angle segments).
        path = simplify(path, used, net)
        add_path(board, path, net, added, width, POWER_VIA if name in protected else (VIA_D, VIA_DRILL))
        stub(board, pcbnew.VECTOR2I(pcbnew.FromMM(pb[0]), pcbnew.FromMM(pb[1])), path[-1], path[-1][0], net, added, width)
        for it in added:
            for l in ROUTE_LAYERS:
                if it.IsOnLayer(l):
                    paint(grids[0][l], polys(it, l, CLEAR + TRACK / 2 + SLACK), net)
                    paint(grids[1], polys(it, l, CLEAR + VIA_D / 2 + SLACK), net)
        results.append({'net': name, 'routed': True, 'items': len(added), 'length_mm': round(dist, 2), 'width_mm': width,
                        'uuids': [it.m_Uuid.AsString() for it in added]})
        print(json.dumps({'net': name, 'routed': True, 'items': len(added)}), flush=True)
    return results


def rip_transaction(board, pair, nets, margin, protected, banned, work, scratch):
    """Route one open pair through other nets' unlocked signal copper (tracks
    and vias), cut that copper back, and reroute whatever native DRC then
    reports open on those nets. Returns (record, None) when every victim
    connection is placed again, else (None, nets to ban next time)."""
    dist, name, pa, pb = pair
    net = nets[name]
    if name in protected:
        return None, set()
    rippable = lambda it: (it.Type() in (pcbnew.PCB_TRACE_T, pcbnew.PCB_VIA_T) and not it.IsLocked()
                           and it.GetNetname() not in protected and it.GetNetname() not in banned)
    track, via, soft_track, soft_via, soft_items = build(board, rippable)
    sa, sb = item_cells(board, pa, net), item_cells(board, pb, net)
    if not sa or not sb:
        return None, set()
    allowed = OUTER_INDICES if name in OUTER_ONLY else None
    ia, ja = to_cell(pcbnew.FromMM(pa[0]), pcbnew.FromMM(pa[1]))
    ib, jb = to_cell(pcbnew.FromMM(pb[0]), pcbnew.FromMM(pb[1]))
    m = int(margin / G)
    box = (max(0, min(ia, ib) - m), max(0, min(ja, jb) - m), min(NX - 1, max(ia, ib) + m), min(NY - 1, max(ja, jb) + m))
    hist = [np.zeros((NY, NX), np.int32) for _ in ROUTE_LAYERS]
    path = astar(track, via, net, sa, sb, box, allowed, (soft_track, soft_via), hist)
    if path is None:
        return None, set()
    crossed = victims(path, net, (soft_track, soft_via), soft_items)
    crossed_nets = {it.GetNetname() for it, _ in crossed}
    if not crossed or len(crossed) > MAX_VICTIMS:
        return None, crossed_nets
    added = []
    stub(board, pcbnew.VECTOR2I(pcbnew.FromMM(pa[0]), pcbnew.FromMM(pa[1])), path[0], path[0][0], net, added)
    add_path(board, simplify(path, track, net), net, added)
    stub(board, pcbnew.VECTOR2I(pcbnew.FromMM(pb[0]), pcbnew.FromMM(pb[1])), path[-1], path[-1][0], net, added)
    graveyard = []
    for it, pts in crossed:
        if it.Type() == pcbnew.PCB_TRACE_T:
            cut(board, it, pts)
        else:
            board.Remove(it)
        graveyard.append(it)
    pcbnew.SaveBoard(str(scratch), board)
    report = drc(scratch, work)
    reopen = [p for p in open_pairs(report) if p[1] in crossed_nets]
    results = route_pairs(board, reopen, nets, margin, protected)
    failed = {r['net'] for r in results if not r.get('routed')}
    if failed:
        return None, failed
    return {'net': name, 'routed': True, 'ripped': sorted(crossed_nets), 'rerouted_pairs': len(reopen),
            'length_mm': round(dist, 2)}, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--margin', type=float, default=20.0, help='Search box margin around a pair, mm')
    ap.add_argument('--rip', action='store_true',
                    help='After the plain sweep, route the rest through other signal tracks and reroute those')
    ap.add_argument('--attempts', type=int, default=4, help='--rip: tries per open pair')
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    load_rules(args.board)
    work = pathlib.Path(tempfile.mkdtemp())
    scratch = work / args.board.name
    # The committed state is kept in its own folder beside a copy of the
    # project: KiCad writes the project file whenever it saves a board, and a
    # board loaded without its project would carry (and save) default rules.
    keep = work / 'best'
    keep.mkdir()
    for folder in (work, keep):
        for extra in (args.board.with_suffix('.kicad_pro'), args.board.parent / 'fp-lib-table'):
            if extra.exists():
                shutil.copy(extra, folder / extra.name)
        if (args.board.parent / 'KUI_Footprints.pretty').exists():
            shutil.copytree(args.board.parent / 'KUI_Footprints.pretty', folder / 'KUI_Footprints.pretty')
    pro = args.board.with_suffix('.kicad_pro')
    rules_before = pro.read_bytes() if pro.exists() else None
    committed = keep / args.board.name
    board = pcbnew.LoadBoard(str(args.board))
    pcbnew.SaveBoard(str(scratch), board)
    report = drc(scratch, work)
    start_open = len(report['unconnected_items'])
    baseline = hard_violations(report)
    nets = {n.GetNetname(): n.GetNetCode() for n in board.GetNetInfo().NetsByNetcode().values()}
    protected = protected_nets(args.board)
    # 1. Plain sweep; any route that native DRC rejects is removed again.
    results = route_pairs(board, open_pairs(report), nets, args.margin, protected)
    pcbnew.SaveBoard(str(scratch), board)
    check = drc(scratch, work)
    bad = {}
    for v in check['violations']:
        for i in v['items']:
            bad.setdefault(i['uuid'], set()).add(v['type'])
    by_uuid = {t.m_Uuid.AsString(): t for t in board.GetTracks()}
    graveyard = []
    for r in results:
        if r.get('routed') and set(bad) & set(r['uuids']):
            for u in r['uuids']:
                if u in by_uuid:
                    board.Remove(by_uuid[u])
                    graveyard.append(by_uuid[u])
            r['routed'] = False
            r['reason'] = 'rejected by DRC: ' + ', '.join(sorted(set().union(*(bad[u] for u in r['uuids'] if u in bad))))
    pcbnew.SaveBoard(str(scratch), board)
    report = drc(scratch, work)
    if hard_violations(report) > baseline:
        raise SystemExit('Plain sweep left new violations; board left unchanged.')
    shutil.copy(scratch, committed)
    print(json.dumps({'plain_sweep_open_after': len(report['unconnected_items'])}), flush=True)
    # 2. Rip-up transactions: each either lowers the open count with no new
    # violation (and is kept) or is discarded by reloading the last state.
    transactions = []
    if args.rip:
        failed_pairs = set()
        progress = True
        while progress and report['unconnected_items']:
            progress = False
            for pair in open_pairs(report):
                key = (pair[1], tuple(round(v, 2) for v in pair[2]), tuple(round(v, 2) for v in pair[3]))
                if key in failed_pairs:
                    continue
                banned = set()
                for attempt in range(args.attempts):
                    board = pcbnew.LoadBoard(str(committed))
                    record, ban = rip_transaction(board, pair, nets, args.margin, protected, banned, work, scratch)
                    if record is None:
                        if not ban:
                            break
                        banned |= ban
                        continue
                    pcbnew.SaveBoard(str(scratch), board)
                    trial = drc(scratch, work)
                    if (len(trial['unconnected_items']) < len(report['unconnected_items'])
                            and hard_violations(trial) <= baseline):
                        shutil.copy(scratch, committed)
                        report = trial
                        record['open_after'] = len(report['unconnected_items'])
                        transactions.append(record)
                        print(json.dumps(record), flush=True)
                        progress = True
                        break
                    banned |= set(record['ripped'])
                else:
                    failed_pairs.add(key)
                if progress:
                    break
                failed_pairs.add(key)
    board = pcbnew.LoadBoard(str(committed))
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    if rules_before is not None and pro.read_bytes() != rules_before:
        pro.write_bytes(rules_before)
        raise SystemExit('Saving changed the project rules file; restored it. Check the KiCad project setup.')
    final = report
    summary = {'open_before': start_open, 'open_after': len(final['unconnected_items']),
               'plain_routed': sum(1 for r in results if r.get('routed')), 'rip_transactions': len(transactions),
               'still_open': sorted({re.search(r'\[([^\]]+)\]', u['items'][0]['description']).group(1)
                                     for u in final['unconnected_items']})}
    if args.report:
        args.report.write_text(json.dumps({'summary': summary, 'plain_sweep': results,
                                           'rip_transactions': transactions}, indent=2) + '\n')
    print(json.dumps(summary))
    shutil.rmtree(work)


if __name__ == '__main__':
    main()
