# Owner next steps — G1 bridge Rev A

23 October 2026. **Where things stand:** the carrier is fully routed and
its native checks are clean, and the converter fault-current screen now
passes (U21/U22 are TPS62172). What remains before an order:

1. **Your measurements, items 1–5 below.** Each has a number to pass,
   except the flex route survey (3a), which reports where the strips
   arrive.
2. **One more carrier layout revision.** The flex sockets J50/J51 must
   move: as placed, flex arm B cannot reach J51 and arm A would have to
   fold into the gap beside CN503 (`design/CARRIER-LAYOUT-REVIEW.md`,
   Mechanical findings, item 1). Where they can go depends on items 1, 3,
   3a and 3b, which may also move an edge or the BIOS opening, and item 5
   (a C5 that does not fit moves it). Both flex arms are redrawn in the
   same revision, which also adds readable silkscreen labels (J20's +5V
   and GND among them). After it, you print the new fit sheet and 1:1
   prints of the redrawn arms and repeat the affected item 3 and 3a checks
   before ordering.
3. **Supplier checks and purchases, items 6–7b**, including two LCSC codes
   that came from a search index (C112291 and C59873).
4. **The release reviews, item 10**, redone on the final board.
5. **Three decisions for you:** whether the device-1 risk in item 4 is
   settled by a small K-UI diagnostic first or accepted for the prototype;
   whether Lattice Diamond place-and-route runs before ordering (item 11);
   and whether an independent human electronics review is wanted (item 10).

The release remains unapproved. See
[current order readiness](ORDER-READINESS-2026-10-11.md).

Claude's `4b8c7d6` is the routed-board baseline. The 11 October follow-up
resolved the JLCPCB/TI lookups, corrected the protection-part lands and
power screening, and refreshed the fit sheets. On 17 October U10's JTAG
port and PROGRAMn were wired to the RP2350. On 23 October the converters
became TPS62172, the fit sheets gained socket, support-band and microSD
overlays, and this plan gained the pass criteria. The release reviews in
`manufacturing/review-evidence.json` must be redone for the new board; your
console measurements are not affected by those changes, and the carrier
outline is unchanged since 11 October.

Original item numbers are retained to match earlier messages; 3a, 3b and
7b are new.

## One console-open session

**1. Prove CN503 row A/B and contact numbering before either flex order.**
Do all of this with the power cord unplugged (switching off is not enough;
wait 5 minutes), the GD-ROM unplugged from CN503, and the meter on
resistance (200 Ω range). The proposed mapping came from a mating riser
symbol and trace references, not a test on your motherboard. Swapped rows
or reversed numbering need new artwork for both arms; with reversed
numbering, arm B's 3.3 V pad would sit on +12 V.

Use fine needle probes (0.5 mm tips or finer; ordinary tips can bridge the
1.00 mm CN503 and 1.27 mm IC501 legs) and a clip lead. Touch the probes
together and note the lead resistance. Read the number on the display: a
beeper may stay silent at 33 Ω.

Names used below: the **A row** is the CN503 leg row on the IC501 (BIOS,
west) side; the **B row** is the board-edge (east) row; legs are counted
1–25 from the end nearest CN601 (south, away from the AV connector). Flag
A1 with tape and photograph the board number (837-13778-02), IC501's
marking and both rows.

*1a. Which end is contact 1.* Clip the black lead to a motherboard ground,
such as a screw-hole ring. On each row, legs 8, 13, 18 and 21 must read
under 2 Ω and leg 5 over 100 Ω (usually OL). Legs 8, 13 and 18 are ground
whichever end is contact 1, so use them to check your counting. If leg 21
is not ground but leg 5 is, contact 1 is at the north end: stop and do not
order either arm. Then, with the clip off ground, measure leg A2 to leg A1:
it should read over 100 Ω. Under 2 Ω means A2 is a 3.3 V contact rather
than the reset line (see `CN503-reference.csv`); report it before ordering.
Also record, without a pass number, the resistance from B15 (IORDY) to B1
and from B16 (INTRQ) and A14 (DMARQ) to ground: the bridge can only pull
IORDY low and relies on a console pull-up, which is not documented.

