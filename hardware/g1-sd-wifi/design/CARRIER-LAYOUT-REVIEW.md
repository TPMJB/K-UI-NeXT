# Carrier layout review — 11 October 2026

Status: **routed, native DRC clean, schematic parity 0 and supply copper rated; owner reviews and the physical fit check remain.** The reviewed PCB is `controller/KUI-G1-Bridge-RevA.kicad_pcb`, SHA-256 **`7d7c3d2693fd01a43f67d1618dabdd9e908bb48c382820c1bf2be7a41367bfc9`**. This report does not qualify electrical operation or mechanical fit and does not by itself authorize an order: the owner reviews in `manufacturing/review-evidence.json` still gate the release exporter.

| Current native snapshot | Result |
|---|---:|
| Outline | 59 × 115 mm, BIOS opening 29 × 19 mm, CN503 notch 15.2 × 47 mm |
| Copper layers | 6: F signal / In1 GND / In2 signal / In3 signal / In4 +3V3_LOGIC / B signal |
| Board thickness, copper | 0.8 mm; 1 oz outer, 0.5 oz inner |
| Physical footprints | 351 |
| Copper segments / vias | 11318 / 1004 |
| Native DRC, all severities, zones refilled | **0 violations** |
| Unconnected items | **0** |
| Schematic parity issues | **0** |
| Supply copper current check (IPC-2221) | **PASS** |
| Schematic ERC (carrier, flex A, flex B) | 0 / 0 / 0 |

## What changed since the 600-open checkpoint

- **Re-floorplan around the FPGA.** U10 moved 2.26 mm north and its pins were reassigned (`design/fpga-pin-swap-2026-10-10.json`, applied by `tools/sync_fpga_pins.py` to the block, pin table and LPF) so the RP2350 link sits on its east edge, the 16-bit data bus on its south edge in U35's A-side order, and host controls on its west edge. U35 turned +90° and sits directly below it. The sixteen BUS_DD 47 k biases were replaced by the FPGA's own I/O pull-downs.
- **Six layers.** Four layers stalled above 290 open connections. In1 is an unbroken ground reference, In4 the +3V3_LOGIC plane.
- **59 × 115 mm outline.** At 59 × 105 mm the parts covered 76 % of the usable area and routing stalled at 30 open connections, all in the corridors beside the BIOS opening. `tools/stretch_board.py` inserts a 10 mm band between U35 and the opening: the opening, notch, J50/J51 and the south half move together, so relative to the BIOS and CN503 the carrier grows 10 mm north and its south edge stays put. Total placement wire length fell from 1886 mm to 1719 mm.
- **Fire and short-circuit protection** (next section): D21 and F21 added, console supply copper removed from the flex, supply nets restricted to the outer layers, extra power-return and thermal vias.
- **Supply trunks and the host-control bundle first.** `tools/route_power_trunks.py` lays the 5 V input path (J20 → F20 → Q20 at 1.0 mm, F20 → D21 at 0.8 mm, the Q20 tree to U20, U71, C235 and C200) and the +3V3_LOGIC plane feed before any signal routing, and locks them. `tools/route_bus_bundle.py` then lays seven of the eight FPGA → U36 host-control lines, which the autorouter had left open on every earlier placement; it found no clear way for BUS_DA2, so that line is left to the autorouter.
- **0.127 mm track and space.** At 0.15 mm the router stalled near 20 open connections. 0.127 mm (5 mil) is a standard six-layer capability; the owner confirms it on the order page.
- **Reproducible flow.** `tools/floorplan_2026_10_10.sh` rebuilds the placed board from the committed four-layer placement; `tools/finish_carrier_routing.sh` takes a Freerouting session through import, cleanup, gap closing, supply widening, the current check and DRC.

## How the last connections were closed

Freerouting and the grid gap closer stopped at 4 open connections. The last ones were closed by hand-directed steps, each checked with native DRC (zones refilled) before the next:

