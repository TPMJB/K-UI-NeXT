<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Toy Commander loader trace

This is an optional loader diagnostic. Keep the working CDDA build as the
normal installation. T measures native DATA requests around the first
accepted CDDA PLAY command while retaining the real audio source, ARM
driver, stereo ring, sound effects, two-sector SCI reader and R cache
policy. It adds no AICA transfer experiment and claims no video fix.

The intro staying unchanged across CDDA experiments does not establish a
CDDA fault when music begins afterwards. This report therefore separates
loader work before and after the first **accepted PLAY mailbox**. That
boundary is not proof of the FMV ending or music becoming audible.

## Files and installation

The ZIP contains one diagnostic, `profiles/T-loader-trace.kui`, and two
exact restoration files. `restore/R-private-write-through.kui` is the
previously tested R build `73e8363c10f7`. The older clean-audio build is
`restore/7b55156aafa2-retail-boot.kui`. Both restorations remain Toy CDDA
pilots; neither is an ordinary nonpilot loader. The packager requires that
this source rebuilt with tracing disabled reproduce R byte for byte.

With power off, preserve the currently working installed reader separately.
Copy only the selected `.kui` file to `/KUI/apps/games/retail-boot.kui`.
Keep the 1.8.5 launcher at `/KUI/runtime.kui`, the same original raw GDI and
tracks, card layout, configuration and SCI transport. Safely eject, cold
boot, and launch Toy Commander through Games with **A**, the standard
reader. T is title specific: converted 2048-byte images and X/Y background
readers are refused, and another game cannot use this diagnostic.

## Three cold runs

Use the same route, without skipping the intro or changing settings:

Record the card model and capacity, **SCI / A standard reader**, game-disc
identity as listed by the launcher, and 50/60 Hz display mode if known.
Describe the same starting point, short gameplay route and return point for
all three recordings so their measurement coverage can be compared.

1. **R baseline:** install the exact R restoration and cold boot. Film the
   natural intro with sound, the transition and the first music. Continue
   along one short repeatable route, then use the game's
   **A+B+X+Y+Start** return request. Record the eight PILOT pages.
2. **T trace:** install T and cold boot. Film the same intro, transition,
   first music and route, then use the identical return request. Keep
   recording all terminal pages for **two complete cycles**.
3. **R restoration:** install the same exact R file and cold boot. Repeat
   the route to check that any difference follows T. Leave the preferred
   working audio build installed afterwards.

Use a stable landscape phone recording with focus and exposure held on the
screen. Keep the page number and four rows of four words legible; include
sound during the intro and first music. T cycles through eight **PILOT PAGE**
screens (`00000000` to `00000007`) and 26 **TRACE PAGE** screens (`00000000`
to `00000019`). Each page stays for 120 video frames, about
two seconds at 60 Hz or 2.4 seconds at 50 Hz. Two complete cycles take
about 2 minutes 20 seconds at 60 Hz. Values and page numbers on the screen
are hexadecimal. The report freezes before RESET and display work, so
filming the report does not extend the measured gameplay.

If T refuses installation or changes loading/audio behavior, photograph
the refusal or record the change, then restore the working reader. An
invalid comparison cannot establish a loader improvement. The diagnostic
adds observation overhead; the repeated R run checks this effect.

## What the measurements mean

The report is `LTR1`, version 1, 416 words (1664 bytes). Its first 32 words
describe identity, the accepted-PLAY boundary, timer validity and coverage.
The following two groups of 192 words describe the first observed GD call
after pilot installation through that boundary, and observations afterwards.
Bootstrap reads before pilot installation are outside this report. If no PLAY was accepted, the first group
covers the entire measured run. Both groups end with eight **unordered**
worst valid DATA-completion contexts, including token, LBA, request size,
destination and timing validity flags. They are a limited retained sample,
not a complete read trace.

| Timing | Observation boundaries |
|---|---|
| GD call body | Begin TMU sample to end TMU sample around the serialized adapter |
| DATA service body | The same samples, only for EXEC/CHECK with DATA command 16/17 owned at entry; includes zero progress and terminal CHECK |
| Pending service gap | Interval between observed loader calls while a DATA request remains pending |
| First credit | Request submission to first observed increase in delivered bytes |
| Request completion | Submission to an observed terminal state; can include failed or aborted requests |
| Completion to CHECK | Terminal observation to a CHECK that actually consumes its handle |

These timings use the admitted existing TMU clock at **781250 ticks per
second**. No clock is reprogrammed. The call-body timer does not separately
measure physical card transfer, decoding or interrupt masking; sampled
scanline wraps and framebuffer-address changes can miss events between
calls and are not FPS or dropped-frame counters.

Call-body and DATA-service-body timings include the trace's begin
bookkeeping and PVR reads after its start sample. They exclude end
bookkeeping after its end sample and the surrounding wrapper/assembly.
Consequently they include some observation cost and do not directly measure
the trace's total cost. The R → T → R recordings check whether observation
itself changes playback.

Each timing records valid sample count, saturating total, maximum, minimum
and eight histogram bins. Inclusive upper bounds are **782, 1563, 3125,
6250, 12500, 25000 and 50000 ticks**, approximately 1, 2, 4, 8, 16, 32 and
64 ms; the last bin contains larger valid intervals. Request-size bins use
inclusive limits of **1, 2, 4, 8, 16, 32 and 64 sectors**, then larger
requests. Convert individual valid ticks to milliseconds with
`ticks × 1000 / 781250`; divide a valid total by its sample count for a mean
only when neither has saturated.

Inspect `invalid_clock_calls`, `invalid_intervals`, `clock_epoch`,
`unmatched_ends`, `nested_begins` and `counters_saturated` before interpreting
means or missing timings. An unsupported/invalid timer profile is recorded
as unavailable; the trace does not reject or alter a game command because
its clock cannot be measured. Intervals spanning an observed invalid-clock
epoch are excluded. Missing context times are `FFFFFFFF` with their
corresponding VALID flag clear; they are not enormous measured delays.
`worst_seen`, `worst_valid`, `worst_retained` and `worst_excluded` describe
coverage. A request still outstanding when a phase freezes has its own
snapshot, rather than an invented completion time.

## Reading the result

A matching clip can show whether the accepted-PLAY boundary occurs before,
during or after the intro. Long pre-PLAY DATA call bodies, service gaps or
request latencies then give specific loader targets. Similar T and R
playback with a valid trace is useful evidence even without an improvement.
The trace does not prove a bandwidth limit, decoder fault or interrupt cause
by itself. Those require a separate controlled loader change.

The exact four-word row legend is included as
`evidence/trace-word-legend.md` and `.json`. The ZIP also contains committed
source, build configurations, actual ELF/map/binary and compiler-stack
evidence, linked audits, host results, restoration identities and
`SHA256SUMS`. There are no game files, ARM firmware or launcher replacement,
and no SD logging occurs during gameplay.
