# K-UI-NeXT disc reader: handoff (2026-09-20)

This is the one document to read before touching GD-ROM reading. It says what the reader does,
how fast, what is proven, what is still open, and where the evidence is. Every number here is
measured on real hardware; the raw console logs and the figures derived from them are in
`docs/evidence/`, and `docs/experiment-plan.md` has the full trip-by-trip record.

## Where it stands

A whole GD-ROM rips in **about 20 minutes** and is **byte-exact**:

| Disc | Tracks | Time | Average | Checked against |
|---|---|---|---|---|
| Sword of the Berserk | 3 (data) | 19.6 min | 985.9 KiB/s | TOSEC on the console; reference file on a PC |
| MDK2 | 31 (27 audio) | 19.6 min | 1011.7 KiB/s | TOSEC on the console; TOSEC on a PC (31/31) |

Sword of the Berserk has now been ripped three ways (SHA-256 with a full read-back, fast PIO, and
fast DMA) and all three are identical by SHA-256. MDK2 has also been interrupted twice mid-disc and
resumed to a verified finish.

For scale: the original settings projected to about 104 minutes for the same Sword disc, so the
work took it from roughly an hour and three quarters to twenty minutes.

## What made it fast (in order of effect)

1. **End read-back off** (`end_readback=off`): re-reading every saved byte doubled the total time.
   The TOSEC check and the PC check now do that job.
2. **CRC32 only** (`capture_hash=crc32`): SHA-256 cost about 28% of capture speed.
3. **GD-ROM DMA, overlapped** (`capture_read=dma`): the drive fills the next chunk while the CPU
   writes the current one to the card. Disc time went from 27% of a capture to 0.5-1.3%.
   +36% on a whole disc. Proven on data and audio tracks.
4. **UI capped at 2 redraws a second** (`ui_hz=2`): the unthrottled UI took about a third of the CPU.

## Defaults today, and one decision left open

| Setting | Default | The fast, verified rips used |
|---|---|---|
| `ui_hz` | 2 | 2 |
| `capture_read` | **dma** (since 2026-09-20) | dma |
| `capture_hash` | both (SHA-256 + CRC32) | crc32 |
| `end_readback` | on | off |

**Open decision:** make `capture_hash=crc32` and `end_readback=off` the defaults. Every verified fast
rip used them. With the old values a rip is correct but roughly twice as slow. Until then, put this
in `/KUI/bench.cfg` (plain text, UTF-8 or ANSI):

```
ui_hz=2
capture_hash=crc32
end_readback=off
capture_read=dma
```

## Where the time goes now

In Trip 12 (MDK2): SD write 88.9%, CRC32 7.9%, disc 1.3%, EDC 1.1%.

**The SD card is the whole bottleneck.** It is driven by bit-banging SPI over the rear serial port
(SCIF): about 1,130 KiB/s writing and 677 KiB/s reading, with the CPU busy for every bit. KOS's
loop is already at the hardware limit, and a faster card path (SCI with DMA) needs a motherboard
mod: it was tried, and the rear port only supports SCIF.

## Verifying a dump

On the console, a capture ends with `Reference check (TOSEC): FULL TRACK MATCH` when the catalogue
files are on the card (`data/known-dumps/*.db` copied to `KUI/`). That check uses the CRC32
computed **while reading**, so it proves the disc read. To prove the card write too, check on a PC:

```
python3 tools/verify_dump.py /path/to/job-folder --reference docs/evidence/sword-of-the-berserk-reference.json
```

Run it from the repository root (a relative `--reference` path is taken from the current
directory). Without `--reference` it still proves the saved files match what the console recorded.
Keep dump folders **outside** the repository.

For projected whole-disc times from any saved report: `python3 tools/rip_time.py REPORT`.

## Still open, most useful first

1. **The default decision above.** One line in `src/core/options.c`.
2. **Faster CRC16 in the SD driver** (link-time `--wrap net_crc16ccitt`, no KOS patch): measured at
   22.7 -> 8.8 CPU cycles a byte, worth about **7% on a capture** and 5% on SD reads.
   `src/core/crc16.c` already has the verified implementation (`kui_crc16_slice2`).
3. **Faster CRC32**: a 256-entry table in place of the nibble table, about 3%.
4. **Failure paths never exercised on hardware.** Every run has had zero read retries. The retry
   path (a scratched disc), a full card, and the lid opened mid-capture are covered by host tests
   only.
5. **A game launcher is a different problem.** SD reads over SCIF top out around 730 KiB/s even
   with the CRC16 work, against 1,250-2,000 KiB/s for the drive, so loading from SD is 2-3x slower
   than disc. Block-compressed images (LZ4, as CSO/ZSO do) might recover much of that; the ratio
   and the SH4 decompression cost are both unmeasured. The real fixes are hardware: the SCI-SPI
   mod, or GDEMU/IDE replacing the drive path.

## Dead ends (recorded so they are not retried)

- **SCI SD transport:** fails to initialise on a stock console; needs the SCI-SPI solder mod.
- **"Audio tracks are drive-bound":** wrong. Audio captures within a few percent of data.
- **KOS's own `arch_dcache_purge_range`** inside the DMA code: the CI's GCC 15.2 cannot register-
  allocate its eight-operand asm. The DMA code uses its own one-register `ocbp`/`ocbi` loops.
- **`#ifdef __SH4__`:** KOS builds with `-m4-single`, which does not define it. Use `KUI_ON_CONSOLE`
  from `src/dreamcast/platform.h`; `tests/test_asm_audit.py` enforces this.

## Builds and branches

- **Normal build** (any branch): everything above, including the DMA capture.
- **Experimental build**: push a branch whose name ends in `-experimental`. It adds only research
  tools: the blocking DMA probe (`sweep_mode=dma`, a 300 KB buffer) and a competing-thread
  benchmark. Artifacts are named `*-experimental`. Nothing a capture needs lives there any more.
- `main` carries no workflow file, so GitHub shows no "Run workflow" button; the branch name is
  the way to request the experimental build.
- CI prints a digest headed `BUILD FAILED: THE ERRORS, IN ONE PLACE` at the end of a failed build.

## Traps that each cost a run

- `/KUI/bench.cfg` is limited to 8 KB; a UTF-8 byte-order mark is skipped and UTF-16 is refused.
  Check the first screen: `Loaded /KUI/bench.cfg` and the `OPTIONS` lines you expect.
- A capture bench with the wrong disc in the drive now ends `BENCH complete but NOTHING was
  measured`, and a `capture_fad` outside a track of the configured type prints `BENCH WARNING`.
- Press **A** for a new dump. **X** resumes the latest matching job with the settings it started with.
- Never `git add -A` with a dump folder or a `.patch` file inside the repository. Capture file
  names are git-ignored, but `manifest.json` is not.

## Where the code is

| Area | File |
|---|---|
| Capture engine, including the overlapped read | `src/core/capture.c` |
| GD-ROM access, PIO and DMA | `src/dreamcast/disc.c` |
| Command state machine (`begin` / `ready` / `end`) | `src/core/command.c` |
| Options and `bench.cfg` parsing | `src/core/options.c`, `src/core/options_file.c` |
| Benchmarks | `src/core/bench.c`, `docs/bench-cfgs/`, `docs/benchmarks.md` |
| PC tools | `tools/verify_dump.py`, `tools/rip_time.py` |
| Tests | `make test` (host), `make test-images` (FAT32 and exFAT images) |
