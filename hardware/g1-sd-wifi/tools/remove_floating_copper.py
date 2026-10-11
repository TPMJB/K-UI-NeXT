#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Remove copper islands that reach no pad and no zone.

Rip-up and rerouting can leave a net's tracks and vias joined to each other
but to nothing else: no pad, no zone. remove_dangling_copper.py keeps such an
island when none of its ends dangles (a via between two tracks, say), and
native DRC then reports it as an open connection of its net. This removes
those islands, and zero-length track pieces anywhere, then saves the board.
Supply nets are included: an island with no pad and no zone carries nothing.
"""
import argparse
import json
import pathlib

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    pro = args.board.with_suffix('.kicad_pro')
    rules_before = pro.read_bytes()
    board = pcbnew.LoadBoard(str(args.board))
    zero = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetStart() == t.GetEnd()]
    for t in zero:
        board.Remove(t)
    board.BuildConnectivity()
    conn = board.GetConnectivity()
    seen, floating = set(), []
    for t in board.GetTracks():
        u = t.m_Uuid.AsString()
        if u in seen:
            continue
        island = [t] + list(conn.GetConnectedItems(t))
        seen.update(i.m_Uuid.AsString() for i in island)
        if not any(i.GetClass() in ('PAD', 'ZONE') for i in island):
            floating.extend(i for i in island if i.Type() in (pcbnew.PCB_TRACE_T, pcbnew.PCB_VIA_T))
    removed = {}
    for t in floating:
        removed[t.GetNetname()] = removed.get(t.GetNetname(), 0) + 1
        board.Remove(t)
    pcbnew.SaveBoard(str(args.board), board)
    if pro.read_bytes() != rules_before:
        pro.write_bytes(rules_before)
        raise SystemExit('Saving changed the project rules file; restored it.')
    result = {'zero_length_removed': len(zero), 'floating_items_removed': len(floating), 'by_net': removed}
    print(json.dumps(result))
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
