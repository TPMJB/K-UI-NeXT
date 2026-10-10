#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Insert a north-south band into the carrier above the BIOS opening.

Everything from the CN503 notch top southwards (the BIOS opening, the notch,
the flex sockets registered to it and the whole south half) moves south by
BAND millimetres, so a clear band of new board opens between U35/the FPGA
and the BIOS opening. In motherboard terms the BIOS opening and the notch
stay where they are and the carrier grows north. Board-edge and zone
vertices at or below EDGE_FROM move; footprints whose origin lies below AT
move; copper islands that touch only moved (or only unmoved) footprints keep
their exact geometry, and islands that span the band are deleted and listed
for the router. Edge-clearance rule areas are regenerated. Run with KiCad
10's Python (pcbnew); the tool constants (HEIGHT, CUTOUT, NOTCH) must
already describe the stretched outline.
"""
import argparse
import json
import pathlib

import pcbnew

import generate_board
import move_cluster

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
ORIGIN = generate_board.ORIGIN


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--band', type=float, default=10.0, help='Inserted band height, mm')
    ap.add_argument('--at', type=float, default=51.0, help='Footprints with origin y below this move (local mm)')
    ap.add_argument('--edge-from', type=float, default=41.4,
                    help='Edge and zone vertices with y at or below this move (local mm)')
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    shift = pcbnew.FromMM(args.band)
    edge_from = pcbnew.FromMM(ORIGIN[1] + args.edge_from)
    split = pcbnew.FromMM(ORIGIN[1] + args.at)

    def down(v):
        return pcbnew.VECTOR2I(v.x, v.y + shift) if v.y >= edge_from else v

    edges = 0
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts:
            d.SetStart(down(d.GetStart()))
            d.SetEnd(down(d.GetEnd()))
            edges += 1
    prefix = 'Routing edge clearance / '
    zones = []
    for z in list(board.Zones()):
        if z.GetZoneName().startswith(prefix):
            continue
        poly = z.Outline()
        for i in range(poly.TotalVertices()):
            poly.SetVertex(i, down(poly.CVertex(i)))
        zones.append(z.GetZoneName())
    generate_board.edge_routing_obstacles(board)
    # Gerbers and the placement file are relative to the aux origin, which
    # sits on the south-west corner of the outline.
    ds = board.GetDesignSettings()
    ds.SetAuxOrigin(down(ds.GetAuxOrigin()))
    ds.SetGridOrigin(down(ds.GetGridOrigin()))

    fps = list(board.GetFootprints())
    south = {f.GetReference() for f in fps if f.GetPosition().y > split}
    pads = [(f.GetReference(), p) for f in fps for p in f.Pads()]
    moved, dropped = 0, []
    for island in move_cluster.islands(board):
        refs = move_cluster.touched_refs(island, pads)
        if refs and refs <= south:
            for t in island:
                if t.Type() == pcbnew.PCB_VIA_T:
                    t.SetPosition(pcbnew.VECTOR2I(t.GetPosition().x, t.GetPosition().y + shift))
                else:
                    t.SetStart(pcbnew.VECTOR2I(t.GetStart().x, t.GetStart().y + shift))
                    t.SetEnd(pcbnew.VECTOR2I(t.GetEnd().x, t.GetEnd().y + shift))
            moved += 1
        elif refs & south:
            dropped.append({'net': island[0].GetNetname(), 'items': len(island), 'refs': sorted(refs)})
            for t in island:
                board.Remove(t)
        elif not refs and all((t.GetPosition() if t.Type() == pcbnew.PCB_VIA_T else t.GetStart()).y > split
                               for t in island):
            # Free-standing copper (no pad) in the south half moves with it.
            for t in island:
                if t.Type() == pcbnew.PCB_VIA_T:
                    t.SetPosition(pcbnew.VECTOR2I(t.GetPosition().x, t.GetPosition().y + shift))
                else:
                    t.SetStart(pcbnew.VECTOR2I(t.GetStart().x, t.GetStart().y + shift))
                    t.SetEnd(pcbnew.VECTOR2I(t.GetEnd().x, t.GetEnd().y + shift))
            moved += 1
    for f in fps:
        if f.GetReference() in south:
            f.SetPosition(pcbnew.VECTOR2I(f.GetPosition().x, f.GetPosition().y + shift))
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    report = {'band_mm': args.band, 'moved_footprints': len(south), 'moved_islands': moved,
              'dropped_islands': dropped, 'edges': edges, 'zones': zones}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'dropped_islands'} | {'dropped': len(dropped)}))


if __name__ == '__main__':
    main()
