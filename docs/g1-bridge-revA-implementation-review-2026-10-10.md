# G1 bridge Rev A: implementation review

10 October 2026. Reviews `design/g1-sd-wifi-20261009` at `8bebc81` (21 commits
since `fb70d55`). Review only; nothing on the design branch was changed.

## Verdict

This is careful work, and the architecture now matches what I recommended:
- a MachXO2-2000 front end owns the ATA bus;
- the RP2350B is the controller, with 8 MiB PSRAM on QMI CS1 (GPIO 47);
- power comes in on a separate fused 5 V pair;
- a hardware bus permit gates every output;
- the stock mask ROM is a physical recovery path.

**The FPGA's read path is combinational on raw DIOR-**
(`read_window`, `read_data` in `fpga/bridge.v`). So my worry that a 50 MHz
synchronous front end would miss the PIO read deadline does not apply to
the leading edge.

**It is not orderable yet,** and the design says so itself:
- 600 open connections on the carrier;
- local decoupling still to place;
- carrier registration against CE501/CE502/CE504;
- the C5 clamp;
- no Lattice place-and-route timing.

## What the measurements settle

| Measurement | Values | Settles |
|---|---|---|
| Shield | 8.62 mm to the outer face, 0.62 mm sheet, 1.4 mm extra local metal; owner ceiling 6.00 mm at IC501/CN503, 8.00 mm elsewhere, 0 over the two thermal chips | H5 heights. The stack is 2.5 (stand-off) + 0.8 (PCB) + 2.0 (FH12) = 5.3 mm, leaving 0.7 mm before insulation. The arithmetic is consistent |
| CN503 | 38.67 × 6.56 mm housing; 25-contact run 24.39 mm; 0.44 mm contacts; paper-template fit; both rows accessible; A row faces west, contact 1 at the south end | **B2.** Both rows can be tapped. Pitch is 1.00 mm: the run implies 0.998 mm (outside edges) or 1.016 mm (centres). Separate flex arms per row make the 5.03 mm row spacing irrelevant, which is a good call |
| IC501 | 26.68 × 12.65 mm, 16.66 mm lead span, about 2 mm tall, about 12 mm to CN503; 44-pin SOP at 1.27 mm | BIOS cutout, with one caveat (finding 5) |
| Side space; C5 | About 7 mm each side; C5 4.48 mm with its USB connector | Outline limits. The C5's height without USB is still unmeasured |

### Not yet measured

- **Continuity:** every CN503/IC501 map is still marked "candidate".
- **Voltages:** VA1's 3.3 V BIOS rail comes only from the RDC drawing; the
  5 V rail has not been measured under load.
- **Behaviour:** B3 (Holly timing), B4 (GD-ROM device-1 behaviour) and the
  host's IORDY pull-up.

### My earlier review items

| Status | Items |
|---|---|
| Done in circuit or RTL | B1, H1–H4, M1, M2, M4, L1–L4 |
| Settled | B2 |
| Partly settled | H5 |
| Still open | B3 (the raw probe pads now provide access), B4, M6 |

## Corrections to my notes that I accept

- **IC501 is an 8-bit ROM on VA1** (RDC trace):
  - DD0–7 are its data lines;
  - DD8 is Q15/A-1;
  - DD9–15 are A0–A6;
  - DA0–2 are A7–A9;
  - DIOR- is its OE.

  I had assumed D0–D15. The byte-mode NOR with A20/A21 banks follows
  correctly from this.
- **A chip erase wipes every NOR bank,** so gating WE by bank never made a
  writable "stock" bank safe. Immutable stock means the mask ROM. Correct.
- **A jump to the reset vector is not a verified restart.** Deferring
  runtime bank switching is right.

## Findings

### 1. High, cheap: prove CN503 row identity before ordering the flex

The supply and ground contacts are symmetric between rows:

| Contacts | Net |
|---|---|
| A1/B1 | 3.3 V |
| A3/B3 | 5 V |
| A8/B8, A13/B13, A18/B18, A21/B21 | GND |
| A25/B25 | 12 V |

So if the CSV's A/B naming is reversed for this board, every data and
control line lands on the wrong conductor, and no voltage check would catch
it. The map comes from the iceGDROM riser symbol (the mating half) and the
RDC trace, never from this board. Flex arms built on a swapped map have to
be remade.

**Four power-off readings settle both row identity and IC501 bus sharing.**
Expect low ohms, because RA507–RA515 are series resistors:

| IC501 pin | CN503 contact | Signal |
|---|---|---|
| 14 | A15 | DIOR- |
| 15 | B11 | DD0 |
| 17 | A11 | DD1 |
| 4 | A19 | DA0 |

**With power on,** these readings at the tails confirm end orientation.
Probe carefully, because 12 V sits next to the audio contacts:

| Contacts | Expected |
|---|---|
| A1/B1 | 3.3 V |
| A3/B3 | 5 V |
| A25/B25 | about 12 V |

