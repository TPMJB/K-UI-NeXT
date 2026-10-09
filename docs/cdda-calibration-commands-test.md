# Short CDDA calibration and command tests

**Recorded result:** both profiles completed successfully on the console with
build `2cb5d25bb97f`, and the owner reported that they sounded successful. See
the [hardware evidence](evidence/cdda-calibration-commands-hardware-2026-10-07.md).
These instructions preserve the tested checkpoint; no repeat run is requested.
The counters and bounds below belong to that exact revision. Later sources
use the documented 12,468,720 Hz timer reference instead of its nominal
12,500,000 Hz conversion. Rebuild the recorded revision to reproduce this
archive; use the new mixed-test checklist for a later experimental binary.

Run these two new automatic profiles in order: **04 calibration**, then
**05 commands**. Calibration takes about one minute; commands takes roughly
20–30 seconds plus initialization. Photograph both final screens. The earlier
controls, soak and
stress profiles already passed their numerical checks; another 15-minute run
is not needed for this step. Their results remain in the
[hardware record](evidence/cdda-controls-soak-stress-hardware-2026-10-07.md).

These tests run in the detached homebrew harness with its own sound channels,
sound RAM and timer. No retail game is running and no BIOS CDDA hooks are
installed. The released 1.8.5 readers remain unchanged. Profiles 00–03 stay in
the previous test ZIP; this package contains only the two new profiles.

## Install calibration first

Use `K-UI-CDDA-Next-Tests.zip` and check its build manifest. The default
`KUI/runtime.kui` is calibration. The archive includes the original generated
`stereo.raw`; neither new profile needs `track14.raw`, `stress.bin` or any
additional game files.

| Archive runtime | Purpose |
| --- | --- |
| `runtimes/04-calibration.kui` | Matched playback-frame and timer endpoints, about 60 seconds |
| `runtimes/05-commands.kui` | Automatic command/job simulation, usually under half a minute plus initialization |

1. Power off the Dreamcast and attach its existing SCI card to the computer.
2. Keep `/KUI/runtime-before-cdda.kui`, the original working-runtime backup.
   Do not replace it with either new profile or a previous test runtime.
   If you already restored the ordinary runtime and this backup name is absent,
   copy that working `/KUI/runtime.kui` to the backup name before installing.
3. Copy the archive's `KUI` folder onto the card. Its default runtime goes to
   `/KUI/runtime.kui`; the fixture goes to `/KUI/tests/cdda/stereo.raw`.
   Reusing the identical existing fixture is also fine.
4. Safely eject the card, install it in SCI, and use the usual SCI boot path.
5. Check the calibration profile and build ID against `build.json`. Let its
   automatic test finish and photograph the whole final screen.

The harness reads the card without writing a log file. No new CD, card format
or Wi-Fi firmware is needed. Keep the owned Toy Commander dump and any earlier
test fixtures where they are; they are not inputs to this pair of tests.

## Profile 04: matched endpoint calibration

The previous long runs each displayed 902 source loops and 900 owned-clock
seconds. Those rounded counters did not share exact playback endpoints, so
they cannot establish a precise clock or sample-rate difference. This profile
records playback frames and timer ticks at paired start/end observations over
approximately 60 seconds, separating initialization and prefill from playback.
The start is the first checked AICA observation after prefill/start. The final
observation is taken immediately before stopping; display and key-off work
are outside the measured endpoint pair. Each position read is bracketed by
timer reads, giving lower and upper elapsed-tick bounds rather than assigning
an exact time to a multi-register read.

This is a **relative comparison between AICA playback and TMU1**. A rate
expressed using the nominal 12.5 MHz timer is conditional on that timer's actual
frequency. The test has no independent wall-clock or audio-frequency reference;
it cannot identify which oscillator accounts for a difference or prove an
absolute 44.1 kHz sample rate. It does not automatically change timer settings,
playback pitch, resampling or the existing reader.

Expect **440 Hz on the left and 660 Hz on the right** during the measurement,
followed by a stop and the final counters. The loop reuses `stereo.raw` from
6.1 through 7.1 seconds, without its fades or silent phase. Planned start/stop
edges are distinct from an
unexplained click or dropout during the measurement. Send the final photograph
even if later analysis finds a relative-rate difference; the raw values are
the evidence being collected, not a reason to retune the system by hand.

| Calibration counter | Meaning |
| --- | --- |
| Paired played frames | Difference in cumulative hardware left-channel frames between the two endpoints, rather than PCM read/prefetch position |
| Elapsed ticks lower / upper | Bounds on playback elapsed time using the bracketing TMU reads |
| Read ticks start / end | Duration of each endpoint's register-read window |
| TMU1 TCR1 start / end; FRQCR start / end | Raw clock-control snapshots used to check divider/configuration stability |
| Nominal TMU Hz / configured pitch | 12,500,000 and 0; these are the assumed timer frequency and programmed AICA pitch, not independent frequency measurement or pitch readback |

The profile passes with one completed stage, zero failures, valid endpoint
bounds, stable clock controls and intact existing guards. It does not grade
the relative rate against 44.1 kHz. Its elapsed lower bound must reach
750,000,000 ticks; each read window is at most 100,000 ticks, and the upper bound
must remain at most 752,521,995 ticks. The nominal conversion is about 60 to
60.202 seconds. The timer's sticky underflow flag can differ without a divider
change. Later analysis also accounts for the displayed ±1-frame endpoint
quantization; a rounded loop counter alone is not the measurement.

