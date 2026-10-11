#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unlock signal copper that earlier finishing steps locked.

force_supply_route.py --signal and route_bus_bundle.py lock what they lay
so that the next Freerouting round routes around it. Once a round has
settled, those locks only stop the next round (and the next forced route)
from moving that copper. This unlocks every locked track and via except:
  - supply nets and the critical local nets in design/carrier-critical-routes.json
    (crystal, regulator switch nodes and feedback), which stay as laid;
  - raw-G1 probe stubs: copper touching a TP pad stays locked, so the
    probe stubs keep their reviewed length.
"""
import argparse
import json
import pathlib
import re

import pcbnew

from generate_board import OUTER_ONLY_NETS

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
CRITICAL = ROOT / 'design' / 'carrier-critical-routes.json'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    args = ap.parse_args()
    keep = set(OUTER_ONLY_NETS) | {'GND', '+3V3_LOGIC', 'CONSOLE_3V3', 'CONSOLE_3V3_FLEX'}
    keep |= set(re.findall(r'"net": "([^"]+)"', CRITICAL.read_text()))
    pro = args.board.with_suffix('.kicad_pro')
    rules_before = pro.read_bytes()
    board = pcbnew.LoadBoard(str(args.board))
    probes = [p for f in board.GetFootprints() if f.GetReference().startswith('TP') for p in f.Pads()]
    unlocked = 0
    for t in board.GetTracks():
        if not t.IsLocked() or t.GetNetname() in keep:
            continue
        ends = [t.GetStart(), t.GetEnd()] if t.Type() == pcbnew.PCB_TRACE_T else [t.GetPosition()]
        if any(p.GetNetCode() == t.GetNetCode() and p.HitTest(e, t.GetWidth() // 2) for p in probes for e in ends):
            continue
        t.SetLocked(False)
        unlocked += 1
    pcbnew.SaveBoard(str(args.board), board)
    if pro.read_bytes() != rules_before:
        pro.write_bytes(rules_before)
        raise SystemExit('Saving changed the project rules file; restored it.')
    print(json.dumps({'unlocked_items': unlocked}))


if __name__ == '__main__':
    main()
