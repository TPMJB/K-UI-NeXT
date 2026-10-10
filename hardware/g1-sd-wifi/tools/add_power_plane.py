#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make In2.Cu the +3V3_LOGIC distribution plane.

The stackup becomes signal / GND reference / +3V3_LOGIC plane / signal.
In2 is marked as a power layer so the router keeps ordinary signals on
F.Cu and B.Cu; the few reviewed In2 analog/feedback tracks already on the
board stay where they are and the plane fills around them. Other rails
(CONSOLE_3V3, +3V3_STORAGE, +1V1_MCU and the 5 V nets) remain routed copper
in the POWER netclass. Run with KiCad 10's /usr/bin/python3 (pcbnew).
"""
import argparse
import json
import pathlib

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
ORIGIN = (70.0, 45.0)
WIDTH, HEIGHT = 59.0, 105.0
CUTOUT = (6.0, 51.0, 35.0, 70.0)
NOTCH = (43.8, 42.0, 59.0, 89.0)
NAME = '+3V3_LOGIC distribution plane'
NET = '+3V3_LOGIC'
# Pull the plane in from the board edge so it never reaches the outline.
PLANE_INSET = 0.5


def mm(v):
    return pcbnew.FromMM(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    args = ap.parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    net = board.FindNet(NET)
    if net is None:
        raise ValueError(NET + ' missing')
    existing = {z.GetZoneName(): z for z in board.Zones()}
    zone = existing.get(NAME)
    new_zone = zone is None
    if new_zone:
        zone = pcbnew.ZONE(board)
    zone.UnFill()
    zone.SetLayer(pcbnew.In2_Cu)
    zone.SetNet(net)
    zone.SetZoneName(NAME)
    zone.SetLocalClearance(mm(0.20))
    zone.SetMinThickness(mm(0.20))
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    zone.SetAssignedPriority(0)
    i = PLANE_INSET
    pts = [(i, i), (WIDTH - i, i), (WIDTH - i, NOTCH[1] - i), (NOTCH[0] - i, NOTCH[1] - i),
           (NOTCH[0] - i, NOTCH[3] + i), (WIDTH - i, NOTCH[3] + i), (WIDTH - i, HEIGHT - i), (i, HEIGHT - i)]
    poly = zone.Outline()
    poly.RemoveAllContours()
    poly.NewOutline()
    for x, y in pts:
        poly.Append(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y))
    poly.NewHole()
    x0, y0, x1, y1 = CUTOUT
    for x, y in [(x0 - i, y0 - i), (x0 - i, y1 + i), (x1 + i, y1 + i), (x1 + i, y0 - i)]:
        poly.Append(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y), 0, 0)
    if new_zone:
        board.Add(zone)
    board.SetLayerType(pcbnew.In2_Cu, pcbnew.LT_POWER)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    print(json.dumps({'plane': NAME, 'layer': 'In2.Cu', 'net': NET, 'new_zone': new_zone,
                      'inset_mm': PLANE_INSET}))


if __name__ == '__main__':
    main()
