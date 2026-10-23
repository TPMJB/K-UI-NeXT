#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Bound the 3.3-V converter fault current (23 October 2026).
#
# Starts from the 6d50fc5 carrier and the regenerated netlist, in which
# design/blocks/power-bus.json makes U21/U22 TPS62172 (0.5 A, static
# high-side limit 1.35 A max) instead of TPS62162 (2.45 A max), and:
#   1. syncs the two footprints' value fields from the netlist (same
#      footprint, pads and nets);
#   2. widens three 0.15 mm +3V3_LOGIC F.Cu links between C2017.1 and
#      C2001.1 to 0.20 mm in place (design/power-fault-screen-2026-10-23/
#      widen.json, tools/edit_tracks.py), the only copper that fails
#      tools/check_power_copper.py at 1.35 A;
#   3. corrects the stale Dwgs.User outline note;
#   4. checks DRC, unconnected items and schematic parity (all 0), the
#      project rules and the supply copper screen (tools/check_power_copper.py
#      must pass), and compares the result with 6d50fc5
#      (tools/check_preservation.py: only the three widths and the note
#      may differ).
# It rewrites controller/KUI-G1-Bridge-RevA.kicad_pcb in place; a rerun
# gives the same board. design/carrier-final-drc.json is replaced only when
# the new report differs in more than its date. If the board changed,
# regenerate the hash-bound reports (design/ROUTING-CHECKPOINT.md).
# Needs KiCad 10's Python as $PY (and $PYNP with numpy for the power check)
# and kicad-cli as $KICAD10_CLI.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/tools
B=$ROOT/controller/KUI-G1-Bridge-RevA.kicad_pcb
PRO=${B%.kicad_pcb}.kicad_pro
DRC=$ROOT/design/carrier-final-drc.json
R=$ROOT/design/power-fault-screen-2026-10-23
BASE=${BASE:-6d50fc5}
: "${PY:?set PY to KiCad 10 Python with pcbnew (design/EDA-ENVIRONMENT.md)}"
: "${KICAD10_CLI:?set KICAD10_CLI to KiCad 10 kicad-cli}"
git -C "$ROOT" cat-file -e "$BASE:hardware/g1-sd-wifi/controller/KUI-G1-Bridge-RevA.kicad_pcb"
[ -f "${B%.kicad_pcb}.kicad_sch" ] || { echo "FAILED: no root schematic; parity would be meaningless" >&2; exit 1; }
[ -f "$ROOT/controller/checks/netlist.xml" ] || { echo "FAILED: no netlist (controller/checks/netlist.xml)" >&2; exit 1; }
[ -x "$KICAD10_CLI" ] || { echo "FAILED: $KICAD10_CLI is not executable" >&2; exit 1; }
"$PY" -c 'import pcbnew' 2> /dev/null || { echo "FAILED: $PY cannot import pcbnew" >&2; exit 1; }
PYNP=${PYNP:-$PY}
"$PYNP" -c 'import numpy, pcbnew' 2> /dev/null || { echo "FAILED: $PYNP lacks numpy/pcbnew" >&2; exit 1; }
WIDEN=(13c7275c-07ac-4d3f-8204-3d0de1fd9ebb 1cbce68d-807c-46f9-9657-3faaa33de420 9dc5389c-2b88-4317-b8e1-94b6b2696570)
NOTE=26c84136-951e-4294-93a8-b9ab47a0fe02
REPORT_NAMES=(value-sync widen-applied preservation-check)
for name in "${REPORT_NAMES[@]}"; do rm -f "$R/$name.json"; done
# The old DRC report is set aside, not left in place, so a failed run cannot
# leave a passing report beside a rewritten board.
[ ! -f "$DRC" ] || mv "$DRC" "$DRC.prev"
git -C "$ROOT" show "$BASE:hardware/g1-sd-wifi/controller/KUI-G1-Bridge-RevA.kicad_pcb" > "$B.tmp"
mv "$B.tmp" "$B"
PRO_SUM=$(sha256sum "$PRO")
CHATTER=(-e '^[0-9:]* Debug' -e 'memory leak' -e 'schema file')
run() {
    local out err; out=$(mktemp); err=$(mktemp)
    if ! "$PY" "$@" > "$out" 2> "$err"; then
        grep -h -v "${CHATTER[@]}" "$out" "$err" >&2 || true
        rm -f "$out" "$err"; echo "FAILED: $*" >&2; exit 1
    fi
    grep -v "${CHATTER[@]}" "$err" >&2 || true
    grep -v "${CHATTER[@]}" "$out" || true
    rm -f "$out" "$err"
}
run "$T/update_board_from_netlist.py" --allow-ref U21 --allow-ref U22 > "$R/value-sync.json"
run "$T/edit_tracks.py" --board "$B" --edits "$R/widen.json" > "$R/widen-applied.json"
run - "$B" "$NOTE" <<'PY'
import sys, pcbnew
board = pcbnew.LoadBoard(sys.argv[1])
hits = [d for d in board.GetDrawings() if d.m_Uuid.AsString() == sys.argv[2]]
if len(hits) != 1 or hits[0].GetText() != '59 x 105 mm provisional envelope; mechanical fit NOT QUALIFIED':
    sys.exit('FAILED: outline note not found as expected')
