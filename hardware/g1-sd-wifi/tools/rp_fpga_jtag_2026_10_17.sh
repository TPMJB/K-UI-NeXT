#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Wire the FPGA configuration pins to the RP2350 (17 October 2026).
#
# Starts from the finished fedc570 carrier and the regenerated netlist
# (design/blocks/controller.json: U201 GPIO21/22/28/29/30 and the 220-ohm
# series resistors R2044-R2048) and produces the routed board:
#   1. the five resistors go in one row in the open field south of U201, pin 1
#      (controller side) to the north; TCK sits west of TMS so neither leg
#      crosses the other resistor;
#   2. U201 pads 21/22 share an F.Cu pocket with one via site, and pads 36-38
#      have two escapes under the package; the copper closing the second
#      pocket via (USB_VBUS_SENSE, and LINK_A1, an address line of the slow
#      asynchronous byte link) and the third south escape (C5_PWR_EN,
#      USB_SERVICE_VBUS, PWR_UPSTREAM_OK) is lifted and rerouted by the gap
#      closer;
#   3. pads 21 and 22 get explicit, locked escapes to the two pocket vias:
#      pad 22's dog-leg under pad 21 clears it by less than the gap closer's
#      one-cell margin, so native DRC checks it instead; U10 pin 95 (TDO) gets a
#      locked fan-out to a via under the FPGA body, so the TDO leg joins at the
#      pin instead of running along the old trace (the other pins' fan-out
#      sites collide with PWR_GOOD/JTAGEN copper and a GND via);
#   4. the configuration nets and the lifted nets are routed with F.Cu tracks
#      and vias kept out of three boxes around the 12-MHz crystal circuit
#      (X201, C2020, C2021, R2002 and the XIN/XOUT traces south of the U201
#      pad row); inner and bottom layers stay open under the boxes, separated
#      from F.Cu parts by the In1.Cu ground plane;
#   5. two junctions the gap closer leaves at shallow angles are tidied
#      (tools/edit_tracks.py with design/rp-fpga-jtag-2026-10-17/tidy.json),
#      leftovers are removed, the release gates checked (DRC, unconnected and
#      schematic parity all 0; project rules unchanged) and the result
#      compared with fedc570 (tools/check_preservation.py).
# FPGA_DONE is not wired to the controller: GPIO27 (pad 28) could only leave
# the package between C2009's pads and past the XIN trace, and the JTAG
# status register reports DONE.
# Rip-up, if the gap closer needs it, is limited to the nets in KUI_RIP_NETS.
#
# The script rewrites controller/KUI-G1-Bridge-RevA.kicad_pcb and
# design/carrier-final-drc.json in place whatever REPORTS says; KiCad gives
# new items random IDs, so a rerun gives the same copper under a new board
# hash, and every hash-bound report must then be regenerated
# (design/ROUTING-CHECKPOINT.md lists them). Needs KiCad 10's Python as $PY
# (PYNP with numpy/Pillow) and kicad-cli as $KICAD10_CLI. Any failing step
# stops the script.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/tools
B=$ROOT/controller/KUI-G1-Bridge-RevA.kicad_pcb
PRO=${B%.kicad_pcb}.kicad_pro
DRC=$ROOT/design/carrier-final-drc.json
R=${REPORTS:-$ROOT/design/rp-fpga-jtag-2026-10-17}
BASE=${BASE:-fedc570}
TIDY=$ROOT/design/rp-fpga-jtag-2026-10-17/tidy.json
: "${PY:?set PY to KiCad 10 Python with pcbnew (design/EDA-ENVIRONMENT.md)}"
: "${KICAD10_CLI:?set KICAD10_CLI to KiCad 10 kicad-cli}"
PYNP=${PYNP:-$PY}
git -C "$ROOT" cat-file -e "$BASE:hardware/g1-sd-wifi/controller/KUI-G1-Bridge-RevA.kicad_pcb"
[ -f "${B%.kicad_pcb}.kicad_sch" ] || { echo "FAILED: no root schematic; parity would be meaningless" >&2; exit 1; }
[ -f "$ROOT/controller/checks/netlist.xml" ] || { echo "FAILED: no netlist (controller/checks/netlist.xml)" >&2; exit 1; }
[ -x "$KICAD10_CLI" ] || { echo "FAILED: $KICAD10_CLI is not executable" >&2; exit 1; }
"$PY" -c 'import pcbnew' 2> /dev/null || { echo "FAILED: $PY cannot import pcbnew" >&2; exit 1; }
"$PYNP" -c 'import numpy, PIL, pcbnew' 2> /dev/null || { echo "FAILED: $PYNP lacks numpy/Pillow/pcbnew" >&2; exit 1; }
mkdir -p "$R"
REPORT_NAMES=(pad-net-changes lift-west lift-east escapes-west fanout-u10 close-gaps tidy-applied dangling preservation-check)
for name in "${REPORT_NAMES[@]}"; do rm -f "$R/$name.json"; done
rm -f "$DRC"
git -C "$ROOT" show "$BASE:hardware/g1-sd-wifi/controller/KUI-G1-Bridge-RevA.kicad_pcb" > "$B.tmp"
mv "$B.tmp" "$B"
PRO_SUM=$(sha256sum "$PRO")
# Each step must succeed. A step's own output (the JSON reports) is kept;
# KiCad's debug and SWIG leak chatter is filtered out of both streams.
CHATTER=(-e '^[0-9:]* Debug' -e 'memory leak' -e 'schema file')
run_with() {
    local py=$1; shift
    local out err; out=$(mktemp); err=$(mktemp)
    if ! "$py" "$@" > "$out" 2> "$err"; then
        grep -h -v "${CHATTER[@]}" "$out" "$err" >&2 || true
        rm -f "$out" "$err"; echo "FAILED: $*" >&2; exit 1
    fi
    grep -v "${CHATTER[@]}" "$err" >&2 || true
    grep -v "${CHATTER[@]}" "$out" || true
    rm -f "$out" "$err"
}
run() { run_with "$PY" "$@"; }
json_ok() { python3 -m json.tool "$1" > /dev/null || { echo "FAILED: $1 is not valid JSON" >&2; exit 1; }; }
export KUI_GRID_MM=0.025 OPENBLAS_NUM_THREADS=1
CRYSTAL='36.4,39.45,41.5,46.2;35.2,41.8,36.4,46.2;41.5,43.8,43.4,46.2'
LIFTED=(USB_VBUS_SENSE LINK_A1 USB_SERVICE_VBUS C5_PWR_EN PWR_UPSTREAM_OK)
export KUI_RIP_NETS='(USB_VBUS_SENSE|USB_SERVICE_VBUS|C5_PWR_EN|C5_SWITCH_EN|C5_EN_ASSERT|C5_BOOT_ASSERT|C5_IO_OEn|PWR_UPSTREAM_OK|PWR_LOCAL_OK|HOST_OKn|STORAGE_PGOOD|SD_DAT_DIR|SD_CMD_DIR|SD_IO_EN|SD_DETECTn|LINK_A1|FPGA_(TCK|TMS|TDI|TDO|PROGRAMn)(_MCU)?)'

