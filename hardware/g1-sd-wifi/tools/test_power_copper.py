#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regression checks for branch-summed and unequal parallel via currents.

Run with the same KiCad 10 / numpy environment as check_power_copper.py.
These circuits exercise the electrical solver, not a second copy of its logic.
"""
import unittest

import pcbnew

import check_power_copper as copper


class Conductor:
    def __init__(self, via=False, width=0.3, drill=0.3):
        self.via, self.width, self.drill = via, width, drill

    def Type(self):
        return pcbnew.PCB_VIA_T if self.via else pcbnew.PCB_TRACE_T

    def GetLayer(self):
        return pcbnew.F_Cu

    def GetWidth(self):
        return pcbnew.FromMM(self.width)

    def GetDrillValue(self):
        return pcbnew.FromMM(self.drill)


class CopperRegression(unittest.TestCase):
    def solve(self, edges, loads):
        return copper.nodal_currents(edges, ['source'], [loads],
                                     {pcbnew.F_Cu, pcbnew.B_Cu}, 34.79, 17.395)

    def test_one_via_carries_sum_of_two_load_branches(self):
        edges = [('source', 'bottom', Conductor(via=True), 0.8),
                 ('bottom', 'load_a', Conductor(), 1.0),
                 ('bottom', 'load_b', Conductor(), 1.0)]
        current = self.solve(edges, {'load_a': 0.3, 'load_b': 0.2})
        self.assertAlmostEqual(abs(current[0][0]), 0.5)
        self.assertAlmostEqual(abs(current[1][0]), 0.3)
        self.assertAlmostEqual(abs(current[2][0]), 0.2)

    def test_parallel_vias_share_by_actual_path_resistance(self):
        edges = [('source', 'plane', Conductor(via=True), 0.2),
                 ('source', 'plane', Conductor(via=True), 0.6)]
        current = self.solve(edges, {'plane': 1.0})
        self.assertAlmostEqual(abs(current[0][0]), 0.75)
        self.assertAlmostEqual(abs(current[1][0]), 0.25)

    def test_plane_capacitor_fault_passes_through_its_own_stub(self):
        edges = [('source', 'plane', Conductor(via=True), 0.6),
                 ('plane', 'capacitor', Conductor(width=0.15), 1.0),
                 ('plane', 'other_load', Conductor(width=0.15), 1.0)]
        current = self.solve(edges, {'capacitor': 2.5})
        self.assertAlmostEqual(abs(current[0][0]), 2.5)
        self.assertAlmostEqual(abs(current[1][0]), 2.5)
        self.assertAlmostEqual(abs(current[2][0]), 0.0)
        self.assertEqual(copper.NETS['+3V3_LOGIC']['fault'], 1.35)
        self.assertEqual(copper.NETS['+3V3_STORAGE']['fault'], 1.35)

    def test_native_plane_graph_keeps_local_stub_and_barrels(self):
        board = pcbnew.BOARD()
        board.SetCopperLayerCount(6)
        net = pcbnew.NETINFO_ITEM(board, 'TEST_3V3')
        board.Add(net)
        pads = {}
        for ref, x in [('L0', 0), ('C0', 6)]:
            fp = pcbnew.FOOTPRINT(board)
            fp.SetReference(ref)
            pad = pcbnew.PAD(fp)
            pad.SetNumber('1')
            pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
            pad_layers = pcbnew.LSET()
            pad_layers.AddLayer(pcbnew.F_Cu)
            pad.SetLayerSet(pad_layers)
            pad.SetShape(pcbnew.PAD_SHAPE_RECT)
            pad.SetSize(pcbnew.VECTOR2I(pcbnew.FromMM(.5), pcbnew.FromMM(.5)))
            pad.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), 0))
            pad.SetNet(net)
            fp.Add(pad)
            board.Add(fp)
            pads[ref + '.1'] = pad
        for a, b in [(0, 1), (5, 6)]:
            track = pcbnew.PCB_TRACK(board)
            track.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(a), 0))
            track.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(b), 0))
            track.SetWidth(pcbnew.FromMM(.15))
            track.SetLayer(pcbnew.F_Cu)
            track.SetNet(net)
            board.Add(track)
        for x in (1, 5):
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), 0))
            via.SetDrill(pcbnew.FromMM(.2))
            via.SetWidth(pcbnew.FromMM(.5))
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetNet(net)
            board.Add(via)
        layer = board.GetLayerID('In4.Cu')
        zone = pcbnew.ZONE(board)
        zone.SetLayer(layer)
        zone.SetNet(net)
        poly = pcbnew.SHAPE_POLY_SET()
        poly.NewOutline()
        for x, y in [(0, -1), (6, -1), (6, 1), (0, 1)]:
            poly.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        zone.SetFilledPolysList(layer, poly)
        board.Add(zone)
        layers = [board.GetLayerID(n) for n in ['F.Cu', 'In1.Cu', 'In2.Cu', 'In3.Cu', 'In4.Cu', 'B.Cu']]
        edges, vias, nodes, islands = copper.net_graph(board, 'TEST_3V3', pads, layers,
                                                     {l: i * .16 for i, l in enumerate(layers)})
        self.assertEqual(islands, 1)
        self.assertNotEqual(nodes['L0.1'], nodes['C0.1'])
        currents = copper.nodal_currents(edges, [nodes['L0.1']], [{nodes['C0.1']: 2.5}],
                                         {pcbnew.F_Cu, pcbnew.B_Cu}, 34.79, 17.395)
        for idx, edge in enumerate(edges):
            if edge[2].Type() == pcbnew.PCB_TRACE_T:
                self.assertAlmostEqual(abs(currents[idx][0]), 2.5)
        for via, indices in vias:
            self.assertAlmostEqual(max(abs(currents[i][0]) for i in indices), 2.5)


if __name__ == '__main__':
    unittest.main()