*1b. Find IC501 pin 1.* Find the dot, dimple or bevelled edge. Pins 1–22
run along the long edge nearest that mark, starting at the marked corner;
pins 23–44 run back along the opposite edge, with pin 23 across from pin 22.
Pin 4 is the 4th leg from pin 1. Counting from the pin-22 corner, with pin
22 as the 1st leg, pins 19, 17, 15, 14 and 13 are the 4th, 6th, 8th, 9th and
10th legs. Self-check with the clip on ground: pin 13 must read under 2 Ω
(it is ground in the standard 44-pin ROM pinout that the IC501 pin numbers
follow). Pin 32, directly opposite, is also ground, so this check cannot
catch counting along the wrong long edge; the table below does.

*1c. IC501-to-CN503 table.* Clip off ground. Hold one probe on the IC501 leg
and one on the CN503 leg, let the reading settle, and record every value.

| IC501 pin | CN503 leg | Signal | Expected |
|---|---|---|---|
| 14 | A15 | DIOR- | under 100 Ω |
| 17 | A11 | DD1 | under 100 Ω |
| 4 | A19 | DA0 | under 100 Ω |
| 15 | B11 | DD0 | under 100 Ω |
| 19 | B9 | DD2 | under 100 Ω |
| 15 | A11 | — | OL; low if rows swapped |
| 17 | B11 | — | OL; low if rows swapped |
| 14 | A11 | — | OL; low if numbering reversed |
| 17 | A15 | — | OL; low if numbering reversed |
| 14 | B11 | — | OL; low if swapped and reversed |
| 15 | A15 | — | OL; low if swapped and reversed |

About 33 Ω is normal for the first five: IC501 is on the console-chip side
of the 33 Ω arrays RA507–RA515, so one array element is in the path; 0–1 Ω
also passes. OL means over the 200 Ω range (over 1 kΩ on a higher range).

Pass: the first five under 100 Ω and the other six OL. That proves the
proposed row identity and south-end numbering on both rows. If a
"swapped", "reversed" or "both" pair reads low instead, do not order either
arm. If all five connected pairs fail and IC501 pin 14 reads about 33 Ω to
A5 (DD8), you counted along the wrong long edge: pin 1 is on the opposite
edge, so recount and repeat. Any other pattern, or readings between 100 Ω
and 1 kΩ: recheck pin 1 and your counting, then send the table and photos
before ordering.

**2. Optional now: console 3.3 V at B1 (power on), and the PSU label.**
Not needed before ordering. The base build uses only B1 (3.3 V), which
reaches the carrier at J51 pin 20 and fuse F21. A1, A3 and B3 have no flex
copper, and A25/B25 (12 V) are excluded (`design/flex-interface.json`). The
required supply check is on installation day, at the arm B tail and at TP26
(`manufacturing/BRINGUP.md`). Do not probe A3/B3 (5 V), A25/B25 (12 V) or
IC501 pin 23. If you do measure B1 now, do it only after item 1 passes:

- Cover legs 22–25 of both rows with Kapton tape (25 is +12 V, 22–24 audio).
- Put the console on a dry, non-conductive surface. Keep hands and tools
  away from the power-supply board, which carries mains voltage whenever
  the cord is plugged in.
- Clip the black lead to a ground screw and set the meter to 20 V DC. Plug
  in and switch on. Touch only the red probe, insulated to about 1 mm of
  tip, to B-row leg 1. Read, then switch off and unplug. Keep the console on
  for under a minute: the shield and its heatsink contact are off.
- Expect 3.20–3.47 V. About 12 V means the numbering is reversed (item 1
  should have caught it): stop. About 5 V means you are on leg 3. 3.0–3.2 V:
  record it. U25 (TPS3808G33, 3.07 V nominal threshold,
  `design/power-and-isolation.md`) may then report the host absent under
  load, so review U25 before the assembly order. Below 3.0 V: stop and
  report before ordering.

Voltage cannot tell row A from row B, because both rows carry the same
supplies. It can show which end is contact 1 (3.3 V at the south end,
12 V at the north end), but item 1's unpowered ground pattern already does
that. With the GD-ROM unplugged this reading is unloaded; the loaded value
comes from TP26 on the installed carrier.

While the console is open, photograph the power-supply board's label. No
5 V rating of the original supply is recorded, and the bridge adds about
1.35 A (6.75 W) to it.

