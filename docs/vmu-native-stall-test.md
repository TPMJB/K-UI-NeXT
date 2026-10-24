<!-- SPDX-License-Identifier: GPL-3.0-only -->
# B reader stall diagnostic

This candidate keeps B's scheduling: 20/25 SD blocks of foreground catch-up,
eight blocks per SCI interrupt, and the existing one-block PIO fallback.
The owner reported A essentially unchanged and B somewhat better, but still
with movement freezing for about a second while audio continues. The supplied
63304.mp4 shows repeated movement pauses. Their cause is not established.

The new build measures reader work and keeps the four longest controller-poll
gaps during a fresh gameplay capture. It also retains the VMU timing report.
Measurements stay in private RAM. Normal VMU saving still uses its existing
journal; diagnostics add no SD log writes.

## Install and capture

Extract the complete ZIP and run from its root:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2 --profile-runtime
```

Adjust the mount/profile if different. Keep the same image, prepared save
profile and launch settings used for B: SCI, Background loading On, Normal,
Auto/default encoding, CD music Off and RAM patches Off. Keep physical A2
empty; virtual A2 and physical A1 retain their existing behavior. Safely eject
and cold boot.

Reach the gameplay area in the video. **Hold both triggers fully and B for
about a second**, then release them. Play through the stutter for roughly
20 seconds. The game then intentionally stops and cycles diagnostic pages.
An unsupported clock uses a 1,200-controller-poll fallback, so the wait can
differ. Record at least 25 seconds of the diagnostic screens, starting with
`B READER STALL CAPTURE`, then power off. Pages repeat if you miss the start.

The first three pages contain reader measurements; the next page contains
VMU submission/completion/free/storage measurements. Capture those four pages.
The remaining pages retain the older optional SDK phase observations.

## What the pages distinguish

All numbers are hexadecimal. When the clock is valid, 781,250 ticks equals
one second (one tick is 1.28 microseconds). A nonzero BAD or CLOCK BAD field
requires checking clock validity before converting durations.

* CALL MAX measures the longest foreground reader delivery visit. It includes
  its SD waits, checking and copying; it excludes GD dispatch outside delivery
  and the assembly entry/exit.
* IRQ MAX measures the same work inside an SCI reader interrupt.
* GAP MAX is the longest interval between successful Maple completion samples. It can show
  when controller polling was held off; it is not a direct measurement of a
  displayed-frame freeze.
* The next two pages keep the four longest gaps with interval reader block,
  PIO, CRC and token-error deltas plus endpoint GD function, command, token
  and block. A gap may span several requests; these endpoints are not a full
  request trace.
* The VMU page separately measures submit, complete, free and storage scopes.
  Storage time is included in completion time, so those totals overlap.

A long CALL/IRQ scope supports a reader-induced pause. Short reader scopes
with long VMU scopes point elsewhere in the hooks. Short scopes throughout
would require investigating game waits/data availability and frame pacing.
These are diagnostic interpretations to test, not a confirmed cause.

The profiler only reads the game's existing TMU0 registers and accepts its
exact known clock configuration at sampled endpoints. It never configures a
timer or cache. A timer stop/reset between unobserved endpoints cannot be
ruled out by endpoint samples alone. Sampling adds overhead, so use this
build to locate the pause rather than judge final performance.

## Startup verification

The stage now uses a 256-entry CRC32 table and four bytes per loop instead of
the resident's compact nibble routine. It still checks every executable byte
and compares every resident byte. Separate `ENTIRE EXECUTABLE VERIFIED` and
`ENTIRE RESIDENT VERIFIED` messages show which phase completed. Console boot
time improvement remains to be measured; discovery and other launch checks
still run.

## Reproduce

```sh
make -f Makefile.vmu-native-stall BUILD=build/native-b-stall all
make -f Makefile.vmu-native-stall BUILD=build/native-b-stall stall-host-tests
python3 tools/check_vmu_native_background.py build/native-b-stall --self-test --require-stage
```

Host checks cover real reader scheduling, bytes, retries and ownership;
the persistent worker's real save/recovery path; capture arming and rollover;
and the fast checksum against the original implementation. The finite-service
model checks 50 schedules, including the original 32 delayed-DMA schedules.
Model rounds are caller opportunities, not measured console milliseconds.
