#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Remove router leftovers that native DRC reports as dangling.

Freerouting leaves vias joined on only one layer and track stubs with a
free end after rip-up. This asks KiCad's own DRC which unlocked items are
dangling, deletes exactly those, and repeats until DRC reports none (a
removed stub can expose the via behind it). Locked copper (reviewed local
routes, plane fanout, probe taps) is never touched; a dangling locked item
is reported instead. Run with KiCad 10's Python and kicad-cli on PATH or in
KICAD10_CLI.
"""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import tempfile

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
DANGLING = {'via_dangling', 'track_dangling'}


def dangling(board_path, work):
    cli = os.environ.get('KICAD10_CLI', 'kicad-cli')
    out = work / 'drc.json'
    subprocess.run([cli, 'pcb', 'drc', '--format', 'json', '-o', str(out), str(board_path)],
                   check=True, capture_output=True)
    report = json.loads(out.read_text())
    return {i['uuid'] for v in report['violations'] if v['type'] in DANGLING for i in v['items']}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    work = pathlib.Path(tempfile.mkdtemp())
    scratch = work / args.board.name
    for extra in (args.board.with_suffix('.kicad_pro'), args.board.parent / 'fp-lib-table'):
        if extra.exists():
            shutil.copy(extra, work / extra.name.replace(args.board.stem, scratch.stem))
    board = pcbnew.LoadBoard(str(args.board))
    graveyard, removed, locked = [], [], set()
    # One pass per stub end: a grid route's staircase can leave a chain of
    # many short pieces, each exposed only when the one beyond it goes.
    for _ in range(400):
        pcbnew.SaveBoard(str(scratch), board)
        found = dangling(scratch, work)
        items = [t for t in board.GetTracks() if t.m_Uuid.AsString() in found]
        locked |= {t.m_Uuid.AsString() for t in items if t.IsLocked()}
        items = [t for t in items if not t.IsLocked()]
        if not items:
            break
        for t in items:
            removed.append({'net': t.GetNetname(), 'layer': board.GetLayerName(t.GetLayer()),
                            'via': t.Type() == pcbnew.PCB_VIA_T})
            board.Remove(t)
            graveyard.append(t)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    result = {'removed_items': len(removed), 'removed_vias': sum(r['via'] for r in removed),
              'locked_dangling_left': sorted(locked)}
    if args.report:
        args.report.write_text(json.dumps(dict(result, items=removed), indent=2) + '\n')
    print(json.dumps(result))
    shutil.rmtree(work)


if __name__ == '__main__':
    main()
