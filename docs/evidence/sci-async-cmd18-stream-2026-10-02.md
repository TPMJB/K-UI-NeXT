# SCI async: CMD18 streaming reader

Recorded 2026-10-02. Follows the `97b590137b9b` console results
([record](sci-async-speed-cmd18-2026-10-02.md)): under continuous clocking
the card leaves exactly one 0xff byte between blocks, and stopping the SCI
after a 514-byte DMA loses two bytes (RDR fills, the next byte overruns),
which there were the gap byte and the next data token. Native game reads and
the CE gate are unchanged.

## The 513-byte block

The DMA now takes 513 bytes per streamed block: the payload and the first CRC
byte. Reception continues for two more bytes after the count. The first, the
second CRC byte, waits in RDR; the second, the gap byte, overruns and stops
the receiver. The reader keeps the RDR byte, so every block is still checked
against both CRC bytes, and the next data token is the first byte clocked
after reselection. A card with no gap byte would lose its token instead; the
reader handles that as below.

## Changes

**Streaming reader (`kui_sci_async_begin_stream`).** A CMD18 of up to any
number of blocks, read through the existing `poll`/`finish` calls one block at
a time:

- Each block: a 513-byte receive-only DMA with the completion interrupt. The
  interrupt handler lets the receiver take the second CRC byte and stop on the
  overrun before it clears SCR, then keeps the RDR byte.
- `finish` checks guards and CRC16 (the RDR byte completes the CRC), then
  deselects the card, resets the SCI module and re-initializes it, exactly as
  after a single-block read. For every block but the last it then starts the
  next block: reselect and a polled search for the data token, then the next
  DMA. The last block is published only after CMD12 has stopped the card.
- A data token that is not found after reselection (another byte, or a
  timeout) and a proven-idle mid-block overrun both stop the card with CMD12
  and re-issue CMD18 at the same block, at most three times per block.
- A block whose stop left nothing in RDR fails with a receive error.
- Cancellation and failures send CMD12 whenever the ordinary bus is usable
  (during framing or after a reset); otherwise the caller's normal recovery
  reinitializes the card, as the speed test already does.
- Counters go to a separate `streaming` stage, with `stream_restarts` and
  `missing_tail_bytes`.

**Per-block resume measurement.** Now uses the same 513-byte cycle, so it
checks the RDR byte on 64 blocks independently of the reader.

**Speed test (R).** After the ordinary pass, the async CMD17 pass and the
two CMD18 measurements, R reads the same 1 MiB again through the streaming
reader in runs of 128 blocks (the ordinary reader's run length), CRC32
compared with the ordinary pass. The result screen:

| Line | Shows |
| --- | --- |
| 2 | Async KiB/s: CMD17 per block, and CMD18 stream |
| 3 | Stream per block: receive, finish, next-block framing in us; restarts |
| 4 | CMD17 per block: setup, receive, finish, card wait in us |
| 5 | CMD18 gap and resume blocks completed |
| 7 | Data match: CMD17 / stream / capture / resume |

The JSON adds `stream_blocks`, `stream_us`, `stream_kib_s`, `stream_crc32` and
`stream_match` to `speed`, a `streaming` stage and `missing_tail_bytes`.
`command rejected` replaces the status name `CMD17 rejected`, since CMD18 and
CMD12 can be rejected too.

## Validation

Host tests (ASan/UBSan). The card model now keeps the first byte after a
stopped DMA in RDR and loses the next. Block contents and gaps depend on the
LBA, so restarted streams are checked against the right blocks. Covered:

- 10 streamed blocks with 1-byte gaps and a longer gap every fourth block
  (one CMD18, one CMD12, ten SCI resets).
- A card with no gap byte: every later block is re-fetched with CMD12 and
  CMD18, and all blocks still verify.
- A mid-block overrun retried with CMD12 and CMD18 at the same block.
- Cancellation between blocks and during a DMA (CMD12 sent in both).
- A block with nothing in RDR (fails before publishing).
- Arguments, a stream ending at the card's last block, and a CMD17 read
  after each stream.

The resume measurement passes with one gap byte and reports a token error
with none. The runtime wrapper test covers the streaming pass and a streamed
block that differs from the ordinary pass.

## What to look for

- **Resume 64/64** on line 5, with data matching: the RDR byte and the token
  after reselection work on the console.
- **CMD18 stream KiB/s** on line 2 against the ordinary reader's 1,095. Each
  block costs about 330 us of wire time plus finish and next-block framing
  (line 3). The CPU is free during the 330 us.
- **Restarts** on line 3 should be 0 with the paused screen.

## Delivery

Source `3aa4554c44d9ab34b2d2ee9dc4a253e800f61f69` passed
[Diagnostic run 205](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37044237806):
host tests and the Dreamcast build. The update is the run's
`kui-1.5.1-dainsleif-sd-update` artifact; only `KUI/runtime.kui` and
`KUI/apps/games/retail-boot.kui` need replacing. Run R and return the JSON.

## Console results (3aa4554c44d9)

The owner ran R; it passed with all four passes returning the same data and
normal recovery verified ([report](sci-async-console-3aa4554c44d9-speed.json)).
1 MiB of `/KUI/runtime.kui` from LBA 88,453,632.

| Pass | KiB/s | us per block |
| --- | ---: | ---: |
| Ordinary reader (CMD18 runs) | 1,095 | 456.2 |
| Async CMD17 per block | 656 | 762.2 |
| Async CMD18 stream (128-block runs) | 1,059 | 472.1 |

**The 513-byte block works.** The resume measurement read 64 of 64 blocks with
every token found at the first byte after reselection (0 bytes waited) and no
missing RDR byte; the capture again showed a 1-byte gap after every one of 31
blocks. The streaming pass read all 2,048 blocks with no restart, no overrun
and no missing RDR byte.

**Where the stream's time goes**, per block: receive 343.0 us (513 bytes on the
wire is 328.3 us), finish 68.9 us (CRC check, copy, deselect, SCI reset and
re-initialization), next-block framing 61.6 us (from finish to the next DMA
start: the test loop's own CRC32 of the block, about 30 us, reselection, the
token search, 7.8 us on average, and the DMA start). The CPU is free for the
343 us of each block; the rest is serial.