**3. Fit check with the refreshed 1:1 sheet.** Choose the Letter or A4 PDF
in `mechanical/`, print at **Actual size / 100%** with Fit/Shrink off, and
measure both 50 mm bars: each must read 49.8–50.2 mm before you cut. Cut
out the black outline, the BIOS opening and the CN503 notch, leaving the
green microSD box and the dotted C5 envelope attached as tabs. Centre IC501
in the opening and align the notch with CN503, including the connector's
wider 6.56 mm ends. Each check below has its pass number; a failure can
move an edge or the opening in the layout revision, so record the
measured margin either way.

| Check | Pass |
|---|---|
| IC501 plastic body length, leads excluded (calipers) | ≤ 28.0 mm (≥ 0.5 mm to each end of the 29.0 mm opening), or seated height ≤ 2.0 mm |
| IC501 outer lead span, including solder fillets | ≤ 18.0 mm (the opening is 19.0 mm wide) |
| West edge against the CPU-side thermal chip and the shield's thermal-pad imprint | ≥ 1.0 mm clear in top view |
| East edge, north and south of the notch, against the motherboard edge | ≥ 0.5 mm inside it, and clear of case walls, ribs and bosses |
| North edge against the AV connector body and tabs, posts, bosses and shield walls | ≥ 1.0 mm clear |
| South edge against CN601 and case features | clear; record the margin |
| Every motherboard part under solid carrier laminate (mark CE501, CE502, CE504 and any other) | ≤ 2.0 mm tall (the stand-off is 2.5 mm) |
| Lower-metal 6 mm / 8 mm shield boundary, traced onto the sheet | the C5 box and its dotted 3 mm clamp envelope entirely in the 8 mm region; note where C202–C205 fall (reserve variant only) |
| Both thermal-contact chips | marked as zero-clearance keepouts; the C5 box and clamp envelope ≥ 1.0 mm clear of both chips and their pads |
| Green microSD box north of J201 | 10 mm clear north of the edge with nothing over 3.3 mm tall; if not, decide whether lifting the carrier for card swaps is acceptable |
| Support anchors (purple bands are 3 mm edge strips with no carrier parts, outside the C5 clamp area) | at least 3 spread anchors in purple bands or on flat part-free motherboard areas, plus one bearing under the narrow strip between the BIOS opening and the notch (where J50 now sits) and one near the C5's east end; record the motherboard surface height at each, which must be below 2.5 mm less the pad thickness; avoid test pads |

Photograph the template in place from above, with a ruler in view. The
earlier four-hole C5 support model intersects current copper; those are
not approved holes. Carrier support and C5 retention still need a
mechanical design, which needs these anchor positions and heights.

**3a. Flex route survey for the socket move.** With the template in place
and the GD-ROM unplugged:

- Record where CN503 sits in the template's notch: the gap from the
  housing to the notch's west wall and from each housing end to the
  notch's north and south walls (CAD: about 1.9 mm, 5.0 mm and 3.3 mm).
  The notch is only 15.2 mm wide, so both 10.5 mm tails cannot climb side
  by side at one end; the arm may have to leave through a longer notch.
- At each end of CN503 (north toward the AV connector, south toward
  CN601), measure the free length from the end of the housing to the
  nearest obstacle along the row direction, and the height of every part
  in a 12 mm-wide strip there.
- Cut two 10.5 mm-wide strips of thin card (the arms are 5 mm wide at the
  neck beside the connector, 6.75 mm at the solder landing and 10.5 mm at
  the tail). Lay one along each row's legs (the A strip on the IC501 side,
  the B strip on the board-edge side) and lead it off the north end, then
  off the south end, flat on the motherboard. Try both strips at the same
  end, and also the split (A off one end, B off the other). Where a strip
  would climb onto the carrier, hold a 3.3 mm block (a card stack checked
  with calipers) on the template and mark where the strip reaches its top
  without folding or twisting. Record the strip length from leg 21 (north)
  or leg 1 (south) to the mark and the direction it arrives from;
  photograph each attempt with a ruler.
- With the strips in place and power off, refit the shield and plug in the
  GD-ROM as in 3b; note whether the drive's connector body or the shield
  edge presses on either strip.
- Photograph the 5 V point you plan to tap for J20 and a ground point near
  CN503, and measure the wire route to J20's position on the template.

There is no pass number: report which end (or split) gives both strips a
clear path, where and from which direction they arrive, and the lengths.
The new socket positions come from this.

