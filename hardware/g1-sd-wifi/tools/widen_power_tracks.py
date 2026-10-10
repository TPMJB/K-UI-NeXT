#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Widen routed supply copper wherever native DRC allows.

The autorouter runs supply nets at an escape width that fits the 0.4-0.5 mm
pitch pads. Afterwards this tool tries each requested width step on every
segment of the named nets, runs KiCad's own DRC, and puts back the previous
width on every segment DRC names in a clearance-type violation, repeating
until those segments are clean. Segments next to fine-pitch pads therefore
keep the narrow neck while open runs get the full width. The result is
reported per net (length at each width) for the current-capacity review.
Run with KiCad 10's Python (pcbnew) and kicad-cli on PATH or in KICAD10_CLI.
"""
import argparse
import collections
import json
import os
import pathlib
import shutil
import subprocess
import tempfile

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
CLEARANCE_TYPES = {'clearance', 'hole_clearance', 'copper_edge_clearance', 'tracks_crossing',
                   'shorting_items', 'solder_mask_bridge', 'track_dangling', 'via_dangling'}


def drc(board_path):
    cli = os.environ.get('KICAD10_CLI', 'kicad-cli')
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / 'drc.json'
        subprocess.run([cli, 'pcb', 'drc', '--format', 'json', '-o', str(out), str(board_path)],
                       check=True, capture_output=True)
        return json.loads(out.read_text())


def offenders(report, candidates):
    hit = set()
    for v in report['violations']:
        if v['type'] not in CLEARANCE_TYPES:
            continue
        for item in v['items']:
            if item.get('uuid') in candidates:
                hit.add(item['uuid'])
    return hit


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--net', action='append', required=True)
    ap.add_argument('--width', action='append', type=float, required=True,
                    help='Width steps in mm, widest first (for example 0.40 then 0.30)')
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    nets = set(args.net)
    work = pathlib.Path(tempfile.mkdtemp())
    project = args.board.with_suffix('.kicad_pro')
    scratch = work / args.board.name
    if project.exists():
        shutil.copy(project, scratch.with_suffix('.kicad_pro'))
    for extra in ('fp-lib-table', 'sym-lib-table'):
        if (args.board.parent / extra).exists():
            shutil.copy(args.board.parent / extra, work / extra)
    board = pcbnew.LoadBoard(str(args.board))
    segs = {str(t.m_Uuid.AsString()): t for t in board.GetTracks()
            if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() in nets}
    original = {u: t.GetWidth() for u, t in segs.items()}
    pcbnew.SaveBoard(str(scratch), board)
    baseline = offenders(drc(scratch), set(segs))
    if baseline:
        raise ValueError('Supply segments already violate DRC: %d' % len(baseline))
    for width in args.width:
        target = pcbnew.FromMM(width)
        trial = {u for u, t in segs.items() if t.GetWidth() < target}
        previous = {u: segs[u].GetWidth() for u in trial}
        for u in trial:
            segs[u].SetWidth(target)
        rounds = 0
        while trial:
            rounds += 1
            pcbnew.SaveBoard(str(scratch), board)
            bad = offenders(drc(scratch), trial)
            if not bad:
                break
            for u in bad:
                segs[u].SetWidth(previous[u])
            trial -= bad
            if rounds > 25:
                raise ValueError('Width step %.2f did not settle' % width)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    summary = collections.defaultdict(lambda: collections.defaultdict(float))
    for u, t in segs.items():
        summary[t.GetNetname()]['%.2f' % pcbnew.ToMM(t.GetWidth())] += pcbnew.ToMM(t.GetLength())
    result = {'nets': {n: {w: round(l, 2) for w, l in sorted(v.items())} for n, v in sorted(summary.items())},
              'widened_segments': sum(1 for u, t in segs.items() if t.GetWidth() > original[u]),
              'segments': len(segs)}
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    shutil.rmtree(work)


if __name__ == '__main__':
    main()
