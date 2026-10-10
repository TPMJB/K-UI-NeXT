#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Finish the carrier from a Freerouting session (10 October 2026).
#
#   tools/finish_carrier_routing.sh ROUTED.ses
#
# 1. import the session into controller/KUI-G1-Bridge-RevA.kicad_pcb;
# 2. remove router leftovers that native DRC reports as dangling;
# 3. close remaining short gaps with the grid router (verified by DRC);
# 4. widen supply copper wherever DRC allows (0.6, then 0.4, then 0.3 mm);
# 5. rate the supply copper against its normal and fault currents
#    (IPC-2221, design/power-copper-check.json);
# 6. native DRC with schematic parity (design/carrier-final-drc.json).
# Needs KiCad 10's Python as $PY and kicad-cli as $KICAD10_CLI.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/tools
B=$ROOT/controller/KUI-G1-Bridge-RevA.kicad_pcb
R=${REPORTS:-$ROOT/design/routing-2026-10-10}
mkdir -p "$R"
SES=$1
run() { "$PY" "$@" 2>&1 | grep -v -e '^[0-9:]* Debug' -e 'memory leak' || true; }
SUPPLY=(CONSOLE_5V FUSED_5V PRIMARY_5V +5V_HOLD +5V_C5 RESERVE_5V +3V3_STORAGE CONSOLE_3V3 CONSOLE_3V3_FLEX)

run "$T/generate_board.py" import-session "$SES"
run "$T/remove_dangling_copper.py" --board "$B" --report "$R/dangling-after-import.json"
run "$T/close_gaps.py" --board "$B" --report "$R/close-gaps.json"
run "$T/remove_dangling_copper.py" --board "$B" --report "$R/dangling-after-gaps.json"
run "$T/widen_power_tracks.py" --board "$B" $(printf -- '--net %s ' "${SUPPLY[@]}") \
    --width 0.6 --width 0.4 --width 0.3 --report "$R/power-widths.json"
run "$T/check_power_copper.py" --board "$B" --report "$ROOT/design/power-copper-check.json"
"$KICAD10_CLI" pcb drc --format json --severity-all --schematic-parity --refill-zones \
    -o "$ROOT/design/carrier-final-drc.json" "$B" > /dev/null 2>&1 || true
python3 - "$ROOT/design/carrier-final-drc.json" <<'PY'
import collections, json, sys
r = json.load(open(sys.argv[1]))
v = collections.Counter(x['type'] for x in r['violations'])
print(json.dumps({'violations': sum(v.values()), 'by_type': dict(v), 'unconnected': len(r['unconnected_items']),
                  'parity': len(r.get('schematic_parity', []))}))
PY
