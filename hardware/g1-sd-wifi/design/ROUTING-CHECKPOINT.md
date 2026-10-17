# Carrier routing checkpoint

Where the carrier stands, and how to continue in a new session or container without redoing work. Keep this file current with every routing commit.

## FPGA configuration wiring (17 October 2026)

`tools/rp_fpga_jtag_2026_10_17.sh` rebuilds the current board from the finished fedc570 carrier: it adds R2044–R2048 south of U201, lifts six slow-net segments that closed U201's pad exits, lays explicit escapes for U201 pads 21/22 and a fan-out for U10 pin 95, routes the five configuration nets and the lifted nets with `close_gaps.py` (crystal keepout; rip-up limited to named slow nets), applies the junction edits in `design/rp-fpga-jtag-2026-10-17/tidy.json`, checks DRC/opens/parity and runs `tools/check_preservation.py` against fedc570. Its reports, including `preservation-check.json` with the arguments used and the board hash, are in `design/rp-fpga-jtag-2026-10-17/`.

A rerun reproduces the same copper, but KiCad gives the new items fresh random IDs, so the board hash changes and the script overwrites `design/carrier-final-drc.json`. After a rerun, regenerate the hash-bound outputs: `check_power_copper.py --report design/power-copper-check.json`, `audit_carrier_routing.py`, `generate_parts_list.py`, both `generate_assembly_draft.py` variants, `generate_carrier_template.py` (then re-measure `mechanical/template-verification.json`), `design/carrier-layout-review-evidence.json` (its `file_sha256` table), the board hash quoted in `design/CARRIER-LAYOUT-REVIEW.md`, the review package (`make_review_package.py`) and, last, `manufacturing/preflight-report.json`.

## Routing finished (11 October 2026)

`controller/KUI-G1-Bridge-RevA.kicad_pcb` is now the fully routed carrier. Native DRC with zones refilled (all severities): 0 violations, 0 unconnected items, schematic parity 0. The supply copper current check (`design/power-copper-check.json`) passes. `design/CARRIER-LAYOUT-REVIEW.md` is the review report for this board; `design/carrier-final-drc.json` is its DRC report.

The routing checkpoint below (`design/routing-checkpoint/`) belongs to the placed board it was exported from, so `tools/route_carrier.sh status` now reports that the committed board differs from it. That is expected: re-routing from scratch is only needed after a placement change.

## Placed board the routing started from

- **Board:** the placed, unrouted six-layer carrier from `tools/floorplan_2026_10_10.sh`. Native DRC with zones refilled shows 0 violations, schematic parity 0 and 477 connections to route.
- **Supply trunks are already on the board and locked** (`tools/route_power_trunks.py`, report in `design/floorplan-2026-10-10/power-trunks.json`):
  - The 1.3 A input path: J20 → F20 → Q20 at 1.0 mm on F.Cu, with no via.
  - F20 → D21 at 0.8 mm.
  - Q20 → U20 at 0.5 mm, with a 0.25 mm neck at the fine-pitch pin.
  - Q20 → U71 / C235 and Q20 → C200 at 0.6–0.8 mm, laid out as a tree.
  - Three 0.3 mm feed vias beside L20.2 into the +3V3_LOGIC plane.
- **Rules:** 0.127 mm track and space; vias 0.2/0.5 mm for signals and 0.3/0.6 mm for supply. At 0.15 mm, routing stalled near 20 open connections. Confirm 0.127 mm against the fabricator's current six-layer capability page before ordering.
- **Routing checkpoint:** `design/routing-checkpoint/`.
  - `carrier.dsn` is the board's export.
  - `carrier.ses` is the best session so far.
  - `checkpoint.json` records its open-connection count and the SHA-256 of the board it belongs to.
- **Safety tooling fixed this session:**
  - `tools/check_power_copper.py` had been passing vacuously: it collapsed supply nets to a single node. It now follows the copper properly and rates the logic plane feed at the converter's short-circuit current.
  - `tools/close_gaps.py` had three problems, all fixed:
    - It painted obstacles half a grid cell off, which produced 0.14 mm clearance misses.
    - It could save a board without its project file, which would have reset the design rules.
    - It routed supply gaps with signal vias.

## Resume

Toolchain: `design/EDA-ENVIRONMENT.md`. You need:
- KiCad 10.0.7 built from source (the committed DRC/ERC reports name 10.0.7), with `PY`, `KICAD10_CLI` and the pcbnew `PYTHONPATH` set.
- A Python with numpy and Pillow beside pcbnew, as `PYNP`.
- The Freerouting 1.9.0 JAR as `FREEROUTING19`, and Java 21.

