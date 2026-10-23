# Carrier layout review — 23 October 2026

Status: **fully routed and native DRC/ERC/parity clean; the normal-current and selected fault-current copper screens pass; the flex sockets must move before either order (Mechanical findings, item 1), and protection coordination and mechanical qualification remain open.** The current PCB SHA-256 is **`6b809188278f43a0f7a8c776976dc72cf207901cd439310714d7bc09ce60fe6a`**. The 23 October follow-up bounds the converter fault current (next section); the 17 October follow-up wires U10's JTAG port and PROGRAMn to the RP2350 (the section after) and keeps everything else of the 11 October board (`8c891b8b…`, commit fedc570), which preserved Claude commit `4b8c7d6` routing, corrected the three protection land patterns and widened existing supply traces without moving routes or vias. The owner reviews in `manufacturing/review-evidence.json` still gate release.

| Current native snapshot | Result |
|---|---:|
| Outline | 59 × 115 mm, BIOS opening 29 × 19 mm, CN503 notch 15.2 × 47 mm |
| Copper layers | 6: F signal / In1 GND / In2 signal / In3 signal / In4 +3V3_LOGIC / B signal |
| Board thickness, copper | 0.8 mm; 1 oz outer, 0.5 oz inner |
| Physical footprints | 356 |
| Copper segments / vias | 12084 / 1043 |
| Native DRC, all severities, zones refilled | **0 violations** |
| Unconnected items | **0** |
| Schematic parity issues | **0** |
| Supply copper screen (IPC-2221) | Normal: pass; selected 1.35 A converter fault screen (TPS62172): pass, 0 failed entries |
| Schematic ERC (carrier, flex A, flex B) | 0 / 0 / 0 |

## Converter fault current (23 October 2026)

U21 and U22 are now TI TPS62172DSGR (0.5 A, static high-side current limit 0.85/1.05/1.35 A min/typ/max at 12 V, 25 °C) instead of TPS62162DSGR (1 A, 2.45 A maximum): same DSG0008A pinout and FB = AGND connection, and the logic (0.35 A) and storage (0.5 A) budgets still fit, with about 0.61 A peak inductor current against the 0.85 A minimum limit. Three 0.15 mm `+3V3_LOGIC` F.Cu links between C2017.1 and C2001.1 were widened to 0.20 mm in place; one runs between R2038's pads, under the 0402 body, with 0.14 mm to each pad (DRC passes; worth a look in the assembly review). `tools/check_power_copper.py` at the exact 1.35 A maximum (not rounded) now reports **0 failures** under the unchanged IPC-2221 criteria; the 2.5 A screen of the old part had 138. The tightest margins are the 87 single `+3V3_LOGIC` plane-to-pad vias (84.7 °C against 90 °C): the screen passes up to about 1.386 A, only 2.7 % above 1.35 A, so rounding up as the old screen did (to 1.4 A) would fail those vias at about 92 °C. The pass therefore rests on the exact static maximum and on the limiter's behaviour: per TI's description, once the high side reaches the limit it stays off until the low-side current falls below the low-side limit (about 0.7 A typical), so the average short-circuit current sits below the 1.35 A peak. The storage trunk reaches 27 °C. The checker merges very short staircase pieces into nodes: between C2017.1 and C2001.1, ten 0.15 mm pieces 0.1–0.25 mm long carry the current physically but are modelled at zero; they are short necks between wider copper. `tools/power_fault_screen_2026_10_23.sh` replays the change from 6d50fc5 and ends with the DRC gates (0 / 0 / 0) and `tools/check_preservation.py`: every footprint, zone and track/via is unchanged apart from the three reviewed widths and the corrected Dwgs.User outline note; U21/U22 change only their value, MPN and datasheet fields. The LCSC code C59873 comes from a search index (parts list status `catalog_identity_search_index_only_assembly_pending`); TI suggests 3.3 µH for low input voltage at the full 0.5 A, so regulation at the hold-up minimum is a bench item.

## RP2350-driven FPGA configuration (17 October 2026)

