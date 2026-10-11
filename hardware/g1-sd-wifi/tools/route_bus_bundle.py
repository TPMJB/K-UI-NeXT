#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Lay the FPGA -> U36 host-control bundle before signal routing, locked.

The eight bridge-side ATA control lines (DA0-2, CS0-1, DIOR, DIOW, DMACK)
run about 60 mm from the FPGA's west pins to U36, the host-side buffer
beside the CN503 flex sockets. The autorouter left four or five of them open
on every placement tried: by the time it reaches them the west corridor and
the strip under the BIOS opening are full. This lays the bundle first on
the placed board with close_gaps.py's grid search, at the Default netclass
width with signal vias. No track fits between U36's 0.65 mm pins, so each
pin escapes straight out; the lines are laid from the north end of each pin
row southwards, so every new line passes outside pins already connected
instead of sealing them in. The FPGA's west pins face a column of bypass
capacitors 1 mm away, so most of these lines escape inwards under the
TQFP body (it has no exposed pad) and leave on an inner layer. The shortest
way then runs past U35 (whose fanout vias need the inner layers) and down
the corridor beside J50 (which the 16-bit ATA data bus needs), so the
bundle is kept out of both boxes below and takes the west corridor and the
strip under the BIOS opening instead. A line that finds no way is left to the autorouter. Native DRC (zones refilled) must
accept every item, otherwise the tool fails. Needs numpy and Pillow
alongside pcbnew (as close_gaps.py).
"""
import argparse
import json
import pathlib
import shutil
import tempfile

import pcbnew

import close_gaps as cg
from route_power_trunks import end_cells, pads_of

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller' / 'KUI-G1-Bridge-RevA.kicad_pcb'
# U36 west row (pins 3, 5, 7, 9) then east row (18, 16, 14, 12), north first.
NETS = ['BUS_DMACKn', 'BUS_DIOWn', 'BUS_DIORn', 'BUS_CS1n', 'BUS_DA0', 'BUS_DA1', 'BUS_DA2', 'BUS_CS0n']
FROM, TO = 'U10', 'U36'
LAYERS = {0, 1, 2, 3}   # close_gaps.ROUTE_LAYERS indices: all four signal layers
# Local mm (x0, y0, x1, y1) the bundle may not enter.
KEEPOUT = [
    (9.5, 41.0, 31.0, 53.0),    # U35 and its fanout
    (34.0, 50.0, 44.0, 83.0),   # the corridor between the BIOS opening and the notch, beside J50
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--board', type=pathlib.Path, default=BOARD)
    ap.add_argument('--report', type=pathlib.Path)
    args = ap.parse_args()
    cg.load_rules(args.board)
    pro = args.board.with_suffix('.kicad_pro')
    rules_before = pro.read_bytes()
    board = pcbnew.LoadBoard(str(args.board))
    nets = {n.GetNetname(): n.GetNetCode() for n in board.GetNetInfo().NetsByNetcode().values()}
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    records = []
    for name in NETS:
        net = nets[name]
        ends = []
        for ref in (FROM, TO):
            pins = [p.GetNumber() for p in fps[ref].Pads() if p.GetNetname() == name]
            if len(pins) != 1:
                raise ValueError('%s: expected one %s pin on %s, found %s' % (name, ref, name, pins))
            ends.append(pads_of(board, '%s.%s' % (ref, pins[0])))
        track, via = cg.build(board)
        for x0, y0, x1, y1 in KEEPOUT:
            win = (slice(int(y0 / cg.G), int(y1 / cg.G) + 1), slice(int(x0 / cg.G), int(x1 / cg.G) + 1))
            via[win] = -1
            for layer in cg.ROUTE_LAYERS:
                track[layer][win] = -1
        sa, sb = end_cells(track, ends[0], net, LAYERS), end_cells(track, ends[1], net, LAYERS)
        path = cg.astar(track, via, net, sa, sb, (0, 0, cg.NX - 1, cg.NY - 1), LAYERS) if sa and sb else None
        if path is None:
            # Left to the autorouter; the rest of the bundle still goes in.
            records.append({'net': name, 'laid': False, 'uuids': []})
            print(json.dumps({'net': name, 'laid': False}), flush=True)
            continue
        added = []
        cg.add_path(board, cg.simplify(path, track, net), net, added)
        for it in added:
            it.SetLocked(True)
        rec = {'net': name, 'laid': True, 'segments': sum(1 for it in added if it.Type() == pcbnew.PCB_TRACE_T),
               'vias': sum(1 for it in added if it.Type() == pcbnew.PCB_VIA_T),
               'length_mm': round(sum(pcbnew.ToMM(it.GetLength()) for it in added
                                      if it.Type() == pcbnew.PCB_TRACE_T), 1),
               'uuids': [it.m_Uuid.AsString() for it in added]}
        records.append(rec)
        print(json.dumps({k: v for k, v in rec.items() if k != 'uuids'}), flush=True)
    work = pathlib.Path(tempfile.mkdtemp())
    for extra in (pro, args.board.parent / 'fp-lib-table'):
        shutil.copy(extra, work / extra.name)
    if (args.board.parent / 'KUI_Footprints.pretty').exists():
        shutil.copytree(args.board.parent / 'KUI_Footprints.pretty', work / 'KUI_Footprints.pretty')
    scratch = work / args.board.name
    pcbnew.SaveBoard(str(scratch), board)
    check = cg.drc(scratch, work)
    ours = {u for r in records for u in r['uuids']}
    bad = [v for v in check['violations'] if any(i['uuid'] in ours for i in v['items'])
           and v['type'] not in cg.CLEANUP_TYPES]
    shutil.rmtree(work)
    if bad:
        raise SystemExit('Bundle copper fails native DRC: %s' % json.dumps(
            [(v['type'], v['description']) for v in bad][:10]))
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(args.board), board)
    if pro.read_bytes() != rules_before:
        pro.write_bytes(rules_before)
        raise SystemExit('Saving changed the project rules file; restored it.')
    if args.report:
        args.report.write_text(json.dumps({'bundle': [{k: v for k, v in r.items() if k != 'uuids'} for r in records]},
                                          indent=2) + '\n')


if __name__ == '__main__':
    main()
