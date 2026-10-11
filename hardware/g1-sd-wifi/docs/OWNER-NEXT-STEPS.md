# Owner next steps — G1 bridge Rev A

11 October 2026. Everything that can be done without the console, a meter
or an account has been done on the design branch. These are the remaining
items that need you, in the order that saves the most money and rework.
Each says why it matters and what to send back.

## Before ordering the flex arms (one console-open session)

**1. Prove which CN503 row is A and which is B (power off, meter on ohms).**
CN503's supply and ground contacts are identical on both rows (A1/B1 3.3 V,
A3/B3 5 V, grounds at 8/13/18/21, A25/B25 12 V), so no voltage check can tell
the rows apart. The CSV comes from the iceGDROM riser symbol (the mating
half) and the RDC trace, never from your board. If the rows are swapped,
every data line lands on the wrong flex conductor and both arms must be
remade. Four readings settle it, and also prove that the BIOS chip shares
the bus. Expect low ohms (there are small series resistors, RA507–RA515):

| IC501 pin | CN503 contact | Signal |
|---|---|---|
| 14 | A15 | DIOR- |
| 15 | B11 | DD0 |
| 17 | A11 | DD1 |
| 4 | A19 | DA0 |

Send back: the four readings, and the A/B silkscreen you probed against.

**2. Three voltages with the console on (careful probing).**

| Point | Expected |
|---|---|
| CN503 A1 or B1 (south end) | 3.3 V |
| CN503 A3 or B3 | 5 V |
| IC501 pin 23 (BIOS supply) | 3.3 V on VA1 |

These confirm the contact numbering runs from the south (CN601) end, and the
3.3 V G1 level the buffers assume. A25/B25 (north end) carry 12 V; never
bridge them.

**3. One fit check with the 1:1 paper template** (`mechanical/`, print at
Actual size, check both 50 mm bars). Lay it on the board with the shield off.
Use sheets printed from commit 1488fac or later: earlier sheets drew the
BIOS and CN503 reference boxes 10 mm north of the opening and notch they
must line up with.

- Do the capacitors CE501, CE502 and CE504 fall outside the carrier outline,
  or under a cutout? Note their heights if they are under solid board.
- Does the BIOS opening (29 × 19 mm) clear the chip body with margin? The
  recorded 26.68 mm "body length" matches a 44-pin SOP's lead span exactly;
  a typical 44-SOP body is about 28.2 mm, which leaves only about 0.4 mm at
  each end.
- Mark where screws or posts could support the carrier.
- The carrier is now 59 × 115 mm. The extra 10 mm sits between the FPGA
  area and the BIOS opening, so with the opening and the CN503 notch on
  their parts the board reaches 10 mm further north (toward the AV
  connector) than the earlier 59 × 105 mm draft; the south edge is where it
  was. Check that the whole north part, the C5 module in particular, still
  lies in the 8 mm region and clear of both thermal-contact chips. If north
  does not fit, say so: the same 10 mm can go south instead.

## Any time, no teardown

**4. Free check of whether the GD-ROM answers for device 1 (B4).** Boot K-UI
with no card in the SCI adapter and nothing on the rear port, and read the
`IDE/CF initialization failed: ATA=N` line in the log. 5 means the drive
likely answers for device 1 (bad news for the shared-bus design), 3 is
favourable, 4 is inconclusive.

**5. C5 height without its USB connector.** After you remove the USB
connector, measure the tallest point of the module. The 8 mm region allows
at most about 3.8 mm including the clamp.

## Accounts and orders

**6. Confirm a six-layer 0.8 mm board on the JLCPCB order page.** The
carrier is now six layers (signal / ground / signal / signal / 3.3 V /
signal). Four layers could not route this outline. The C5 and connector
height budget assumes 0.8 mm total thickness, so pick the fabricator's
standard six-layer stack at 0.8 mm (no impedance control needed). If 0.8 mm
is not offered for six layers, tell me the thinnest option; 1.0 mm costs
0.2 mm of the 3.8 mm C5 budget. On the same capability page, confirm these
minimums, which the board uses (the fabricator's site was not reachable
from the design environment):

