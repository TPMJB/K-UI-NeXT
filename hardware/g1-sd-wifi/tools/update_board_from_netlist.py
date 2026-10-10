#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply reviewed schematic changes to the routed carrier without re-placing it.

Adds footprints that are new in the exported netlist, swaps a footprint whose
library identity changed (keeping position and orientation), and moves pads to
their new nets. Copper attached to a pad whose net changes is deleted and
listed, so it must be rerouted; nothing else on the board is touched. Finishes
with the board generator's native metadata sync. Run with KiCad 10's Python.
"""
import argparse
import importlib.util
import json
import pathlib

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAD = ROOT / 'controller'
BOARD = CAD / 'KUI-G1-Bridge-RevA.kicad_pcb'
spec = importlib.util.spec_from_file_location('generate_board', ROOT / 'tools' / 'generate_board.py')
gb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gb)


def copper_on_pad(board, pad):
    items = []
    for t in board.GetTracks():
        if t.GetNetCode() != pad.GetNetCode():
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            if pad.HitTest(t.GetPosition()):
                items.append(t)
        elif pad.HitTest(t.GetStart()) or pad.HitTest(t.GetEnd()):
            items.append(t)
    return items


def dangling_vias(board, removed):
    """Vias left with no track after the removed stubs (pad-fanout vias)."""
    ends = set()
    for t in removed:
        if t.Type() != pcbnew.PCB_VIA_T:
            ends.update({(t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)})
    result = []
    for v in board.GetTracks():
        if v.Type() != pcbnew.PCB_VIA_T or (v.GetPosition().x, v.GetPosition().y) not in ends:
            continue
        others = [t for t in board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T and t not in removed
                  and t.GetNetCode() == v.GetNetCode() and (v.HitTest(t.GetStart()) or v.HitTest(t.GetEnd()))]
        if not others:
            result.append(v)
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--components', type=pathlib.Path, default=ROOT / 'design' / 'integrated-components.json')
    ap.add_argument('--netlist', type=pathlib.Path, default=CAD / 'checks' / 'netlist.xml')
    ap.add_argument('--allow-ref', action='append', required=True,
                    help='Reference whose footprint, presence or pad nets may change')
    ap.add_argument('--new-at', action='append', default=[],
                    help='REF=x,y,rot provisional local position for an added footprint')
    args = ap.parse_args()
    allowed = set(args.allow_ref)
    new_at = {}
    for item in args.new_at:
        ref, xyr = item.split('=')
        x, y, r = (float(v) for v in xyr.split(','))
        new_at[ref] = (x, y, r)
    board = pcbnew.LoadBoard(str(BOARD))
    comps, padnets = gb.read_netlist(args.netlist)
    data = {c['ref']: c for c in gb.components(args.components)}
    nets = {n.GetNetname(): n for n in board.GetNetInfo().NetsByNetcode().values()}
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    report = {'added': [], 'swapped': [], 'pad_net_changes': [], 'removed_copper': []}
    graveyard = []

    def fpid(fp):
        return str(fp.GetFPID().GetLibNickname()) + ':' + str(fp.GetFPID().GetLibItemName())

    def from_template(ref):
        """Clone an on-board footprint with the same library identity.

        A Duplicate() keeps the localized geometry exactly and gets a new KIID;
        library-loaded footprints lose their SWIG type once owned by the board.
        """
        want = comps[ref]['footprint']
        # Materialize the list: a partly consumed SWIG iterator over the
        # board's footprint container leaves later iterations untyped.
        matches = [f for f in list(board.GetFootprints()) if fpid(f) == want]
        template = matches[0] if matches else None
        if template is None:
            raise ValueError('No on-board template for ' + want)
        fp = template.Duplicate(False).Cast()
        fp.SetReference(ref)
        fp.SetValue(comps[ref]['value'])
        for pad in fp.Pads():
            pad.SetNetCode(0)
        return fp

    for ref in sorted(set(data) - set(fps)):
        if ref not in allowed or ref not in new_at:
            raise ValueError('Unreviewed new footprint ' + ref)
        fp = from_template(ref)
        x, y, r = new_at[ref]
        fp.SetPosition(gb.p(x, y))
        fp.SetOrientationDegrees(r)
        board.Add(fp)
        fps[ref] = fp
        report['added'].append(ref)
    for ref in sorted(set(fps) - set(data)):
        raise ValueError('Footprint removed from the circuit: ' + ref)

    for ref, fp in list(fps.items()):
        want = comps[ref]['footprint']
        have = str(fp.GetFPID().GetLibNickname()) + ':' + str(fp.GetFPID().GetLibItemName())
        if want == have:
            continue
        if ref not in allowed:
            raise ValueError('Unreviewed footprint change at ' + ref)
        for pad in fp.Pads():
            for t in copper_on_pad(board, pad):
                report['removed_copper'].append({'ref': ref, 'pin': pad.GetNumber(), 'net': t.GetNetname()})
                board.Remove(t)
        new = from_template(ref)
        new.SetPosition(fp.GetPosition())
        new.SetOrientationDegrees(fp.GetOrientationDegrees())
        board.Remove(fp)
        # Keep the removed footprint referenced until the board is saved;
        # collecting its proxy early leaves later container iteration untyped.
        graveyard.append(fp)
        board.Add(new)
        fps[ref] = new
        report['swapped'].append({'ref': ref, 'from': have, 'to': want})

    # Re-read: footprints added above are owned by the board now.
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    for ref, fp in fps.items():
        for pad in fp.Pads():
            name = padnets.get((ref, pad.GetNumber()))
            if name is None or name == pad.GetNetname():
                continue
            if ref not in allowed:
                raise ValueError('Unreviewed pad-net change at %s.%s' % (ref, pad.GetNumber()))
            removed = copper_on_pad(board, pad) if pad.GetNetname() else []
            removed += dangling_vias(board, removed)
            for t in removed:
                report['removed_copper'].append({'ref': ref, 'pin': pad.GetNumber(), 'net': t.GetNetname()})
                board.Remove(t)
            if name not in nets:
                net = pcbnew.NETINFO_ITEM(board, name)
                board.Add(net)
                nets[name] = net
            report['pad_net_changes'].append({'ref': ref, 'pin': pad.GetNumber(),
                                              'old_net': pad.GetNetname(), 'new_net': name})
            pad.SetNet(nets[name])

    report['metadata_synced'] = gb.sync_native_metadata(board, args.components, args.netlist)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(BOARD), board)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
