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
| Sword of the Berserk, on FAT32 | 3 (data) | 20.1 min | 961.8 KiB/s | TOSEC on the console; reference CRC32s on a PC |

Sword of the Berserk has now been ripped three ways (SHA-256 with a full read-back, fast PIO, and
fast DMA) and all three are identical by SHA-256. MDK2 has also been interrupted twice mid-disc and
resumed to a verified finish.

For scale: the original settings projected to about 104 minutes for the same Sword disc, so the
work took it from roughly an hour and three quarters to twenty minutes.

## Latest app work — 2026-09-24

Runtime **cc2320bb6d3a** passed the three small Advanced CRC fixtures and
completed a saved-file scan of Armada with no bad/unsupported sectors. All five
reported CRCs and the aggregate data/audio lengths match the bundled TOSEC
Armada US entry. Music Clear cache freed all nine file allocations. The owner
confirmed a THPS2 save was absent after VMU deletion and usable after restore.
Pure audio-CD playback worked; mixed-CD filtering and track-change readiness
are app follow-ups. Network hardware testing is blocked by adapter availability,
and the owner has deferred salvage to a future version. See
[the current app evidence](evidence/m15-app-round-five-2026-09-24.json) and
[focused app guide](apps-round-five.md). No reader benchmark is reopened.

## Prior app work — 2026-09-23

The latest supplied app evidence is runtime **6f1be4cf53c3**, documented in
[hardware-evidence.md](hardware-evidence.md) and
[its sanitized record](evidence/m15-doa2-app-round-four-2026-09-23.json).
Dead or Alive 2 completed with TOSEC FULL TRACK MATCH after bounded retries and
Quick Resume; its final segment retained DMA. The damaged Advanced CRC fixture
correctly reported two suspect data sectors and three CRC mismatches. Music
cache counters stayed stable during these app/capture snapshots. Existing-dump
scan selection, custom-song cycling, title/progress display and VMU management
are the current app fixes; the [round-five guide](apps-round-five.md) tracks
implementation and hardware acceptance. The accepted disc reader remains frozen.

The app/failure sections below describe **earlier runtime snapshots** and their
then-current pending work; they are retained as history, not the latest status.

## Historical app work and failure report — 2026-09-23

The latest report is an **Omikron new capture on `0599097a8bfb`**, not a
saved-file verification attempt. Four tracks finished and the final track
stopped at **99.7855% committed**: a DMA timeout was followed by PIO fallback
abort-recovery failure and **RESET REQUIRED**. The partial checkpoint was kept;
no verification or reference result followed. Its committed-data average was
932.80 KiB/s including the terminal waits. This is not a completed dump or a
controlled performance comparison. [Sanitized evidence](evidence/m15-omikron-timeout-2026-09-23.json).
Reboot before resuming that job; the existing failed-recovery latch is retained.

The next M1.5 delivery extends the launcher with idle inserted-disc titles,
phase ETA and explicit capture/prefix/verification failure messages. Separate
system preferences cover video timing, memory display and optional menu music;
Memory Test, VMU listing/SD backup and network-adapter inspection are separate
app modules. Music plays a bounded RAM cache and pauses for foreground I/O.
Disc-title identification runs only on the single idle worker, with one attempt
per observed insertion; an unsuccessful attempt waits for a new insertion or
explicit foreground optical activity rather than continuously rereading.

**App and system-setting hardware acceptance is pending.** These modules are
still linked into the SD runtime, not independently loaded executables. The
raw/DMA reader, command recovery and core capture engine are unchanged in this
app round. Follow [apps-test.md](apps-test.md) using the existing boot disc;
ordinary recovery work can cover ETA without another benchmark or full rip.

## Current UI/destination work — 2026-09-23

The restored-menu runtime `8bae3efe7c2f` has now completed MDK2: **973.96 KiB/s**,
20 min 23.5 s of capture, all 31 tracks and TOSEC FULL TRACK MATCH. This is 3.73%
below the accepted Trip 12 capture rate. Its 8.3% UI scheduled share supports
remaining display overhead; saved-file/PC checking was not supplied for this job.
See [the measured evidence](evidence/m15-mdk2-2026-09-23.json). This result belongs
to the restored-menu runtime, before the new destination controls below.

