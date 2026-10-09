# Faster SCI loading: lessons from PlayStation, Saturn and drive emulators

Status: theory and proposals, 2026-10-09. Nothing here is implemented or
console-tested. Measured figures come from K-UI's own records (cited); the
PlayStation/Saturn/emulator descriptions are architecture-level summaries;
items marked **unverified** need a console or bench check first.

## Where SCI loading stands

| Measurement | Value | Source |
|---|---:|---|
| SCI clocked-synchronous ceiling, BRR=0 at a 50 MHz peripheral clock | 12.5 Mbit/s, about 1,526 KiB/s | SH7750 rate formula |
| Best sequential CMD18 stream, DMA | 1,191 KiB/s | `HANDOFF.md` (stream test) |
| GD-ROM drive alone, K-UI ripper | about 1,450 KiB/s | `experiment-plan.md` |
| Standard reader inside games | about 1 ms of masked CPU per KiB | CE placement test |
| Native background reader, per 512-byte block | receive 343 µs (CPU free), check 69 µs, framing 62 µs | CE placement test |
| Windows CE background reader, ARMADA | 420–470 KiB/s; 22% of blocks overrun and are re-read by polling; stream starts ≈ overruns | Astra, `HANDOFF.md` 2026-10-04 |
| Synchronous paced CDDA callback, Toy Commander | 4.44 ms per 2,352-byte sector (about 517 KiB/s) | `cdda-eight-block-console-clean-2026-10-08.md` |
| Two-sector GD step cap | at most 240 KiB/s at one EXEC per 60 Hz frame | same |
| Asynchronous CDDA, host model | 60 of the 75 sectors/s needed | `toy-async-cdda-scheduling-2026-10-09.md` |

Toy Commander's intro now plays continuous CDDA but the video stalls; ARMADA
and Worms FMVs are only marginally better with the CE background reader.
Raw CDDA alone needs 75 sectors/s, 172.3 KiB/s.

Five limits explain most of this:

1. **CPU work per byte.** Every word is bit-reversed (SCI receives LSB first,
   SD sends MSB first), CRC16-checked and copied to the game's buffer. The
   standard reader also clocks the card with interrupts masked.
2. **A one-byte receive register.** The SCI has no FIFO: channel-1 DMA must
   take each byte within one byte time (640 ns at 12.5 Mbit/s). Any longer bus
   hold overruns it. Each overrun currently costs a stream restart (CMD12,
   CMD18, token search) plus a polled re-read, roughly 0.7 ms masked.
   **Hypothesis to check:** overruns cluster during FMV, when the game moves
   large texture and display-list DMA. A lower bit rate cannot survive a bus
   hold of tens or hundreds of microseconds.
3. **No read-ahead buffer.** The game owns all RAM. The readers keep two
   544-byte receive areas (CE: a 2 KiB queue), so the card idles whenever the
   game is not asking.
4. **Few service opportunities.** The reader runs only inside GD calls, its
   SCI interrupt or a hook. Astra's asynchronous CDDA fell short (60 of 75
   sectors/s) because the sound worker gets two clustered visits per frame.
5. **Audio shares the link at full PCM rate.** CDDA takes 172 KiB/s of a link
   that delivers 500–1,100 KiB/s in practice, and its refill deadlines then
   compete with the game's reads.

## How the other consoles handled slow optical media

**PlayStation (2x drive, about 300 KB/s).** FMV with audio worked because:
- **Compressed audio, decoded in hardware.** XA-ADPCM audio sectors are
  decoded by the CD subsystem and routed to the SPU without the CPU. 4-bit
  stereo at 37.8 kHz carries 2,016 frames per sector, so it needs 18.75
  sectors/s against CD-DA's 75: a quarter of the bandwidth. At double speed
  that is one sector in eight.
- **Interleaving.** STR movies put video (Mode 2 Form 1) and XA audio
  (Form 2) sectors in one sequential stream. The drive never seeks between
  them; hardware separates them.
- **A controller buffer.** The CD controller buffers sectors; the CPU only
  moves sectors that are ready.

**Saturn (2x drive).** A separate **CD Block** (an SH-1 with the YGR019
controller) has a **200-sector buffer (about 460 KB)** divided into 24
partitions. Selectors and filters sort incoming sectors (data, audio
channels, file ranges) into partitions while the drive reads ahead. The main
SH-2s pull ready sectors by DMA. CD-DA goes from the CD Block straight to the
sound chip, and the main CPUs never service the drive.

