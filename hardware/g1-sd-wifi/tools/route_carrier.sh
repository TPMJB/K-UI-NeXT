#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Route the placed carrier with Freerouting 1.9, keeping a resumable
# checkpoint in the repository (design/routing-checkpoint/).
#
#   tools/route_carrier.sh start [SEED]   export the committed board's DSN and route from scratch
#   tools/route_carrier.sh resume [SEED]  continue from the checkpoint session
#   tools/route_carrier.sh save SES LOG   make SES (routed from the checkpoint DSN) the checkpoint
#   tools/route_carrier.sh status         what the checkpoint holds
#   tools/route_carrier.sh finish         tools/finish_carrier_routing.sh on the checkpoint session
#
# A run writes WORK/run.ses (best so far) and WORK/run.log; `save` copies the
# best session into the checkpoint with its open-connection count, so a new
# session or container continues from it instead of routing again. The
# checkpoint names the board it belongs to (SHA-256); resume and finish refuse
# a checkpoint made for a different board.
# Needs KiCad 10's Python as $PY (PYNP with numpy/Pillow for finishing),
# kicad-cli as $KICAD10_CLI, the Freerouting 1.9.0 JAR as $FREEROUTING19 and
# a JDK (java/javac on PATH or $JAVA_HOME). WORK defaults to a temp folder.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/tools
B=$ROOT/controller/KUI-G1-Bridge-RevA.kicad_pcb
CK=$ROOT/design/routing-checkpoint
WORK=${WORK:-$(mktemp -d)}
JAVA=${JAVA_HOME:+$JAVA_HOME/bin/}java
JAVAC=${JAVA_HOME:+$JAVA_HOME/bin/}javac
PRIORITY='^(ATA_|BUS_D|BUS_CS|BUS_RESET|LINK_|QSPI|MCU_USB|C5_M|SD_)'
mkdir -p "$CK" "$WORK"

board_sha() { sha256sum "$B" | cut -d' ' -f1; }
check_board() {
    local want; want=$(python3 -c "import json;print(json.load(open('$CK/checkpoint.json'))['board_sha256'])")
    if [ "$want" != "$(board_sha)" ]; then
        echo "Checkpoint was made for another board ($want); start again." >&2; exit 1
    fi
}
classes() {
    if [ ! -f "$WORK/classes/Headless19Ripup.class" ]; then
        mkdir -p "$WORK/classes"
        "$JAVAC" -nowarn -cp "$FREEROUTING19" -d "$WORK/classes" "$T/Headless19Ripup.java"
    fi
}
route() {   # route DSN SEED
    classes
    echo "Routing $1 in $WORK (log: $WORK/run.log)"
    "$JAVA" -Xmx3g -Djava.awt.headless=true -cp "$FREEROUTING19:$WORK/classes" Headless19Ripup \
        "$1" "$WORK/run.ses" 80 14400 3000 false "$PRIORITY" "${2:-0}" 3 > "$WORK/run.log" 2>&1
    grep -E '^(PASS|DONE)' "$WORK/run.log" | tail -3
}

case "${1:-}" in
start)
    "$PY" "$T/generate_board.py" export-dsn --dsn "$WORK/native.dsn" --signal-starter "$WORK/carrier.dsn" \
        --power-width 0.25 > /dev/null 2>&1
    cp "$WORK/carrier.dsn" "$CK/carrier.dsn"
    python3 - "$CK/checkpoint.json" "$(board_sha)" <<'PY'
import json, sys
json.dump({'board_sha256': sys.argv[2], 'session': None, 'incomplete': None}, open(sys.argv[1], 'w'), indent=2)
PY
    route "$CK/carrier.dsn" "${2:-0}"
    ;;
resume)
    check_board
    python3 "$T/ses_to_dsn.py" "$CK/carrier.dsn" "$CK/carrier.ses" "$WORK/resume.dsn"
    route "$WORK/resume.dsn" "${2:-7}"
    echo "The new session holds the checkpoint's copper plus this run's; save it with: $0 save $WORK/run.ses $WORK/run.log"
    ;;
save)
    ses=$2; log=$3
    open=$(grep -oE 'best=[0-9]+' "$log" | tail -1 | cut -d= -f2)
    old=$(python3 -c "import json;print(json.load(open('$CK/checkpoint.json')).get('incomplete') or 10**9)")
    if [ -n "$open" ] && [ "$open" -ge "$old" ]; then
        echo "Not saved: $open open connections, checkpoint has $old." >&2; exit 1
    fi
    # A resumed run's session already includes the checkpoint's wiring, so it
    # replaces the checkpoint session outright.
    cp "$ses" "$CK/carrier.ses"
    python3 - "$CK/checkpoint.json" "$open" <<'PY'
import datetime, json, sys
d = json.load(open(sys.argv[1]))
d.update(session='carrier.ses', incomplete=int(sys.argv[2]),
         saved_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'))
json.dump(d, open(sys.argv[1], 'w'), indent=2)
PY
    cat "$CK/checkpoint.json"
    ;;
status)
    cat "$CK/checkpoint.json"; echo; [ "$(python3 -c "import json;print(json.load(open('$CK/checkpoint.json'))['board_sha256'])")" = "$(board_sha)" ] \
        && echo "matches the committed board" || echo "STALE: made for another board"
    ;;
finish)
    check_board
    bash "$T/finish_carrier_routing.sh" "$CK/carrier.ses"
    ;;
*)
    sed -n 2,22p "$0"; exit 2
    ;;
esac