New captures select a saved parent (default `/Games`) and use title-based output:
`/Games/MDK2/MDK2.gdi`, then `/Games/MDK2 (2)/MDK2.gdi`, without overwriting existing
files. Resume/Verify choose the greatest numbered folder with a valid checkpoint
matching the inserted disc's full identity, with legacy `/KUI/dumps` fallback.
Advanced contains Verify, Resume and Settings. Only a full independent catalogue
match gives the stream CRC badge a green result; saved-file readback is separate.
Controls and the pending console check are in [ripper-controls.md](ripper-controls.md).

The acquisition code remains frozen for this delivery. Both baseline test suites
passed on clean `54711b931554` before minimal `capture.c` changes for destination
selection, named metadata and result observation. The raw/DMA reader, command
state machine, acquisition loop, retry policy and checkpoint encoding are
unchanged. New destination/metadata tests pass on FAT32 and exFAT; **the named
capture, catalogue display and PC saved-file check still need hardware acceptance**.
A dedicated salvage workflow is planned separately, not included in Advanced yet.

## What made it fast (in order of effect)

1. **End read-back off** (`end_readback=off`): re-reading every saved byte doubled the total time.
   The TOSEC check and the PC check now do that job.
2. **CRC32 only** (`capture_hash=crc32`): SHA-256 cost about 28% of capture speed.
3. **GD-ROM DMA, overlapped** (`capture_read=dma`): the drive fills the next chunk while the CPU
   writes the current one to the card. Disc time went from 27% of a capture to 0.5-1.3%.
   +36% on a whole disc. Proven on data and audio tracks.
4. **UI capped at 2 redraws a second** (`ui_hz=2`): the unthrottled UI took about a third of the CPU.

## Defaults today

| Setting | Default | The fast, verified rips used |
|---|---|---|
| `ui_hz` | 2 | 2 |
| `capture_read` | **dma** (since 2026-09-20) | dma |
| `capture_hash` | **crc32** (M1.5 shell) | crc32 |
| `end_readback` | **off** (M1.5 shell) | off |

**M1.5 default decision:** the shell now selects `capture_hash=crc32` and `end_readback=off`.
Every verified fast rip used them; no reader code changed for this decision. Saved preferences
can change these choices, and explicit `bench.cfg` keys override preferences. Y Verify still
rereads saved bytes. The equivalent `/KUI/bench.cfg` is (plain text, UTF-8 or ANSI):

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

1. **M1.5 app/destination acceptance.** Launcher, persistent preferences, selected destinations
   and named output are implemented; a completed named capture and saved-file PC check remain
   pending. The next app round adds system settings, idle disc titles, music, Memory Test, VMU
   backup and network inspection, all awaiting console acceptance. See [the app checks](apps-test.md)
   and [ripper controls](ripper-controls.md). The default decision above is applied.
2. **Faster CRC16 in the SD driver** (link-time `--wrap net_crc16ccitt`, no KOS patch): measured at
   22.7 -> 8.8 CPU cycles a byte, worth about **7% on a capture** and 5% on SD reads.
   `src/core/crc16.c` already has the verified implementation (`kui_crc16_slice2`).
3. **Faster CRC32**: a 256-entry table in place of the nibble table, about 3%.
4. **Failure paths on hardware: all but one done.** A B stop and a lid-open both stop cleanly and
   resume to a verified finish; a physically damaged disc retries a fixed 10 times, pins the bad
   sector and stops without zero-filling. Only a card filling up mid-capture is host-tested only.
   FAT32 is done: about 2.4% slower than exFAT, otherwise identical.
5. **Fixed 2026-09-20: a stop no longer turns DMA off.** A lid-open on Omikron confirmed on
   hardware that any cut-short DMA read switched DMA off until reboot, so a same-boot resume ran
   at PIO speed. Now only a timeout, or three DMA failures in a row on an unchanged disc, does
   that; a stop, a disc change or one damaged sector leaves DMA on. The async read also latches a
   disc change itself now. **Confirmed on hardware**: a B stop and a lid-open in one boot, both
   resumed on DMA ([evidence](evidence/dma-stop-fix-confirmed-2026-09-20.json)). One small gap:
   the `Disc read:` summary prints only when a capture finishes, not on a stop or failure.
6. **A game launcher is a different problem.** SD reads over SCIF top out around 730 KiB/s even
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