```sh
tools/route_carrier.sh status                  # does the checkpoint match the committed board?
tools/route_carrier.sh resume 7                # continue routing from the checkpoint (seed 7)
tools/route_carrier.sh save "$WORK/run.ses" "$WORK/run.log"   # keep it if it has fewer open connections
git add design/routing-checkpoint && git commit  # make the checkpoint durable
```

`tools/route_carrier.sh start` routes from scratch. It is needed only after the floorplan changes; `status` reports a stale checkpoint.

## Steps, in order (1–4 done)

1. **Route to a plateau and save the best session.** Two runs (plain order, and shuffled with seed 11) were started from `carrier.dsn` at this commit. At 0.127 mm the earlier trial without trunks reached 18 open connections after 11 passes.
2. **Finish:** `PYNP=… tools/finish_carrier_routing.sh design/routing-checkpoint/carrier.ses`. This runs import, dangling cleanup, gap closing, supply widening, the current check and DRC. Add `--rip` to the `close_gaps.py` step if plain gap closing leaves openings. Each rip-up attempt is kept only when native DRC shows fewer open connections and no new violation.
3. **Release gates:**
   - DRC 0 and parity 0.
   - Unconnected 0.
   - `design/power-copper-check.json` must pass. Every supply segment is rated at normal and fault current; a vacuous pass is no longer possible.
4. **Outputs:**
   - `tools/generate_assembly_draft.py`, for BASE and for BASE_PSRAM_DNP.
   - `tools/generate_carrier_template.py`, the 1:1 fit sheets.
   - `tools/make_review_package.py`: Gerbers, drill, renders and checks, marked NOT FOR ORDER.
   - `design/CARRIER-LAYOUT-REVIEW.md`.
   - The status and owner docs.
5. **Owner items (open):** see `docs/OWNER-NEXT-STEPS.md`. Also:
   - Confirm 0.127 mm track and space with the fabricator.
   - Confirm the TPS62162 current limit; 1.5 A is assumed for the plane-feed and storage fault ratings.
   - F20 runs at 87 % of its 1.5 A rating at the full 1.3 A worst-case budget. Measure the real load: sustained current should stay under about 1.1 A, or the fuse may open in normal use. That is a safe failure, but a nuisance.

## How finishing went (11 October 2026)

The 42-open session of the second plain-order run (seed 11) was finished with the current tools: the gap closer on a 0.025 mm grid, landing on any point of the other copper island. From 14 open:

1. The FPGA -> U36 bundle was unlocked.
2. Boxed-in local connections were laid with `force_supply_route.py --signal` (locked), and `reroute_finished.sh` rounds put back the connections they cut. That reached 4 open.
3. **USB D− beside the RP2350.** R2038 and C2016 swapped places so the series resistor sits on the D− side of the pin row; C2016 then moved 0.2 mm north, its two short feeds following, to make room for a QSPI_D0 via between it and the QFN pins.
4. **FPGA pin 15.** The diagonal escapes of U10 pins 13 and 14 walled pin 15 (BUS_DIOWn) in. `tools/fanout_pins.py --ref U10 --rip-box 8.3,31.5,11.6,36.6 --rip-net BUS_CS1n --rip-net BUS_DIORn --rip-net BUS_DIOWn --pin 15:10.55,33.05` gave it a straight escape to its own via under the FPGA body.
5. **Exhaustive search.** `close_gaps.py --exhaustive --net BUS_DIOWn --net BUS_DIORn --net BUS_CS1n` routed the three; the 57 mm BUS_DIOWn detour had exhausted the weighted search's budget.
6. **ATA_DMACKn** had no way on the four signal layers (the exhaustive search proves it): `KUI_PLANE_HOPS=1 KUI_PLANE_BOX=34.5,50,44,100 close_gaps.py --exhaustive --net ATA_DMACKn` laid it with a 14.6 mm hop on In4.Cu, confined to the east corridor beside the BIOS opening.
7. Dangling and floating copper cleanup, supply widening, the current check and DRC, as in `finish_carrier_routing.sh`.

Signal copper on In4.Cu (the +3V3_LOGIC plane) is listed in `design/CARRIER-LAYOUT-REVIEW.md`. Each hop was tried again on the finished board with the exhaustive search restricted to the four signal layers. ATA_DD6's 3.9 mm hop found a way and moved back; the other nine (120 mm in all) have none, so they stay.

Diagnostics that helped: a flood fill of the grid from each end of an open connection shows whether the two ends can meet at all, and which end is boxed in. Both long FPGA -> U36 lines turned out to be blocked within a millimetre of the FPGA pins, not in the corridors.