**3b. Shield height, measured directly.** The 6.00 mm limit in the BIOS/
CN503 region was derived from an outer-face reading minus assumed sheet
and metal thicknesses, not measured. Put small balls of modelling clay (or
card stacks) at the orange socket positions, at both candidate socket
zones from item 3a, on U202 and J201, and at the four C5 corners; refit the
shield and screw it down normally; remove it and measure the squashed
heights with calipers.

- Socket positions and zones: pass ≥ 6.0 mm (2.5 mm stand-off + 0.9 mm
  maximum carrier + 2.2 mm maximum FH12 connector = 5.6 mm, plus two
  0.07 mm tape layers and at least 0.2 mm of air).
- U202: pass ≥ 5.9 mm (SOIC-8 208 mil, 2.16 mm maximum; confirm against
  the Winbond package drawing). J201: pass ≥ 5.2 mm (1.42 mm socket).
- C5 corners: record each reading; pass if it is at least 4.6 mm (2.5 +
  0.9 + 0.9 + 0.07 tape + 0.2 air) plus the C5 height from item 5. The
  excess is the clamp's height budget. Do item 5 first, or record the
  readings and compute the pass later.
- Wrap each clay ball in cling film so nothing stays on the board.

Connector evidence is in `design/sources/connector-height-review.json`.

**5. Measure the USB-free C5 and plan its antenna.** Buy two Seeed XIAO
ESP32-C5 modules (one to modify). With a 2.5 mm stand-off, 0.8 mm carrier
and 0.90 mm copper-to-module-underside gap, the 8 mm zone allows **3.80 mm
nominal for module plus clamp above the seating plane**, 3.70 mm with a
0.9 mm carrier, before other tolerances and insulation. After removing USB
(and any header), measure:

- the tallest feature, measured from the module's underside, including
  the mated U.FL antenna plug; pass if it is
  at most 3.5 mm, leaving room for a clamp (the earlier 4.48 mm reading
  included USB);
- the bare module PCB thickness;
- underside protrusions and flatness: pass if nothing stands more than
  0.3 mm below the module PCB.

Sixteen spring contacts require at least 6.24 N combined reaction at the
working gap. Hard supports and retention must maintain alignment and
prevent bow or over-compression. Paper fit does not qualify this fixture.

Antenna: the supplied FPC antenna goes outside the shield. Find an exit
gap that needs no shield cutting, measure the cable length that route
needs against the supplied cable, and record the mated U.FL height and its
exit direction so the clamp can avoid it.

## No teardown needed

**4. Check retained-GD-ROM device-1 response (B4).** On the K-UI boot
screen, press Left once so the menu shows `Source: IDE / CF` (any button
stops the autoboot), wait about 10 s for the drive to go idle, then select
Start K-UI and press A (or leave Source on Auto with no card in either
serial-port adapter and let it start). Then press Y to open the log and
photograph the `IDE/CF initialization failed: ATA=N` line. The codes come from K-UI's
`include/kui/ata.h`:

| ATA= | Meaning |
|---|---|
| 3 | Status read 00h or FFh with device 1 selected. Not proof the drive stays off the bus: 00h is also what a drive returns when it answers for a missing device 1, and the log does not say which value was read. |
| 5 | Something answered with an error while device 1 was selected, most likely the GD-ROM ignoring the device-select bit. Report it before ordering. |
| 6 | Something returned IDENTIFY data while device 1 was selected. Treat like 5. |
| 4 | Timed out. Inconclusive; retry with the drive idle. |
| 2 | A GD-ROM DMA transfer was active; retry. |
| 8 | K-UI's G1 bus unlock failed, so the test did not run. |
| no line | IDE was not tried: storage was found elsewhere, a disc read was pending, or the source was fixed to another transport. |

This preliminary check does not qualify shared-bus coexistence. Telling
00h from FFh needs a raw status read that K-UI does not have yet, and 3 is
the most likely result. A small K-UI diagnostic would settle it: select
DEV=1, write Count = 55h and LBA-low = AAh, read them back with Status and
AltStatus, and log the raw values (and the same with DEV=0 for reference);
an echo with DEV=1 means the GD-ROM answers for device 1, which would make
it fight the bridge on the data lines. Decide whether that diagnostic is
written before ordering or the risk is accepted for the prototype.

## Supplier and release checks