1. **USB D− and the RP2350's north pins.** R2038 (27 Ω, USB D−) and C2016 (100 nF, U201 pin 68 bypass) swapped places, so the series resistor sits on the D− side of the pin row; C2016 then moved 0.2 mm north (its two short feeds followed) to make room for a QSPI_D0 via between it and the QFN pins. The bypass capacitor stays within 2 mm of its pin.
2. **FPGA west pins 13–15.** The autorouter's diagonal escapes from pins 13 and 14 walled pin 15 (BUS_DIOWn) in on F.Cu with no room for a via. `tools/fanout_pins.py` removed those three nets' copper beside the pin row and gave pin 15 a straight escape to its own via under the FPGA body (U10 pin 15 → via at 10.55, 33.05 mm); the gap closer then rerouted all three.
3. **Exhaustive search for long detours.** The 57 mm BUS_DIOWn route round the BIOS opening exhausted the weighted search's budget although a way existed. `close_gaps.py --exhaustive` falls back to a uniform-cost search over the whole board, which finds the cheapest way or proves there is none.
4. **One plane hop for ATA_DMACKn.** With every signal layer full beside J50, the exhaustive search proved ATA_DMACKn had no way on the four signal layers. It runs 14.6 mm on In4.Cu, confined to the east corridor beside the BIOS opening (`KUI_PLANE_BOX=34.5,50,44,100`), parallel to the corridor's other signals.

## Fire and short-circuit protection

| Hazard | Protection on the board | Evidence |
|---|---|---|
| Short or overload on the board's 5 V | F20, 1.5 A fast fuse at J20 | `design/power-and-isolation.md` |
| 12 V connected to J20 by mistake | D21, bidirectional 5 V TVS after F20: conducts above about 6.4 V and opens F20 | same; downstream parts can still be damaged while F20 clears |
| Reversed 5 V | Q20 P-MOSFET blocks it; D21 stays off, F20 survives | same |
| Short on the host-side 3.3 V buffers | F21, 0.5 A fast fuse at the only host 3.3 V entry (J51 B1) | `design/flex-interface.json`, flex checks |
| Chafed or cut flex | No console 5 V on either flex arm; contacts A1, A3, B3 end at dead fingers | `flex/tools/check_flex.py` |
| Supply copper overheating before a fuse acts | 5 V and storage nets routed only on 1 oz outer layers; widened; IPC-2221 rated at normal and fault current | `design/power-copper-check.json` |
| Thin return path | J20's ground pad has four 0.3 mm vias into the plane; each TPS62162 thermal pad two | `design/floorplan-2026-10-10/power-vias.json` |
| Underside contact with the motherboard | Vias tented both sides; bare probe pads must be covered with Kapton after probing | `manufacturing/BRINGUP.md` |
| Harness wire before F20 | Not on the board: a ~2 A fuse at the console tap is an owner step | `docs/OWNER-NEXT-STEPS.md` |

## Supply copper current check

`tools/check_power_copper.py` turns each supply net's copper into a graph, works out the current in every segment from the net's source to its loads (the budget in `power-and-isolation.md`), and rates each segment and via with the IPC-2221 fit for its layer's copper. Trunk nets must also carry their fuse or regulator current to any point where a capacitor, connector, spring contact, test point or load could short. Limits: 10 °C rise at normal current, 45 °C at fault current, doubled for pad escapes of 2 mm or less and for via barrels.

