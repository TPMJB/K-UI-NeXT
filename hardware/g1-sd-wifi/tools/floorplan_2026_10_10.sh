#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Re-floorplan the carrier around the FPGA (10 October 2026).
#
# Starts from the committed four-layer placement and the regenerated netlist
# (FPGA pins per design/fpga-pin-swap-2026-10-10.json, U35/U40 probe
# endpoints per design/blocks/frontend.json) and produces the six-layer,
# placed and fanned-out board ready for routing:
#   1. pad nets follow the new FPGA pin map; the sixteen BUS_DD biases go
#      (the FPGA's I/O pull-down replaces them); the input TVS D21 and the
#      host 3.3-V entry fuse F21 arrive, and the unused flex supply pins go bare;
#   2. the FPGA cluster moves 2.26 mm north; U35 turns +90 degrees and sits
#      directly under it, so BUS_DD runs straight from the FPGA's south pins
#      to U35's A side in matching order; a 10 mm band is then inserted
#      between U35 and the BIOS opening (59 x 115 mm outline), because the
#      59 x 105 mm board was 76 % full and could not route;
#   3. displaced and badly placed small ICs, then passives, are re-placed by
#      connectivity (narrow plane fanout cleared first);
#   4. bypass capacitors go back next to their owners (the moved ones first)
#      and D21 next to F20's output, F21 next to J51's 3.3-V pin,
#      and the optional reserve bank sits beside the power mux it feeds;
#   5. the raw-G1 data probes move to U40's pins (same nets, open space);
#   6. GND and +3V3_LOGIC fanout, power-return and thermal vias, six-layer stack.
# Needs KiCad 10's Python as $PY and kicad-cli as $KICAD10_CLI.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/tools
B=$ROOT/controller/KUI-G1-Bridge-RevA.kicad_pcb
R=${REPORTS:-$ROOT/design/floorplan-2026-10-10}
mkdir -p "$R"
# Start from the last four-layer placement unless told otherwise.
BASE=${BASE:-4c525f6}
git -C "$ROOT" show "$BASE:hardware/g1-sd-wifi/controller/KUI-G1-Bridge-RevA.kicad_pcb" > "$B"
run() { "$PY" "$@" 2>&1 | grep -v -e '^[0-9:]* Debug' -e 'memory leak' || true; }

run "$T/update_board_from_netlist.py" --allow-ref U10 --allow-ref D21 --new-at D21=56.5,96.5,90 \
    --allow-ref F21 --new-at F21=34.0,72.5,90 --allow-ref J50 --allow-ref J51 \
    $(printf -- '--allow-remove %s ' R223 R225 R227 R229 R231 R233 R235 R237 R239 R241 R243 R245 R247 R249 R251 R253) > "$R/pad-net-changes.json"
run "$T/move_cluster.py" --board "$B" --owner U10 \
    $(printf -- '--member %s ' C100 C101 C102 C106 C107 C108 C109 C110) \
    $(printf -- '--lift %s ' C103 C104 C105) --to 16.0,31.74,0 --report "$R/move-u10.json"
run "$T/move_cluster.py" --board "$B" --owner U35 \
    $(printf -- '--lift %s ' C224 C225 C226 C227) --to 16.5,45.95,90 --report "$R/move-u35.json"
run "$T/stretch_board.py" --board "$B" --band 10 --report "$R/stretch.json"
run "$T/replace_passives.py" --board "$B" --clear-plane-fanout --report "$R/replace-parts.json" \
    $(printf -- '--part %s ' X10 J10 J202 U28 U32 U33 U23 U207 U27 U26 U31 Q201 Q202 U34 U29 U30 \
                            U41 U44 U72 U38 U70 U39 U37 U42 U43 U73 U209) \
    $(printf -- '--bypass %s=U35 ' C224 C225 C226 C227) \
    $(printf -- '--bypass %s=U10 ' C103 C104 C105 C111 C112 C114 C115 C118) \
    --bypass C2036=K214 --bypass C119=X10 $(printf -- '--bypass %s=U20 ' C202 C203 C204 C205)
run "$T/place_local_bypass.py" --board "$B" --max-radius 10 --report "$R/bypass-owners.json" > /dev/null
run "$T/add_carrier_raw_probes.py" "$B" "$B" \
    $(for n in $(seq 100 115) 128 136; do printf -- '--replace TP%s ' "$n"; done)
run "$T/add_carrier_ground_fanout.py" "$B" "$B" --report "$R/fanout-gnd.json"
run "$T/add_carrier_ground_fanout.py" "$B" "$B" --net +3V3_LOGIC --report "$R/fanout-3v3.json"
# The whole 5-V return enters the ground plane at J20.2, and the buck
# regulators' exposed pads need a thermal and ground path: locked vias.
run "$T/add_power_vias.py" --board "$B" --pad J20.2=2x2@0.3/0.6 \
    --pad U21.9=1x2@0.2/0.5 --pad U22.9=1x2@0.2/0.5 --report "$R/power-vias.json"
run "$T/set_six_layer_stack.py" --board "$B"
