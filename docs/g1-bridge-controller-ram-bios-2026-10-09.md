# G1 bridge: controller choice, extra RAM and dual BIOS

9 October 2026. Answers three owner questions about the G1 bridge on
`design/g1-sd-wifi-20261009` (`fb70d55`). Review only; nothing on the
design branch was changed. Builds on
[`g1-bridge-review-2026-10-09.md`](g1-bridge-review-2026-10-09.md) and
[`g1-bridge-disposition-response-2026-10-09.md`](g1-bridge-disposition-response-2026-10-09.md).

## Short answers

- **Controller:** keep the RP2350B for SD, filesystem, Wi-Fi link and USB,
  but move the ATA bus timing into a small FPGA/CPLD. No other MCU class
  does the ATA side better. The bigger MCUs bring native SD and more RAM,
  but none can answer an asynchronous bus strobe deterministically.
- **RAM:** yes. One 8 MB QSPI PSRAM is cheap, about 1 mm tall, and takes
  one pin. It hides SD latency (read-ahead and caching). It does not raise
  peak bandwidth, because the SD card already outruns the G1 bus.
- **Dual BIOS:** it belongs on this board. The BIOS ROM sits on the same G1
  bus as the GD-ROM, so a board that replaces IC501 could be both the dual
  BIOS and most of the bridge's bus tap. This needs a continuity check.

## 1. Controller

### Why ATA timing is the hard part

KOS names 0xA05F7490/94 `G1_ATA_PIO_RACCESS_WAIT` and `..._WACCESS_WAIT`.
Holly therefore most likely has one *read* timing for both register and data
reads, so register reads must meet the data-read deadline. At ATA PIO mode 0
minimums, that is about 115 ns from DIOR- falling to valid data. This
corrects my earlier note that register reads get the longer 290 ns strobe.

Estimated RP2350-only register-read path at 150 MHz:

| Step | Clocks |
|---|---|
| Input synchronizer | 2 |
| PIO wait, sample, push | 3 |
| Chained DMA lookup (FIFO to read-trigger, table to TX FIFO) | 6–9 |
| PIO pull and output | 2 |
| Buffer and board delay | about 1 |
| **Total** | **about 15–18 (100–120 ns)** |

That leaves no margin unless Holly's real strobes are longer than the ATA
minimum, which B3 must measure. In an FPGA, register reads come straight from
flip-flops in about 10–20 ns.

### The FPGA also absorbs other review items

- **M1:** latched DMA ownership through the final word.
- **M2:** output ownership, which becomes moot.
- **H2:** relock on RESET- and SRST.
- **Section 3:** the BIOS bank latch and write gate.

All of these need hardware state anyway. Doing them in one small part is
simpler than in discrete logic plus PIO.

### Options compared

| Option | ATA timing | SD | RAM | Verdict |
|---|---|---|---|---|
| RP2350B alone | PIO plus DMA lookup; marginal | PIO 4-bit | 520 KB, plus PSRAM | Works only if B3 finds long strobes. PSRAM takes the last spare pin (GPIO 47) |
| RP2350B + FPGA/CPLD | Deterministic | PIO 4-bit | Block RAM FIFO, SRAM, PSRAM | **Recommended** |
| STM32H7, i.MX RT1062, ESP32-P4 | Interrupt latency; would still need an FPGA | Native 4-bit/UHS | 1 MB+, P4 has 32 MB PSRAM | No gain at the bus; BGA packages; new toolchain |
| FPGA with soft CPU (iceGDROM style) | Deterministic | Soft | Block RAM | SD, filesystem and Wi-Fi link in a soft core is too much software |

### FPGA parts

- **Lattice MachXO2-1200HC or -2000HC (TQFP-100):**
  - single 3.3 V supply;
  - instant-on from internal flash, so the bus defaults and the BIOS latch
    are valid from console power-on;
  - about 79 I/O and 8–9 KB of block RAM;
  - vendor tools (Diamond, free licence).
- **Open-toolchain alternative: iCE40HX4K (TQFP-144, yosys/nextpnr).** It
  needs a 1.2 V core and loads its configuration at power-up, so the
  hardware pull-ups must hold every enable off until then.
- **iCE40UP5K:** its 128 KB SPRAM is tempting, but the 48-pin package has
  only 39 I/O, which is too few.

### Pin budget with the FPGA

- **FPGA, about 46 pins:** 28 ATA, 3 buffer control, about 10 for the MCU
  link, and about 5 for the BIOS.
- **MCU link:** an 8-bit PIO bus at about 40 MHz gives about 40 MB/s, well
  above MWDMA2's 16.7 MB/s.
- **RP2350B, about 25 of 48 pins:** link about 10, SD 6, C5 7, PSRAM chip
  select 1, power-fail input 1.

### Physical form

- **Bare RP2350B on the bridge PCB,** as the current schematic has it, is
  the lowest option.
- **RP2354B** (RP2350B with 2 MB flash in the package) removes the external
  flash chip. Check availability, and that the firmware fits.
- **PGA2350** (RP2350B, 8 MB PSRAM, 16 MB flash; 25.4 mm square, 3.6 mm
  tall) is a good bench board. Buy A4 stepping, because store listings
  disagree on which stepping ships.

### Precedent

ZuluSCSI and BlueSCSI serve SCSI from microSD on RP-class chips using PIO.
A forum report says ZuluIDE pairs an RP2040 with an FPGA for IDE. That is
unverified, so check its documentation.

## 2. Extra RAM

### Where bridge read time goes

- **SD command latency:** usually under 1 ms, but the SD read timeout allows
  up to 100 ms.