U201 can now program U10 itself: TMS GPIO21, TCK GPIO22, TDI GPIO29, TDO GPIO30 and PROGRAMn GPIO28 reach U10 through R2044–R2048 (220 Ω, Yageo RC0402FR-07220RL), placed in one row in the open field south of U201. No FPGA cable is needed once the RP2350 configuration firmware exists; it has not been written, so first bring-up still uses a target-powered 3.3 V MachXO2 cable on J10 with RUN (J203.5) held low. `tools/rp_fpga_jtag_2026_10_17.sh` replays the change from fedc570 and ends with the DRC gates and the preservation check. Two replays gave the same placement and copper item for item; KiCad gives new items random IDs, so a replay changes the board hash and every hash-bound report must be regenerated (`ROUTING-CHECKPOINT.md`). The board's `git diff` (about 11,100 lines added, 3,000 removed) is the 821 new tracks and vias, the five new footprints and the refill of two zones, whose old fill polygons account for most of the removed lines; `preservation-check.json` is the item-by-item track/via, footprint, zone and drawing comparison.

- **Pad escapes.** U201 pads 21/22 share an F.Cu pocket with one via site, and pads 36–38 have two escapes under the package. Six segments on five nets were lifted to open the missing exits and rerouted by the gap closer: USB_VBUS_SENSE and LINK_A1 in the pocket, C5_PWR_EN, USB_SERVICE_VBUS and PWR_UPSTREAM_OK south-east of pad 38. LINK_A1 is an address line of the deliberately slow asynchronous byte link (set up before the WRn/RDn strobe), and its route got shorter. Pads 21 and 22 have explicit locked escapes (`tools/fanout_pins.py` now takes bend points), because pad 22's dog-leg under pad 21 clears by less than the gap closer's one-cell margin; native DRC checks it exactly. U10 pin 95 (TDO) has a locked fan-out to a via under the FPGA body, so the TDO leg joins at the pin; the other pins' fan-out sites collide with PWR_GOOD/JTAGEN copper and a GND via.
- **Crystal circuit.** The configuration nets and the rerouted nets were routed with no F.Cu track or via inside three boxes around X201, C2020, C2021, R2002 and the XIN/XOUT traces south of the U201 pad row (`KUI_KEEPOUT_BOX` in `close_gaps.py`). Closest new F.Cu copper: the FPGA_TDO_MCU via 0.75 mm from X201.3 (XTAL_OUT), the USB_SERVICE_VBUS via 0.99 mm from C2021.1 (XTAL_OUT), the FPGA_TCK_MCU via 1.22 mm from C2020.1 (XIN). New copper under the crystal is on In2 and B.Cu, separated from F.Cu parts by the In1 ground plane. Rip-up was restricted to named slow nets (`KUI_RIP_NETS`) and none was needed.
- **Junctions.** Two places where the gap closer met old copper at a shallow angle are tidied by explicit edits (`design/rp-fpga-jtag-2026-10-17/tidy.json`, `tools/edit_tracks.py`): C5_PWR_EN joins its old corner directly instead of through a 10° hairpin, and FPGA_TMS meets its old 45° segment at right angles instead of 22°. No new track runs between the pads of a two-pad part or within 0.25 mm alongside same-net copper.
- **DONE and INITn stay on J10 only.** The nearest free pad, GPIO27 (pad 28), could leave the package only between C2009's pads and past the XIN trace; GPIO39 (pad 48, east edge) is boxed in by SD and C5 routing with no via site. The JTAG status register reports DONE, configuration errors and USERCODE, so neither pad is used; both remain spare.
- **Routes** (copper path length between the pad centres, vias; measured from the pad edges the lengths are up to 0.7 mm shorter): TCK 11.0 mm/2 to R2044 and 45.2 mm/5 to U10.91; TMS 11.2/2 and 36.1/5; TDI 14.3/2 and 40.6/3; TDO 10.1/2 and 44.9/5; PROGRAMn 21.1/4 and 53.3/7. The longest, PROGRAMn, is a static line. With 220 Ω at the source the JTAG edges are RC-limited (about 13 ns on TCK); the lines are electrically short at the 10 MHz TCK limit. Rerouted slow nets, before → after: LINK_A1 39.9 → 35.4 mm, C5_PWR_EN (U201 → U34) 18.1 → 29.1 mm, USB_SERVICE_VBUS (static sense) 27.2 → 41.3 mm; USB_VBUS_SENSE (U201 → R2041) 4.45 → 4.66 mm and PWR_UPSTREAM_OK 25.00 → 25.03 mm, essentially unchanged.
- **Preservation** (`design/rp-fpga-jtag-2026-10-17/preservation-check.json`, run by the replay with its arguments recorded): all 351 earlier footprints, 15 zones and 29 drawings unchanged; 12,300 of 12,322 earlier tracks/vias unchanged; 4 split where new copper joins them, with the pieces covering the originals (FPGA_TCK, FPGA_TMS, C5_PWR_EN, USB_VBUS_SENSE); 2 shortened on lifted nets (LINK_A1 to a 13 µm stub, USB_SERVICE_VBUS from 5.95 to 4.23 mm); 16 removed, all on the five lifted nets. No new copper on In1 or In4. The power-copper report is unchanged apart from the board hash.
- **Release state.** DRC 0, unconnected 0, parity 0, ERC 0. The new part is a new BOM line (55 BASE rows, 269 SMT parts); its LCSC identity C112291 comes from a search index and its JLC SMT entry is pending (parts list status `catalog_identity_search_index_only_assembly_pending`). The owner reviews in `manufacturing/review-evidence.json` were made on fedc570 and must be repeated.

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
| Reversed 5 V | Bench (flex unplugged): Q20 blocks it, D21 stays off, F20 survives. Installed: not protected on the board; a pair swapped at the carrier shorts console 5 V through the flex grounds and only the tap-end harness fuse clears it. Check harness polarity before connecting J20 | same; `manufacturing/BRINGUP.md` |
| Short on the host-side 3.3 V buffers | F21, 0.5 A fast fuse at the only host 3.3 V entry (J51 B1) | `design/flex-interface.json`, flex checks |
| Chafed or cut flex | No console 5 V on either flex arm; contacts A1, A3, B3 end at dead fingers. Arm B's B1 trace carries console 3.3 V, unfused until F21 on the carrier: keep its coverlay intact and tape an unplugged tail | `flex/tools/check_flex.py` |
| Supply copper overheating before a fuse acts | 5 V/storage nets use 1 oz outer layers; normal-current screening and the 1.35 A converter fault screen pass; fuse-clearing and transient qualification remain open | `design/power-copper-check.json` |
| Thin return path | J20's ground pad has four 0.3 mm vias into the plane; each converter (U21/U22) thermal pad two | `design/floorplan-2026-10-10/power-vias.json` |
| Underside contact with the motherboard | Vias tented both sides; the 39 bare probe pads and J10's ten plated holes are exposed underneath: tape the whole underside before the first console power | `manufacturing/BRINGUP.md` |
| Harness wire before F20 | Not on the board: a 2 A Littelfuse 0251002.MXL in line at the console tap is an owner step | `design/PARTS-TO-BUY.md`, `docs/OWNER-NEXT-STEPS.md` |

