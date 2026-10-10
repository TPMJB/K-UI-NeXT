# Owner next steps — G1 bridge Rev A

10 October 2026. Everything that can be done without the console, a meter
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
Actual size, check both 50 mm bars). Lay it on the board with the shield off:

- Do the capacitors CE501, CE502 and CE504 fall outside the carrier outline,
  or under a cutout? Note their heights if they are under solid board.
- Does the BIOS opening (29 × 19 mm) clear the chip body with margin? The
  recorded 26.68 mm "body length" matches a 44-pin SOP's lead span exactly;
  a typical 44-SOP body is about 28.2 mm, which leaves only about 0.4 mm at
  each end.
- Mark where screws or posts could support the carrier.

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

**6. Ask JLCPCB about two assembly fixtures.** Their catalog flags the Harwin
S7221-45R spring contacts (C22445132) and the Abracon ASE oscillator
(C596955) as needing assembly fixtures. Get their agreement before the
assembly order.

**7. Lattice Diamond (free licence).** The FPGA logic simulates and maps,
but a placed-and-routed bitstream and real timing need Lattice's own tools,
which need a Lattice account and licence file. Open-source tools cannot
produce a MachXO2 bitstream with timing sign-off.

## After the boards arrive

The bring-up order is in `manufacturing/BRINGUP.md`: bench power first, with
no console, then the FPGA link, then SD, then the C5, and only then the
console. The 5 V tap for J20 and its voltage during GD-ROM spin-up are
measured then; R270 can be changed if the power-fail warning trips.

## Software still to write

Not hardware tasks, but needed before the bridge does anything:

- RP2350 firmware: ATA command engine, SD driver, FPGA link, PSRAM cache.
- K-UI host driver: the two-frame NOP unlock, IDENTIFY, PIO mode 0 reads.
- C5 firmware changes for the RP2350 link (the existing Wi-Fi bridge
  firmware is a starting point).