- 0.127 mm (5 mil) track width and copper-to-copper spacing, on outer and
  inner layers. At 0.15 mm the board could not be routed.
- Vias with a 0.2 mm drill and 0.5 mm pad (signals), and a 0.3 mm drill
  and 0.6 mm pad (supplies).
- 1 oz outer and 0.5 oz inner copper. The supply current check assumes
  these weights.

**7. Ask JLCPCB about two assembly fixtures.** Their catalog flags the Harwin
S7221-45R spring contacts (C22445132) and the Abracon ASE oscillator
(C596955) as needing assembly fixtures. Get their agreement before the
assembly order.

**8. Confirm two protection parts on JLC.** Both were added after the
catalog check, and the makers' and JLC's sites were not reachable from the
design environment. On the JLC parts page confirm:

- D21, input TVS, SMF5.0CA: bidirectional ("CA"), 5.0 V working voltage,
  breakdown above 6 V, SOD-123FL package.
- F21, host 3.3 V fuse, Littelfuse 0467.500NR: 0.5 A fast, 0603, the same
  family as F20 (C99559).

Send back both C-numbers so the purchasing list can record them.

**9. One datasheet number: the TPS62162 current limit.** TI's site was not
reachable from the design environment. The supply check rates the 3.3 V
storage rail and the logic-plane feed at 1.5 A, the assumed short-circuit
current of the two TPS62162 converters. From the datasheet's electrical
characteristics, send back the maximum high-side (switch) current limit.
If it is above 1.5 A, I re-run the check at that current before you order.

**10. Accept or reject the engineering reviews.** The release exporter
(`manufacturing/prepare_jlcpcb_release.py`) refuses to build order archives
until every topic in `manufacturing/review-evidence.json` is accepted by
you and `approved` is true. I do not mark these myself. Read
`design/CARRIER-LAYOUT-REVIEW.md` and the check reports it links, then set
each topic's status, reviewer, date and notes.

The carrier is now fully routed (DRC 0, unconnected 0, parity 0, supply
check passing). Three layout choices deserve your eye in the review:

- Some signals run on In4.Cu, the +3V3_LOGIC plane layer, where the four
  signal layers had no way left (listed with lengths in the review). They
  cut slots in the plane; the plane stays connected and the supply check
  passes, but signals crossing a slot on B.Cu or In3.Cu lose their nearest
  return path there. For these slow bus and control lines that is
  acceptable; say so if you disagree.
- R2038 (USB D− series resistor) and C2016 (bypass for RP2350 pin 68)
  swapped places beside the RP2350's north pins.
- The review package (`tools/make_review_package.py`) plots the Gerbers
  and renders for inspection; the files marked REVIEW-ONLY are not an
  order.

**11. Lattice Diamond (free licence).** The FPGA logic simulates and maps,
but a placed-and-routed bitstream and real timing need Lattice's own tools,
which need a Lattice account and licence file. Open-source tools cannot
produce a MachXO2 bitstream with timing sign-off.

## After the boards arrive

The bring-up order is in `manufacturing/BRINGUP.md`: bench power first, with
no console, then the FPGA link, then SD, then the C5, and only then the
console. The 5 V tap for J20 and its voltage during GD-ROM spin-up are
measured then; R270 can be changed if the power-fail warning trips. Put a
fast fuse of about 2 A in the J20 harness at the console end: the board's
own fuse F20 protects the board, not the wire leading to it.

Measure the board's steady 5 V input current once everything runs. F20 is
rated 1.5 A, and the worst-case budget of 1.3 A loads it to 87 %. If the
sustained current is above about 1.1 A, the fuse may open in normal use.
That fails safe, but it is a nuisance; tell me and I will propose a
larger fuse with matching copper.

## Software still to write

Not hardware tasks, but needed before the bridge does anything:

- RP2350 firmware: ATA command engine, SD driver, FPGA link, PSRAM cache.
- K-UI host driver: the two-frame NOP unlock, IDENTIFY, PIO mode 0 reads.
- C5 firmware changes for the RP2350 link (the existing Wi-Fi bridge
  firmware is a starting point).
