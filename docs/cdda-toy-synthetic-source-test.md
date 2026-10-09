<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Toy Commander: S audio-source control

S deliberately plays silent CDDA. It generates PCM16 samples in RAM instead
of reading audio sectors from the card. This is a diagnostic for video,
loading and sound effects; it is not an audio-quality candidate or a claimed
performance fix.

The normal AICA voices, stereo ring, cursor proofs, source-frame/FAD position,
looping, pause, release and stop remain active. Native DATA loading and sound
effects remain real. Cache policy is the same as R (`00000101`), with the same
two-sector GD steps and synchronous SCI DATA reader. The original ARM driver
is retained. No AICA DMA, native interrupt hook or new buffer is added.

## Run one comparison

1. With the Dreamcast off, copy `profiles/S-silent-source.kui` to
   `/KUI/apps/games/retail-boot.kui`. Keep the existing launcher, GDI, tracks
   and SCI settings. The build ID is recorded in `build.json`.
2. Cold boot and play the same intro segment used for R. Missing CD music is
   expected. Observe video speed/holds, loading and sound-effect dropouts.
3. Continue briefly into gameplay to exercise pause/resume and scene changes.
   Record the segment tested and the terminal pages if a report appears.
4. With power off, restore `restore/R-private-write-through.kui` to the same
   installed path and cold boot. This is the exact previously tested R file,
   build `73e8363c10f7`. The exact older clean-audio build is also included.

## Interpret the result

| Result against R | What it supports | Next engineering target |
|---|---|---|
| Video or SFX improves | Incremental audio card-read work contributes to the problem | Decouple prepared PCM consumption from source reads; reduce masked SCI occupancy |
| Video and SFX stay the same | Removing audio reads alone is insufficient | Investigate native DATA reading, decoder/presentation cadence and retained G2 work; packed DATA is a separate route |
| New loading/control failure | The diagnostic is not a valid comparison | Restore R and investigate before using its performance result |

An unchanged result does not clear SCI as a whole: S retains actual DATA
reads and their masks, and retains the DATA-priority exclusion gate. This
control removes a bundle of audio-source costs, including card traffic,
callback CPU work and interrupt masking. It cannot identify which one
dominates by itself.

## Report labels

The wire layout stays at version 8 and eight pages. For S only, `raw_calls`,
`raw_bytes`, `raw_read_ticks_last`, `raw_read_ticks_max`,
`raw_read_ticks_total` and `raw_read_timing_calls` describe generated source
requests and their duration. They are **not physical card-read counters**.
Source frames still advance normally; the end-of-track padding path is not
used to synthesize the track. All other fields retain the existing meanings.
`raw_errors == 0` in S says nothing about card reliability.

This ZIP includes the exact source, compiler layout/stack evidence, host
test output, checksums and restoration files. It contains no game files,
game executable, ARM driver or launcher. Host checks establish the authored
control and ownership behavior; console testing decides its usefulness.