- **Bus transfer:** up to 16.7 MB/s, if Holly sustains MWDMA2 (M5).
- **Sequential SD:** a 4-bit SD bus at 50 MHz carries about 20 MB/s, which
  already exceeds the G1 bus.

So RAM buys latency hiding:

- **Read-ahead.** K-UI's GD service knows each track's extents, so the
  bridge can stream ahead into PSRAM. 8 MB holds about 4,000 sectors of
  2,048 bytes.
- **Caching.** Filesystem metadata, cluster maps and small hot files.
- **Stream smoothing.** Tens of seconds of FMV or CD audio.
- **Not write-back.** Writes complete only when the card confirms (M6).

### Tiers

| Tier | Size | Covers at 16.7 MB/s | Role |
|---|---|---|---|
| FPGA block RAM | 8–9 KB | about 0.5 ms | Serves the bus deterministically |
| RP2350 SRAM ring | e.g. 256 KB | about 15 ms | Absorbs MCU jitter |
| PSRAM | 8 MB | about 0.5 s of full-rate DMA (minutes at FMV rates) | Read-ahead and cache |
| microSD | — | — | Backing store |

- **PSRAM speed:** on the RP2350's QMI at 75 MHz quad, peak is about
  37 MB/s before command overhead. That is enough to refill SRAM faster than
  the bus drains it. It is not deterministic, so never serve ATA cycles
  directly from PSRAM.
- **Part:** an APS6404L-class 64 Mbit QSPI PSRAM, SOP-8 or USON-8.
- **Pin:** the chip select must be on GPIO 0, 8, 19 or 47 (QMI CS1). The
  current CSV uses 0, 8 and 19 for ATA, which leaves 47, the only spare.
  With the FPGA option this constraint disappears.
- **Dreamcast side:** needs no extra RAM. Holly's G1 DMA writes straight
  into main RAM.

## 3. Dual BIOS on the same board

### Evidence that the BIOS shares the bus

- **KOS `dc/syscalls.h`:** before reading the ROM font, call
  `syscall_font_lock()` "to ensure that you have exclusive access to the G1
  BUS the ROM is located on". The font address points into ROM, so games
  read the BIOS in place.
- **BIOS-flash guides:** they wire the replacement chip's WE# to CN503 B14
  for in-system flashing. ConsoleMods' guide says so as reported by search;
  the site is unreachable from here. B14 is DIOW- in `CN503-reference.csv`,
  so Holly's ROM-area writes strobe DIOW-.
- **MAME's notes:** Holly sums the BIOS reads on the G1 data bus to unlock
  the GD-ROM.

### Consequences

1. **IC501 probably carries G1 D0–D15 and the read strobe.** Check
   continuity with power off:
   - IC501 data pins against the CN503 DD pins (possibly through
     RA507–RA515);
   - IC501 OE# against A15 (DIOR-);
   - IC501 address pins against A19, B17 and B19 (DA0–2).

   If confirmed, a board that replaces IC501 gets all 16 data lines from
   the footprint. The B-row list then drops from 14 contacts to 6: DA1,
   DA2, CS1-, DIOW-, IORDY and INTRQ.
2. **The bridge must ignore strobes without CS0-/CS1-** (or its own DMACK-),
   because BIOS, flash and font accesses use the same lines. M1's gate
   already requires this; the FPGA makes it exact.
3. **Supply level.** The same guides measure the BIOS supply (pin 23)
   because some boards run it at 5 V. Measure the target board. It sets the
   G1 signalling level that the buffers and the flash must handle.

### Proposed design

- **Chips:**
  - the original mask ROM as stock: no software can erase it;
  - one 3.3 V 64 Mbit NOR (TSOP-48) holding up to four 2 MB images,
    selected by its A20/A21 lines;
  - exactly one chip's CE# follows Holly's ROM CE#;
  - if moving the mask ROM is impractical, use a NOR bank 0 that hardware
    never lets WE# reach.
- **Power-on selection:** a hardware switch only (stock or user default).
  Firmware cannot change it, which matches the disposition's rule.
- **Session selection:**
  - K-UI requests image N with an ATA vendor command, and the FPGA latches
    it;
  - K-UI, running from RAM with interrupts off and no G1 traffic, jumps to
    the ROM reset vector to warm-boot;
  - the latch holds until power-off, and RP2350 resets do not touch it;
  - nothing else may move it, because games read the font in place.
- **Writing images:**
  - K-UI writes through Holly's ROM-area write cycles, as existing
    in-system flashers do (independent code, no DreamShell);
  - WE# reaches the NOR only through the FPGA's gate, which opens on a
    vendor command, never for the stock chip, and closes on timeout or
    reset;
  - the bank being programmed is mapped in the ROM window, so K-UI must not
    read ROM (font included) until it has verified the image and rebooted.
- **Holly unlock:** each image must pass Holly's G1 unlock. K-UI's
  `activate()` already handles one custom-BIOS signature (0xE6FF). Test
  GD-ROM and bridge access under every image.
- **Content:** users dump and install their own BIOS. K-UI ships tools and
  patches, never Sega images.
- **FPGA pins for the BIOS:** about 6 (ROM CE# in; two chip CE# outputs or
  bank lines; WE# gate; switch input).
- **Later payoff:** a K-UI boot image that starts K-UI from the bridge at
  power-on, with no disc and no SCI.

## Additions to the bring-up order

- **Step 1 (with the shield measurements):**
  - IC501 package and continuity map, as listed above;
  - IC501 supply voltage.
- **Step 2:**
  - draft the FPGA's ATA register file, DMA-owner latch and BIOS latch;
  - exercise them in the disposition's host-side model before connecting
    anything.
