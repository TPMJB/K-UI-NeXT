# Review: G1 microSD and Wi-Fi bridge, Rev A proposal

Reviewer: Claude (Claude Code), 2026-10-09. Responds to
`hardware/g1-sd-wifi/docs/REVIEW.md` on `design/g1-sd-wifi-20261009`
(commit `89f6dc6`). Review only: no files on that branch were changed and no
fabrication output was produced.

**Scope of what was reviewed.** `README.md`, `controller/K-UI-G1-GPIO-RevA.csv`,
`docs/CN503-reference.csv`, `controller/START-HERE.txt`, `controller/CHECKS.txt`
and the parts placed in `controller/KUI-G1-Bridge-RevA.kicad_sch`. The sheet
contains:
- the RP2350B (U1);
- W25Q128 boot flash (U3);
- a 12 MHz crystal (X1);
- an NCP1117 LDO (U2);
- the USB (J1) and SWD (J4) connectors, BOOTSEL and RUN switches;
- two bench headers (J2/J3).

There is no G1 buffering, SD, C5, power-fail or BIOS circuitry yet, as the
brief says.

**Sources checked directly:**
- RP2350 register definitions in the
  [pico-sdk](https://github.com/raspberrypi/pico-sdk/tree/master/src/rp2350/hardware_regs/include/hardware/regs)
  (`io_bank0.h`, `pio.h`).
- KallistiOS
  [`g1ata.c`](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/g1ata.c).
- K-UI's own `src/dreamcast/ata_bus.c` and `hardware/cf-board/README.md`.
- The iceGDROM FPGA GD-ROM emulator for this connector
  ([repository](https://github.com/zeldin/iceGDROM): `pcb/riser/riser.sch`,
  `fpga/source/top.v`).

The RP2350 datasheet host (`datasheets.raspberrypi.com`) is blocked from this
environment. Datasheet-only claims below are marked **verify in datasheet**.

## Summary

The pin plan holds up: the CN503 mapping is correct, the PIO pin windows
work and the SPI1 mux is right. Nothing in it is a confirmed error. The open
risks are all electrical and protocol questions the brief already lists.
Four of them block layout, and three of those can be settled on the bench
with the existing **CF board** and a logic analyzer before any new hardware
is built:
1. what timing Holly's G1 controller actually produces;
2. whether Holly honours IORDY;
3. how the original drive behaves while device 1 is selected.

An MCU-only implementation is plausible because K-UI controls the host side's
timing registers and can choose slow modes. That is unproven until those
measurements exist.

## Confirmed correct (no change needed)

- **CN503 mapping** (`docs/CN503-reference.csv`). All 28 signals match
  `hardware/cf-board/README.md` contact for contact, and the standard 40-pin
  IDE numbers are right throughout. For example:
  - DD0 is IDE 17 (B11), DD15 is IDE 18 (B12), DA2 is IDE 36 (B19);
  - CS1n is IDE 38 (B20), IORDY is IDE 27 (B15), INTRQ is IDE 31 (B16);
  - DMARQ is IDE 21 (A14), DMACKn is IDE 29 (A16).

  The supplies (A1/B1 3.3 V, A3/B3 5 V) and the eight grounds also agree.
- **PIO pin windows.** On RP2350, PIO `GPIOBASE` accepts only 0 or 16
  (`pio.h`: "Only the values 0 and 16 are supported"), so each PIO block
  sees GPIO 0–31 or 16–47.
  - The ATA side (GPIO 0–30, including PFAILn on 31) fits one window at base 0.
  - SD on GPIO 32–37 needs PIO2 at base 16, which also reaches 32–37.
  - The layout is unusually good for capture. GPIO 0–22 hold DD0–15, DA0–2,
    CS0n/CS1n, DIORn and DIOWn contiguously, so one `in pins, 23` samples a
    whole write cycle.
  - DATA_OEn and DATA_DIR on 28–29 can be PIO side-set pins, switching the
    buffer in the same cycle as the bus action.
- **SPI1 for the C5.** `io_bank0.h` gives GPIO40 = SPI1_RX, GPIO42 =
  SPI1_SCLK and GPIO43 = SPI1_TX (function 1); GPIO41 as an SIO chip select
  is fine.
- **SD data pins.** D0–D3 are consecutive (34–37), as a four-bit PIO
  `in`/`out` requires, with CMD on 33 and CLK on 32 as side-set.
- **Silicon stepping.** Targeting stepping A4 is right. Erratum E9 (inputs
  latching near 2.2 V) affected A2; A4 is the public stepping with the fix
  ([Hackaday on the A4 product change note](https://hackaday.com/2025/07/31/)).
  Check date codes on purchased parts.

## Findings by severity

### Blockers (resolve before PCB layout)

**B1. No isolation stage yet.** The GPIO CSV wires GPIO 0–27 straight to G1
nets (`G0_DD0` … `G27_DMACKn`). That is correct as a logical plan, but the
board must never be connected that way. Per the datasheet, RP2350 GPIO 5 V
tolerance requires IOVDD to be present (**verify in datasheet**, GPIO
electrical section). An unpowered or booting bridge could back-power from
the console, or load the drive's lines.

*Requirement:* every G1 line goes through parts with partial-power-down
(Ioff) protection:
- data: 74LVC16245A (single supply) or SN74LVC16T245 (dual supply);
- the eight inputs: a 74LVC244-class buffer;
- IORDY, INTRQ and DMARQ: tri-state 74LVC1G125-class parts.

Avoid bus-hold ("H") parts on the G1 side, because they would fight the
drive. Hardware pull-ups must keep every enable inactive by default, so the
outputs only drive when all of these hold:
- the MCU's response enable is set;
- local 3.3 V is good;
- console 3.3 V is good;
- G1 RESET- is not asserted.

Inputs need no gating if their buffer has Ioff.
*Next experiment:* none; this is a schematic task (next pass, item 1 in
START-HERE).

**B2. Half the signals are on the board-edge row.** 14 of the 28 signals sit
on CN503's B row:
- DD0, DD2, DD4, DD6, DD9, DD11, DD13, DD15;
- DA1, DA2, CS1n;
- DIOWn, IORDY, INTRQ.

CN503 is surface-mount with 1.0 mm pitch and nothing through to the underside
(`cf-board/README.md`). A top-side tap therefore needs proven access to the
B-row tails.

*Fallback to qualify:* the 33 Ω arrays (RA507–RA515 and others) on the
underside. Tap their **CN503 side**, so the bridge sits electrically where
the drive does, behind the same series termination. Identify which side is
which by continuity before relying on them.
*Next experiment:* a macro photo and continuity map of both rows and the RA
pads on the owner's VA1.

**B3. Unknown G1 cycle timing.** KallistiOS programs Holly's G1 timing with
opaque codes: `G1_ACCESS_PIO_DEFAULT 0x00000222` and
`G1_ACCESS_WDMA_MODE2 0x00001001`, written to 0xA05F7490/94 and
0xA05F74A0/A4. They are write-only, and their mapping to nanoseconds is not
publicly documented. K-UI uses the same PIO value (`ata_bus.c`). The
standard ATA limits for those modes are below; both need checking on
hardware:

| Mode | Cycle | Strobe width | Device data deadline |
|---|---|---|---|
| PIO mode 0 | 600 ns | 165 ns | about 115 ns after DIOR- falls |
| Multiword DMA mode 2 | 120 ns | 70 ns | 50 ns from DIOR- falling (tE) |

It is also unknown whether Holly samples IORDY. KOS selects transfer mode
00h (PIO default) rather than 01h ("PIO default, IORDY disabled"), but that
is the device side only.

*Next experiment (smallest):* fit the existing CF board with a CF card as
device 1 and attach a logic analyzer of at least 200 MS/s to CN503.
- Record DIOR-/DIOW-/CS/DA/DMACK-/DMARQ during KallistiOS or K-UI g1ata
  IDENTIFY, PIO reads and a multiword DMA read.
- Sweep the 0xA05F7490 and 0xA05F74A0 values to map the codes to
  nanoseconds.
- Repeat with IORDY held low by a GPIO during a status read, to see whether
  the strobe stretches.

**B4. Shared bus with no DASP-/PDIAG-/CSEL.** CN503 carries no DASP-, PDIAG-
or CSEL (contact B2 is unconnected on the iceGDROM riser), so the GD-ROM
cannot learn that a device 1 exists. ATA's device-0-only rules allow a lone
device 0 to answer some register reads while device 1 is selected, which
would contend with the bridge. Working "GD-IDE" installs with the drive
retained suggest the Samsung drive does not do this. That is anecdotal, not
measured.

*Next experiment:* in the same CF-board session:
- With no CF card, select DEV=1 and read Status/Error/Sector registers.
  Values other than floating-bus values mean the drive is answering for
  device 1.
- With the card fitted, watch the data lines on a scope for intermediate
  levels during device-1 reads, which would indicate contention.
- Repeat on both drive types if possible.

### High

**H1. INTRQ is the console's GD-ROM interrupt.** Anything the bridge
asserts arrives as a GD-ROM event at the BIOS or a running game.
- Keep INTRQ released whenever the bridge is not selected, and while nIEN=1.
- Default to nIEN=1 behaviour until K-UI explicitly enables interrupts.
  Prefer polling or the G1 DMA-end interrupt.
- K-UI must leave DEV=0 selected whenever it returns control.
  KallistiOS already does this (`g1_ata_mutex_unlock`: "Make sure to select
  the GD-ROM drive back"). Make it a hard rule in K-UI's resident.

**H2. Stay invisible until K-UI unlocks the bridge.** The BIOS polls the
drive at boot and on disc checks, and games never expect device 1. Suggested
firmware policy: the bridge answers nothing (data, IORDY, INTRQ and DMARQ
all released) until K-UI sends a vendor unlock while DEV=1. It drops back
to invisible on hardware RESET- or power-on. This bounds the effect of any
firmware bug on stock behaviour.

**H3. DMA ownership.**
- **DMARQ and DMACK-:** assert DMARQ only for a DMA command addressed to
  device 1 while DEV=1, and honour DMACK- only while our own DMARQ is
  pending (the CSV already notes that DMACK- matters with CS inactive).
- **Holly's G1 DMA engine (0xA05F7404–7418):** the drive shares it, so
  K-UI must never start a bridge DMA while a drive DMA is active
  (`g1_dma_in_progress`).
- **Multiword DMA data path:** the device may keep driving the data bus for
  the whole burst while DMACK- is asserted (it releases within tZ after
  DMACK- negates). The next word can therefore go out as soon as DIOR-
  rises, rather than within 50 ns of the next falling edge. That makes MWDMA
  feasible for PIO.

**H4. Power source.**
- The current through CN503's 3.3 V contacts (A1/B1) is unqualified; they
  also feed the drive.
- Peak load is the RP2350B, an SD card writing (100–200 mA class peaks) and
  the C5 transmitting (several hundred mA).
- Recommended default: console 5 V into a local buck or LDO for MCU and SD,
  plus a separately switched 5 V branch for the C5.
- Keep USB VBUS behind an ideal diode or power multiplexer so it can never
  back-feed the console.
- Size everything from measured current with the drive spinning.

**H5. Clearance: the dual-BIOS fit is necessary evidence, not
sufficient.** A dual-BIOS piggyback that fits without modification shows
roughly a TSOP plus thin-PCB height of clearance above IC501. This board
stacks taller parts:
- the RP2350B QFN-80 (about 0.9 mm);
- a microSD socket (about 1.5–2 mm);
- above all, a **socketed** XIAO: header sockets plus the module.

Options if it is tight:
- reflow the XIAO by its castellated edge instead of socketing it;
- move the C5 off the carrier on a short cable.

The antenna also needs to sit outside the metal shield for usable RF. The
two START-HERE measurements are still required.

### Medium

**M1. Let hardware enable the data buffer.** PIO side-set can switch
DATA_OEn and DATA_DIR on the strobe, but a crashed or rebooting MCU would
leave them wherever they were. The safest form is a small gate network (or a
GreenPAK/CPLD) that enables the buffer toward the host only when all of
these hold:
- RESP_EN;
- power-good;
- DIOR- asserted;
- either (CS asserted and our device selected) or (DMACK- asserted and our
  DMARQ active).

The MCU then cannot drive the bus outside a valid read cycle.

**M2. Concrete PIO/DMA/core plan:**

| Block | Window | State machines |
|---|---|---|
| PIO0 | base 0 | SM0 register-read responder: on DIOR- with CS0n/CS1n asserted it samples DA/CS, pushes an index, and a DMA pair returns the byte from a 32-entry shadow table. SM1 write capture: samples GPIO 0–22 on DIOW- rising |
| PIO1 | base 0 | SM0 data-port PIO streamer (16-bit). SM1 multiword DMA streamer driven by DMARQ/DMACK-/DIOR-/DIOW- |
| PIO2 | base 16 | SD four-bit: one SM for CMD, one for DAT |
| SPI1 | hardware | C5 link |

- The ATA programs are split over PIO0 and PIO1 because of the
  32-instruction limit per block.
- The register-read lookup uses two chained DMA channels: the first writes
  the pushed address into the second's read-address trigger, and the second
  copies the table byte to the TX FIFO. Expect about 10–15 system clocks of
  latency (about 70–100 ns at 150 MHz).
- Bypass input synchronizers on strobe pins only (`INPUT_SYNC_BYPASS`
  exists in `pio.h`) to save two cycles, at metastability risk. Measure
  before relying on it.
- About 8–10 of the 16 DMA channels are needed.
- Core 1 runs only the ATA state machine: polling FIFOs, updating the shadow
  Status/Error, device-select snooping. Core 0 runs SD, C5 and USB.

**M3. Register semantics to implement:**
- Snoop every Device register write (DEV bit 4) regardless of selection.
- Device Control writes (SRST bit 2, nIEN bit 1) reach both devices: SRST
  resets the bridge too.
- Ignore command writes while DEV=0, except EXECUTE DEVICE DIAGNOSTIC (90h),
  which addresses both devices.
- IDENTIFY should advertise:
  - LBA28, plus LBA48 if more than 128 GiB will be exposed;
  - PIO 0 and only the multiword DMA modes actually proven;
  - honest write-cache bits (words 82/85, bit 5) with FLUSH CACHE
    (E7h/EAh), or no write cache at all.
- Hold BSY until data is genuinely ready.

**M4. Hardware RESET- (A2).** DragonCity ties the IDE device's reset to
3.3 V in the GD-IDE install. That suggests the BIOS pulses G1 RESET- at
moments that would disturb a hard drive. The bridge must reset quickly and
return to "invisible" (H2), and K-UI must reinitialise after any reset.
Record A2 during boot, disc checks and a drive error, to see when it fires.

**M5. Throughput and stalls.** Even modest modes beat SCI by a wide margin:
- multiword DMA mode 0 (480 ns per word) bursts at about 4 MB/s;
- mode 2 reaches about 16.7 MB/s;
- SCI peaks at about 1.2 MB/s and the real drive measured about 1.45 MB/s.

SD reads can stall for milliseconds and writes for hundreds. Hold BSY (PIO)
or deassert DMARQ between words (DMA) rather than inventing data. Read
ahead in the RP2350's 520 KB SRAM. Proven four-bit PIO SDIO implementations
exist for RP2040/RP2350, but bus compatibility is still unproven. **Start in
the slowest modes K-UI can program** (the host owns the timing registers),
then speed up only after B3's measurements.

**M6. Data integrity:**
- Reserve energy can at most finish the SD command in flight; SD busy times
  run to hundreds of milliseconds. It cannot protect the card's internal
  flash translation layer or data still in Dreamcast RAM.
- Treat it as optional.
- Rely on these instead:
  - read-only operation by default, plus a firmware write gate opened only
    by K-UI's write session (vendor command);
  - write-through, or FLUSH honoured with correct ordering;
  - temporary file, then verify, then rename;
  - an ext4 journal, if lwext4's journaling proves sufficient in K-UI's
    port.
- Power-cut test: a relay on the console PSU cuts at random points during
  write sessions, at least 100 cycles. After each cut, run fsck and check
  SHA-256 of every previously completed game against a stored manifest.
  Also cut during play: expect zero changes.

### Low

- **L1.** Add the remaining CN503 contacts to `CN503-reference.csv` as
  explicit do-not-connect rows, so no one taps +12 V by accident:
  - A17 (EMPH) and B2 (no connection on the iceGDROM riser);
  - A22–A25 and B22–B25 (CD audio, ground and +12 V on A25/B25).
- **L2.** Those audio contacts carry the drive's digital audio. iceGDROM's
  `top.v` drives CDCLK (33.868 8 MHz), SCK, SDAT and LRCK as **outputs** of
  the emulated drive, so the drive sources the CD audio clocks. Leave them
  unconnected in Rev A. A future hardware-CDDA feature would need to
  multiplex them with the retained drive; a footprint for that can wait.
- **L3.** Put test points on all 28 G1 lines and on the enables. Logic
  analyzer access will be needed for every bring-up step.
- **L4.** The bench breakouts J2/J3 duplicate the GPIOs. Keep them on the
  bench board only.

## Dual BIOS and networking (brief question 7)

- **Dual BIOS:** selection must work without the MCU.
  - Use a physical switch or jumper on the second ROM's chip enable, or the
    top address line, with stock as the pulled default.
  - Give the stock chip a hard disable path (its CE switched through a gate
    or lifted pin) when the custom ROM is selected.
  - An MCU-controlled select would make boot depend on bridge firmware.
- **Networking in existing games:** they look for the modem or the
  Broadband Adapter on G2. A G1 device cannot stand in for either. Treat it
  as out of scope, as the brief already says.

## What can proceed now, and what must wait

| Can proceed now | Waits for measurement |
|---|---|
| CF-board logic-analyzer session (B3, B4, M4) | Tap method and board outline (B2, H5) |
| Isolation and power schematic (B1, H4, M1) | Final power path and hold-up sizing (H4, M6) |
| ATA firmware on a host-side register model, and PIO programs in simulation | Choosing multiword DMA modes (after B3) |
| Bench responder on an RP2350 board against the CF board's bus | Antenna placement (H5) |
| SD four-bit driver and C5 link on a dev board | |

For the bench responder, use a board that breaks out all 48 RP2350B GPIOs,
such as the Pimoroni PGA2350 or an RP2350B core board. A Pico 2 is not
enough: its headers skip GPIO 23–25 and 29, so the contiguous GPIO 0–27 ATA
block cannot be reproduced on it.

**Recommendation.** Do the CF-board measurement session first. One evening
with a logic analyzer answers B3 and B4, and decides whether Rev B needs a
small CPLD/FPGA. iceGDROM's iCE40 shows this bus is workable with
programmable logic. Then draw the isolation stage before anything touches a
console.