run "$T/update_board_from_netlist.py" --allow-ref U201 \
    --allow-ref R2044 --new-at R2044=32.5,48.25,270 \
    --allow-ref R2045 --new-at R2045=33.75,48.25,270 \
    --allow-ref R2048 --new-at R2048=36.25,48.25,270 \
    --allow-ref R2046 --new-at R2046=37.5,48.25,270 \
    --allow-ref R2047 --new-at R2047=38.75,48.25,270 > "$R/pad-net-changes.json"
run "$T/fanout_pins.py" --board "$B" --ref U201 --rip-box 34.3,39.2,34.65,39.65 \
    --rip-net USB_VBUS_SENSE --rip-net LINK_A1 > "$R/lift-west.json"
run "$T/fanout_pins.py" --board "$B" --ref U201 --rip-box 42.1,39.3,42.6,40.0 \
    --rip-net C5_PWR_EN --rip-net USB_SERVICE_VBUS --rip-net PWR_UPSTREAM_OK > "$R/lift-east.json"
run "$T/fanout_pins.py" --board "$B" --ref U201 --pin 21:34.5,38.85 \
    --pin 22:35.7,39.3/35.7,39.45/35.6,39.55/34.95,39.55 > "$R/escapes-west.json"
run "$T/fanout_pins.py" --board "$B" --ref U10 --pin 95:12.5,25.65 > "$R/fanout-u10.json"
KUI_KEEPOUT_BOX=$CRYSTAL run_with "$PYNP" "$T/close_gaps.py" --board "$B" --rip \
    $(printf -- '--net %s ' FPGA_TCK_MCU FPGA_TMS_MCU FPGA_TDO_MCU FPGA_TDI_MCU FPGA_PROGRAMn_MCU \
        "${LIFTED[@]}" FPGA_TCK FPGA_TMS FPGA_TDI FPGA_TDO FPGA_PROGRAMn) \
    --report "$R/close-gaps.json" | tail -1
run "$T/edit_tracks.py" --board "$B" --edits "$TIDY" > "$R/tidy-applied.json"
run "$T/remove_dangling_copper.py" --board "$B" --report "$R/dangling.json"
for name in "${REPORT_NAMES[@]}"; do
    [ "$name" = preservation-check ] && continue
    [ -s "$R/$name.json" ] || { echo "FAILED: no report $R/$name.json" >&2; exit 1; }
    json_ok "$R/$name.json"
done
if [ "$(sha256sum "$PRO")" != "$PRO_SUM" ]; then
    echo "FAILED: the project rules file changed" >&2; exit 1
fi
rm -f "$DRC"
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
run "$T/check_preservation.py" --board "$B" --base-rev "$BASE" \
    $(printf -- '--allow-net %s ' "${LIFTED[@]}") \
    $(printf -- '--pad-net-change U201:%s ' 21 22 36 37 38) \
    --report "$R/preservation-check.json" > /dev/null
json_ok "$R/preservation-check.json"