| Net | Budget / fault A | Worst rise along runs, normal / fault (°C) | Worst rise at pad necks (°C) | Copper (layer, width: length mm) |
|---|---:|---:|---:|---|
| CONSOLE_5V | 1.3 / 1.5 | 0.0 / 0.0 | 2.5 / 3.5 | F.Cu 0.60: 1.75, F.Cu 1.00: 1.05 |
| FUSED_5V | 1.3 / 1.5 | 2.5 / 3.5 | 0.0 / 0.0 | F.Cu 0.60: 2.34, F.Cu 0.80: 1.25, F.Cu 1.00: 4.23 |
| PRIMARY_5V | 1.3 / 1.5 | 1.0 / 8.1 | 1.0 / 8.1 | B.Cu 0.25: 41.42, B.Cu 0.40: 3.25, B.Cu 0.60: 34.11, F.Cu 0.25: 1.57, F.Cu 0.30: 6.1, F.Cu 0.40: 1.68, F.Cu 0.60: 24.03, F.Cu 0.80: 10.45 |
| +5V_HOLD | 0.75 / 1.5 | 7.1 / 34.3 | 1.6 / 15.8 | B.Cu 0.25: 9.26, B.Cu 0.30: 2.82, B.Cu 0.60: 18.61, F.Cu 0.25: 4.35, F.Cu 0.30: 6.15, F.Cu 0.40: 3.06, F.Cu 0.60: 4.93 |
| +5V_C5 | 0.6 / 1.5 | 2.0 / 15.8 | 1.0 / 8.1 | B.Cu 0.40: 51.8, B.Cu 0.60: 42.26, F.Cu 0.40: 26.1, F.Cu 0.60: 29.88 |
| RESERVE_5V | 0.75 / 1.5 | 5.4 / 34.3 | 5.4 / 25.4 | B.Cu 0.25: 7.72, B.Cu 0.30: 3.35, B.Cu 0.40: 8.19, B.Cu 0.60: 20.88, F.Cu 0.30: 3.2, F.Cu 0.60: 16.18 |
| +3V3_STORAGE | 0.5 / 1.5 | 2.8 / 34.3 | 0.4 / 34.3 | B.Cu 0.25: 60.28, B.Cu 0.30: 19.47, B.Cu 0.40: 3.0, B.Cu 0.60: 11.62, F.Cu 0.25: 41.71, F.Cu 0.30: 26.78, F.Cu 0.40: 9.88, F.Cu 0.60: 40.76 |
| CONSOLE_3V3_FLEX | 0.1 / 0.5 | 0.8 / 31.7 | 0.0 / 1.3 | F.Cu 0.40: 0.9, F.Cu 0.60: 3.6, In3.Cu 0.30: 3.87, In3.Cu 0.60: 1.92 |
| CONSOLE_3V3 | 0.1 / 0.5 | 1.1 / 42.8 | 0.1 / 2.8 | F.Cu 0.25: 43.25, F.Cu 0.30: 41.92, F.Cu 0.40: 21.85, F.Cu 0.60: 101.45, In2.Cu 0.25: 42.93, In2.Cu 0.40: 18.78, In2.Cu 0.60: 12.81, In3.Cu 0.40: 7.83, In3.Cu 0.60: 5.3 |
| +3V3_LOGIC | 0.35 / 1.5 | 2.9 / 0.0 | 2.9 / 0.0 | F.Cu 0.15: 134.92, F.Cu 0.25: 12.61, F.Cu 0.40: 12.49, F.Cu 0.60: 1.35 |

Limits: 10.0 °C normal, 45.0 °C fault; pad necks of 2.0 mm or less and via barrels are allowed twice that. The +3V3_LOGIC plane is fed through 3 vias rated together at 1.5 A (assumed TPS62162 current limit; owner confirms), worst 4.5 °C. Result: **PASS**.

## Checks passed on this snapshot

- **All sixteen C5 spring tips match Seeed's native underside lands.** `generate_board.validate_c5_contact_group` checks every K201–K216 origin, rotation and tip against the official coordinates on each export and import; the current board passes (tolerance 0.00001 mm).
- **Flex mating positions.** Both arms: ERC 0, DRC 0, unconnected 0, parity 0; 37 connected contacts match the original logical CN503 reference, and the three unused supply contacts end at unconnected fingers. Tail handedness (pin 1 left, pin 20 right, tip north, contacts down) is checked physically.
- **Reference planes.** In1 ground and In4 +3V3_LOGIC fill the whole 59 × 115 mm outline around the BIOS opening and notch; In1 carries no signal tracks, In4 only the plane hops listed under open items.
- **Raw G1 probes.** All 39 probe pads are bare B.Cu without paste; the ATA data probes sit at U40's pins with stubs of at most 3 mm.

## Open layout items (not safety)

