# Next isolated CDDA console tests

**Results received:** the owner supplied two successful controls screens and
successful 15-minute soak/stress screens for build `cd207bc06def`, all with
zero unexpected failures. See the [exact hardware record](evidence/cdda-controls-soak-stress-hardware-2026-10-07.md).
The numerical request is complete; listening and precise timer/audio
calibration remain open. Retain the procedure below for targeted reproduction.

The first console screen completed all five stages with zero software failures
on build `9020d5101c7e`. Its exact counters are in the
[hardware evidence](evidence/cdda-hardware-2026-10-07.md). Audible output has
not yet been reported. Confirm what you heard in that run; repeat the short
baseline only if you need to check its output again. Run controls before either
15-minute test.

These profiles are controlled homebrew experiments on the existing SCI card.
They do not run Toy Commander, install retail CDDA hooks, or establish ownership
of a retail game's RAM, sound channels, sound RAM or timers. The released
1.8.5 reader remains the fallback; no ordinary reader is replaced or modified.

## Card setup and profile changes

Keep the original runtime backup made for the first harness. Do not overwrite
`/KUI/runtime-before-cdda.kui` with another test runtime. Keep the complete
uncompressed Toy Commander dump and its descriptor together in the game folder.

Use `K-UI-CDDA-Tomorrow.zip` and its build manifest. The archive's
`KUI/runtime.kui` runs controls by default. It supplies the generated
`stereo.raw` and 8 MiB `stress.bin`; it does not include Toy Commander audio.

| Runtime in the archive | Profile / purpose |
| --- | --- |
| `runtimes/00-tested-baseline.kui` | 0: exact previously tested build `9020d5101c7e`; optional listening repeat |
| `runtimes/01-controls.kui` | 1: automatic controls and deadline recovery; also the default `KUI/runtime.kui` |
| `runtimes/02-soak.kui` | 2: 15-minute uninterrupted tone soak |
| `runtimes/03-stress.kui` | 3: 15-minute tone playback with verified serialized data reads |

Each is a temporary replacement runtime. Install the supplied `KUI` folder
once, keeping the existing runtime backup and owned `track14.raw`. To change
profiles afterward:

1. Power off, remove the existing SCI card, and attach it to the computer.
2. Copy the selected profile's runtime to `/KUI/runtime.kui`.
3. Keep the supplied generated fixtures in `/KUI/tests/cdda/`. Reuse the same
   `/KUI/tests/cdda/track14.raw` from your owned original dump; game audio is
   not included in the test package.
4. Safely eject, install the card, and use the normal SCI boot path.
5. Check the displayed build and profile against the package manifest. The
   preserved baseline identifies itself by build `9020d5101c7e` without a
   numeric profile label. Let the automatic sequence run, listen, then
   photograph the final screen.

No new CD or Wi-Fi firmware is needed. The harness reads the card without
writing logs; photographs and listening notes are the test record. A power
cycle ends each profile. To return to the retained runtime, power off, remove
the temporary `/KUI/runtime.kui`, and rename the preserved backup to
`/KUI/runtime.kui`.

## Baseline listening: report the first run, or optionally repeat profile 0

If you heard the complete first run, report its audible result against the
sequence below. Otherwise use `00-tested-baseline.kui` to repeat it once.
Listen at a comfortable volume with stereo output.
Both channels must be available; a mono display or speaker cannot establish
left/right order. The first fixture uses these audible phases:

| Fixture time | Expected output |
| --- | --- |
| 0–3 seconds | 440 Hz on the left; right silent |
| 3–6 seconds | 660 Hz on the right; left silent |
| 6–9 seconds | 440 Hz left and 660 Hz right together |
| 9–12 seconds | Silence |

The fixture fades each phase edge over 15 ms. Its planned transitions and
three seconds of silence are not dropouts. The next two stages each seek to
the same stereo region and play it for two seconds; both should sound alike.
Track 14 should then play its approximately 40.95-second music excerpt,
including its trailing silence. The final stage seeks to its last second,
primarily silence, and stops at actual EOF.

Pass only when the screen completes five stages with zero failures **and** the
tones have the expected channels, stereo is steady, and the music has no
unexplained clicks, distortion, missing channel or dropout. Record whether the
final stop is silent. An all-zero counter screen alone is insufficient.