**Optical drive emulators** (xStation, PSIO; Rhea/Phoebe, Satiator, Fenrir;
GDEMU, MODE, USB-GDROM). These replace the drive *below* the console's own
controller. A dedicated MCU/FPGA with its own memory reads SD, while the
console's CD controller or CD Block still buffers, routes audio and uses DMA.
CD audio "just works" because it stays a drive-side stream. The game's CPU
never touches the SD card.

**PlayStation 2's OPL** is the closest software analogue to K-UI. It
replaces the disc driver on the IOP, a separate I/O processor, so the game's
CPU is not interrupted. It requires defragmented (contiguous) images and
supports compressed ZSO/CSO. FMV stutter on slow USB 1.1 was fixed by faster
transports (HDD, SMB, faster USB stacks), not by software tricks.

**The common pattern:**
- Storage work runs off the game's CPU.
- Read-ahead fills memory the game does not own.
- Audio is compressed, and the sound hardware decodes it.
- Media is laid out for sequential streaming.

K-UI does none of the first three yet, because the Dreamcast has no free
I/O processor and the game owns all memory. The AICA's ARM7 cannot reach the
SH-4's SCI.

## What this suggests for K-UI

### 1. Make the SD bytes arrive ready to use (software, low risk)

- **First, a one-register test (unverified).** The SCI's smart card mode
  register at `0xffe00018` (K-UI writes 0) has an SDIR bit (bit 3) for
  MSB-first transfer. If the SH7091 honours it in clocked synchronous mode,
  received bytes arrive in SD order and all software bit reversal disappears.
  Test: send a CMD0/CMD8 and compare responses with SDIR on and off.
- **Otherwise, an "SCI-ready" image variant.** Extend Astra's 2048 converter
  (`tools/gdi_optimize.py`) to write data tracks that are:
  - cooked to 2,048 bytes, which is already supported and 12.9% smaller;
  - contiguous on the card;
  - stored with every byte bit-reversed.

  The DMA then lands the game's final bytes. The SD CRC can still be checked
  on the landed bytes with an LSB-first (reflected, polynomial 0x8408)
  table, so no data is transformed.
- **Zero-copy DMA.** A 2,048-byte sector is exactly four card blocks. Blocks
  of P1/P2 or physical destinations (DMAREAD, CE's DMA streams) can DMA
  straight into the game's buffer, with no receive-area copy. The exception
  is a request's last block, whose 513th byte (the first CRC byte) would land
  past the request end, so that block goes through the receive area. This
  removes the reversal and copy parts of the 131 µs per-block CPU cost;
  only the CRC read remains.

### 2. CDDA as AICA ADPCM (the PlayStation XA lesson)

Convert audio tracks on the PC to the AICA's native 4-bit Yamaha ADPCM,
keeping the raw tracks. At 44.1 kHz stereo that is 44,100 bytes/s, the
same 18.75 sectors/s PlayStation XA used, and the AICA decodes it with no
SH-4 work.
- Astra's 64 KiB ring then holds about 1.5 s of audio instead of 371 ms.
- The 60-of-75 deficit becomes a 3x surplus (60 against 18.75).
- Refills could happen a few times a second instead of every frame.

This is lossy, so keep an exact-CDDA option for purists. **Unverified:** the
AICA's ADPCM long-stream format (sample format 3) is meant for looping
streams without resetting the decoder; test it in Astra's harness with
synthetic audio before any game.

### 3. Cheaper overruns and fewer per-block interrupts (reader work)

- **Measure first.** Count overruns per video frame during FMV against menus
  (hypothesis 2 above) before tuning anything.
- **Contain the cost.** The card simply waits when the clock stops. Astra's
  receive pacing (one byte in flight, CPU-fed) already guarantees no overrun.
  Use it only for the remainder of a block that overran, not for whole
  streams. Even better, resume in place without a CMD12/CMD18 restart, where
  the existing repair evidence allows.
- **Multi-block DMA.** One DMA can cover several blocks including their CRC,
  gap and token bytes (the measured inter-block gap is one byte). Tokens are
  then found in the buffer, and the existing CRC repair handles a lost byte.
  This cuts ERI interrupts, module resets and framing work per block, at the
  cost of larger receive areas.
- **Background reader as the native default** once stable. The synchronous
  reader's throughput is tied to how often the game calls EXEC (240 KiB/s at
  60 Hz with two sectors), whereas the background reader's is not.

