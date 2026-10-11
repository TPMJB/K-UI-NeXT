#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""IPC-2221 engineering screen of routed supply tracks and via barrels.

Track currents follow a resistor graph. Each via has a separate node on each
copper layer and finite barrel resistance between adjacent layers: current
through a via is the solved barrel current, including sums of load branches.
Filled plane islands are ideal equipotential hubs, not a plane thermal model.
Every capacitor, connector, test point and IC rail pad is faulted separately,
including the +3V3_LOGIC plane's local stubs. Unmapped distribution stubs are
also screened conservatively at the whole normal budget.

The 2.5-A converter case rounds up TPS6216x Rev E's 2.45-A maximum static
current limit at 12 V / 25 C. It is a selected peak-current engineering screen,
not a guaranteed bound over all operating conditions; the typical dynamic
comparator delay and inductor ramp can produce higher peaks. Fuse cases use
nameplate current and do not establish clearing/I-squared-t coordination or
protection of entry copper upstream of the fuse. A passing report therefore
means copper_screen_pass, never protection coordination or release approval.

The original 10-C normal / 45-C fault rises, 2x short pad-neck / via allowance,
and 2-mm neck length remain unchanged. Run with KiCad 10 pcbnew and numpy.
"""
import argparse
import collections
import hashlib
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
# Source pads, load budgets and explicit screening currents (not fuse limits).
CONVERTER_FAULT_SCREEN_A = 2.5
NETS = {
    'CONSOLE_5V': dict(normal=1.35, fault=1.5, outer=True, source=['J20.1'], loads={'F20.1': 1.35}),
    'FUSED_5V': dict(normal=1.35, fault=1.5, outer=True, source=['F20.2'], loads={'Q20.3': 1.35}),
    'PRIMARY_5V': dict(normal=1.35, fault=1.5, outer=True, source=['Q20.2'],
                       loads={'U20.3': 0.75, 'U71.1': 0.6}),
    '+5V_HOLD': dict(normal=0.75, fault=1.5, outer=True, source=['U20.2', 'U20.7'],
                     loads={'U21.2': 0.3, 'U22.2': 0.45}),
    '+5V_C5': dict(normal=0.6, fault=1.5, outer=True, source=['U71.6'], loads={'K214.1': 0.6}),
    # DNP reserve bank, rated as if populated: discharge into U20.
    'RESERVE_5V': dict(normal=0.75, fault=1.5, outer=True, source=['U20.6'],
                       loads={'C202.1': 0.19, 'C203.1': 0.19, 'C204.1': 0.19, 'C205.1': 0.19}),
    '+3V3_STORAGE': dict(normal=0.5, fault=CONVERTER_FAULT_SCREEN_A, outer=True, source=['L21.2'],
                         loads={'J201.4': 0.4, 'U207.16': 0.05, 'U208.16': 0.05}),
    'CONSOLE_3V3_FLEX': dict(normal=0.1, fault=0.5, outer=False, source=['J51.20'], loads={'F21.1': 0.1}),
    # F21 nameplate screen; clearing/protection coordination remains separate.
    'CONSOLE_3V3': dict(normal=0.1, fault=0.5, outer=False, source=['F21.2'], loads=None),
    '+3V3_LOGIC': dict(normal=0.35, fault=CONVERTER_FAULT_SCREEN_A, outer=False,
                        source=['L20.2'], loads=None, plane=True),
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


def net_graph(board, net, pads, copper_layers, layer_z):
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
    for j, v in enumerate(vias):
        for layer in copper_layers:
            if v.IsOnLayer(layer):
                g.find(('via', j, layer))
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
            if v.IsOnLayer(layer) and v.HitTest(p, reach(node)):
                g.join(node, ('via', j, layer))
    for name, pad in pads.items():
        for node, layer, p in ends:
            if pad.IsOnLayer(layer) and pad.HitTest(p, reach(node)):
                g.join(node, ('pad', name))
        for j, v in enumerate(vias):
            if pad.HitTest(v.GetPosition()):
                for layer in copper_layers:
                    if pad.IsOnLayer(layer) and v.IsOnLayer(layer):
                        g.join(('via', j, layer), ('pad', name))
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
            # The same quarter-width overlap rule as for pads: a round end
            # just outside a narrower track's edge still overlaps it.
            if node[1] != i and layer == s.GetLayer() and s.HitTest(p, reach(node)):
                tap(i, s, p, node)
        for j, v in enumerate(vias):
            if v.IsOnLayer(s.GetLayer()) and s.HitTest(v.GetPosition(), 0):
                tap(i, s, v.GetPosition(), ('via', j, s.GetLayer()))
    # Filled islands are separate ideal hubs. Only actual filled copper at
    # a terminal is connected, so a void or isolated island cannot silently
    # feed a stub. Plane thermal/choke analysis remains a separate review.
    plane_islands = 0
    for zi, zone in enumerate(board.Zones()):
        if zone.GetNetname() != net or zone.GetIsRuleArea():
            continue
        for layer in copper_layers:
            if not zone.IsOnLayer(layer) or not zone.HasFilledPolysForLayer(layer):
                continue
            polys = zone.GetFilledPolysList(layer)
            plane_islands += polys.OutlineCount()
            terminals = [(node, p) for node, l, p in ends if l == layer]
            terminals += [(('via', j, layer), v.GetPosition()) for j, v in enumerate(vias) if v.IsOnLayer(layer)]
            terminals += [(('pad', name), p.GetPosition()) for name, p in pads.items() if p.IsOnLayer(layer)]
            for node, p in terminals:
                for island in range(polys.OutlineCount()):
                    if polys.Contains(p, island):
                        g.join(node, ('plane', zi, layer, island))
                        break
    edges = []
    for i, s in enumerate(segs):
        points = sorted([(0.0, ('end', i, 0)), (1.0, ('end', i, 1))] + taps[i], key=lambda q: q[0])
        total = pcbnew.ToMM(s.GetLength())
        for (t0, n0), (t1, n1) in zip(points, points[1:]):
            edges.append((g.find(n0), g.find(n1), s, (t1 - t0) * total))
    via_edges = []
    for j, v in enumerate(vias):
        layers = [l for l in copper_layers if v.IsOnLayer(l)]
        indices = []
        for a, b in zip(layers, layers[1:]):
            indices.append(len(edges))
            edges.append((g.find(('via', j, a)), g.find(('via', j, b)), v, layer_z[b] - layer_z[a]))
        via_edges.append((v, indices))
    pad_nodes = {name: g.find(('pad', name)) for name in pads}
    return edges, via_edges, pad_nodes, plane_islands


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
        if s.Type() == pcbnew.PCB_VIA_T:
            # Same conductance units as tracks: mm * um / mm. Each barrel
            # span gets finite resistance; no post-hoc equal-sharing rule.
            area = math.pi * pcbnew.ToMM(s.GetDrillValue()) * VIA_PLATING_UM
        else:
            thick = outer_um if s.GetLayer() in outer_layers else inner_um
            area = pcbnew.ToMM(s.GetWidth()) * thick
        g = area / max(length_mm, 0.01)
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
            out.append(g * (volts[index[a]] - volts[index[b]]))
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
    copper_layers = [board.GetLayerID(n) for n in settings['gerber_layers'] if n.endswith('.Cu')]
    # Use the selected nominal dielectric plan for relative span lengths,
    # normalized to the complete board thickness. The supplier must confirm
    # the actual stack; changing it requires re-running this screen.
    dielectric = [v for _, v in settings['stackup']['dielectric_plan_mm']]
    if len(dielectric) != len(copper_layers) - 1:
        raise ValueError('Dielectric plan does not match copper layer count')
    scale = settings['board_thickness_mm'] / sum(dielectric)
    z = [0.0]
    for span in dielectric:
        z.append(z[-1] + span * scale)
    layer_z = dict(zip(copper_layers, z))
    all_pads = {'%s.%s' % (f.GetReference(), p.GetNumber()): p for f in board.GetFootprints() for p in f.Pads()}
    nets, failures = {}, []
    for net, spec in NETS.items():
        if not board.FindNet(net):
            failures.append({'net': net, 'item': 'missing_net'})
            continue
        pads = {n: p for n, p in all_pads.items() if p.GetNetname() == net}
        missing = [n for n in spec['source'] + list(spec['loads'] or {}) if n not in pads]
        if missing:
            raise ValueError('%s: pads not on the net: %s' % (net, missing))
        edges, via_edges, pad_nodes, plane_islands = net_graph(board, net, pads, copper_layers, layer_z)
        # Where a part can realistically short: capacitors, connectors,
        # spring contacts, test points and the loads themselves.
        fault_points = [node for n, node in pad_nodes.items() if n.split('.')[0].rstrip('0123456789') in
                        ('C', 'J', 'K', 'TP', 'U')]
        loads = collections.Counter()
        for n, c in (spec['loads'] or {}).items():
            loads[pad_nodes[n]] += c
        sources = [pad_nodes[s] for s in spec['source']]
        # No vacuous pass: every pad of the net must be fed from the source
        # through copper (or a plane, for plane nets).
        fed = reachable(edges, sources)
        unfed = sorted(n for n, node in pad_nodes.items() if node not in fed)
        if unfed:
            failures.append({'net': net, 'item': 'not_fed_from_source', 'pads': unfed})
        if spec.get('plane') and not plane_islands:
            failures.append({'net': net, 'item': 'missing_filled_plane'})
        shorts = sorted(set(fault_points) | set(loads), key=str) if spec['fault'] else []
        solved = nodal_currents(edges, sources, [dict(loads)] + [{p: spec['fault']} for p in shorts],
                                outer_layers, outer_um, inner_um)
        currents = [abs(float(x[0])) for x in solved] if spec['loads'] else [spec['normal']] * len(edges)
        fault_amps = [float(np.abs(x[1:]).max()) if len(x) > 1 else 0.0 for x in solved]
        worst = {'normal_rise_c': 0.0, 'fault_rise_c': 0.0, 'neck_normal_rise_c': 0.0, 'neck_fault_rise_c': 0.0,
                 'via_normal_rise_c': 0.0, 'via_fault_rise_c': 0.0}
        pad_set = set(pad_nodes.values())
        tracks = [(idx, edge) for idx, edge in enumerate(edges) if edge[2].Type() != pcbnew.PCB_VIA_T]
        via_nodes = {n for v, indices in via_edges for idx in indices for n in edges[idx][:2]}
        run_info = runs([edge for _, edge in tracks], pad_set | via_nodes)
        by_width, inner_long = {}, []
        for (idx, (a, b, s, length_mm)), (run_mm, run_nodes) in zip(tracks, run_info):
            amps, at_fault = currents[idx], fault_amps[idx]
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
                                     'uuid': s.m_Uuid.AsString(),
                                     'start_mm': [round(pcbnew.ToMM(s.GetStart().x) - 70, 2),
                                                  round(pcbnew.ToMM(s.GetStart().y) - 45, 2)]})
        for v, indices in via_edges:
            drill_mm = pcbnew.ToMM(v.GetDrillValue())
            area = math.pi * drill_mm * 1000 * VIA_PLATING_UM * UM_TO_MIL ** 2
            normal = max((currents[i] for i in indices), default=0.0)
            fault = max((fault_amps[i] for i in indices), default=0.0)
            cases = [('normal', normal, NORMAL_LIMIT_C)]
            if fault > 1e-6:
                cases.append(('fault', fault, FAULT_LIMIT_C))
            for kind, i, limit in cases:
                dt = rise(i, area, 0.024)
                worst['via_' + kind + '_rise_c'] = max(worst['via_' + kind + '_rise_c'], round(dt, 1))
                if dt > limit * 2:
                    failures.append({'net': net, 'item': 'via', 'case': kind, 'current_a': round(i, 3),
                                     'drill_mm': round(drill_mm, 2), 'rise_c': round(dt, 1), 'uuid': v.m_Uuid.AsString(),
                                     'at_mm': [round(pcbnew.ToMM(v.GetPosition().x) - 70, 2),
                                               round(pcbnew.ToMM(v.GetPosition().y) - 45, 2)]})
        if inner_long:
            failures.append({'net': net, 'item': 'inner_layer_copper', 'segments': inner_long})
        nets[net] = {'budget_current_a': spec['normal'], 'fault_current_a': spec['fault'], 'outer_only': spec['outer'],
                     'segments': len(tracks), 'vias': len(via_edges), 'fault_cases': len(shorts),
                     'filled_plane_islands': plane_islands, 'unfed_pads': unfed,
                     'length_by_layer_width_mm': by_width, **worst}
    report = {'method': 'IPC-2221 chart fit; outer %.0f um, inner %.0f um, via plating %.0f um; branch currents from '
                        'a layer-aware resistive copper graph with finite via barrels and ideal filled plane islands' %
                        (outer_um, inner_um, VIA_PLATING_UM),
              'board_sha256': hashlib.sha256(args.board.read_bytes()).hexdigest(),
              'converter_screen_current_a': CONVERTER_FAULT_SCREEN_A,
              'converter_screen_basis': 'TI TPS62160 family Rev E electrical characteristics: static ILIM max 2.45 A '
                                       'at VIN=12 V, TJ=25 C, rounded to 2.5 A; dynamic comparator delay is typical '
                                       '30 ns, so this is a screening envelope, not a guaranteed peak bound.',
              'converter_primary_source': 'https://www.ti.com/lit/ds/symlink/tps62160.pdf',
              'protection_coordination_qualified': False,
              'limitations': ['Fuse cases use nameplate currents, not hard limits; source prospective current, '
                              'fuse time-current/clearing I-squared-t and short-pulse copper survival need validation.',
                              'Board fuses do not protect entry copper upstream of themselves; the 5-V harness '
                              'requires its documented tap-end fuse, and the host 3.3-V entry short needs source review.',
                              'Filled planes are ideal electrical hubs; plane constrictions, ground return, '
                              'thermal environment and actual supplier stack/plating require engineering review.',
                              '2.5-A converter screen does not bound dynamic peaks or all input/temperature conditions.'],
              'limits_c': {'normal': NORMAL_LIMIT_C, 'fault': FAULT_LIMIT_C, 'neck_mm': NECK_MM, 'neck_and_via_factor': 2},
              'nets': nets, 'failures': failures,
              'normal_screen_pass': not any(f.get('case') != 'fault' for f in failures),
              'fault_screen_pass': not any(f.get('case') != 'normal' for f in failures),
              'copper_screen_pass': not failures, 'pass': not failures}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'pass': not failures, 'failures': len(failures),
                      # [normal, fault] along runs, then [normal, fault] at pad necks (2x allowance)
                      'worst_rise_c': {n: [v['normal_rise_c'], v['fault_rise_c'],
                                           v.get('neck_normal_rise_c', 0.0), v.get('neck_fault_rise_c', 0.0)]
                                       for n, v in nets.items()}}))
    return 0 if not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