**IC501 pin 23 at 3.3 V** confirms the VA1 rail.

This needs the shield off once more. I would do it before paying for the
flex.

### 2. Medium: PIO read data is held past the ATA release limit

`READ_HOLD_CYCLES = 2` at 50 MHz keeps DD driven for about 40–60 ns after
DIOR- rises, plus synchronizer phase and buffer delay. ATA allows at most
30 ns (t6z). With PIO0's 600 ns cycle this is probably harmless, and the raw
probes can measure it.

The fix is easy:
- the input buffer, LUT, OE gate and transceiver delays already exceed the
  5 ns minimum hold (t6);
- so use 0 hold cycles for PIO reads, and keep the latched word only for
  DMA's final word.

Separately, raw `BUS_DIORn` and `BUS_DA` are sampled directly in clocked
logic (`bridge.v` lines 65, 254 and 263) without synchronizers. That is a
low-impact metastability risk.

### 3. Medium: R410 turns an assembly slip into a supply short

R410 is a 0 Ω strap fitted only in BASE. If someone forgets to remove it
before fitting the selector harness, STOCK_RECOVERY shorts console 3.3 V to
ground, as the design itself notes.

Make R410 about 1 kΩ, which keeps its pull-up role. R409 is a 10 kΩ
pull-down, so BASE still reads about 3.0 V. The same mistake then draws
about 3.3 mA instead of shorting the rail. Remove the hazard rather
than relying on an assembly note.

### 4. Medium: the 5 V power-fail threshold may trip during disc activity

U26 trips at 4.36 V nominal on PRIMARY_5V, which is measured after the
harness, the fuse and Q20. At about 1.2 A, those drops total roughly
0.15–0.2 V. With the console rail at its low tolerance and GD-ROM spin-up
sag, `PWR_FAILn` could disarm the bridge mid-transfer.

Measure the chosen 5 V tap during GD-ROM spin-up and C5 transmit with the
bridge loaded before fixing the divider. The bucks regulate down to about
3.6 V input, so a threshold near 4.2 V costs nothing.

### 5. Low: the BIOS cutout may be short

The recorded 26.68 mm "body length" equals 21 × 1.27 = 26.67 mm, which is the
first-to-last lead-centre span of a 44-pin, 1.27 mm SOP. 500 mil 44-SOP bodies
are typically about 28.2 mm long.

The cutout is 29.0 × 19.0 mm (`generate_board.py` `CUTOUT`). That leaves
about 0.4 mm per end if the body is 28.2 mm, before registration error.
Check this on the 1:1 fit sheet, and widen the opening to about 30.5 mm if
the outline allows.

### 6. Low: 16 console-side 47 kΩ pull-downs load the shared bus

R224, R226 … R254 put a 47 kΩ pull-down on every console-side ATA_DD line.
That bus is shared with the GD-ROM and, on VA1, the BIOS ROM.

- **Electrically:** probably harmless, at about 70 µA each.
- **By convention:** it is not standard ATA practice, where the host has one
  DD7 pull-down.
- **Behaviourally:** it changes what an undriven bus reads.

If the SN74LVC16T245's B-side inputs need no bias while OE is high, leave
the console-side set unfitted and keep the bridge-side set.

### 7. Low: the flex passes the 12 V contacts

The arms run north past the excluded contacts A22–A25 and B22–B25, which
include +12 V. Keep that stretch free of exposed copper, and inspect for
solder bridges after hand-soldering A21/B21.

### 8. Note on the BIOS option: no in-system repair of the NOR

In STOCK_RECOVERY the bridge is disabled and the NOR cannot be written. So a
corrupted NOR bank 0 can only be repaired off the board (desoldered, or with
a TSOP clip). Document that repair path.

## Free test still available for B4

The unlock frames send command F0 with DEV=1. If the GD-ROM ignores the DEV
bit, it executes them too. K-UI's existing AUTO storage probe already asks
whether the drive answers for device 1, with no teardown:

1. Boot K-UI with no card in the SCI adapter and nothing on the rear port.
2. Read `IDE/CF initialization failed: ATA=N` in the log.

Interpretation is in `g1-bridge-disposition-response-2026-10-09.md`:
- **5:** bad news; the drive likely ignores DEV.
- **3:** favourable.
- **4:** ambiguous.

## K-UI software this design implies

- **Activation:**
  - send both keyed F0 frames on DEV=1 within 1 ms, with interrupts masked;
  - unlock again after any ATA reset or SRST, including from the in-game
    resident reader;
  - relock only after writes are durably flushed.
- **`src/core/ata.c`:**
  - unlock before IDENTIFY;
  - PIO0 only;
  - expect BSY to stay set after the final write word until the bridge
    reports media commit.
- **Diagnostics:** the raw DEV=1 register probe offered earlier.
