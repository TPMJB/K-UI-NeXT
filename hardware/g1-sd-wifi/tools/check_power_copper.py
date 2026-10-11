#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Current-capacity check of the routed supply copper (IPC-2221).

Every track segment and via on the supply nets below is rated with the
IPC-2221 chart fit I = k * dT^0.44 * A^0.725 (A in square mils; k = 0.048
for outer layers, 0.024 for inner layers and via barrels), using the copper
weights in manufacturing/rigid-order-settings.json and a conservative via
plating thickness.

Normal current: the copper of each net is turned into a graph (pads, vias and
points on tracks are nodes, track pieces are edges) and solved as a resistor
network (conductance = width x copper thickness / length; vias and pads join
their layers without resistance): the source supplies the loads of the power
budget in design/power-and-isolation.md, and every edge is rated for the
current it then carries. On a tree this is exactly the sum of the loads
beyond each edge; where copper runs in parallel the paths share it as real
copper does. Distribution nets without a load map charge every edge the
whole budget.

Fault current: for trunk nets, each point where a part can realistically
short (a load, a capacitor, a connector, spring contact or test point) is
shorted in turn at the current the upstream fuse or regulator passes
indefinitely, and every edge and via is rated for the largest current any
such short drives through it, so copper outlasts the protection. Branches
that end only at IC sense/control pins or resistors carry no fault current.