**6. Select the verified rigid-board options in the actual JLCPCB quote.**
The official [six-layer capabilities](https://jlcpcb.com/resources/6-layer-pcbs)
list 0.8 mm boards, 0.09 mm track/space and sufficiently small vias.
Research is complete; confirm the uploaded-board quote retains:

| Option | Required setting |
|---|---|
| Layers | Six: signal / ground / signal / signal / +3V3_LOGIC / signal |
| Finished thickness | 0.8 mm; published tolerance ±0.1 mm |
| Track/space | Support 0.127 mm / 0.127 mm minimum |
| Signal vias | 0.2 mm drill / 0.5 mm copper diameter |
| Supply vias | 0.3 mm drill / 0.6 mm copper diameter |
| Copper | 1 oz outer / 0.5 oz inner |

Height and copper checks depend on these settings. A substituted stack
needs a revised check before ordering. Also record JLC's actual six-layer
0.8 mm stack ID, the outline tolerance and the material Tg; the
order-settings files and the power check use them. Choose epoxy-filled and
capped vias in pads if the quote allows: U201's nine thermal holes, the two
under each converter, four in J20's ground pad and an open via in X201's
pad 4. If JLC adds panel rails, ask for tab or mouse-bite rails rather than
V-score: parts sit as close as 0.5 mm to the outline and the inner ground
plane comes to 0.3 mm of it. Check the quote's DFM report for hole-to-hole
spacing (109 different-net via pairs are 0.43–0.5 mm apart). For the flex arms, have the supplier confirm the
0.30 ± 0.05 mm finished tail thickness, the 10.5 ± 0.07 mm tail width and
the coverlay openings.

**7. Obtain assembly agreement.** Harwin S7221-45R, C22445132, and Abracon
ASE, C596955, have catalog assembly-fixture flags. Have JLCPCB confirm the
selected service can assemble them and explain any fixture charge or
handling requirement. Inspect its placement/orientation preview. Factory
assembly fixtures are separate from in-console C5 retention. Record stock,
unit price, Basic/Extended status and lead time for each line at quote
time; RP2350B and the LCMXO2-2000HC have no listed alternates (U10's -5 and
-6 speed grades share package and pinout but need their own timing run).
The RP2350B code C9900138392 looks like a JLC global-sourcing listing, so
allow for pre-order lead time.

**7a. Confirm two search-index part codes.** lcsc.com and jlcpcb.com could
not be reached from the design environment, so these LCSC codes were
taken from a search engine's index. For each, check on lcsc.com that the
code is that exact part, find its JLC SMT parts-library entry and note
whether it is a Basic or Extended part (Extended parts add a fee):

| Refs | Part | LCSC code |
|---|---|---|
| R2044–R2048 | Yageo RC0402FR-07220RL (220 Ω, 0402) | C112291 |
| U21, U22 | TI TPS62172DSGR (3.3 V, 0.5 A buck, WSON-8 DSG) | C59873 |

**7b. Buy the harness and installation parts** (`design/PARTS-TO-BUY.md`,
Harness and installation): the Littelfuse 0251002.MXL tap fuse, 24 AWG
red/black twisted pair and 25 µm polyimide tape. The tap fuse is the only
reverse-polarity protection once the flex is plugged in.

Bring-up also needs tools you may not have: a current-limited bench
supply, an oscilloscope (100 MHz or more), a 16-channel logic analyser, an
SWD probe, a MachXO2-capable 3.3 V JTAG cable for J10, a 3.3 V USB-UART
adapter, hot air for removing the C5's USB connector, 0.5 mm FFC
breakouts, and pogo pins or fine wire for the pad-only service ports.

**8. Protection-part lookup completed.**

| Ref | Exact selected part | JLCPCB code |
|---|---|---|
| D21 | High Diode SMF5.0CA, bidirectional 5 V, SOD-123FL, 1.4 mm maximum height | [C22458622](https://jlcpcb.com/partdetail/HighDiode-SMF50CA/C22458622) |
| F21 | Littelfuse 0467.500NR, 0.5 A fast, 0603, exact NR suffix | [C206990](https://jlcpcb.com/partdetail/Littelfuse-0467500NR/C206990) |

Manufacturer-recommended lands have been applied to D21 and the F20/F21
467-family fuses. Fresh native DRC/opens/parity/ERC are zero; BASE/DNP assembly exports use the current board. Catalog
identity does not reserve stock; substitutions need electrical and
package review.

**9. Converter current limit: resolved by a part change.** The TPS62162's
2.45 A maximum static limit made the local-branch copper screen fail at
138 entries. U21/U22 are now TI TPS62172DSGR, the pin-compatible 0.5 A
part: its [TPS62170-family datasheet](https://www.ti.com/lit/ds/symlink/tps62170.pdf)
gives a high-side limit of 0.85 A minimum, 1.05 A typical and **1.35 A
maximum at VIN = 12 V, TJ = 25°C**. With three short logic links widened,
the screen at 1.35 A passes with no failures. It is still not a
guaranteed continuous fault-current bound or fuse-clearing qualification:
delay can temporarily raise the peak.

**10. Review current evidence before release approval.** The "owner
reviews" are the release review records in
`manufacturing/review-evidence.json`, not console work. Two of them
(rigid electrical and power/bus isolation) were accepted by an AI review;
decide whether you want an independent human electronics review before
ordering. Read
`design/CARRIER-LAYOUT-REVIEW.md` and its refreshed reports. The exporter
requires an approved manifest and accepted reviews bound to current
input hashes, so the reviews are redone on the board that comes out of
the socket-move revision. Do not change approval flags merely to obtain an
archive. REVIEW-ONLY files support inspection/quotation; they are not
order files.

Signal routes on In4.Cu cut slots in the +3V3_LOGIC plane. Connected planes
and DC screening do not prove return-path integrity, bus timing or
retained-drive coexistence. Final DRC, opens, parity, physical-pad and
power reports must match the final board; unresolved checks remain
release conditions.

**11. Diamond for functional bring-up.** FPGA simulation/mapping exists;
vendor placement/routing, a bitstream and timing verification still need
Lattice Diamond and an account/licence. This is a bring-up dependency,
separate from consciously ordering an accepted first hardware prototype,
but a pin or timing problem found by Diamond after the order could need a
board change, so decide whether to run it first (U201 can reload U10 in
the field, which only helps once the pins are right). Since 17 October 2026 the RP2350 drives U10's JTAG port and PROGRAMn
(GPIO21/22/28-30 through R2044-R2048), so assembled boards need no Lattice
cable once the RP2350 configuration firmware (a JTAG player) exists. That
firmware is new work and not written yet: for first bring-up, have a
target-powered 3.3 V MachXO2 JTAG cable for the J10 pads and hold RUN
(J203.5) low while it is attached.

## After fabrication

Follow `manufacturing/BRINGUP.md`: current-limited bench power, FPGA, SD,
C5, then the console installation steps (flex continuity and bridge
checks, tail voltages, harness polarity, a staged boot with the bridge
unpowered, then powered). Do not seat the spring-contact C5 without
qualified supports/retention. F20 cannot protect wire ahead of it, and
fuse nameplate current alone does not bound fault current.

Pass numbers for the installed console:

- 5 V at the tap during GD-ROM spin-up with the bridge at full load (C5
  transmitting, SD writing): at least 4.75 V, and U26 never asserts
  `PWR_FAILn`.
- Sustained J20 current: at most 1.1 A (75 % of F20's 1.5 A before
  rerating for the temperature under the shield). If it is higher,
  0467002.NR (2 A) fits the same land, but a 2 A screen adds 40 5 V
  copper-screen entries, so it is a copper change, not a drop-in.
- TP26 (`CONSOLE_3V3`) while a GD-ROM game loads: at least 3.20 V.
- J201.4 during sustained card writes: at least 3.15 V, with
  `STORAGE_PGOOD` never deasserting.
- A 30-minute soak with the shield fitted (Wi-Fi traffic, SD writes and a
  game running): the microSD card stays below 70 °C and nothing on the
  carrier exceeds 85 °C (thermocouple leads routed out under the shield).

Firmware still needed before operation: RP2350 ATA/SD/link/PSRAM cache and
the JTAG player; K-UI NOP unlock, IDENTIFY and PIO reads; and C5-to-RP2350
link changes; the ATA vendor commands that carry Wi-Fi traffic between
K-UI and the C5 are not designed yet. The bridge is LBA28-only (cards are
usable up to 128 GiB less 512 bytes) unless a later FPGA image adds 48-bit
addressing. K-UI must not write Device/Head or the command block while
device 1 is BSY or DRQ (the FPGA ignores DEV changes while busy), and the
in-game reader must serialise with the game's GD-ROM driver. These
and real timing/coexistence tests remain deliberate prototype bring-up
work, not completed functionality.