## Supply copper screen and its limit

The corrected checker models finite via barrels by copper layer and sums branch
currents. It faults each local capacitor, IC and other rail pad, including the
logic-plane stubs omitted by the previous checker. Since 23 October the
converters are TPS62172; TI's TPS6217x table gives a 1.35 A maximum static
high-side limit at VIN=12 V, TJ=25°C, and the 1.35 A screen is not a
guaranteed continuous fault-current bound. Dynamic peaks may be higher. Fuse
nameplate current is also not a fault-current bound. The history below is
for the TPS62162 and its 2.5 A screen.

A bounded widening pass changed 1,298 logic, eight storage and four FUSED_5V
segments. Every route coordinate, layer, net, footprint placement and via
geometry was preserved. All 92 attempted 0.5/0.8 mm via enlargements failed
native spacing checks and were restored. Final DRC/parity/opens are zero.
Normal-current screening passed; the 2.5 A fault screen then had 138 failed entries:
34 storage track pieces + five vias, and 12 logic track pieces + 87 vias.
The IPC numbers are steady-current estimates, not measured transient heating.

`power-copper-check.json`, `power-native-preservation-check.json`,
`power-bounded-widths.json` and `power-bounded-vias.json` record the exact result.
The former unconditional supply PASS is superseded. The 23 October converter
change (section above) closes the 138 entries; fuse clearing and transient
heating still need first-article qualification. Neither a clean DRC nor owner
acceptance supplies that evidence.

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

