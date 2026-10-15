# SCI async: leaner single-block reader and CMD18 measurements

Recorded 2026-10-02. Follows the `8daab44490f5` console results: the screen-on
stress passed (51 overruns, all retried) and the speed test read the same data
at 1,089 KiB/s with the ordinary reader and 587 KiB/s with the async reader
(one CMD17 per block). Native game reads and the CE gate are unchanged.

## Correction to the 8daab44 reading

The speed report's card wait (58.3 bytes between R1 and the data token) was
converted at the wire rate, 0.64 us per byte, to "about 37 us". That assumed
back-to-back bytes. The framing loop clocked each byte on its own and read
the wall clock after every byte, so each byte took several microseconds and
the wait in time is unknown; it may be most of the 312 us setup. This build
measures it.

## Changes

**Reader (`src/dreamcast/sci_async_probe.c`).**

- The ready, token and token-end waits check their 100 ms limit every 32
  bytes instead of reading the wall clock on every byte. Byte limits are
  unchanged.
- The finish step reverses bit order and checks CRC16 by table lookup. The
  tables are built at open from the existing bit-by-bit routines.
- The re-initialization after each block waits one bit time at the new rate:
  64 loop iterations at 12.5 MHz instead of 1,024 (slow speed unchanged).
- Interrupt-masked windows are timed from after the mask (lease, release,
  module reset, DMA start, open and the DMA poll). Taking the timestamp
  first counted any preemption between the timestamp and the mask, the likely
  source of the 10 ms `max_irq_masked_us` in the screen-on stress.
- Each stage reports `token_us` and `max_token_us`: the wall-clock card wait
  between R1 and the data token.

**CMD18 measurements (R only).** Both run after the async pass, read only,
inside the paused-display window, and always stop the card with CMD12. Each
block is received by a receive-only DMA with interrupts masked and no
completion interrupt; the SCI is stopped only after the overrun that ends its
continuous clocking, exactly as after an interrupt-driven read.

- *Continuous capture.* CMD18 at the first block of the range, a polled wait
  for the first data token, then one 16 KiB DMA (about 10.5 ms with interrupts
  masked) so the card is clocked without a pause across about 30 block
  boundaries. The capture is parsed block by block: CRC16 of each block and
  the 0xff bytes before the next data token, the card's natural gap at
  12.5 MHz (0.64 us per byte). Reports minimum, average and maximum gap, the
  first 32 gaps in order, and CRC or token errors.
- *Per-block resume.* 64 blocks of one CMD18 (from block 64 of the range),
  each received by its own 514-byte DMA, then deselect, SCI module reset,
  reselect and a polled search for the next data token. This is the cycle a
  receive-only streaming reader would repeat. Reports blocks completed, the
  rate of that cycle in KiB/s, the token wait after each reselection (bytes and
  microseconds) and the time in each phase.

Every block that passes its CRC must equal the async pass's copy (CRC32 per
block). What the card sends (a CRC or token error, no next token, an early
stop) is reported as a finding and does not fail the run when CMD12 then
stops the card. A rejected CMD18, an unanswered CMD12, a bus fault or a DMA
fault fails the run and goes through the normal recovery and reinitialization.

