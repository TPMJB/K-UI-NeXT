#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert the carrier to a six-layer stack without touching placement.

Stack: F.Cu signal / In1.Cu GND reference / In2.Cu signal / In3.Cu signal /
In4.Cu +3V3_LOGIC plane / B.Cu signal. The four-layer board could not close
its routing on this outline (two signal layers for 265 nets around a
TQFP-100, QFN-80 and TSOP-48), and the outline cannot grow until the owner's
fit check. In2 keeps the reviewed analog/feedback tracks already on it and
gains In1 as its reference; the +3V3_LOGIC plane moves to In4 next to B.Cu.
The board stays a 0.8 mm build so the C5 and connector height budget
is unchanged; the fabricator must confirm the stack before ordering. Run with
KiCad 10's Python (pcbnew).
"""
import argparse
import json
import pathlib

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
PLANE_NET = '+3V3_LOGIC'
# Dielectric thicknesses (mm) for a nominal 0.8 mm six-layer build:
# outer copper 0.035, inner copper 0.0152 (0.5 oz), mask 0.01 each side.
# Total about 0.80 mm with 0.02 mm of mask.
DIELECTRICS = [('prepreg', 0.10), ('core', 0.175), ('prepreg', 0.10),
               ('core', 0.175), ('prepreg', 0.10)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    args = ap.parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    if board.GetCopperLayerCount() != 4:
        raise ValueError('Expected the four-layer carrier, found %d copper layers' % board.GetCopperLayerCount())
    board.SetCopperLayerCount(6)
    enabled = board.GetEnabledLayers()
    for layer in (pcbnew.In3_Cu, pcbnew.In4_Cu):
        enabled.AddLayer(layer)
    board.SetEnabledLayers(enabled)
    visible = board.GetVisibleLayers()
    for layer in (pcbnew.In3_Cu, pcbnew.In4_Cu):
        visible.AddLayer(layer)
    board.SetVisibleLayers(visible)
    board.SetLayerType(pcbnew.In2_Cu, pcbnew.LT_SIGNAL)
    board.SetLayerType(pcbnew.In3_Cu, pcbnew.LT_SIGNAL)
    board.SetLayerType(pcbnew.In4_Cu, pcbnew.LT_POWER)
    moved = []
    for zone in board.Zones():
        if zone.GetNetname() == PLANE_NET and zone.GetLayer() == pcbnew.In2_Cu:
            zone.UnFill()
            zone.SetLayer(pcbnew.In4_Cu)
            moved.append(zone.GetZoneName())
    if len(moved) != 1:
        raise ValueError('Expected one In2 %s plane, found %d' % (PLANE_NET, len(moved)))
    # The board file carries no stackup block (its general thickness stays
    # 0.8 mm); the dielectric plan below is recorded for the order review
    # because KiCad 10's Python API does not expose stackup editing.
    thickness = pcbnew.ToMM(board.GetDesignSettings().GetBoardThickness())
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    print(json.dumps({'copper_layers': 6, 'plane_moved_to': 'In4.Cu', 'zones': moved,
                      'board_thickness_mm': thickness,
                      'dielectric_plan_mm': DIELECTRICS}))


if __name__ == '__main__':
    main()
