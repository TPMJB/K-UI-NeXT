#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Writes the CF board's fabrication files (fab/) and its drawings (docs/)
# from cf-board.kicad_sch and cf-board.kicad_pcb, with KiCad 7's kicad-cli.
set -eu
cd "$(dirname "$0")/.."
KPY=${KPY:-/usr/bin/python3}
rm -rf fab docs
mkdir -p fab/gerbers docs
kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
    --subtract-soldermask -o fab/gerbers/ cf-board.kicad_pcb >/dev/null
kicad-cli pcb export drill --format excellon --excellon-separate-th -o fab/gerbers/ cf-board.kicad_pcb >/dev/null
# One archive for the board house, with fixed dates so it only changes when the board does.
$KPY - <<'PY'
import zipfile
from pathlib import Path
files = sorted(Path("fab/gerbers").iterdir())
with zipfile.ZipFile("fab/cf-board-gerbers.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for f in files:
        info = zipfile.ZipInfo(f.name, date_time=(2026, 9, 29, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, f.read_bytes())
PY
rm -rf fab/gerbers
kicad-cli pcb export pos --format csv --units mm --side both -o fab/cf-board-positions.csv cf-board.kicad_pcb >/dev/null
$KPY tools/bom.py > fab/cf-board-bom.csv
kicad-cli sch export pdf -o docs/cf-board-schematic.pdf cf-board.kicad_sch >/dev/null
# Printed at 100 %, the fit template is the board at full size; its 50 mm bar checks the print.
kicad-cli pcb export pdf --layers Edge.Cuts,F.SilkS,F.Fab,Dwgs.User --black-and-white \
    -o docs/cf-board-fit-template.pdf cf-board.kicad_pcb >/dev/null
kicad-cli pcb export svg --layers F.Cu,F.SilkS,F.Mask,Edge.Cuts --page-size-mode 2 --exclude-drawing-sheet \
    -o docs/cf-board-top.svg cf-board.kicad_pcb >/dev/null
kicad-cli pcb export svg --layers B.Cu,B.SilkS,B.Mask,Edge.Cuts --mirror --page-size-mode 2 --exclude-drawing-sheet \
    -o docs/cf-board-bottom.svg cf-board.kicad_pcb >/dev/null
ls fab docs
