#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Add locked through vias inside pads that carry supply or return current.

A single 0.2 mm drill is a poor path for the board's whole 5-V return or a
regulator's thermal pad. Each --pad REF.PIN=COLSxROWS@DRILL/SIZE puts a
COLS x ROWS grid of vias of that drill and diameter inside the pad, on the
pad's net, spaced evenly over its smaller-than-pad interior; the vias are
locked so the router and later cleanup keep them. Existing vias on the same
net inside the pad are kept and counted. Run with KiCad 10's Python; native
DRC follows.
"""
import argparse
import json
import pathlib
import re

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
SPEC = re.compile(r'(?P<ref>[A-Z]+\d+)\.(?P<pin>\w+)=(?P<cols>\d+)x(?P<rows>\d+)@(?P<drill>[\d.]+)/(?P<size>[\d.]+)$')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--pad', action='append', required=True,
                    help='REF.PIN=COLSxROWS@DRILL/SIZE, e.g. J20.2=2x2@0.3/0.6')
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    report = []
    for item in args.pad:
        m = SPEC.match(item)
        if not m:
            raise ValueError('Bad --pad spec: ' + item)
        fp = board.FindFootprintByReference(m['ref'])
        pads = [p for p in fp.Pads() if p.GetNumber() == m['pin'] and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD]
        if len(pads) != 1:
            raise ValueError('Expected one SMD pad %s.%s' % (m['ref'], m['pin']))
        pad = pads[0]
        cols, rows = int(m['cols']), int(m['rows'])
        drill, size = pcbnew.FromMM(float(m['drill'])), pcbnew.FromMM(float(m['size']))
        # Pad box in board coordinates (pads here are axis-aligned).
        box = pad.GetBoundingBox()
        w, h = box.GetWidth(), box.GetHeight()
        if cols * size > w or rows * size > h:
            raise ValueError('%s does not fit %dx%d vias of %.2f mm' % (item, cols, rows, pcbnew.ToMM(size)))
        added = []
        for i in range(cols):
            for j in range(rows):
                x = box.GetX() + w * (2 * i + 1) // (2 * cols)
                y = box.GetY() + h * (2 * j + 1) // (2 * rows)
                via = pcbnew.PCB_VIA(board)
                via.SetPosition(pcbnew.VECTOR2I(x, y))
                via.SetDrill(drill)
                via.SetWidth(size)
                via.SetNet(pad.GetNet())
                via.SetLocked(True)
                board.Add(via)
                added.append([round(pcbnew.ToMM(x) - 70.0, 3), round(pcbnew.ToMM(y) - 45.0, 3)])
        report.append({'pad': '%s.%s' % (m['ref'], m['pin']), 'net': pad.GetNetname(),
                       'drill_mm': float(m['drill']), 'diameter_mm': float(m['size']), 'vias': added})
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps([{k: r[k] for k in ('pad', 'net')} | {'added': len(r['vias'])} for r in report]))


if __name__ == '__main__':
    main()