Limits: NORMAL_LIMIT_C rise at the normal current, FAULT_LIMIT_C at the fault
current; pad escapes and via barrels are allowed twice the rise because heat
spreads into the copper either side. A pad escape is a run (consecutive
segments of one layer and width, joined only end to end) that starts at a pad
and is no longer than NECK_MM in total; the router splits long tracks into
short segments, so the length of a single segment says nothing.
Outer-only nets fail if any inner-layer segment is longer than NECK_MM. Run
with KiCad 10's Python (pcbnew) with numpy available.
"""
import argparse
import collections
import json
import math
import pathlib

import numpy as np
import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
SETTINGS = ROOT / 'manufacturing' / 'rigid-order-settings.json'
OZ_UM = 34.79
VIA_PLATING_UM = 18.0
NORMAL_LIMIT_C = 10.0
FAULT_LIMIT_C = 45.0
NECK_MM = 2.0
UM_TO_MIL = 1 / 25.4
# net: budget current A, fault current A (None = distribution net, no
# per-leaf fault check), outer layers only, source pads, load pads in A.
NETS = {
    'CONSOLE_5V': dict(normal=1.3, fault=1.5, outer=True, source=['J20.1'], loads={'F20.1': 1.3}),
    'FUSED_5V': dict(normal=1.3, fault=1.5, outer=True, source=['F20.2'], loads={'Q20.3': 1.3}),
    'PRIMARY_5V': dict(normal=1.3, fault=1.5, outer=True, source=['Q20.2'],
                       loads={'U20.3': 0.75, 'U71.1': 0.6}),
    '+5V_HOLD': dict(normal=0.75, fault=1.5, outer=True, source=['U20.2', 'U20.7'],
                     loads={'U21.2': 0.3, 'U22.2': 0.45}),
    '+5V_C5': dict(normal=0.6, fault=1.5, outer=True, source=['U71.6'], loads={'K214.1': 0.6}),
    # DNP reserve bank, rated as if populated: discharge into U20.
    'RESERVE_5V': dict(normal=0.75, fault=1.5, outer=True, source=['U20.6'],
                       loads={'C202.1': 0.19, 'C203.1': 0.19, 'C204.1': 0.19, 'C205.1': 0.19}),
    '+3V3_STORAGE': dict(normal=0.5, fault=1.5, outer=True, source=['L21.2'],
                         loads={'J201.4': 0.4, 'U207.16': 0.05, 'U208.16': 0.05}),
    'CONSOLE_3V3_FLEX': dict(normal=0.1, fault=0.5, outer=False, source=['J51.20'], loads={'F21.1': 0.1}),
    # F21 limits this rail to 0.5 A; every branch is rated for it.
    'CONSOLE_3V3': dict(normal=0.1, fault=0.5, outer=False, source=['F21.2'], loads=None),
    # Plane net: each track is an IC's stub to a plane via; budget only.
    # Its feed from the converter (L20.2 to the vias into the In4 plane) must
    # also carry the converter's short-circuit current, shared by the feed
    # vias. 1.5 A is the assumed TPS62162 current limit, as for
    # +3V3_STORAGE; confirm against the datasheet limit.
    '+3V3_LOGIC': dict(normal=0.35, fault=None, outer=False, source=['L20.2'], loads=None, plane_feed_fault=1.5),
}


def runs(edges, terminals):
    """Run length for each edge: the chain of same-layer, same-width edges
    through nodes that join exactly two edges and are not terminals (pads,
    vias). Returns per-edge (run length mm, run touches a pad)."""
    at = collections.defaultdict(list)
    for idx, (a, b, *_) in enumerate(edges):
        at[a].append(idx)
        at[b].append(idx)
    run_of = {}
    out = [None] * len(edges)
    for start in range(len(edges)):
        if start in run_of:
            continue
        seg = edges[start][2]
        key = (seg.GetLayer(), seg.GetWidth())
        members, stack = {start}, [start]
        while stack:
            idx = stack.pop()
            for node in edges[idx][:2]:
                if node in terminals or len(at[node]) != 2:
                    continue
                for nxt in at[node]:
                    s2 = edges[nxt][2]
                    if nxt not in members and (s2.GetLayer(), s2.GetWidth()) == key:
                        members.add(nxt)
                        stack.append(nxt)
        length = sum(edges[i][3] for i in members)
        nodes = [n for i in members for n in edges[i][:2]]
        for i in members:
            run_of[i] = start
            out[i] = (length, nodes)
    return out


def rise(current, area_sq_mil, k):
    """Temperature rise in C for a conductor cross-section (IPC-2221 fit)."""
    return (current / (k * area_sq_mil ** 0.725)) ** (1 / 0.44) if current > 0 else 0.0


class Graph:
    def __init__(self):
        self.parent = {}

    def find(self, a):
        self.parent.setdefault(a, a)
        while self.parent[a] != a:
            self.parent[a] = self.parent[self.parent[a]]
            a = self.parent[a]
        return a

    def join(self, a, b):
        self.parent[self.find(a)] = self.find(b)


def net_graph(board, net, pads):
    """Copper graph of a net. Nodes are pads, vias and points on tracks;
    edges are track pieces (segment, length mm). A track end or via that
    lands part-way along another track splits that track there, so current
    is followed along the copper rather than short-circuited across it."""
    items = [t for t in board.GetTracks() if t.GetNetname() == net]
    vias = [t for t in items if t.Type() == pcbnew.PCB_VIA_T]
    segs = [t for t in items if t.Type() != pcbnew.PCB_VIA_T]
    g = Graph()
    tol = pcbnew.FromMM(0.001)
    ends = []
    for i, s in enumerate(segs):
        for k, p in enumerate((s.GetStart(), s.GetEnd())):
            g.find(('end', i, k))
            ends.append((('end', i, k), s.GetLayer(), p))
    for j in range(len(vias)):
        g.find(('via', j))
    for name in pads:
        g.find(('pad', name))
    # Ends that coincide on one layer.
    by_cell = collections.defaultdict(list)
    cell = pcbnew.FromMM(0.05)
    for node, layer, p in ends:
        by_cell[(layer, p.x // cell, p.y // cell)].append((node, p))
    for node, layer, p in ends:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for other, q in by_cell.get((layer, p.x // cell + dx, p.y // cell + dy), []):
                    if other != node and abs(p.x - q.x) <= tol and abs(p.y - q.y) <= tol:
                        g.join(node, other)
    # Ends on a via or a pad belong to it; vias inside a pad too. A track's
    # round end may stop just outside a pad (a rounded corner, say) and
    # still overlap it; it counts when its centre is within a quarter of the
    # track width of the pad, so the overlap is at least 87 % of the width.
    def reach(node):
        return segs[node[1]].GetWidth() // 4

    for j, v in enumerate(vias):
        for node, layer, p in ends:
            if v.HitTest(p, reach(node)):
                g.join(node, ('via', j))
    for name, pad in pads.items():
        for node, layer, p in ends:
            if pad.IsOnLayer(layer) and pad.HitTest(p, reach(node)):
                g.join(node, ('pad', name))
        for j, v in enumerate(vias):
            if pad.HitTest(v.GetPosition()):
                g.join(('via', j), ('pad', name))
    # Points part-way along a segment: other ends on its layer, vias.
    taps = collections.defaultdict(list)

    def tap(i, s, p, node):
        a, b = s.GetStart(), s.GetEnd()
        dx, dy = b.x - a.x, b.y - a.y
        l2 = float(dx * dx + dy * dy)
        t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2))
        length = math.sqrt(l2)
        if t * length <= tol:
            g.join(node, ('end', i, 0))
        elif (1 - t) * length <= tol:
            g.join(node, ('end', i, 1))
        else:
            taps[i].append((t, node))

    for i, s in enumerate(segs):
        for node, layer, p in ends:
            if node[1] != i and layer == s.GetLayer() and s.HitTest(p, 0):
                tap(i, s, p, node)
        for j, v in enumerate(vias):
            if s.HitTest(v.GetPosition(), 0):
                tap(i, s, v.GetPosition(), ('via', j))
    edges = []
    for i, s in enumerate(segs):
        points = sorted([(0.0, ('end', i, 0)), (1.0, ('end', i, 1))] + taps[i], key=lambda q: q[0])
        total = pcbnew.ToMM(s.GetLength())
        for (t0, n0), (t1, n1) in zip(points, points[1:]):
            edges.append((g.find(n0), g.find(n1), s, (t1 - t0) * total))
    via_nodes = [(g.find(('via', j)), v) for j, v in enumerate(vias)]
    pad_nodes = {name: g.find(('pad', name)) for name in pads}
    return edges, via_nodes, pad_nodes


def reachable(edges, sources):
    adj = collections.defaultdict(set)
    for a, b, *_ in edges:
        adj[a].add(b)
        adj[b].add(a)
    seen, stack = set(sources), list(sources)
    while stack:
        for m in adj[stack.pop()]:
            if m not in seen:
                seen.add(m)
                stack.append(m)
    return seen


def nodal_currents(edges, sources, cases, outer_layers, outer_um, inner_um):
    """Current in every edge for each case by nodal analysis. A case maps
    node -> current drawn there; the sources (tied together) supply it.
    Returns one array per edge with a value per case; edges not fed from the
    source carry nothing."""
    fed = reachable(edges, sources)
    index = {n: k for k, n in enumerate(sorted(fed, key=str))}
    size = len(index)
    cond = np.zeros((size, size))
    g_edge = []
    for a, b, s, length_mm in edges:
        thick = outer_um if s.GetLayer() in outer_layers else inner_um
        g = pcbnew.ToMM(s.GetWidth()) * thick / max(length_mm, 0.01)
        g_edge.append(g)
        if a != b and a in index and b in index:
            i, j = index[a], index[b]
            cond[i, i] += g
            cond[j, j] += g
            cond[i, j] -= g
            cond[j, i] -= g
    ground = index[sources[0]]
    for extra in sources[1:]:
        if extra in index and index[extra] != ground:
            i, j = ground, index[extra]
            cond[i, i] += 1e9
            cond[j, j] += 1e9
            cond[i, j] -= 1e9
            cond[j, i] -= 1e9
    drawn = np.zeros((size, len(cases)))
    for c, case in enumerate(cases):
        for node, amps in case.items():
            if node in index:
                drawn[index[node], c] -= amps
    keep = [k for k in range(size) if k != ground]
    volts = np.zeros((size, len(cases)))
    if keep:
        volts[keep] = np.linalg.solve(cond[np.ix_(keep, keep)], drawn[keep])
    out = []
    for (a, b, *_), g in zip(edges, g_edge):
        if a in index and b in index:
            out.append(np.abs(g * (volts[index[a]] - volts[index[b]])))
        else:
            out.append(np.zeros(len(cases)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    settings = json.loads(SETTINGS.read_text())
    outer_um = settings['outer_copper_oz'] * OZ_UM
    inner_um = settings['inner_copper_oz'] * OZ_UM
    board = pcbnew.LoadBoard(str(args.board))
    outer_layers = {pcbnew.F_Cu, pcbnew.B_Cu}
    all_pads = {'%s.%s' % (f.GetReference(), p.GetNumber()): p for f in board.GetFootprints() for p in f.Pads()}
    nets, failures = {}, []
    for net, spec in NETS.items():
        if not board.FindNet(net):
            continue
        pads = {n: p for n, p in all_pads.items() if p.GetNetname() == net}
        missing = [n for n in spec['source'] + list(spec['loads'] or {}) if n not in pads]
        if missing:
            raise ValueError('%s: pads not on the net: %s' % (net, missing))
        edges, via_nodes, pad_nodes = net_graph(board, net, pads)
        # Where a part can realistically short: capacitors, connectors,
        # spring contacts, test points and the loads themselves.
        fault_points = [node for n, node in pad_nodes.items() if n.split('.')[0].rstrip('0123456789') in
                        ('C', 'J', 'K', 'TP')]
        loads = collections.Counter()
        for n, c in (spec['loads'] or {}).items():
            loads[pad_nodes[n]] += c
        sources = [pad_nodes[s] for s in spec['source']]
        # No vacuous pass: every pad of the net must be fed from the source
        # through copper (or a plane, for plane nets).
        fed = reachable(edges, sources)
        unfed = sorted(n for n, node in pad_nodes.items() if node not in fed)
        if unfed and net != '+3V3_LOGIC':
            failures.append({'net': net, 'item': 'not_fed_from_source', 'pads': unfed})
        shorts = sorted(set(fault_points) | set(loads), key=str) if spec['fault'] else []
        solved = nodal_currents(edges, sources, [dict(loads)] + [{p: spec['fault']} for p in shorts],
                                outer_layers, outer_um, inner_um)
        currents = [float(x[0]) for x in solved] if spec['loads'] else [spec['normal']] * len(edges)
        fault_amps = [float(x[1:].max()) if len(x) > 1 else 0.0 for x in solved]
        worst = {'normal_rise_c': 0.0, 'fault_rise_c': 0.0, 'neck_normal_rise_c': 0.0, 'neck_fault_rise_c': 0.0}
        pad_set = set(pad_nodes.values())
        run_info = runs(edges, pad_set | {n for n, _ in via_nodes})
        by_width, inner_long = {}, []
        node_current = collections.defaultdict(float)
        node_fault = collections.defaultdict(float)
        for (a, b, s, length_mm), amps, at_fault, (run_mm, run_nodes) in zip(edges, currents, fault_amps, run_info):
            node_current[a] = max(node_current[a], amps)
            node_current[b] = max(node_current[b], amps)
            node_fault[a] = max(node_fault[a], at_fault)
            node_fault[b] = max(node_fault[b], at_fault)
            layer = s.GetLayer()
            outer = layer in outer_layers
            width_mm = pcbnew.ToMM(s.GetWidth())
            area = width_mm * 1000 * (outer_um if outer else inner_um) * UM_TO_MIL ** 2
            k = 0.048 if outer else 0.024
            neck = run_mm <= NECK_MM and any(n in pad_set for n in run_nodes)
            key = '%s %.2f' % (board.GetLayerName(layer), width_mm)
            by_width[key] = round(by_width.get(key, 0) + length_mm, 2)
            if spec['outer'] and not outer and run_mm > NECK_MM:
                inner_long.append({'layer': board.GetLayerName(layer), 'length_mm': round(length_mm, 2)})
            cases = [('normal', amps, NORMAL_LIMIT_C)]
            if at_fault > 1e-6:
                cases.append(('fault', at_fault, FAULT_LIMIT_C))
            for kind, i, limit in cases:
                dt = rise(i, area, k)
                tag = ('neck_' if neck else '') + kind + '_rise_c'
                worst[tag] = max(worst[tag], round(dt, 1))
                if dt > limit * (2 if neck else 1):
                    failures.append({'net': net, 'item': 'track', 'case': kind, 'current_a': round(i, 3),
                                     'layer': board.GetLayerName(layer), 'width_mm': round(width_mm, 3),
                                     'length_mm': round(length_mm, 2), 'run_mm': round(run_mm, 2), 'neck': neck,
                                     'rise_c': round(dt, 1),
                                     'start_mm': [round(pcbnew.ToMM(s.GetStart().x) - 70, 2),
                                                  round(pcbnew.ToMM(s.GetStart().y) - 45, 2)]})
        # A via carries at most the largest current of a segment it joins;
        # parallel vias at one node share it.
        per_node = collections.Counter(n for n, _ in via_nodes)
        for node, v in via_nodes:
            drill_mm = pcbnew.ToMM(v.GetDrillValue())
            area = math.pi * drill_mm * 1000 * VIA_PLATING_UM * UM_TO_MIL ** 2
            share = per_node[node]
            cases = [('normal', node_current[node] / share, NORMAL_LIMIT_C)]
            if node_fault[node] > 1e-6:
                cases.append(('fault', node_fault[node] / share, FAULT_LIMIT_C))
            for kind, i, limit in cases:
                dt = rise(i, area, 0.024)
                if dt > limit * 2:
                    failures.append({'net': net, 'item': 'via', 'case': kind, 'current_a': round(i, 3),
                                     'drill_mm': round(drill_mm, 2), 'parallel': share, 'rise_c': round(dt, 1),
                                     'at_mm': [round(pcbnew.ToMM(v.GetPosition().x) - 70, 2),
                                               round(pcbnew.ToMM(v.GetPosition().y) - 45, 2)]})
        if inner_long:
            failures.append({'net': net, 'item': 'inner_layer_copper', 'segments': inner_long})
        feed = {}
        if spec.get('plane_feed_fault'):
            amps = spec['plane_feed_fault']
            feed_vias = [(node, v) for node, v in via_nodes if node in fed]
            if not feed_vias:
                failures.append({'net': net, 'item': 'no_plane_feed_via'})
            else:
                share = amps / len(feed_vias)
                feed_loads = collections.Counter()
                for node, _ in feed_vias:
                    feed_loads[node] += share
                feed_currents = [float(x[0]) for x in nodal_currents(edges, sources, [dict(feed_loads)],
                                                                     outer_layers, outer_um, inner_um)]
                worst_feed = 0.0
                for (a, b, s, length_mm), i, (run_mm, run_nodes) in zip(edges, feed_currents, run_info):
                    outer = s.GetLayer() in outer_layers
                    area = pcbnew.ToMM(s.GetWidth()) * 1000 * (outer_um if outer else inner_um) * UM_TO_MIL ** 2
                    dt = rise(i, area, 0.048 if outer else 0.024)
                    neck = run_mm <= NECK_MM and any(n in pad_set for n in run_nodes)
                    worst_feed = max(worst_feed, round(dt, 1))
                    if dt > FAULT_LIMIT_C * (2 if neck else 1):
                        failures.append({'net': net, 'item': 'plane_feed_track', 'case': 'fault', 'current_a': round(i, 3),
                                         'layer': board.GetLayerName(s.GetLayer()), 'width_mm': round(pcbnew.ToMM(s.GetWidth()), 3),
                                         'rise_c': round(dt, 1)})
                for node, v in feed_vias:
                    area = math.pi * pcbnew.ToMM(v.GetDrillValue()) * 1000 * VIA_PLATING_UM * UM_TO_MIL ** 2
                    dt = rise(share, area, 0.024)
                    worst_feed = max(worst_feed, round(dt, 1))
                    if dt > FAULT_LIMIT_C * 2:
                        failures.append({'net': net, 'item': 'plane_feed_via', 'case': 'fault', 'current_a': round(share, 3),
                                         'drill_mm': round(pcbnew.ToMM(v.GetDrillValue()), 2), 'rise_c': round(dt, 1)})
                feed = {'plane_feed_fault_current_a': amps, 'plane_feed_vias': len(feed_vias),
                        'plane_feed_worst_rise_c': worst_feed}
        nets[net] = {'budget_current_a': spec['normal'], 'fault_current_a': spec['fault'], 'outer_only': spec['outer'],
                     'segments': len(edges), 'vias': len(via_nodes), 'length_by_layer_width_mm': by_width, **worst, **feed}
    report = {'method': 'IPC-2221 chart fit; outer %.0f um, inner %.0f um, via plating %.0f um; branch currents from '
                        'the copper graph' % (outer_um, inner_um, VIA_PLATING_UM),
              'limits_c': {'normal': NORMAL_LIMIT_C, 'fault': FAULT_LIMIT_C, 'neck_mm': NECK_MM, 'neck_and_via_factor': 2},
              'nets': nets, 'failures': failures, 'pass': not failures}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'pass': not failures, 'failures': len(failures),
                      # [normal, fault] along runs, then [normal, fault] at pad necks (2x allowance)
                      'worst_rise_c': {n: [v['normal_rise_c'], v['fault_rise_c'],
                                           v.get('neck_normal_rise_c', 0.0), v.get('neck_fault_rise_c', 0.0)]
                                       for n, v in nets.items()}}))


if __name__ == '__main__':
    main()
