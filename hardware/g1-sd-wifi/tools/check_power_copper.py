#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Current-capacity check of the routed supply copper (IPC-2221).

Every track segment and via on the supply nets below is rated with the
IPC-2221 chart fit I = k * dT^0.44 * A^0.725 (A in square mils; k = 0.048
for outer layers, 0.024 for inner layers and via barrels), using the copper
weights in manufacturing/rigid-order-settings.json and a conservative via
plating thickness.

Normal current: the copper of each net is turned into a graph (pads, vias and
track ends are nodes, segments are edges). A segment carries the sum of the
load currents beyond it as seen from the net's source; a segment inside a
loop is charged the whole net load. Loads come from the power budget in
design/power-and-isolation.md. Distribution nets without a load map charge
every segment the whole budget.

Fault current: for trunk nets, every segment and via that leads towards a
point where a part can realistically short (a load, a capacitor, a
connector, spring contact or test point) must also survive the current the
upstream fuse or regulator passes indefinitely, so copper outlasts the
protection. Branches that end only at IC sense/control pins or resistors are
rated for their normal current.

Limits: NORMAL_LIMIT_C rise at the normal current, FAULT_LIMIT_C at the fault
current; segments no longer than NECK_MM (pad escapes) and via barrels are
allowed twice the rise because heat spreads into the copper either side.
Outer-only nets fail if any inner-layer segment is longer than NECK_MM. Run
with KiCad 10's Python (pcbnew).
"""
import argparse
import collections
import json
import math
import pathlib

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
    '+3V3_LOGIC': dict(normal=0.35, fault=None, outer=False, source=['L20.2'], loads=None),
}


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
    """Nodes for pads, vias and track ends; edges for segments."""
    items = [t for t in board.GetTracks() if t.GetNetname() == net]
    vias = [t for t in items if t.Type() == pcbnew.PCB_VIA_T]
    segs = [t for t in items if t.Type() != pcbnew.PCB_VIA_T]
    g = Graph()
    ends = []
    for i, s in enumerate(segs):
        for k, p in enumerate((s.GetStart(), s.GetEnd())):
            node = ('end', i, k)
            g.find(node)
            ends.append((node, s.GetLayer(), p, s.GetWidth()))
    tol = pcbnew.FromMM(0.001)
    by_cell = collections.defaultdict(list)
    for node, layer, p, w in ends:
        by_cell[(layer, p.x // pcbnew.FromMM(0.05), p.y // pcbnew.FromMM(0.05))].append((node, p))
    for node, layer, p, w in ends:
        cx, cy = p.x // pcbnew.FromMM(0.05), p.y // pcbnew.FromMM(0.05)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for other, q in by_cell.get((layer, cx + dx, cy + dy), []):
                    if other != node and abs(p.x - q.x) <= tol and abs(p.y - q.y) <= tol:
                        g.join(node, other)
    for j, v in enumerate(vias):
        g.find(('via', j))
        for node, layer, p, w in ends:
            if v.HitTest(p):
                g.join(node, ('via', j))
    for name, pad in pads.items():
        g.find(('pad', name))
        for node, layer, p, w in ends:
            if pad.IsOnLayer(layer) and pad.HitTest(p):
                g.join(node, ('pad', name))
        for j, v in enumerate(vias):
            if pad.HitTest(v.GetPosition()):
                g.join(('via', j), ('pad', name))
    # A track end in the middle of another segment (T junction).
    for node, layer, p, w in ends:
        for i, s in enumerate(segs):
            if s.GetLayer() == layer and node[1] != i and s.HitTest(p, 0):
                g.join(node, ('end', i, 0))
                g.join(node, ('end', i, 1))
    edges = [(g.find(('end', i, 0)), g.find(('end', i, 1)), s) for i, s in enumerate(segs)]
    via_nodes = [(g.find(('via', j)), v) for j, v in enumerate(vias)]
    pad_nodes = {name: g.find(('pad', name)) for name in pads}
    return edges, via_nodes, pad_nodes


def reachable(edges, sources):
    adj = collections.defaultdict(set)
    for a, b, _ in edges:
        adj[a].add(b)
        adj[b].add(a)
    seen, stack = set(sources), list(sources)
    while stack:
        for m in adj[stack.pop()]:
            if m not in seen:
                seen.add(m)
                stack.append(m)
    return seen


def branch_currents(edges, sources, loads, fault_points=()):
    """Per edge: load current beyond it from the source (all load in a loop),
    and whether a fault point lies beyond it."""
    adj = collections.defaultdict(list)
    for idx, (a, b, _) in enumerate(edges):
        adj[a].append((b, idx))
        adj[b].append((a, idx))
    total = sum(loads.values())

    def reach(start, skip):
        seen = {start}
        stack = [start]
        while stack:
            n = stack.pop()
            for m, idx in adj[n]:
                if idx != skip and m not in seen:
                    seen.add(m)
                    stack.append(m)
        return seen

    currents, faults = [], []
    points = set(fault_points) | set(loads)
    for idx, (a, b, _) in enumerate(edges):
        if a == b:
            currents.append(0.0)
            faults.append(False)
            continue
        from_source = set()
        for s in sources:
            from_source |= reach(s, idx)
        if a in from_source and b in from_source:
            currents.append(total)          # inside a loop
            faults.append(True)
            continue
        far = reach(b, idx) if a in from_source else reach(a, idx) if b in from_source else set()
        currents.append(sum(c for n, c in loads.items() if n in far))
        faults.append(bool(far & points))
    return currents, faults


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
        loads = {pad_nodes[n]: c for n, c in (spec['loads'] or {}).items()}
        sources = [pad_nodes[s] for s in spec['source']]
        # No vacuous pass: every pad of the net must be fed from the source
        # through copper (or a plane, for plane nets).
        fed = reachable(edges, sources)
        unfed = sorted(n for n, node in pad_nodes.items() if node not in fed)
        if unfed and net != '+3V3_LOGIC':
            failures.append({'net': net, 'item': 'not_fed_from_source', 'pads': unfed})
        currents, faulted = branch_currents(edges, sources, loads, fault_points)
        if not spec['loads']:
            currents = [spec['normal']] * len(edges)
        worst = {'normal_rise_c': 0.0, 'fault_rise_c': 0.0}
        by_width, inner_long = {}, []
        node_current = collections.defaultdict(float)
        for (a, b, s), amps, at_fault in zip(edges, currents, faulted):
            node_current[a] = max(node_current[a], amps)
            node_current[b] = max(node_current[b], amps)
            layer = s.GetLayer()
            outer = layer in outer_layers
            width_mm, length_mm = pcbnew.ToMM(s.GetWidth()), pcbnew.ToMM(s.GetLength())
            area = width_mm * 1000 * (outer_um if outer else inner_um) * UM_TO_MIL ** 2
            k = 0.048 if outer else 0.024
            neck = length_mm <= NECK_MM
            key = '%s %.2f' % (board.GetLayerName(layer), width_mm)
            by_width[key] = round(by_width.get(key, 0) + length_mm, 2)
            if spec['outer'] and not outer and not neck:
                inner_long.append({'layer': board.GetLayerName(layer), 'length_mm': round(length_mm, 2)})
            cases = [('normal', amps, NORMAL_LIMIT_C)]
            if spec['fault'] and at_fault:
                cases.append(('fault', spec['fault'], FAULT_LIMIT_C))
            for kind, i, limit in cases:
                dt = rise(i, area, k)
                if not neck:
                    worst[kind + '_rise_c'] = max(worst[kind + '_rise_c'], round(dt, 1))
                if dt > limit * (2 if neck else 1):
                    failures.append({'net': net, 'item': 'track', 'case': kind, 'current_a': round(i, 3),
                                     'layer': board.GetLayerName(layer), 'width_mm': round(width_mm, 3),
                                     'length_mm': round(length_mm, 2), 'rise_c': round(dt, 1),
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
            if spec['fault'] and node_current[node] > 0:
                cases.append(('fault', spec['fault'] / share, FAULT_LIMIT_C))
            for kind, i, limit in cases:
                dt = rise(i, area, 0.024)
                if dt > limit * 2:
                    failures.append({'net': net, 'item': 'via', 'case': kind, 'current_a': round(i, 3),
                                     'drill_mm': round(drill_mm, 2), 'parallel': share, 'rise_c': round(dt, 1),
                                     'at_mm': [round(pcbnew.ToMM(v.GetPosition().x) - 70, 2),
                                               round(pcbnew.ToMM(v.GetPosition().y) - 45, 2)]})
        if inner_long:
            failures.append({'net': net, 'item': 'inner_layer_copper', 'segments': inner_long})
        nets[net] = {'budget_current_a': spec['normal'], 'fault_current_a': spec['fault'], 'outer_only': spec['outer'],
                     'segments': len(edges), 'vias': len(via_nodes), 'length_by_layer_width_mm': by_width, **worst}
    report = {'method': 'IPC-2221 chart fit; outer %.0f um, inner %.0f um, via plating %.0f um; branch currents from '
                        'the copper graph' % (outer_um, inner_um, VIA_PLATING_UM),
              'limits_c': {'normal': NORMAL_LIMIT_C, 'fault': FAULT_LIMIT_C, 'neck_mm': NECK_MM, 'neck_and_via_factor': 2},
              'nets': nets, 'failures': failures, 'pass': not failures}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'pass': not failures, 'failures': len(failures),
                      'worst_rise_c': {n: [v['normal_rise_c'], v['fault_rise_c']] for n, v in nets.items()}}))


if __name__ == '__main__':
    main()