hits[0].SetText('59 x 115 mm outline; mechanical fit NOT QUALIFIED')
pcbnew.SaveBoard(sys.argv[1], board)
PY
for name in value-sync widen-applied; do
    [ -s "$R/$name.json" ] && python3 -m json.tool "$R/$name.json" > /dev/null || { echo "FAILED: bad report $name" >&2; exit 1; }
done
if [ "$(sha256sum "$PRO")" != "$PRO_SUM" ]; then
    echo "FAILED: the project rules file changed" >&2; exit 1
fi
log=$(mktemp)
if ! "$KICAD10_CLI" pcb drc --format json --severity-all --schematic-parity --refill-zones -o "$DRC" "$B" > "$log" 2>&1; then
    grep -v "${CHATTER[@]}" "$log" >&2 || true; rm -f "$log"; echo "FAILED: kicad-cli pcb drc" >&2; exit 1
fi
rm -f "$log"
python3 - "$DRC" "$B" <<'PY'
import collections, json, os, sys
r = json.load(open(sys.argv[1]))
if os.path.basename(r.get('source', '')) != os.path.basename(sys.argv[2]):
    sys.exit('FAILED: the DRC report is not for %s' % sys.argv[2])
v = collections.Counter(x['type'] for x in r['violations'])
gates = {'violations': sum(v.values()), 'by_type': dict(v), 'unconnected': len(r['unconnected_items']),
         'parity': len(r.get('schematic_parity', []))}
print(json.dumps(gates))
if 'schematic_parity' not in r:
    sys.exit('NOT FINISHED: the DRC report has no schematic parity section.')
if gates['violations'] or gates['unconnected'] or gates['parity']:
    sys.exit('NOT FINISHED: a release gate is open (see above).')
PY
if [ -f "$DRC.prev" ]; then
    if python3 - "$DRC" "$DRC.prev" <<'PY'
import json, sys
a, b = (json.load(open(f)) for f in sys.argv[1:])
a.pop('date', None); b.pop('date', None)
sys.exit(0 if a == b else 1)
PY
    then mv "$DRC.prev" "$DRC"; else rm -f "$DRC.prev"; fi
fi
pc=$(mktemp)
if ! OPENBLAS_NUM_THREADS=1 "$PYNP" "$T/check_power_copper.py" --board "$B" --report "$pc" > /dev/null 2>&1 \
        || ! python3 -c 'import json, sys; sys.exit(0 if json.load(open(sys.argv[1]))["pass"] else 1)' "$pc"; then
    rm -f "$pc"; echo "NOT FINISHED: tools/check_power_copper.py does not pass" >&2; exit 1
fi
rm -f "$pc"
run "$T/check_preservation.py" --board "$B" --base-rev "$BASE" \
    $(printf -- '--allow-change %s ' "${WIDEN[@]}" "$NOTE") \
    --report "$R/preservation-check.json" > /dev/null
python3 -m json.tool "$R/preservation-check.json" > /dev/null
