# G1 bridge: response to the review disposition

9 October 2026. Responds to `hardware/g1-sd-wifi/docs/review-disposition-2026-10-09.md`
on `design/g1-sd-wifi-20261009` at `fb70d55`, which answers
[`g1-bridge-review-2026-10-09.md`](g1-bridge-review-2026-10-09.md). Review only;
nothing on the design branch was changed.

I accept the disposition almost entirely. Two of its corrections are errors in
my review (M2 output ownership, the DMARQ term in M1). I differ on ordering in
one place: the device-1 contention question (B4) needs no fixture, because
K-UI already asks it on every boot without a card.

## Corrections accepted

**CF board.** My "first experiment" assumed a CF rig that was never built.
Withdrawn. The two items it covered are re-planned below without it.

**M2: PIO output ownership (my error).** FUNCSEL gives each GPIO one output
source, so my split (PIO0 register replies and PIO1 data streaming, both on
DD0–15) cannot work. Replacement rule:
- one PIO block owns every ATA-side output: DD0–15, the IORDY/INTRQ/DMARQ
  enables, and the buffer OE/DIR if PIO drives them;
- input-only work (write capture, select/SRST snooping) can live in any
  block, since every PIO reads every GPIO input;
- the register responder, data-port streamer and MWDMA streamer must then
  fit one 32-instruction memory. Show it with drafted programs before
  layout, not by estimate. RP2350 PIO IRQs can reach neighbouring blocks,
  which helps the input blocks hand off to the owner (confirm in §11).

My 10–15-clock lookup figure was a typical path, not a bound. The worst case
must include the synchronizers, DMA arbitration against SD and C5 traffic,
SRAM bank contention and buffer delay. Two things help:
- give the ATA DMA channels high priority, and keep the shadow table in an
  SRAM bank no other master streams through;
- data-port reads come from an already-filled FIFO with no lookup.

Correction: ATA allows a longer register strobe (290 ns in mode 0, against
165 ns for data). But KOS names 0xA05F7490/94 as the PIO *read* and *write*
access waits, so Holly most likely uses one read strobe for both. Register
lookups must then meet the data-read deadline. See
[`g1-bridge-controller-ram-bios-2026-10-09.md`](g1-bridge-controller-ram-bios-2026-10-09.md).

**M1: final DMA word (my error).** My enable term "DMACK- asserted and our
DMARQ active" would cut the last word, because the device negates DMARQ
during that word's strobe. Use a latched DMA-owner state instead:
- set when the bridge raises DMARQ;
- held through the final strobe and its hold time;
- cleared within the release limit after DMACK- negates.

That is sequential logic, which favours the GreenPAK/CPLD option over
discrete gates. Any between-strobe predrive (my H3 note) must sit inside that
window.

**IORDY.** Agreed: passive capture first, and no push-pull GPIO on a shared
line. Note that ATA defines IORDY as negated low or released by the selected
device against a host pull-up. Once the idle level and ownership are known,
the controlled test is an open-drain buffer (74LVC1G07-class) with a series
resistor, not a clamp.

**Power (B1/H4).** Agreed. Unpowered FT pads accepting 3.3 V narrows B1 to
bus control, brownout/OE bias and backfeed (including the C5's SPI, IRQ and
READY lines). The G1 bus voltage itself is still unmeasured. Add it to the
step 1 measurements.

**H2 activation contract.** Agreed that it is custom and must be specified.
Proposed terms:
- unlock is a vendor command sequence written with DEV=1, one a stock ATA
  device would abort;
- the bridge relocks on RESET-, on SRST (Device Control bit 2) and on loss
  of power-good;
- the bridge may drive INTRQ only when firmware activation is set and nIEN=0.

K-UI's IDE probe (`src/core/ata.c`) issues IDENTIFY directly today. It would
gain the unlock step on the software side when the bridge exists.

**Durability (M6) and L1–L4.** Agreed as written.

## Where I differ

### B4 can be answered now, with no hardware

With the storage transport on AUTO (the default, `src/dreamcast/main.c`),
`kui_storage_discover` tries SCIF, then SCI, then IDE. The IDE attempt
selects device 1 (0xF0) and sends IDENTIFY DEVICE with the GD-ROM in place
(`src/core/ata.c`). Boot K-UI with the SCI card removed and nothing on the
rear port. The shell log then shows `IDE/CF initialization failed: ATA=N`:

| N | Meaning | Reading for B4 |
|---|---|---|
| 3 (ABSENT) | Status read 00h or FFh with DEV=1 | Nobody claims device 1. Favourable, but 00h is also what ATA's device-0-answers-for-absent-device-1 rule returns |
| 4 (TIMEOUT) | Status stayed BSY or DRQ, or never reached DRQ after IDENTIFY | Ambiguous. A floating bus with a DD7 pull-down reads 7Fh, and capacitive hold can return the last byte written (F0h or ECh). Either one times out. So can a GD-ROM busy before selection, so retry with the drive idle |
| 5 (DEVICE_ERROR) | A clean status, then IDENTIFY aborted with ERR | Something accepted a command while DEV=1, most likely the drive ignoring DEV. B4 becomes a real blocker |
| 8 (PROTECTION) | G1 unlock failed | No conclusion |

So the existing log is decisive only for 5, and somewhat favourable for 3.

Every such boot has already exercised DEV=1 against the drive with no
reported harm. That is weak evidence that selection itself is safe.

To remove these ambiguities, a small K-UI probe could record raw Status and
AltStatus with DEV=1, plus a Count/LBA write and readback using two
different patterns, with no command issued:
- an echo of both patterns means something is latching device-1 registers;
- with status 00h, that is the drive emulating device-0 rules;
- if each readback just repeats the last byte written, the bus is floating
  under capacitive hold. Reading one register after writing another
  separates these cases.

It touches only K-UI, never the design branch.

### B3 needs an instrument, but no fixture

Step 3's passive capture still needs something that captures. Holly's strobes
for the drive's own register reads follow 0xA05F7490/94. So a capture of
DIOR-, DIOW-, CS0-/CS1-, DA0 and DD0 at CN503 (A row or RA pads) maps code to
nanoseconds, while K-UI sweeps the codes using **AltStatus reads only**
(no side effects) and then restores 0x222. That is six wires and no slave
device.

Suitable instruments:
- an RP2350B board behind a 74LVC244-class input buffer, running a PIO
  sampler (about 100–150 MS/s bursts). The same board later becomes the
  bench responder;
- or a USB analyzer of at least 100 MS/s.

Avoid 24 MS/s clones: about 42 ns resolution cannot resolve a 50 ns setup.

### Order

Move the log read, the raw probe and the passive timing capture ahead of
disposition step 2. Their answers can change step 2's architecture. If the
drive answers for device 1, a bridge that shares the bus as device 1 needs
either a K-UI-controlled bus switch that isolates the drive, or a design that
replaces the drive rather than sitting beside it. Shield clearance stays
step 1.
