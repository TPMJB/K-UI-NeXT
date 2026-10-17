#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Give fine-pitch pins an explicit escape to a via of their own.

At a 0.5 mm-pitch QFP row the autorouter fans pins out with diagonal
escapes that can wall a neighbouring pin in on F.Cu, with no room left for
its via. This removes the named nets' copper inside a box around the row
(locked copper included), then lays, for each named pin, an F.Cu track from
the pad centre to a via at the given point, straight or through the bend
points given (the Default netclass track width and via). The new copper is locked; the gap closer then joins
each via, and the nets' other copper, on the inner layers.

  tools/fanout_pins.py --ref U10 --rip-box 8.3,31.5,11.6,36.6 \\
      --rip-net BUS_CS1n --rip-net BUS_DIORn --rip-net BUS_DIOWn --pin 15:10.55,33.05

Where a straight line would clip a neighbouring pad, give the escape bend
points before the via: --pin 22:35.7,39.3/35.7,39.45/35.6,39.55/34.95,39.55
(U201 pad 22, tools/rp_fpga_jtag_2026_10_17.sh) lays pad -> each point in
turn and puts the via on the last one.

Coordinates are board millimetres from the top-left corner of the outline.
Choose via points that native DRC accepts (check before the gap closer runs).
"""
import argparse
import json
import pathlib

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
ORIGIN = (70.0, 45.0)


def at(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(ORIGIN[0] + x), pcbnew.FromMM(ORIGIN[1] + y))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--ref', required=True)
    ap.add_argument('--rip-box', help='x0,y0,x1,y1: remove the --rip-net copper touching this box')
    ap.add_argument('--rip-net', action='append', default=[])
    ap.add_argument('--pin', action='append', default=[], help='PIN:X,Y[/X,Y...]: bend points from the pad, via on the last')
    args = ap.parse_args()
    pro = args.board.with_suffix('.kicad_pro')
    rules_before = pro.read_bytes()
    board = pcbnew.LoadBoard(str(args.board))
    ripped = []
    if args.rip_box:
        x0, y0, x1, y1 = map(float, args.rip_box.split(','))
        box = pcbnew.BOX2I(at(x0, y0), pcbnew.VECTOR2L(pcbnew.FromMM(x1 - x0), pcbnew.FromMM(y1 - y0)))
        ripped = [t for t in board.GetTracks() if t.GetNetname() in args.rip_net and t.HitTest(box, False)]
        for t in ripped:
            board.Remove(t)
    netclass = board.GetDesignSettings().m_NetSettings.GetDefaultNetclass()
    width, via_d, drill = netclass.GetTrackWidth(), netclass.GetViaDiameter(), netclass.GetViaDrill()
    pads = {p.GetNumber(): p for p in board.FindFootprintByReference(args.ref).Pads()}
    laid = []
    for spec in args.pin:
        number, points = spec.split(':')
        points = [tuple(map(float, p.split(','))) for p in points.split('/')]
        pad = pads[number]
        items, start = [], pad.GetPosition()
        for x, y in points:
            if at(x, y) == start:
                ap.error('pin %s: repeated point %s,%s would make a zero-length track' % (number, x, y))
            track = pcbnew.PCB_TRACK(board)
            track.SetStart(start)
            track.SetEnd(at(x, y))
            track.SetWidth(width)
            track.SetLayer(pcbnew.F_Cu)
            track.SetNet(pad.GetNet())
            items.append(track)
            start = at(x, y)
        x, y = points[-1]
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(at(x, y))
        via.SetWidth(via_d)
        via.SetDrill(drill)
        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        via.SetNet(pad.GetNet())
        for item in items + [via]:
            item.SetLocked(True)
            board.Add(item)
        laid.append({'pin': number, 'net': pad.GetNetname(), 'via': [x, y],
                     'bends': [list(p) for p in points[:-1]]})
    pcbnew.SaveBoard(str(args.board), board)
    if pro.read_bytes() != rules_before:
        pro.write_bytes(rules_before)
        raise SystemExit('Saving changed the project rules file; restored it.')
    print(json.dumps({'ripped_items': len(ripped), 'ripped_nets': sorted({t.GetNetname() for t in ripped}),
                      'fanouts': laid}))


if __name__ == '__main__':
    main()
