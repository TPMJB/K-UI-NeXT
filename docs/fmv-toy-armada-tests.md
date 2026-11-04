# Toy Commander and Armada FMV tests

These are console candidates, built from main `0045dd96937e` plus the included
FMV test changes. Gameplay already works; the tests target Toy's slideshow
video and Armada's hard-to-hear movie audio. No console improvement is claimed.

## Install once

With the console off, copy your current **KUI folder to the computer as a backup**.
Then copy the **KUI folder inside `01-control`** onto the SCI card, merging folders
and replacing the matching files. Do not copy every variant onto the card at once.
No Python installer, game conversion, card import or settings reset is needed.

The test menu routes the admitted Toy image to the supplied diagnostic adapters.
The normal Toy modules, normal native reader and ordinary CE reader are not
installation targets. The test menu is based on the main commit above; restore
your backed-up files when returning to newer experimental builds from other tests.

Use the same image and format for both runs of a game. Raw and 2048 DATA Toy
adapters are both included. Audio tracks remain original raw audio. Keep the
console/TV volume unchanged between Armada runs.

## Four short runs

| Run | Files to copy | Launch options | What to do |
|---|---|---|---|
| Toy control | Already installed by `01-control` | CD music On; Background loading Off; Virtual VMU Off | Play the troublesome intro to a recognizable point, then open the report. |
| Toy candidate | `02-toy-idle-background/KUI` | Exactly the same | Repeat the same video and stop at the same point. |
| Armada control | Copy `01-control/KUI` again | Background loading On; CD music On; Virtual VMU Off | Listen to the same hard-to-hear part of the opening movie, then open the report. |
| Armada CDDA silent | `03-armada-cdda-silent/KUI` | Exactly the same | Repeat the movie with the same volume and stop at the same point. |

Power off before each copy and cold boot each run. To open the terminal report,
use **A+B+X+Y+Start**, as in the earlier CDDA tests. For Toy, record **20 seconds**
to capture all three pages; for Armada, record **25 seconds** to capture all four.
If possible, start recording five seconds before the gesture so the faulty
picture/audio is also captured. The report stops gameplay; power off to exit.
If a candidate stops before the report, photograph its last screen.

For each run, report whether the video is smoother and whether the audio is
quiet, clear, broken up, distorted or absent. For Toy, state whether this is
the first or second intro. No extended gameplay retest is needed for this round.

## What the comparisons establish

**Toy:** the retained receive-ahead DATA engine normally retires permanently at
the first accepted CDDA PLAY. The candidate can re-enable that same engine
after the audio owner is fully quiescent: no outstanding sound packet, active
bank, stop acknowledgement or unapplied command. It also checks that the DATA
receiver has no live DMA, nested IRQ, card lease or quarantined fault. Every
subsequent PLAY or resume still fences the DATA engine before publishing audio work.
No extra RAM, IRQ priority change, larger cache or new speculative prefetch is used.

The third Toy page shows **REARMS REFUSED ENABLED RETIRED**. A positive REARMS
count establishes activation. Zero means this candidate did not get a suitable
post-PLAY idle window; unchanged playback in that case does not rule out a
different background-reader integration. RETIRED records that a PLAY previously
retired the engine, so it can be 1 alongside ENABLED=1 after a successful rearm.

Toy's first two pages split observations before/after the first accepted PLAY.
These are command phases, not automatically identified movie boundaries.
Values are hexadecimal. Timing fields are milliseconds. A timing maximum with
zero valid samples is unavailable, not a measured zero. Sampled loader time is
not a frame-rate measurement or an exact assembly interrupt-mask measurement.

**Armada:** the second module retains physical CDDA reads, staging, lock/unlock,
cursor accounting and scheduling, but publishes zero samples to its own CDDA
buffer. It does not alter the movie decoder's audio, game effects, driver or TV
volume. If movie speech becomes clear while music disappears, CDDA masking is
a useful lead. If the soundtrack is still quiet, the reports and original clip
will help distinguish an inactive CDDA path from another movie/audio issue.
This is an audio-routing control, not an audio-quality or performance fix.

The original Armada reports retain audio state, source progress, gaps, worker
cadence and frozen underrun context. No runtime SD logging is added.

## Restore

Restore the files from your saved KUI folder. The package changes `runtime.kui`,
`apps/games/cdda-native-toy-raw.kui`, `cdda-native-toy-cooked.kui`, and
`ce-cdda-armada.kui`. If the two diagnostic Toy adapter files did not previously
exist, they can remain unused after restoring the menu. Saves and game files
are not changed by installation.

`build.json` records the exact source commit, per-variant build IDs and hashes.
`source/` contains the committed source and `evidence/` contains host and linked
checks. Build IDs use the first ten source-commit digits plus `c0`/`c1` for Toy
control/candidate and `a0`/`a1` for Armada normal/silent. The menu uses the normal
twelve-digit source prefix. Compiled/host-checked does not mean console-tested.
