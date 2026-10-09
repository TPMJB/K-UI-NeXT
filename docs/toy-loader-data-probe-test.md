<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Toy Commander U DATA diagnostic

U gathers the missing loader measurements from the same Toy Commander
intro. It retains the working R CDDA implementation and the existing
two-sector SCI reader. This is a diagnostic build; no FMV improvement is
claimed. The R and T recordings already supplied remain the controls, so
there is no need to repeat them for this capture.

## Install and run

1. With power off, save the currently installed reader separately. Copy
   `profiles/U-loader-data-probe.kui` to
   `/KUI/apps/games/retail-boot.kui`.
2. Keep the same original raw Toy Commander GDI and tracks, card layout,
   settings, display mode and SCI connection. Keep the 1.8.5 launcher at
   `/KUI/runtime.kui`. Safely eject, cold boot and launch with **A**, the
   standard reader. Converted 2048-byte images and X/Y background readers
   are outside this title-specific diagnostic.
3. Let the complete intro play naturally, without skipping it. Film with
   sound if convenient. Stop at the same idle/title point used previously;
   record that point and whether CD music has started. Use the existing
   **A+B+X+Y+Start** return request to reach the terminal report.
4. Capture every **DATA PAGE**, `00000000` through `0000000B`, from the
   first report cycle. A continuous focused video is easiest: twelve pages
   take about 24 seconds at 60 Hz. Then capture **PILOT PAGE 00000005**
   and **00000007**. These retain the comparable DATA and raw-audio totals.
   Keep the page number and all four rows of four words legible.

U shows DATA first, then eight PILOT pages, then 26 TRACE pages. An optional
video of the complete cycle takes about 92 seconds at 60 Hz, or 110 seconds
at 50 Hz. Each page remains for 120 video frames. The terminal freezes the
reports before RESET and display work; filming them does not extend the
measured run. Page numbers and values are hexadecimal.

After the capture, install `restore/R-private-write-through.kui` at the
same reader path to restore exact R build `73e8363c10f7`. U's disabled
configuration must reproduce those R bytes before this ZIP can be made.
R is the previously tested Toy CDDA pilot. If U refuses installation or
changes audio/loading behavior, record the refusal or change and restore R.

Please include the card model/capacity if known, **SCI / A standard reader**,
the game-disc identity shown by the launcher, display mode if known, and
the point at which you requested the terminal report. Keep focus steady;
no additional R/T run is needed merely to complete these pages.

## What U measures

The added report is `LDP1`, version 1: 192 words (768 bytes), with a
16-word header and two 88-word phases. It uses the same admitted TMU clock
at **781250 ticks per second** without reprogramming it. Phase 0 runs
through the first accepted CDDA PLAY mailbox; phase 1 follows that
boundary. This boundary does not establish the exact FMV ending or
audible music start. The existing 416-word `LTR1` TRACE report and the
128-word PILOT wire report remain available unchanged.

Only DATA-owned EXEC/CHECK visits enter U's probe. Original caller SR and
PR are sampled before measuring a cooked 2048-byte read. U temporarily
wraps the existing read and 512-byte payload callbacks, restoring them
after the cooked read; raw 2352-byte CDDA calls use the original callbacks.
An unexpected callback is preserved and counted as a conflict.

| Metric | Observed interval |
|---|---|
| `read_body` | Around the original cooked read, including acquire, image read and cleanup |
| `first_payload_gap` | Read entry to the first original payload callback entry |
| `payload_body` | Around each original payload callback, including its setup, transfer, bit reversal and computed CRC |
| `inter_payload_gap` | Prior payload callback exit to the next payload callback entry |
| `tail_gap` | Final payload callback exit to original cooked-read exit |

The gaps can contain command/token waits, CRC tail reads, copying and
cleanup. They do not separately identify physical card latency, pure wire
time, CMD18 setup or CMD12 cleanup. A read satisfied without a payload
callback is counted in `no_payload_reads` and has a `read_body` sample,
with no invented first/inter/tail sample. The measurements include
observation overhead. Convert valid ticks to milliseconds with
`ticks × 1000 / 781250`; divide an unsaturated total by its nonzero sample
count for a mean.

The 32 `sr_buckets` count original caller status on DATA EXEC/CHECK
visits, including terminal checks. Bucket index is the caller's IMASK
value, plus 16 when SR.BL is set. This records caller interrupt state;
it does not measure the full duration for which interrupts are masked.
The one retained worst valid read per phase records token, request LBA,
chunk LBA, chunk sector count, saved SR, caller PR, read ticks and payload
callback count. It is a limited sample rather than a chronological trace.

Before interpreting means or missing timings, inspect `saturated`,
`unmatched_end`, `nested_begin`, `invalid_intervals`, `reentrant_reads`,
`reentrant_payload`, `callback_conflicts` and `nonstandard_payload`.
Read/payload failures and invalid timing remain visible in the counts.
These checks distinguish unavailable observations from fast or absent work.

The exact DATA, TRACE and PILOT row legends are in `evidence/` as Markdown
and JSON. The ZIP includes the reviewed source snapshot, build
configuration, compiler identity, actual linked ELF/map/binary and stack
evidence, executable linked audits, host results, exact R restoration and
`SHA256SUMS`. It contains no game tracks, ARM firmware or launcher replacement.

The result will tell us whether most observed cost is inside payload
transfer or between callbacks, and whether the caller enters with
interrupts already masked. That selects the next controlled DATA loader
change while preserving the working audio path.