## Priority 1: profile 1, automatic controls and recovery

This short profile exercises explicitly announced play, pause/resume, seek,
repeat/track change, EOF/stop, and deliberate late-service recovery. No
controller interaction is required. Compare each audible transition with the
on-screen stage; planned silence or a deliberately interrupted segment is not
an incidental dropout.

| Controls stage | Expected output / behavior |
| --- | --- |
| 1: left tone and stop | 440 Hz left-only for one second, then automatic stop and seek/restart |
| 2: right seek | 660 Hz right-only for one second, starting 3.1 seconds into `stereo.raw` |
| 3: stereo pause/resume | 440 Hz left and 660 Hz right for two seconds of played audio, starting at 6.1 seconds; after about one played second, pause silently for one second, then resume from the actual played cursor |
| 4: EOF and bounds | One additional second of stereo tone, then EOF/bounds checks and a seek to Toy Commander track 14's final second; that second is primarily silence |
| 5: deadline and recovery | Announced 190 ms service delay, expected deadline refusal before refill publication, stop, then a fresh one-second stereo playback |

Pause/resume must preserve the actual played position; queued/prefetched audio
does not define where to resume. A deliberate stop/seek can have a planned gap.
This controls profile does not promise gapless transitions between tracks.

The deliberate deadline case must be reported as an expected refusal, followed
by confirmed stop and a fresh controlled restart. It must not be counted as an
unexpected pass or silently ignored. The 190 ms delay exceeds the approximately
185.76 ms half-ring observation limit. Listen for silence during each announced
pause/stop and for the intended tone after restart. A timer cannot mute audio
while the CPU is intentionally not servicing it; the acceptance point is the
bounded detection and stop once service resumes, before any unsafe refill.

Pass only when all announced stages complete, the expected deadline event is
identified, recovery succeeds, and there are no unexpected failures or stale
audio loops after stopping. The expected deadline event is separate from the
unexpected failure count.

## Priority 2: profile 2, 15-minute seamless tone soak

Let one continuous playback session run for the full 15 minutes. This is a
synthetic tone loop with no deliberate silent phases and no stop/restart at
file-loop boundaries. A brief silence, repeated click, audible channel drift
or unannounced restart is a failure, even if playback later recovers.

Both longer profiles reuse the unfaded interior of `stereo.raw` from
6.1 through 7.1 seconds: frame 269,010 for 44,100 frames. Expect continuous
**440 Hz on the left and 660 Hz on the right**. Each frequency completes an
integer number of cycles in this one-second region; the ring carries samples
across file-loop boundaries without restarting the channels. The baseline's
fades and three-second silent phase are outside this region.

The purpose is to exercise repeated file and ring wraps together with the
free-running timer. A 32-bit clock at 12.5 MHz wraps approximately every
343.6 seconds. Fifteen minutes crosses at least two timer wraps while playback
continues. The run must not reset the timer or sound channels to hide those
boundaries. Record the elapsed time, observed timer-wrap counter if shown,
loop/refill counts, deadline counters, timing margins and stack result. The
profile requires at least 900 elapsed seconds, 900 actually played loop
completions and two observed timer wraps, rather than counting prefetched loops.

Pass only after the full duration with continuous expected stereo, zero
unexpected deadlines/failures, and intact guards. Do not infer a 15-minute pass
from a shorter partial run.

## Priority 3: profile 3, 15-minute serialized SCI read stress

Use the same continuous synthetic tone and additionally read the supplied
8 MiB `/KUI/tests/cdda/stress.bin`. The data requests are serialized with audio
work on the same SCI card; they must not create two simultaneous card owners.
The harness verifies the deterministic fixture bytes rather than accepting
successful read calls as sufficient evidence.

**No game is running.** This measures a controlled extra data-read workload;
it is not yet a test of retail command delivery, game load latency, a game
arbiter, sound effects or safe sharing of game-owned sound resources. Keep
those later gates separate from a successful synthetic stress result.

