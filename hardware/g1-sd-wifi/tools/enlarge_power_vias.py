#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One bounded enlargement candidate for vias named by the copper screen.

The report must identify the exact input board. Try 0.5-mm drill / 0.8-mm
diameter, restore every via involved in native DRC violations, and keep the
result only if DRC still has zero physical violations and unconnected items.
No new vias or route changes are made. Run with KiCad 10 pcbnew and CLI.
"""
import argparse
import hashlib
import json
import pathlib

import pcbnew

from widen_power_tracks import drc

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller/KUI-G1-Bridge-RevA.kicad_pcb'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--fault-report', type=pathlib.Path, required=True)
    ap.add_argument('--report', type=pathlib.Path, required=True)
    args = ap.parse_args()
    raw = args.board.read_bytes()
    faults = json.loads(args.fault_report.read_text())
    if faults['board_sha256'] != hashlib.sha256(raw).hexdigest():
        raise ValueError('Fault report does not identify the current board')
    baseline = drc(args.board)
    if baseline['violations'] or baseline['unconnected_items']:
        raise ValueError('Input board has native physical DRC/connectivity failures')
    ids = {f['uuid'] for f in faults['failures'] if f['item'] == 'via'}
    board = pcbnew.LoadBoard(str(args.board))
    vias = {v.m_Uuid.AsString(): v for v in board.GetTracks()
            if v.Type() == pcbnew.PCB_VIA_T and v.m_Uuid.AsString() in ids}
    if set(vias) != ids:
        raise ValueError('Fault vias missing from board')
    original = {u: (v.GetDrillValue(), v.GetWidth(pcbnew.F_Cu)) for u, v in vias.items()}
    accepted = set(vias)
    rejected = set()
    try:
        for via in vias.values():
            via.SetDrill(pcbnew.FromMM(.5))
            via.SetWidth(pcbnew.FromMM(.8))
        for _ in range(3):
            pcbnew.SaveBoard(str(args.board), board)
            native = drc(args.board)
            if not native['violations'] and not native['unconnected_items']:
                break
            bad = {i['uuid'] for v in native['violations'] for i in v['items']
                   if i.get('uuid') in accepted}
            if not bad:
                raise ValueError('Enlargement introduced DRC failures not mapped to candidate vias')
            for u in bad:
                vias[u].SetDrill(original[u][0])
                vias[u].SetWidth(original[u][1])
            accepted -= bad
            rejected |= bad
        else:
            raise ValueError('Bounded enlargement did not settle in three DRC rounds')
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
        pcbnew.SaveBoard(str(args.board), board)
        native = drc(args.board)
        if native['violations'] or native['unconnected_items']:
            raise ValueError('Final enlarged board failed native DRC')
    except Exception:
        args.board.write_bytes(raw)
        raise
    result = {'drill_mm': .5, 'diameter_mm': .8, 'attempted_vias': len(ids),
              'accepted_vias': len(accepted), 'rejected_vias': len(rejected),
              'accepted_uuids': sorted(accepted), 'rejected_uuids': sorted(rejected),
              'native_physical_violations': 0, 'native_unconnected_items': 0}
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if not k.endswith('_uuids')}))


if __name__ == '__main__':
    main()
