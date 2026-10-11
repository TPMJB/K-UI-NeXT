# Carrier layout review — 11 October 2026

Status: **fully routed and native DRC/ERC/parity clean; normal-current screening passes, fault screening and mechanical qualification remain open.** The current PCB SHA-256 is **`8c891b8b5a62dc336a8c3707fa846e28394412a2acb061c2c4e02a207ac74ed8`**. This follow-up preserves Claude commit `4b8c7d6` routing, corrects the three protection land patterns and widens existing supply traces without moving routes or vias. The owner reviews in `manufacturing/review-evidence.json` still gate release.

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
| Supply copper screen (IPC-2221) | Normal: pass; selected 2.5 A fault screen: 138 failed track/via entries |
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
| 12 V connected to J20 by mistake | D21, High Diode SMF5.0CA shunt after F20; TVS/fuse clearing coordination remains unqualified | same; downstream parts can still be damaged while F20 clears |
| Reversed 5 V | Q20 P-MOSFET blocks it; D21 stays off, F20 survives | same |
| Short on the host-side 3.3 V buffers | F21, 0.5 A fast fuse at the only host 3.3 V entry (J51 B1) | `design/flex-interface.json`, flex checks |
| Chafed or cut flex | No console 5 V on either flex arm; contacts A1, A3, B3 end at dead fingers | `flex/tools/check_flex.py` |
| Supply copper overheating before a fuse acts | 5 V/storage nets use 1 oz outer layers; normal-current screening passes; fault-current and fuse-clearing qualification remains open | `design/power-copper-check.json` |
| Thin return path | J20's ground pad has four 0.3 mm vias into the plane; each TPS62162 thermal pad two | `design/floorplan-2026-10-10/power-vias.json` |
| Underside contact with the motherboard | Vias tented both sides; bare probe pads must be covered with Kapton after probing | `manufacturing/BRINGUP.md` |
| Harness wire before F20 | Not on the board: a ~2 A fuse at the console tap is an owner step | `docs/OWNER-NEXT-STEPS.md` |

## Supply copper screen and its limit

The corrected checker models finite via barrels by copper layer and sums branch
currents. It faults each local capacitor, IC and other rail pad, including the
logic-plane stubs omitted by the previous checker. TI TPS6216x Rev E gives a
2.45 A maximum static high-side threshold at VIN=12 V, TJ=25°C; the selected
2.5 A screen is not a guaranteed continuous fault-current bound. Dynamic peaks
may be higher. Fuse nameplate current is also not a fault-current bound.

A bounded widening pass changed 1,298 logic, eight storage and four FUSED_5V
segments. Every route coordinate, layer, net, footprint placement and via
geometry was preserved. All 92 attempted 0.5/0.8 mm via enlargements failed
native spacing checks and were restored. Final DRC/parity/opens are zero.
Normal-current screening passes; the fault screen still has 138 failed entries:
34 storage track pieces + five vias, and 12 logic track pieces + 87 vias.
The IPC numbers are steady-current estimates, not measured transient heating.

`power-copper-check.json`, `power-native-preservation-check.json`,
`power-bounded-widths.json` and `power-bounded-vias.json` record the exact result.
The former unconditional supply PASS is superseded. Remaining local paths
require fault-current/clearing and transient thermal qualification or a layout/
protection change. Neither a clean DRC nor owner acceptance supplies that evidence.

## Checks passed on this snapshot

- **All sixteen C5 spring tips match Seeed's native underside lands.** `generate_board.validate_c5_contact_group` checks every K201–K216 origin, rotation and tip against the official coordinates on each export and import; the current board passes (tolerance 0.00001 mm).
- **Flex mating positions.** Both arms: ERC 0, DRC 0, unconnected 0, parity 0; 37 connected contacts match the original logical CN503 reference, and the three unused supply contacts end at unconnected fingers. Tail handedness (pin 1 left, pin 20 right, tip north, contacts down) is checked physically.
- **Reference planes.** In1 ground and In4 +3V3_LOGIC fill the whole 59 × 115 mm outline around the BIOS opening and notch; In1 carries no signal tracks, In4 only the plane hops listed under open items.
- **Raw G1 probes.** All 39 probe pads are bare B.Cu without paste; the ATA data probes sit at U40's pins with stubs of at most 3 mm.

## Open layout items (not safety)

