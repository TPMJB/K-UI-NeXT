# SPDX-License-Identifier: GPL-3.0-only
"""Fills the ground pours of cf-board.kicad_pcb, saves it, and runs KiCad's design rule check (KiCad 7 has no
command-line DRC, so through its Python module) and prints the report's
findings. Exit status 1 when anything is found. Usage: drc.py REPORT"""
import re
import sys
from pathlib import Path

import pcbnew

BOARD = Path(__file__).resolve().parent.parent / "cf-board.kicad_pcb"


def main(report):
    board = pcbnew.LoadBoard(str(BOARD))
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    # Keep the filled pours: the Gerbers are made from this file.
    pcbnew.SaveBoard(str(BOARD), board)
    if not pcbnew.WriteDRCReport(board, report, pcbnew.EDA_UNITS_MILLIMETRES, True):
        raise SystemExit("DRC did not run")
    text = Path(report).read_text()
    counts = {k: int(v) for v, k in re.findall(r'\*\* Found (\d+) (DRC violations|unconnected pads|Footprint errors)', text)}
    print(text if any(counts.values()) else "", end="")
    print(counts)
    return 1 if any(counts.values()) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