Run the full 15 minutes and record verified data-read counts/bytes, mismatches,
worst data-read time if displayed, audio service/refill timing, timer wraps,
guard status and audible stability. Pass requires completed duration, checked
data with zero mismatches, zero unexpected audio deadlines/failures and
continuous correct stereo. It additionally requires at least 8 MiB of verified
data and at least 64 KiB of verified reads in each completed 60-second interval;
no five-second gap without a completed verified data job is permitted. Record
the full data-pass count and maximum data-job completion gap as well as read
latency. Idle or starved data jobs cannot produce a stress pass. A safe deadline stop
is useful failure evidence, but it does not pass the stress profile.

## Finish with a cold-boot controls repeat

After the longer profiles pass, power off, copy `01-controls.kui` back to
`/KUI/runtime.kui`, and run controls once more from a fresh boot. Confirm the
same sequence, silent stops and expected recovery; photograph its final
screen. This checks that success does not depend on a previous profile's
hardware state. Restore the retained ordinary runtime afterward.

## Stop criteria and result format

Stop the current run after photographing its screen if the harness reports
an unexpected failure, corrupted guard, data mismatch, unavailable fixture,
or a deadline outside the deliberate controls case. Also stop for loud
unexpected noise, sustained stale playback after an announced stop, missing
or reversed channels, or persistent dropouts. Power off to end a stopped or
unresponsive test; keep the screen photograph and approximate stage/time.
Restore the retained runtime if the test cannot start or complete. Do not
continue into the longer profiles to work around a baseline or controls fault.

For each profile, return a photograph plus this small record:

```text
Build / profile:
Completed or failed stage / elapsed time:
Audio: left-only correct; right-only correct; stereo steady; final stop silent:
Clicks, distortion, dropout or unexpected silence, with approximate time:
Completed stages / failures / expected deadline recoveries:
Worst refill us / maximum service gap us / minimum margin us:
Timer wraps / loops (when displayed):
Verified stress bytes / full data passes / mismatches (profile 3):
Worst data-job us / maximum verified completion gap us (profile 3):
Checked card blocks / private stack bytes / guard result:
Card or output setup changed from the first run:
```

## What a pass permits next

| Evidence | Next step |
| --- | --- |
| Baseline counters and correct listening | Continue controls and recovery checks |
| Controls, stop and deliberate deadline recovery pass | Continue uninterrupted soak |
| Full soak, timer wraps and intact guards pass | Continue serialized read stress |
| Full stress with verified bytes and stable audio passes | Design and test separate audio/data jobs and their bounded card arbitration |
| Any unexpected failure | Reproduce and fix that failure in the isolated harness before proceeding |

Passing this set establishes a stronger isolated engine baseline. Retail
integration still needs its own package/memory admission, periodic service
bridge, command semantics, and evidence that Toy Commander's sound driver and
effects coexist with CDDA. Do not enable automatic retail dispatch or borrow
the 1.8.5 reader's reservation based solely on these homebrew results.

## Rebuilding and interpreting host evidence

The archive includes the complete committed source snapshot, exact private
FatFs inputs, licenses, source patch, and each new profile's ELF/map/stack
reports. New runtimes are not console-tested until you run them. Host checks
exercise real PCM/ring/session code with deterministic hardware models; they
cannot certify actual SCI/G2 latency, output quality or console stack use.

From the source checkout, with the SH-4 cross tools on `PATH` and the supplied
FatFs files in a local source directory:

```sh
python3 tools/test_cdda_host.py --sanitize
python3 tools/test_cdda_storage.py --fatfs-source /path/to/fatfs --mkfs-exfat /path/to/mkfs.exfat
make -f Makefile.cdda CDDA_FATFS_SOURCE=/path/to/fatfs cdda cdda-profiles cdda-fixture
```

The exact published revision is in `build.json` and `source-url.txt`. When
rebuilding from a source snapshot without Git history, also provide
`CDDA_BUILD_ID=` with that revision's first twelve hexadecimal characters.

Further performance changes should follow the hardware results. If the extra
data jobs exhaust the margin, first compare per-job latency with audio refill
latency, then test smaller bounded data requests. Larger audio read batches or
retained SD streams need their own block-identity, switching and deadline
checks before replacing this measured path. G2 DMA and retail sound sharing
remain separate experiments.
