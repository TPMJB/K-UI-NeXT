# Carrier routing checkpoint

Where the carrier stands, and how to continue in a new session or container without redoing work. Keep this file current with every routing commit.

## State at this commit (11 October 2026)

- **Board:** `controller/KUI-G1-Bridge-RevA.kicad_pcb` is the placed, unrouted six-layer carrier from `tools/floorplan_2026_10_10.sh`. Native DRC with zones refilled shows 0 violations, schematic parity 0 and 477 connections to route.
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
- KiCad 10.0.6 built from source, with `PY`, `KICAD10_CLI` and the pcbnew `PYTHONPATH` set.
- A Python with numpy and Pillow beside pcbnew, as `PYNP`.
- The Freerouting 1.9.0 JAR as `FREEROUTING19`, and Java 21.

```sh
tools/route_carrier.sh status                  # does the checkpoint match the committed board?
tools/route_carrier.sh resume 7                # continue routing from the checkpoint (seed 7)
tools/route_carrier.sh save "$WORK/run.ses" "$WORK/run.log"   # keep it if it has fewer open connections
git add design/routing-checkpoint && git commit  # make the checkpoint durable
```

`tools/route_carrier.sh start` routes from scratch. It is needed only after the floorplan changes; `status` reports a stale checkpoint.

## What remains, in order

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
5. **Owner items:** see `docs/OWNER-NEXT-STEPS.md`. Also:
   - Confirm 0.127 mm track and space with the fabricator.
   - Confirm the TPS62162 current limit; 1.5 A is assumed for the plane-feed and storage fault ratings.
   - F20 runs at 87 % of its 1.5 A rating at the full 1.3 A worst-case budget. Measure the real load: sustained current should stay under about 1.1 A, or the fuse may open in normal use. That is a safe failure, but a nuisance.

## Finishing in progress (11 October 2026)

`design/routing-checkpoint/finishing-wip.kicad_pcb` is the board part-way through finishing. Native DRC 0, parity 0, supply current check passing (+5V_C5 included), 8 signal connections open. One plane hop: ATA_DD12 runs 10.5 mm on In4.Cu (the +3V3_LOGIC plane), laid by the gap closer with KUI_PLANE_HOPS=1; DRC with zones refilled accepts it and no net lost its plane connection.

It is the 42-open session of the second plain-order run (seed 11), finished with the current tools: the gap closer on a 0.025 mm grid, landing on any point of the other copper island. That beat finishing the 31-open checkpoint session (16 with the earlier gap closer, 14 now): the gap closer's result depends on where the router left its copper, not only on the open count.

From 14 open to 9:
1. The FPGA -> U36 bundle was unlocked.
2. Nine boxed-in local connections were laid with `force_supply_route.py --signal` on the 0.025 mm grid; the routes are locked.
3. One `reroute_finished.sh` round put back the 35 connections they cut.
4. The gap closer ran again.

To continue:
1. Copy it over `controller/KUI-G1-Bridge-RevA.kicad_pcb`.
2. Repeat `tools/reroute_finished.sh` while it improves.

`tools/force_supply_route.py` (and `--signal`) lays connections that are boxed in, cutting the unlocked signal copper they cross for the next round to reroute. The committed `controller/` board is still the placed board the routing checkpoint belongs to.