**Report and screen.** The JSON adds `stream`, `resume` and a `cmd18` stage
(the measurements' own counters, kept apart from `fast`). R's result screen:

| Line | Shows |
| --- | --- |
| 2 | Async reader KiB/s and overruns |
| 3 | Per block: setup, receive, finish and card wait in us |
| 4 | CMD18 gap min/avg/max in bytes and blocks parsed |
| 5 | Per-block resume: blocks, KiB/s, average wait after reselection |
| 7 | Data match for the async pass, capture and resume |

## Validation

Host tests (ASan/UBSan) model a CMD18 card that sends a token wait, blocks,
fill gaps (with a longer gap every fourth block), accepts CMD12 mid-stream
with a stuff byte, R1 and busy, and loses two bytes after each DMA stops. They
cover: a 16 KiB capture (30 blocks, every gap and payload checked, CMD12 and
module reset counted), an overrun mid-capture (parsed up to the stop, reader
still usable), a 12-block resume, resume findings (two fill bytes are enough;
one lets the stop swallow the token; a card that abandons the read after
deselection), a rejected CMD18 and an unanswered CMD12 (both fail the reader),
argument and state checks, and a normal CMD17 read after each measurement.
The runtime wrapper test covers the R sequence, a captured block that differs
from the async copy and a failed measurement going through reinitialization.

## What the results decide

- The CMD17 card wait in microseconds says how much of single-block setup is
  card time, which no reader change can remove.
- If the natural gap is at least 3 bytes and the resume pass completes all 64
  blocks with matching data, receive-only streaming works on this card: the
  next step turns the resume cycle into an interrupt-driven streaming reader,
  with the CPU free during each 330 us block.
- If the stop swallows the next token, or the card abandons the read when
  deselected, streaming needs the transmit-fed design instead: a second DMA
  channel supplies exactly 514 clock bytes per block, so there is no trailing
  overrun, reset or deselection between blocks.

## Delivery

Source `97b590137b9bb67828f4bdea6d0ce2198770b5d9` passed
[Diagnostic run 204](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37025928884):
host tests and the Dreamcast build. The update is the run's
`kui-1.5.1-dainsleif-sd-update` artifact; only `KUI/runtime.kui` and
`KUI/apps/games/retail-boot.kui` need replacing. Run R (and optionally Y, to
confirm the masked-time reading) and return the JSON reports.

## Console results (97b590137b9b)

The owner ran R; it passed with matching data in all three passes and normal
recovery verified ([report](sci-async-console-97b590137b9b-speed.json)).
1 MiB of `/KUI/runtime.kui` from LBA 83,701,248.

**Readers.** Ordinary reader 1,095 KiB/s. Async CMD17 reader 634 KiB/s (was
587), 787.8 us per block:

| Phase | us per block | 8daab44 |
| --- | ---: | ---: |
| Setup (command, card wait, DMA start) | 331.2 | 312.2 |
| of which card wait (R1 to data token) | 279.7 | not measured |
| Receive | 345.4 | 345.4 |
| Finish (checks, handoff, SCI reset) | 64.1 | 148.1 |

The card wait is time, not bytes: 127.5 polled bytes on average now (max 398,
longest 873 us) against 58.3 before, when each byte also paid a clock read.
About 280 us of card access per CMD17 caps single-block reads near
700 KiB/s whatever the reader does. Longest interrupt-masked window 53 us.

**Continuous capture.** CMD18's first data token came after 209 us. The
16 KiB DMA took 10,488 us (0.640 us per byte, 12.5 MHz) and held 31 complete
blocks, every CRC correct and every payload equal to the async pass's copy.
Every gap was exactly 1 byte: under continuous clocking the card sends block,
CRC, one 0xff, token, next block. Streaming is therefore wire-limited: 516
bytes, 330 us, per block, about 1,510 KiB/s. CMD12 then stopped the card
(R1 0, no busy) after the SCI reset.

**Per-block resume.** The first block verified (its first token after 216 us);
the cycle measured receive 331 us, deselect/reset/reselect 15 us and check
39 us. The search for the second block's token read 0x00 first: stopping the
SCI after the 514-byte DMA loses two bytes (RDR fills, the next byte
overruns), here the 1-byte gap and the data token. CMD12 stopped the card
and the reader stayed usable.

**Next.** Receive 513 bytes per block instead: the 514th (the second CRC
byte) is then the one held in RDR when the overrun stops reception, and only
the 1-byte gap is lost, so the next data token is the first byte after
reselection. The following build checks this with the resume measurement and
uses it for an interrupt-driven streaming reader
([change record](sci-async-cmd18-stream-2026-10-02.md)).
