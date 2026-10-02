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

Pending CI.
