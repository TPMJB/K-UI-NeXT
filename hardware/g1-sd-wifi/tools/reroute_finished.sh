#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Give the finished carrier back to Freerouting for more rip-up passes.
#
#   tools/reroute_finished.sh [PASSES] [SEED]
#
# After tools/finish_carrier_routing.sh the board can still have open
# connections that the grid gap closer cannot join without moving other
# nets. This exports the board as it stands (with the gap closer's copper),
# lets Freerouting rip up and reroute it, and runs the finishing steps on the
# result. The new board is kept only if native DRC shows fewer unconnected
# items and no more violations than before; otherwise the board and its
# reports are put back. Run it again while it keeps improving.
#
# KiCad and the DSN round pad and track coordinates separately, so some
# exported trace ends miss their pad by a rounding step; Headless19Ripup's
# healUnits option (20 DSN units = 2 um) puts those ends back on the pad
# before routing. Needs the same environment as route_carrier.sh and
# finish_carrier_routing.sh (PY, PYNP, KICAD10_CLI, FREEROUTING19, a JDK).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/tools
B=$ROOT/controller/KUI-G1-Bridge-RevA.kicad_pcb
WORK=${WORK:-$(mktemp -d)}
JAVA=${JAVA_HOME:+$JAVA_HOME/bin/}java
JAVAC=${JAVA_HOME:+$JAVA_HOME/bin/}javac
PRIORITY='^(ATA_|BUS_D|BUS_CS|BUS_RESET|LINK_|QSPI|MCU_USB|C5_M|SD_)'
PASSES=${1:-8}
SEED=${2:-5}
REPORTS=(design/carrier-final-drc.json design/power-copper-check.json design/routing-2026-10-10)
mkdir -p "$WORK/keep"

drc_counts() {   # prints "unconnected violations" for the board on disk
    "$KICAD10_CLI" pcb drc --format json --severity-all --schematic-parity --refill-zones \
        -o "$WORK/drc.json" "$B" > /dev/null 2>&1 || true
    python3 -c "import json;r=json.load(open('$WORK/drc.json'));print(len(r['unconnected_items']),len(r['violations'])+len(r.get('schematic_parity',[])))"
}

# KiCad rewrites the project file on every board save; the design rules in
# it must come through unchanged.
PRO=${B%.kicad_pcb}.kicad_pro
PRO_SUM=$(sha256sum "$PRO")
read -r open_before bad_before < <(drc_counts)
echo "before: $open_before unconnected, $bad_before violations"
cp "$B" "$WORK/keep/"
for r in "${REPORTS[@]}"; do [ -e "$ROOT/$r" ] && cp -r "$ROOT/$r" "$WORK/keep/"; done

"$PY" "$T/generate_board.py" export-dsn --dsn "$WORK/native.dsn" --signal-starter "$WORK/reroute.dsn" \
    --power-width 0.25 > /dev/null 2>&1
if [ ! -f "$WORK/classes/Headless19Ripup.class" ]; then
    mkdir -p "$WORK/classes"
    "$JAVAC" -nowarn -cp "$FREEROUTING19" -d "$WORK/classes" "$T/Headless19Ripup.java"
fi
"$JAVA" -Xmx3g -Djava.awt.headless=true -cp "$FREEROUTING19:$WORK/classes" Headless19Ripup \
    "$WORK/reroute.dsn" "$WORK/reroute.ses" "$PASSES" 3600 3000 false "$PRIORITY" "$SEED" 3 20 \
    > "$WORK/reroute.log" 2>&1
grep -E '^(HEALED|LOADED|PASS|DONE)' "$WORK/reroute.log" | sed -E 's/ traces=.*//'

bash "$T/finish_carrier_routing.sh" "$WORK/reroute.ses" || true
read -r open_after bad_after < <(drc_counts)
echo "after: $open_after unconnected, $bad_after violations"
if [ "$open_after" -lt "$open_before" ] && [ "$bad_after" -le "$bad_before" ]; then
    echo "Kept: $open_before -> $open_after unconnected."
else
    cp "$WORK/keep/$(basename "$B")" "$B"
    for r in "${REPORTS[@]}"; do
        rm -rf "${ROOT:?}/$r"
        [ -e "$WORK/keep/$(basename "$r")" ] && cp -r "$WORK/keep/$(basename "$r")" "$ROOT/$r"
    done
    echo "Not kept: no improvement; board and reports restored."
fi
if [ "$(sha256sum "$PRO")" != "$PRO_SUM" ]; then
    echo "FAILED: the project rules file changed" >&2; exit 1
fi
