# Resident loader probe

This is the next Games hardware test. The existing Games browser has already
passed its recorded ARMADA inspection. **No new rip or full verification is
needed.** This build runs an original test program after shutting down K-UI.
It does not launch a retail game yet.

## Install and run

1. Finish any active operation and switch the Dreamcast off.
2. From this build's `sd-update` download, replace `/KUI/runtime.kui` and copy
   the whole **`KUI/apps/games/`** folder onto the SD card. It contains
   `probe.kui` and the original 56,832-byte `probe.dat`. Keep your games and
   other settings in place. Keep the existing boot CD.
3. Boot normally into **SD runtime**. Open **Games**, press **Start** for
   Advanced, select **Resident loader probe**, then press **A**.
4. Read the confirmation and press **A** again. **B** cancels before handoff.
   After handoff, this small test has no controller/menu service.
5. Photograph the final **PROBE PASSED** or **PROBE FAILED** screen, including
   the build ID and post-handoff SD block count. Then power off/on to return.

If preparation fails and returns to Diagnostics, use **Y** there to save its
log. If the standalone screen stops progressing, photograph the last stage
shown and tell us how long it stayed there. It cannot save a K-UI log after
the launcher has been removed. A photo is the intended result for this test.

The probe only reads the SD card. The resident backend has no write command.
Do not replace the fixture with a game or move/remove the card during the test.

## What PASS means

The separate client verifies the versioned request API, drive status and TOC,
sequential and random data, a raw data/audio track boundary, expected failures
for audio-as-data and gaps, unsupported commands, and cancellation before I/O.
It computes expected bytes independently and compares actual SD reads.

The resident service is separately linked at `0x8ce00000`. Before initializing
its own SD backend, it erases the old launcher/heap/stack memory outside its
new resident regions and the first 64 KiB firmware area. It installs the
independent client at `0x8c010000`, with a separate stack at `0x8cd00000`.
The resident stack is reserved at `0x8cfe0000..0x8cff0000`. No KOS functions,
threads, allocation, filesystem pointers or cached track data survive.

KOS performs its normal shutdown and resets the display before the entry shim.
The shim establishes its own SH-4 state; instruction/data caches stay disabled
for this correctness experiment. The display and font live in the resident
image. This is **not a throughput benchmark** or a proposed retail memory map.

The launcher opens only the two fixed probe files, verifies the executable
envelope, and builds a fresh, bounded file-to-card-sector map. FAT32/exFAT and
fragmented files use the same map. Its CRC-protected 1,600-byte manifest contains
sector addresses and limits, not fixture contents or pointers into the shell.
The resident read-only SCIF backend then initializes the card independently,
checks its capacity and reads blocks with CRC16 and bounded command waits.

## Scope and follow-up

The request interface is **K-UI probe ABI v1**, not a retail GD BIOS replacement.
An on-console PASS establishes the independent storage/handoff foundation.
Retail BIOS request translation, game boot state, memory reservations, CDDA
and game-specific compatibility remain subsequent work. First retail target
remains the owner's Dead or Alive 2 image after those pieces are implemented.

The SD protocol/SCIF implementation derives from pinned permissively licensed
KallistiOS, with attribution in `THIRD_PARTY.md` and source comments. No
DreamShell loader code was used. The proven optical capture engine is unchanged.

Host tests cover corrupt packages/manifests, fragmented and out-of-range maps,
SD protocol errors/CRC/timeouts, byte mismatches, cancellation and lifecycle
rules. Real FAT32/exFAT tests compare raw mapped reads after unmount and verify
whole-card SHA-256 remains unchanged. Only the console test can establish the
actual SH-4 handoff and post-shutdown pin behavior; that acceptance is pending.
