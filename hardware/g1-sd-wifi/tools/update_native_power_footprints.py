#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply the three manufacturer-recommended protection land patterns in place.

Keep reference/footprint UUID, path, placement, orientation, fields and pin
nets. Copy only library pad geometry and graphics, without board regeneration.
Native DRC and source/native parity checks must follow.
"""
import json
import pathlib

import pcbnew

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / 'controller/KUI-G1-Bridge-RevA.kicad_pcb'
PATTERNS = {'D21': 'HighDiode_SMF_SOD123FL',
            'F20': 'Littelfuse_467_0603_Recommended',
            'F21': 'Littelfuse_467_0603_Recommended'}


def main():
    board = pcbnew.LoadBoard(str(BOARD))
    evidence = []
    for ref, name in PATTERNS.items():
        fp = board.FindFootprintByReference(ref)
        source = pcbnew.FootprintLoad(str(ROOT / 'controller/KUI_Footprints.pretty'), name)
        if fp is None or source is None:
            raise ValueError('Missing native/library footprint: ' + ref)
        original = (fp.m_Uuid.AsString(), tuple(k.AsString() for k in fp.GetPath()), fp.GetPosition().x, fp.GetPosition().y,
                    fp.GetOrientationDegrees())
        pads = {p.GetNumber(): p for p in fp.Pads()}
        source_pads = {p.GetNumber(): p for p in source.Pads()}
        if set(pads) != set(source_pads):
            raise ValueError('Pad-number mismatch: ' + ref)
        pin_nets = {n: p.GetNetname() for n, p in pads.items()}
        old_id = fp.GetFPIDAsString()
        for n, pad in pads.items():
            template = source_pads[n]
            pad.SetShape(template.GetShape())
            pad.SetSize(template.GetSize())
            pad.SetFPRelativePosition(template.GetFPRelativePosition())
            pad.SetFPRelativeOrientation(template.GetFPRelativeOrientation())
        source.SetOrientation(fp.GetOrientation())
        source.SetPosition(fp.GetPosition())
        for graphic in list(fp.GraphicalItems()):
            fp.RemoveNative(graphic)
        for graphic in list(source.GraphicalItems()):
            if graphic.GetClass() == 'PCB_SHAPE':
                clone = pcbnew.PCB_SHAPE(fp)
                clone.SetShape(graphic.GetShape())
                clone.SetStart(graphic.GetStart())
                clone.SetEnd(graphic.GetEnd())
                clone.SetWidth(graphic.GetWidth())
            elif graphic.GetClass() == 'PCB_TEXT':
                clone = pcbnew.PCB_TEXT(fp)
                clone.SetText(graphic.GetText())
                clone.SetPosition(graphic.GetPosition())
                clone.SetTextSize(graphic.GetTextSize())
                clone.SetTextThickness(graphic.GetTextThickness())
                clone.SetTextAngle(graphic.GetTextAngle())
            else:
                raise ValueError('Unsupported source graphic: ' + graphic.GetClass())
            clone.SetLayer(graphic.GetLayer())
            fp.Add(clone)
        fp.SetFPIDAsString('KUI_Footprints:' + name)
        if original != (fp.m_Uuid.AsString(), tuple(k.AsString() for k in fp.GetPath()), fp.GetPosition().x, fp.GetPosition().y,
                        fp.GetOrientationDegrees()):
            raise AssertionError('Placement or identity changed: ' + ref)
        if pin_nets != {n: p.GetNetname() for n, p in pads.items()}:
            raise AssertionError('Pin nets changed: ' + ref)
        evidence.append({'reference': ref, 'old_footprint': old_id, 'new_footprint': fp.GetFPIDAsString(),
                         'pin_nets_preserved': pin_nets})
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(BOARD), board)
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