1. **The flex arms cannot reach their sockets; J50/J51 must move (blocks both the carrier and the flex orders).** J50 and J51 sit on the notch's west wall, rotated so their mouths face east into the 2–3 mm gap beside CN503, the same gap in which arm A is soldered to the A row. Arm B is soldered to the east (B) row and would have to go round CN503's north end and back south past J50 to reach J51: about 49–52 mm of path against the 39 mm it has, and nothing can cross over CN503, because the GD-ROM plugs in there. Arm A has the length but would have to fold or twist into the same gap over its own landing. The tails were drawn for unrotated sockets (tip north, `flex/README.md`), but the sockets were placed rotated. The fix is a layout revision that lets each arm run straight off one end of the connector into a socket facing it. The geometry is tight: the notch is 15.2 mm wide and an FH12-20 is 16.2 mm long, so the two 10.5 mm tails cannot climb side by side at one end, and each end has only about 3–5 mm between CN503 and the notch wall for a tail to climb 3.3 mm. The likely options are one socket per end (A off one end, B off the other), a longer notch, or sockets set back from the notch with longer arms; each means re-placing parts in the dense regions there (U201's supervisors and crystal to the north, the converters and J20 to the south), re-routing about 40 flex nets and redrawing both arms. Which option fits depends on the owner's fit check (CN503's real position, the parts and heights at each end of CN503, the AV connector and CN601), so this revision follows `docs/OWNER-NEXT-STEPS.md` items 1, 3, 3a, 3b and 5, and the owner repeats the affected fit checks on the new sheet before ordering. `ROUTING-CHECKPOINT.md` lists the other changes to fold into it. The fit sheet shows the current socket bodies in orange.
2. **The larger outline is unregistered.** The carrier is now 59 × 115 mm and reaches 10 mm further north than the earlier draft; the C5 module and everything north of the BIOS opening moved 10 mm north relative to the motherboard. The paper template must show CE501, CE502 and CE504 outside the outline or under a cutout, the C5 inside the 8 mm region and clear of both zero-clearance thermal chips, and the north edge clear of the AV connector area. If north does not fit, the same band can be inserted south of the notch instead.
3. **The C5 compression fixture has no finished mechanical design. All four old mounting holes hit the current copper/pads (`mechanical-support-review.md`); an independent support must use physically verified anchors.** An insulating rigid support and removable clamp must hold the copper-to-module gap at 0.90 mm, keep XY alignment and resist at least 6.24 N of spring reaction without bowing either board; hard stops must prevent over-compression.
4. **C5 height without USB is not measured.** With the 2.5 mm stand-off, 0.8 mm carrier and 0.90 mm spring gap, the module plus clamp has at most 3.80 mm nominal, or 3.70 mm with maximum carrier thickness, before other tolerances in the 8 mm region.
5. **Stand-off and insulation are not established by DRC.** FH12 closed height is 2.0 ±0.2 mm. At the 0.9 mm maximum carrier thickness, 2.5 +0.9 +2.2 =5.6 mm in the 6 mm BIOS/CN503 region, leaving 0.4 mm before other tolerances and insulation. See `sources/connector-height-review.json`. Shield contact with pads, springs, solder joints or connector shells is unacceptable.
6. **Card, flex and harness service envelopes** (microSD access, flex insertion and bend, J20 harness route and strain relief) need physical fit checks.

## Manufacturing notes

Gerbers, drill and the placement file are relative to the aux origin at the outline's south-west corner. The BIOS opening and notch are milled; their inside corners come out with the router radius (about 0.5 mm). Vias inside the J20 ground pad, the U21/U22 converter thermal pads and the RP2350 thermal pad are open and tented; take the fabricator's epoxy-filled, capped via option if it is free for six-layer boards. `tools/make_review_package.py` plots an unapproved review package (Gerbers, drill, BOM/CPL, renders, checks) for inspection; order only from `prepare_jlcpcb_release.py --export` after the reviews are accepted. The RP2350, microSD socket and 50 MHz oscillator have no published 3D model and render as bare pads.

## Primary source records

The geometry checks use Seeed's [official C5 V1.1 schematic/PCB archive](https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.zip), identified by native PCB SHA-256 in `sources/xiao-c5-carrier-contact-coordinates.json`, and Harwin's [S7221-45R technical drawing](https://content.harwin.com/asset/1801555a-aeb3-408d-b163-bf8c8afc2263/DRG-02334-Technical-Drawing-Datasheet-S7221-45R-pdf.pdf). The selected RP and TI circuits cite their official design guides in `controller-implementation.md` and `power-and-isolation.md`. Earlier snapshots, withdrawn candidates and their measurements remain in `carrier-layout-review-evidence.json` for traceability; they are superseded by this snapshot.