- **Signal copper on the +3V3_LOGIC plane (In4.Cu), 120 mm in all:** BIOS_BANK0 28.9 mm, BUS_SAFE 20.8 mm, ATA_DIORn 19.2 mm, BUS_CS1n 16.1 mm, ATA_DMACKn 14.6 mm, ATA_DD12 10.5 mm, ATA_DD5 3.8 mm, ATA_INTRQ 3.5 mm, BUS_DA2 2.4 mm. On the finished board each hop was tried again with the exhaustive grid search limited to the four signal layers: ATA_DD6's found a way and moved back, these have none. The plane is refilled around them, native DRC confirms every +3V3_LOGIC pad and via is still connected, and the supply check passes. They cut slots in the plane: B.Cu and In3.Cu signals that cross a slot lose their nearest return path there. Acceptable for these bus and control lines (ATA PIO strobes at most, edges of a few ns); the owner confirms in the layout review.
- **Locked copper:** supply trunks, the critical regulator/crystal routes and the raw-G1 probe stubs are locked; finishing tools never move them.
- **Return paths:** In1.Cu is an unbroken ground plane with 0 signal tracks; F.Cu and In2.Cu reference it. B.Cu and In3.Cu reference the In4 +3V3 plane.
- **Router artefacts:** long detours remain where the corridors beside the BIOS opening are full (some ATA and bus lines exceed 70 mm). The ATA interface runs at PIO timings; lengths are not matched and need no matching at these speeds.

## Mechanical findings that block release

1. **The larger outline is unregistered.** The carrier is now 59 × 115 mm and reaches 10 mm further north than the earlier draft; the C5 module and everything north of the BIOS opening moved 10 mm north relative to the motherboard. The paper template must show CE501, CE502 and CE504 outside the outline or under a cutout, the C5 inside the 8 mm region and clear of both zero-clearance thermal chips, and the north edge clear of the AV connector area. If north does not fit, the same band can be inserted south of the notch instead.
2. **The C5 compression fixture has no finished mechanical design.** An insulating rigid support and removable clamp must hold the copper-to-module gap at 0.90 mm, keep XY alignment and resist at least 6.24 N of spring reaction without bowing either board; hard stops must prevent over-compression.
3. **C5 height without USB is not measured.** With the 2.5 mm stand-off, 0.8 mm carrier and 0.90 mm spring gap, the module plus clamp has at most 3.80 mm nominal in the 8 mm region.
4. **Stand-off and insulation are not established by DRC.** The 2.0 mm FPC connectors reach 5.30 mm in the 6 mm BIOS/CN503 region, leaving 0.70 mm before tolerances and insulation. Shield contact with pads, springs, solder joints or connector shells is unacceptable.
5. **Card, flex and harness service envelopes** (microSD access, flex insertion and bend, J20 harness route and strain relief) need physical fit checks.

## Manufacturing notes

Gerbers, drill and the placement file are relative to the aux origin at the outline's south-west corner. The BIOS opening and notch are milled; their inside corners come out with the router radius (about 0.5 mm). Vias inside the J20 ground pad, the TPS62162 thermal pads and the RP2350 thermal pad are open and tented; take the fabricator's epoxy-filled, capped via option if it is free for six-layer boards. `tools/make_review_package.py` plots an unapproved review package (Gerbers, drill, BOM/CPL, renders, checks) for inspection; order only from `prepare_jlcpcb_release.py --export` after the reviews are accepted. The RP2350, microSD socket and 50 MHz oscillator have no published 3D model and render as bare pads.

## Primary source records

The geometry checks use Seeed's [official C5 V1.1 schematic/PCB archive](https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.zip), identified by native PCB SHA-256 in `sources/xiao-c5-carrier-contact-coordinates.json`, and Harwin's [S7221-45R technical drawing](https://content.harwin.com/asset/1801555a-aeb3-408d-b163-bf8c8afc2263/DRG-02334-Technical-Drawing-Datasheet-S7221-45R-pdf.pdf). The selected RP and TI circuits cite their official design guides in `controller-implementation.md` and `power-and-isolation.md`. Earlier snapshots, withdrawn candidates and their measurements remain in `carrier-layout-review-evidence.json` for traceability; they are superseded by this snapshot.