## Profile 05: automatic command and job checks

After recording calibration:

1. Power off and attach the SCI card to the computer.
2. Copy `runtimes/05-commands.kui` over `/KUI/runtime.kui`.
3. Leave the original runtime backup and `stereo.raw` untouched.
4. Safely eject, boot through SCI, and let commands finish automatically.
5. Photograph its whole final screen.

This profile models play, stop, seek, pause/resume, status, EOF, completion and
cancellation in the controlled harness. It exercises command/job state and
completion checks against the generated source; it does not intercept a
retail game's BIOS calls or implement the retail GD interface. The distinction
matters: a successful simulated command sequence does not establish correct
Toy Commander command delivery, sound effects or memory/sound-driver sharing.

No controller input is required. The screen announces each automatic stage.
Expect tone segments interspersed with deliberate pauses, stops and seeks.
Planned silence and transitions are part of this test; continuous uninterrupted
music is not its purpose. Status must describe the actually played cursor and
the current command state, rather than treating queued source reads as played
audio. Cancellation must retire the selected modeled action without allowing
an old completion token to satisfy a later request. Staged actions and
deliberately stale completion callbacks exercise the model; they do not
demonstrate interrupting an active retail DMA or SD transfer.

| Commands stage | Expected sequence |
| --- | --- |
| 1: STOP, PLAY and STATUS | Repeated STOP is harmless; play about one second of the left-only 440 Hz region and verify the played status |
| 2: seek validation | Refuse an out-of-range seek while preserving state, then seek into the right-only 660 Hz region and play about one second |
| 3: pause and resume | Pause twice, remain silent for one second with unchanged paused status, then resume the right tone from the actual played cursor for about one second |
| 4: seek while paused | Pause, seek to the stereo region while remaining paused, then resume about one second of 440 Hz left / 660 Hz right and stop |
| 5: EOF and loop controls | Reach finite EOF, refuse RESUME at EOF, enable a one-second stereo loop, pause/resume it, seek within it, then disable looping and reach EOF |
| 6: modeled cancellation | Replace queued PLAY actions with STOP or newer PLAY; refuse their old completion/observation tokens, then play a brief stereo segment and stop |
| 7: rapid restart | Four short stereo PLAY/STOP cycles, repeated STOP and checked stopped status |

Pass requires seven completed stages, zero unexpected failures, two expected
invalid/state refusals, four expected stale-token refusals and a maximum played
loop count of at least four. The final screen should also report **48 accepted
model commands, 27 completed command actions and 12 checked STATUS snapshots**.
An expected refusal proves that the selected invalid or stale request was rejected; it is
not an unexpected audio failure. The saved PAUSE frame is the actual source
playback cursor and may vary slightly with observation timing.

## What to return and when to stop

Send one final-screen photograph for calibration and one for commands. Include
the build/profile labels and all displayed counters. A brief note such as
“no obvious audio problem” or a stage/time for any noticeable glitch is useful.
Casual listening through the current TV setup is sufficient for these numerical
checks; precise channel separation can wait for clearly separated stereo
output. The earlier listening report remains a limited observation, not a
verified left/right or pitch measurement.

If a profile reports an unexpected failure, fixture/read error, guard failure,
or deadline refusal, photograph that screen and stop there. Record any loud
unexpected noise, repeated stale tone after an announced stop, or a hang with
its last visible stage. Power off to end the test. Do not repeatedly rerun a
failed profile or proceed to commands to work around a calibration failure.

After both tests, power off, remove the temporary `/KUI/runtime.kui`, and restore
the retained working runtime by renaming `/KUI/runtime-before-cdda.kui` back to
`/KUI/runtime.kui`. The harness requires a power cycle to exit; the old shell
has shut down.

## Source and fixture identity

Use the package's `build.json` and `source-url.txt` for the exact published
source/build identity. A new build is untested on the console until these two
profiles are run; host checks establish the implemented model and bounds, not
hardware audio quality or retail compatibility.

The reused `stereo.raw` is independently generated little-endian PCM16 stereo,
44.1 kHz, 12 seconds and 2,116,800 bytes. Its unchanged SHA-256 is:

```text
94b1b9e619e9be5a5dc5068d2fd9073eaf86d0c93e0b5b39a76108e5586f65ea
```

After reviewing both screens, use the matched endpoint data to investigate the
relative clocks and the command counters to select the next job-state test.
Retail work still requires its own command/service bridge and demonstrated
main-RAM, AICA and sound-driver ownership. A successful homebrew model does not
enable automatic retail CDDA dispatch.

To rebuild only these two profiles from the published source checkout, with
the SH-4 toolchain on `PATH` and pinned FatFs sources available:

```sh
python3 tools/test_cdda_host.py --sanitize
make -f Makefile.cdda CDDA_FATFS_SOURCE=/path/to/fatfs/source cdda-next-tests
```

The build target produces `build/cdda-calibration/cdda-harness.kui` and
`build/cdda-commands/cdda-harness.kui`. A source snapshot without Git history
also needs `CDDA_BUILD_ID=` set to the first twelve hexadecimal characters of
the published revision in `build.json`. Read the manifest rather than copying
an earlier test's build ID.
