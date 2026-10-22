#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Rebuilds the CF board from tools/design.py: schematic, netlist check,
# placement, autorouting, ground pours, DRC, and the fabrication files.
# Needs KiCad 7 (kicad-cli and its python3 module), kiutils (pip install
# kiutils), Java 17 or later with xvfb-run, and Freerouting 1.9:
#   FREEROUTING=/path/to/freerouting-1.9.0.jar sh tools/build.sh
# Freerouting may route differently from one version to the next; the board
# in the repository is the one its DRC and netlist checks passed.
set -eu
cd "$(dirname "$0")/.."
PY=${PY:-python3}
KPY=${KPY:-/usr/bin/python3}
: "${FREEROUTING:?set FREEROUTING to the path of freerouting-1.9.0.jar}"
export KICAD7_FOOTPRINT_DIR="${KICAD7_FOOTPRINT_DIR:-/usr/share/kicad/footprints}"
export KICAD7_SYMBOL_DIR="${KICAD7_SYMBOL_DIR:-/usr/share/kicad/symbols}"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

$PY tools/gen_schematic.py
kicad-cli sch export netlist --format kicadsexpr -o "$W/cf-board.net" cf-board.kicad_sch >/dev/null
$KPY tools/check_netlist.py "$W/cf-board.net"
$KPY tools/gen_board.py place "$W/cf-board.net" "$W/cf-board.dsn"
(cd "$W" && xvfb-run -a java -jar "$FREEROUTING" -de cf-board.dsn -do cf-board.ses -mp 100 -dct 0 >freerouting.log 2>&1)
$KPY tools/gen_board.py route "$W/cf-board.net" "$W/cf-board.ses"
$KPY tools/drc.py "$W/drc.rpt"
$KPY tools/check_netlist.py cf-board.kicad_pcb
sh tools/export.sh