### 4. A storage coprocessor on the serial port (hardware, the Saturn answer)

Put a small MCU between the SCI and the microSD card. Candidates are an
RP2040/RP2350, whose PIO suits a fast clocked serial slave, or the ESP32-C5
already supported by the SPI link firmware. It becomes K-UI's CD Block:
- It reads SD in 4-bit mode, far faster than the SCI link.
- It holds the launch map and reads ahead into its own RAM. The RP2350's
  520 KB is about the Saturn CD Block's buffer; PSRAM would give megabytes.
- It serves cooked, pre-reversed sectors in large frames with a short
  header. That removes SD tokens, CRC16 timing and card latency from the
  Dreamcast side.
- An overrun becomes a cheap "resend from offset N" instead of a card
  restart.
- It could store ADPCM audio and replay recorded per-game request traces as
  prefetch hints. PlayStation and Saturn developers achieved the same thing
  through disc mastering.

The link ceiling stays about 1.5 MB/s, in the range of the real drive's
1,450 KiB/s, with the Dreamcast doing only DMA and a header check.

### 5. Hardware CDDA (the drive-emulator lesson)

Drive emulators keep CD audio a drive-side stream. **Unverified:** on the
Dreamcast the GD-ROM drive's audio reaches the AICA's external inputs
(EXTS), whose volume games already set. If the drive's audio lines and
format can be confirmed, the coprocessor could feed PCM there while the
drive is disconnected. That would remove:
- sound-RAM and channel ownership, which avoids Toy Commander's reported
  channel conflict;
- refill deadlines;
- SH-4 work.

It needs a soldered link to the drive connector and a bench check of the
signals first.

## What not to expect

- A larger audio ring cannot fix a sustained delivery deficit, only delay
  it (Astra's 60-of-75 result).
- A lower SCI clock does not survive long DMA bus holds; pacing by clock
  gating does.
- Compression (ZSO/LZ4) helps data that compresses. Sofdec video and ADX
  audio, the FMV payloads, mostly do not.

## Suggested order

1. Instrument overrun timing during FMV and the per-block CPU split
   (reversal, CRC, copy). This needs no behavior change.
2. Run the SDIR register test. If it works, remove reversal everywhere.
3. Run the ADPCM CDDA experiment in Astra's harness, synthetic audio first.
4. Build the SCI-ready image variant with zero-copy DMA, pre-reversing only
   if SDIR fails.
5. In parallel, as hardware: prototype the coprocessor on a bench board, then
   the hardware CDDA bench check.

## References

- K-UI: `docs/cdda-reader-design.md`, `docs/cdda-roadmap.md` and the CDDA
  evidence on `checkpoint/cdda-night-2026-10-08`;
  `docs/windows-ce-placement-test.md`; `docs/gdi-2048-test.md`;
  `src/loader/sci_stream.c` (reversal and CRC per block).
- Saturn CD Block buffer, partitions and selectors: Sega's CD communication
  interface guide ([buffer](https://www.infochunk.com/saturn/segahtml_en/prgg/sysg/cdif/hon/p08_16.htm),
  [overview](https://www.infochunk.com/saturn/segahtml_en/prgg/sysg/cdif/hon/p01_10.htm));
  MAME [`saturn_cdb.cpp`](https://git.redump.net/hbmame/tree/src/mame/machine/saturn_cdb.cpp).
- PlayStation STR/XA interleaving: mkpsxiso guides for
  [STR video](https://www.mintlify.com/lameguy64/mkpsxiso/guides/str-video) and
  [XA audio](https://www.mintlify.com/lameguy64/mkpsxiso/guides/xa-audio).
- Drive emulators: [xStation](https://dragonbox.de/en-ca/everdrives-flashcarts/xstation-optical-discdrive-emulator-ode-mod-kit-psx-pu-8),
  [GDEMU](https://consolemods.org/wiki/Dreamcast:GDemu).
- DreamShell CDDA from files (research only; no code reuse, per
  `cdda-reader-design.md`): [audio system overview](https://deepwiki.com/DC-SWAT/DreamShell/5.3-audio-system).