**Next.** Check each block during the next block's DMA rather than before it:
the receive area alternates, the next DMA starts right after the SCI reset,
and the check and copy follow. That leaves receive, reset and next-block
framing on the serial path, about 370 us per block.

**Open question.** `max_irq_masked_us` was 2,504 us (53 us in the previous run).
It matches one streamed block whose receive took 2,845 us instead of about
343 us (2,845 - 341 = 2,504). No masked section in the reader is that long by
design; the next build records which section set the maximum.

## Overlapped checks (next build)

`kui_sci_async_begin_stream(reader, lba, count, dst)` now reads a whole run
into `dst`, and `poll` drives it; `finish` is no longer used for streams.
Blocks alternate between two receive areas. When a block's DMA completes,
the same poll resets the SCI, reselects the card, finds the next token and
starts the next DMA, and only then checks the completed block (CRC16 with its
RDR byte) and copies it into `dst`. The check now runs while the next block
is on the wire. For streams, `framing_us` is the serial gap between blocks
(reset, reselection, token search, DMA start) and `finish_us` the overlapped
check.

A block that fails its check is read again: if the next block is already in
flight, that block is dropped when it arrives, then CMD12 and CMD18 at the
failed block; after the final stop, CMD18 alone. Restarts (lost token,
overrun, failed check) are limited to three until another block has been
checked, so a block that keeps failing ends the run instead of looping.

The R stream pass now reads each 128-block run into one buffer and CRC32s it
afterwards, like the ordinary pass, so the comparison is like for like. The
JSON adds `max_irq_masked_site` (1 lease, 2 release, 3 module reset, 4 DMA
start, 5 DMA poll, 6 open) and `max_irq_masked_stage` (0 slow, 1 fast,
2 cmd18, 3 streaming, 4 none) to locate the 2.5 ms masked window. Screen line
3 reads: receive, gap (serial) and check (overlapped) per block, restarts.

Host tests add: the previous block is in `dst` after each poll that starts a
DMA; a bad CRC in the middle of a run (the in-flight block dropped, 8 DMAs for
6 blocks) and on the last block (CMD18 again without CMD12); cancellation
while a token search spans polls and while a DMA runs; the run-wide restart
limit with a card that leaves nothing in RDR.

Source `bac1b152b4ac7ce5814160e53a51b167e322c74c` passed
[Diagnostic run 206](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37048656141):
host tests and the Dreamcast build. Install `KUI/runtime.kui` and
`KUI/apps/games/retail-boot.kui` from its `kui-1.5.1-dainsleif-sd-update`
artifact, run R and return the JSON.

## Console results (bac1b152b4ac)

The owner ran R; it passed with all four passes returning the same data and
normal recovery verified ([report](sci-async-console-bac1b152b4ac-speed.json)).
1 MiB of `/KUI/runtime.kui` from LBA 83,702,784.

| Pass | KiB/s | us per block |
| --- | ---: | ---: |
| Ordinary reader (CMD18 runs) | 1,095 | 456.5 |
| Async CMD17 per block | 654 | 763.6 |
| Async CMD18 stream, overlapped checks | 1,202 | 415.9 |

Both CMD18 passes include the test's own CRC32 of each 128-block run, about
38 us per block (16 runs of 64 KiB in 77.8 ms). Without it the stream runs at
377.9 us per block (1,323 KiB/s) and the ordinary reader at about 418.5 us
(about 1,195 KiB/s).

Streaming per block: receive 344.6 us (513 bytes on the wire is 328.3 us),
serial gap 30.4 us (SCI reset, reselection, token search of 0.6 bytes on
average, DMA start) and the overlapped check 54.5 us. No restart, overrun or
missing RDR byte in 2,048 blocks; the resume measurement passed 64/64 again.
The stream now reaches 88% of the wire limit (516 bytes per block, 330 us).

**The 2.5 ms masked window.** `max_irq_masked_us` 2,508 us at site 3 (module
reset) in stage 1 (the CMD17 pass): one SCI module reset, with interrupts
masked, took about 2.5 ms instead of a few microseconds. The streaming stage
again shows one block whose receive took 2,843 us (2,845 us in the previous
run). These are rare (about one per R run) but not explained by the code's
own loops; the next build looks for their source
([pause hunt](sci-async-pause-hunt-2026-10-02.md)).
