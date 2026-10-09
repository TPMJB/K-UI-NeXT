# Toy Commander queued CDDA SCI test

This is a separate, opt-in hardware test for the original Toy Commander image.
It replaces the single staged audio sector with four private, verified RAW
sectors. SCI interrupts and bounded foreground service can prepare successors
between sound-worker visits. The worker can then consume several ready sectors
without waiting for another card transfer.

The card scheduler uses a conservative estimate of playable audio reserve.
Below 4,096 frames it favors audio; above 8,192 frames it favors game data.
The estimate excludes unfinished PCM blocks and decays while the worker is
away. It is only a scheduling hint: the existing sound-memory, cursor, control,
and STOP gates still decide whether PCM may be written.

The package has not been tested on a Dreamcast. Host throughput and memory
checks do not establish smoother intro video. The preceding foreground top-up
build recovered a regression but did not improve the original intro.

## Install and compare

With the Dreamcast powered off:

1. Keep a copy of your existing `/KUI/apps/games/retail-boot.kui`.
2. Copy this ZIP's `KUI/apps/games/retail-boot.kui` to that path on the SD card.
   The included `KUI/runtime.kui` is the unchanged launcher install fix; copy
   it only if that launcher is not already installed.
3. Keep the existing card-path configuration, original GDI, and all 15 original
   track files. This ZIP contains no game or sound-driver files.
4. Safely eject, cold boot using **SCI**, and launch Toy Commander with **A**.

Let the intro run without skipping. Compare both visible motion and audible
continuity with the working audio build. At the first menu, hold
**A+B+X+Y+Start** and photograph the build ID and pages **0 through 7**.
A short intro recording makes the comparison more useful than counter photos
alone. The exact candidate ID and source commit are recorded in `build.json`.

If launch, audio, or video regresses, power off and restore
`fallback/KUI/apps/games/retail-boot.kui` from this ZIP to
`/KUI/apps/games/retail-boot.kui`. Cold boot again. That is the unchanged,
working-audio build `7b55156aafa2`; the fixed launcher can remain installed.
Do not copy the `fallback` directory over the candidate during installation.

## Scope and limits

This experiment is admitted only for the exact supported Toy Commander title
and sound-driver signature. Audio production stays within one explicitly
selected audio-track interval and stops at its exclusive end. STOP, seek,
repeat-wrap, generation change, and failed admission revoke old queue authority
before the existing bounded receive fence. Game-data ownership is independent.

PENDING never modifies the worker's RAW output or publishes PCM. Only complete,
CRC-verified sectors are consumed, in order, once. The queue holds four sectors
(2,352 bytes each), about 53 ms of CDDA. The existing eight-block, 32,768-frame
PCM ring can cover occasional longer gaps while subsequent visits catch up.

CDDA still requires 75 sectors per second. Four ready sectors remove the old
single-slot acceptance bottleneck, but do not make arbitrarily sparse worker
visits sustainable. If visits occur only in tightly clustered pairs every
80 ms, the four-sector queue cannot supply the required rate. This revision
adds neither a new sound hook nor sound work inside game-data calls or IRQs.

The normal build remains `GD_FIXED_STEP=2`, `SHARED_SCI=0`, `ASYNC_CDDA=0`.
The test uses `GD_FIXED_STEP=3`, `SHARED_SCI=1`, `ASYNC_CDDA=1`.
The original PCM, sound driver, SD layout, and game image are retained.

Verified 512-byte boundaries remain the card handoff points. Foreground
CHECK/EXEC service admits at most four verified card blocks since the preceding
service entry, up to 1,024 finite steps, and only while its between-step elapsed
count is below 1,500 timer ticks. These are bounded admission rules, not a hard
1.92 ms completion deadline. Existing token, retry, DMA-fence, and stream-close
limits remain in force.

## Diagnostics and reproduction

Pages 0 through 4 retain the CDDA report. Page 5 reports the transport:

| Row | Column 1 | Column 2 | Column 3 | Column 4 |
| --- | --- | --- | --- | --- |
| 1 | Transport calls | SCI IRQ calls | Blocks consumed in calls | Blocks consumed in IRQs |
| 2 | Audio card claims | Claims deferred | Audio card releases | Data resumes |
| 3 | Errors | Retries | Token waits yielded | PIO fallback blocks |
| 4 | Longest transport call, ticks | Longest IRQ, ticks | Data token | Card owner |

Card owner is 0 when idle, 1 for data, and 2 for audio. Page 7 RAW timing measures
elapsed request-to-delivery time, including intervening game execution. It is
not CPU blocking time. Completed RAW calls exclude repeated pending polls.

The package includes source, licences, host-test output, scheduling evidence,
linked memory and stack audits, and SHA-256 checksums. The worker remains inside
the lower 64 KiB reservation; the sound SDK's upper 128 KiB scratch region and
the low resident's `0x8c007800` limit remain protected.

With the SH-4 toolchain on PATH:

```sh
python3 tools/test_toy_pilot.py
make -f Makefile.toy_pilot BUILD=build/queued-sci-async-cdda SHARED_SCI=1 ASYNC_CDDA=1 GD_FIXED_STEP=3 SCI_REUSE_TDRE=0 BUILD_ID=<source-commit-first-12> all
```

The older single-sector async experiment remains documented as withheld in
`docs/toy-shared-sci-test.md`. It is not the candidate in this ZIP.
