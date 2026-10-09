# Toy Commander: SCI cache comparison

This ZIP contains two experimental Toy Commander runtimes and the exact
retained clean-audio fallback, build `7b55156aafa2`. It tests whether isolating
our writable state lets the game keep its original copy-back cache policy.
Console testing must establish whether this helps video or preserves audio.
No speed improvement has been measured for either new runtime.

| Profile | Private writable state | Game cache policy | Expected report word |
| --- | --- | --- | --- |
| R: `profiles/R-private-write-through.kui` | P2 uncached | Retained write-through override | `00000101` |
| C: `profiles/C-native-copy-back.kui` | P2 uncached | Original copy-back policy | `00000105` |
| Fallback: `fallback/7b55156aafa2-retail-boot.kui` | Exact retained build | Exact retained build | `00000101` |

R and C use distinct build IDs recorded in `build.json` and displayed on the
terminal report. Both keep SCI, two-sector GD steps, the existing read
protocol, eight audio blocks, existing channel polling, original ARM driver,
and current AICA transfers. Shared asynchronous SCI and queued CDDA are off.
The experiment adds neither ARM firmware changes nor AICA DMA.

## Install R first

1. Power off the Dreamcast and keep a copy of the currently installed
   `/KUI/apps/games/retail-boot.kui`.
2. Copy `profiles/R-private-write-through.kui` to
   `/KUI/apps/games/retail-boot.kui`, replacing that file. Keep the existing
   launcher, SCI card-path settings, GDI, track files and driver settings.
3. Safely eject the card and cold boot. Run the intro, continue into gameplay,
   and listen to both CD music and sound effects. Use the same sequence for C.
4. Capture the terminal report pages and R's displayed build ID. Page 4's
   second row begins with pause-detail, pause-max-attempts, cache-policy and
   service-visits; cache-policy should be `00000101`.

## Then install C

Power off, replace the same installed file with
`profiles/C-native-copy-back.kui`, safely eject, and cold boot again. Repeat
the same intro and gameplay sequence. Page 4's cache-policy word should be
`00000105`. Record video stutter, speed, music interruptions, sound-effect
interruptions, loading failures and the report pages alongside its build ID.
If R itself regresses, restore the fallback before proceeding with C.

Useful comparisons include the measured GD read ticks, raw-sector read
ticks, data-blocked intervals, recovery counters and reserve-proof report
fields. Lower timings alone do not establish smooth video or correct audio;
the listening and gameplay result decides whether the profile is useful.

## Restore the retained audio build

With power off, copy `fallback/7b55156aafa2-retail-boot.kui` to
`/KUI/apps/games/retail-boot.kui`, safely eject and cold boot. Its displayed
build ID is `7b55156aafa2`. The complete original rollback delivery is also
included as `fallback/K-UI-Toy-Audio-Rollback.zip`.

The ZIP contains no game tracks, game executable, driver image or launcher.
`build.json` records source, runtime identities, controlled flags and linked
audits. `source-snapshot.tar` contains the exact committed source for R/C;
the fallback has its exact corresponding source archive. Licence notices
are included. `SHA256SUMS` covers every other ZIP member. The audits verify
packaged layout and authored code constraints; they do not establish a
console performance result.
