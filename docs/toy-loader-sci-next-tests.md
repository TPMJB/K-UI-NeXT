<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Toy Commander SCI next tests: V, W and X

This kit contains three hardware test readers and exact R restoration. Start
with **V**, then **W**. **X is an optional PIO control**, useful if V reports
fallbacks or if V and W perform similarly. These are experiments; no video
or loading improvement has been established on hardware.

| Test | File | Change to cooked DATA payloads |
|---|---|---|
| V | `profiles/V-sci-attribution.kui` | Original payload call; adds DMA-start/success/fallback attribution |
| W | `profiles/W-cooked-p1.kui` | Uses a temporary cached P1 alias for the exact 512-byte cooked payload buffer, with the required cache cleanup |
| X | `profiles/X-cooked-pio-control.kui` | Uses deliberately unaligned private scratch for the exact cooked payload, selecting the existing bounded PIO fallback and copying successful data back |

Raw 2352-byte CDDA calls retain the original callback. The low SCI reader,
working audio routines, fixed two-sector GD step, baud rate and CRC algorithm
are retained. X does not test improved interrupt responsiveness: the caller's
masking and existing PIO bounds remain in effect. W and X only admit the known
512-byte cooked read shape; unexpected calls keep the original callback.
W and X freeze and restore the DATA observer **before the next dispatch after
the first accepted PLAY**. The accepting PLAY operation has no DATA payload.
A rejected PLAY or refused mailbox leaves the startup phase active. A requested
terminal report also freezes the observer. Later reads use the original callbacks.
V continues observing both phases until its terminal report. W and X are Toy
Commander intro/startup experiments, not general game-wide SCI profiles.

## Install one test at a time

1. Power off. Save the reader currently at `/KUI/apps/games/retail-boot.kui`.
   Copy **one** selected file from `profiles/` to that path. The ZIP does not
   overwrite your SD card automatically. Keep the 1.8.5 launcher at
   `/KUI/runtime.kui` and the same original raw Toy Commander GDI and tracks.
2. Safely eject, cold boot and launch Toy Commander with **A / standard
   SCI reader**. Keep the same card, layout, settings and display mode.
3. Let the same first **30 seconds of the intro** play naturally, without
   skipping. Film with sound and note the freezes or repeated sound. Request
   the terminal report with **A+B+X+Y+Start** at that point. Keep the same
   duration for V, W and any X comparison.
4. Start the report recording at **DATA PAGE 00000000**. A continuous focused
   video of all twelve DATA pages, followed by all eight PILOT pages, is
   easiest. Ensure DATA pages **5 and 6** and PILOT pages **5 and 7** are
   legible. Filming one complete DATA/PILOT/TRACE cycle **starting at DATA 0**
   prevents missing the mode and validity header.
5. On a separate ordinary launch, let the intro finish and confirm that the
   previously working CD music starts and continues. This is an audio
   regression check; keep its terminal statistics separate from the fixed
   30-second intro run.

Record the **V/W/X label**, displayed build ID, card model/capacity if known,
display mode and the exact point at which the report was requested. Each
test uses a distinct build ID. Report numbers and values are hexadecimal.
The reports freeze before the terminal display, so filming does not extend
the measurements. Pages remain for 120 video frames: the full 46-page cycle
takes about 92 seconds at 60 Hz or 110 seconds at 50 Hz.

Stop a candidate if it refuses installation, reports a new read failure,
damages playback or changes previously working CD music. Restore
`restore/R-private-write-through.kui` to the same reader path. This is the
exact previously tested R build **73e8363c10f7**. Neither game tracks nor the
launcher need replacement. No new R or T run is required merely to obtain
these next diagnostics.

## Interpreting the data

The DATA report is `LDP1`, **version 2**, still 192 words (768 bytes).
Its original read/payload timing, failures, caller status and worst-read
context remain available. Added attribution is captured around only the
wrapped cooked payload calls from the existing low SCI diagnostics. The
generated `evidence/data-word-legend.md` is the exact row-by-row map for the
compiled header. Read the mode and validity fields before comparing totals.
Header words 11–14 (`payload_attempts`, `payload_declines`,
`payload_publications`, `payload_failed`) become authoritative at freeze;
they remain zero beforehand. If any frozen helper counter is `FFFFFFFF`,
the report conservatively sets `saturated`. Use the frozen terminal report
for these counts.

V answers whether the original path actually starts/completes DMA or falls
back to PIO. Compare W against V using the same run duration, validity flags,
read/payload counts, total time, mean and maximum. A faster mean alone does
not establish smoother FMV; compare the recording and failures too. X gives
a retained PIO comparison with added scratch/copy cost, so its difference
from V is not a pure DMA timing measurement.

Convert valid ticks to milliseconds with `ticks × 1000 / 781250`. Check
saturation, invalid intervals, reentrancy, callback conflicts, nonstandard
payloads, read/payload failures and the attribution status before calculating
means. V's two observational phases split at the first accepted PLAY mailbox;
that does not identify the exact FMV ending or audible music start. W and X
stop before the following dispatch, so their later DATA phase has no new samples.
Keep every frozen run separate, even when its build ID matches another capture.

The SCI payload scope includes setup, transfer, bit reversal and calculated
CRC. This kit attributes the transfer mechanism; it does not separately
time DMA wire activity versus the bit-reversal/CRC work. First/inter/tail
gaps may include commands, token waits, wire CRC bytes, copying and cleanup.
Measurements include the observer and helper overhead.

The ZIP includes exact DATA/PILOT/TRACE legends, source snapshot, build
configuration, linked ELF/map/binary/stack evidence, executable linked
admission results, host regressions, runtime identities and `SHA256SUMS`.
Candidates are packaged only after those checks pass. The candidates are
not hardware tested by the build system.