- **Signal copper on the +3V3_LOGIC plane (In4.Cu), 120 mm in all:** BIOS_BANK0 28.9 mm, BUS_SAFE 20.8 mm, ATA_DIORn 19.2 mm, BUS_CS1n 16.1 mm, ATA_DMACKn 14.6 mm, ATA_DD12 10.5 mm, ATA_DD5 3.8 mm, ATA_INTRQ 3.5 mm, BUS_DA2 2.4 mm. On the finished board each hop was tried again with the exhaustive grid search limited to the four signal layers: ATA_DD6's found a way and moved back, these have none. The plane is refilled around them, native DRC confirms every +3V3_LOGIC pad and via is still connected, and native connectivity passes; fault qualification remains open. They cut slots in the plane: B.Cu and In3.Cu signals that cross a slot lose their nearest return path there. PIO transaction rate alone does not establish edge/return-path integrity; prototype signal-level and timing tests must qualify these crossings.
- **Locked copper:** supply trunks, the critical regulator/crystal routes and the raw-G1 probe stubs are locked; finishing tools never move them.
- **Return paths:** In1.Cu is an unbroken ground plane with 0 signal tracks; F.Cu and In2.Cu reference it. B.Cu and In3.Cu reference the In4 +3V3 plane.
- **Router artefacts:** long detours remain where the corridors beside the BIOS opening are full (some ATA and bus lines exceed 70 mm). The ATA interface runs at PIO timings; routed detours and their return paths remain hardware timing/edge-integrity qualification items.

## Mechanical findings that block release

1. **The larger outline is unregistered.** The carrier is now 59 × 115 mm and reaches 10 mm further north than the earlier draft; the C5 module and everything north of the BIOS opening moved 10 mm north relative to the motherboard. The paper template must show CE501, CE502 and CE504 outside the outline or under a cutout, the C5 inside the 8 mm region and clear of both zero-clearance thermal chips, and the north edge clear of the AV connector area. If north does not fit, the same band can be inserted south of the notch instead.
2. **The C5 compression fixture has no finished mechanical design. All four old mounting holes hit the current copper/pads (`mechanical-support-review.md`); an independent support must use physically verified anchors.** An insulating rigid support and removable clamp must hold the copper-to-module gap at 0.90 mm, keep XY alignment and resist at least 6.24 N of spring reaction without bowing either board; hard stops must prevent over-compression.
3. **C5 height without USB is not measured.** With the 2.5 mm stand-off, 0.8 mm carrier and 0.90 mm spring gap, the module plus clamp has at most 3.80 mm nominal, or 3.70 mm with maximum carrier thickness, before other tolerances in the 8 mm region.
4. **Stand-off and insulation are not established by DRC.** FH12 closed height is 2.0 ±0.2 mm. At the 0.9 mm maximum carrier thickness, 2.5 +0.9 +2.2 =5.6 mm in the 6 mm BIOS/CN503 region, leaving 0.4 mm before other tolerances and insulation. See `sources/connector-height-review.json`. Shield contact with pads, springs, solder joints or connector shells is unacceptable.
5. **Card, flex and harness service envelopes** (microSD access, flex insertion and bend, J20 harness route and strain relief) need physical fit checks.

## Manufacturing notes

Gerbers, drill and the placement file are relative to the aux origin at the outline's south-west corner. The BIOS opening and notch are milled; their inside corners come out with the router radius (about 0.5 mm). Vias inside the J20 ground pad, the TPS62162 thermal pads and the RP2350 thermal pad are open and tented; take the fabricator's epoxy-filled, capped via option if it is free for six-layer boards. `tools/make_review_package.py` plots an unapproved review package (Gerbers, drill, BOM/CPL, renders, checks) for inspection; order only from `prepare_jlcpcb_release.py --export` after the reviews are accepted. The RP2350, microSD socket and 50 MHz oscillator have no published 3D model and render as bare pads.

## Primary source records

The geometry checks use Seeed's [official C5 V1.1 schematic/PCB archive](https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.zip), identified by native PCB SHA-256 in `sources/xiao-c5-carrier-contact-coordinates.json`, and Harwin's [S7221-45R technical drawing](https://content.harwin.com/asset/1801555a-aeb3-408d-b163-bf8c8afc2263/DRG-02334-Technical-Drawing-Datasheet-S7221-45R-pdf.pdf). The selected RP and TI circuits cite their official design guides in `controller-implementation.md` and `power-and-isolation.md`. Earlier snapshots, withdrawn candidates and their measurements remain in `carrier-layout-review-evidence.json` for traceability; they are superseded by this snapshot.
