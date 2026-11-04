# Toy Commander and Armada: five focused runs

This replaces the earlier 35-run plan. There are **five runs**, using one
recognizable bad movie scene per game. Keep your existing image and format;
the supplied raw/cooked Toy modules support that choice, not an extra format
comparison. No Power Stone, native VMU, save, cooked-conversion or mute tests
are requested.

The questions are whether Toy's accumulated diagnostics cost useful game
time, and whether the remaining movie problem points toward physical read
supply or time spent inside the loader. Armada's observation also identifies
the actual guest read mode before proposing a PIO/deferred-completion change.
Only Toy TB removes the old trace, data-probe and request-history hooks. It
retains the reader's existing budget/transport counters and includes disabled
observer entry checks, so TA versus TB measures that actual candidate rather
than the isolated cost of every historical counter. The new observers do not change
scheduling, quotas, audio output or completion semantics.

## Copy one folder at a time

With the console powered off, retain a copy of your current KUI folder, then
merge `00-control/KUI` onto the SCI card, replacing matching files. This set
contains only the Toy and Armada CDDA test modules. It contains no menu,
native Background, Standard VMU or prefetch module, and makes no native
memory-reservation change.

Keep the existing menu. If either old exact Toy module below exists, move it
into the PC backup before testing:

- `KUI/apps/games/cdda-toy-raw.kui`
- `KUI/apps/games/cdda-toy-cooked.kui`

The normal menu's existing missing-exact-module fallback then selects the
supplied checked `cdda-native-toy-*` adapter. This is needed because an old
exact module otherwise takes priority. Do not rename the supplied files.
Restore the two old files from backup when testing is finished. A menu from
the previous FMV suite already selects these adapters directly.

For each later row, power off, then merge just that numbered folder's `KUI`.
Restore `00-control/KUI` before switching from Toy to Armada. No installer,
settings reset, save preparation or game conversion is needed.

| Run | Profile | Folder | Purpose |
| --- | --- | --- | --- |
| 1 | TA | `00-control` | Toy accepted control, with its existing diagnostics |
| 2 | TB | `01-toy-clean` | Same Toy reader with legacy trace/probe/history hooks removed |
| 3 | TC | `02-toy-observer` | TB plus the bounded recent-window observer |
| 4 | CA | `00-control` | Armada accepted control |
| 5 | CB | `03-armada-observer` | Same Armada reader plus the bounded observer |

Each profile has its own full twelve-character module ID in `build.json`.
The final two hexadecimal characters are TA=`10`, TB=`11`, TC=`12`, CA=`20`
and CB=`21`. Record the full displayed ID; the menu's ID alone does not
identify the game module.

## Use the same bad scene

Use SCI, the same game revision/image, display mode, controller and TV volume.
Cold boot every run. Keep speculative prefetch Off.

| Game | Background loading | CD music | Virtual VMU |
| --- | --- | --- | --- |
| Toy Commander | Off | On | Off |
| Armada | On | On | Off |

For Toy, choose the same troublesome movie in all three runs and state whether
it is the first or second intro. Do not test both. For Armada, use the same
part of the opening movie with the hard-to-hear sound. Gameplay is not the
test window. A pre-CDDA movie is still useful; there is no requirement to
force music playback or produce a positive CDDA counter.

For each run, make one phone recording:

1. Start before launch so the game module's startup ID is visible.
2. Let the chosen bad movie section play naturally for about 20–30 seconds.
   Use the same recognizable visual landmarks each time. If the bad section
   is shorter, record all of it and stop before the following menu or load.
3. While that section is still playing, press **A+B+X+Y+Start** to halt on the
   report. Do not press Skip first. There is no separate arming gesture.
4. Keep filming the report. For TA/TB/CA, capture its identity and initial
   page clearly. For TC/CB, capture a complete cycle of the new recent-window
   pages, including the summary and clock/validity information; allow about
   65 seconds and continue until the first page returns.

The observers retain recent activity automatically. Do not use L+R+B or any
other movie-time arming chord. The halt gesture ends the run; power off to
exit. If a profile stops before the report, retain that screen in the clip.

Use `00-control/KUI` when returning to a test control. When all tests are
finished, restore your pre-test KUI backup to return to your previous setup,
including any newer work from other test branches. These are temporary test
overlays; they do not replace a production release.

Send the five clips labeled **TA, TB, TC, CA, CB**, with the chosen scene and
whether picture/sound changed. There is no need to transcribe hexadecimal
values, fill a spreadsheet or run a computer script.

## What changes the next decision

| Comparison or observation | Consequence |
| --- | --- |
| TB improves over TA | Removing legacy diagnostic work is useful; retain that production simplification before further scheduling changes. |
| TB behaves like TA | The old diagnostics are not the main explanation for this scene. |
| TC behaves like TB | The new observer is usable within the visible comparison; inspect its recent-window data. |
| TC changes the scene materially | Treat its timings as a perturbed workload and reduce observer cost before drawing a performance conclusion. |
| CB behaves like CA | Inspect the actual CE read modes, physical progress and outer-call costs. |
| CB changes picture or sound materially | Calibrate the observer before treating its data as representative. |
| Demanded reads occupy most available wire time | Prioritize physical traffic/redundancy and supply limits; another small callback quota is unlikely to create a large throughput gain. |
| Loader body time is substantial while physical supply has headroom | Prioritize CPU/interrupt availability and completion scheduling. |
| Armada actually uses the synchronous guest PIO path, with long bodies | Investigate a correct deferred-completion implementation for that path. If the path is absent, do not optimize it for this movie. |

These are investigation branches, not automatic diagnoses. Physical bandwidth
and CPU availability can both constrain the same scene. An inactive CDDA
worker rules out its current work as the immediate cause; it does not measure
the game's own decoded movie audio.

## Reading the recent-window report

The observer uses a bounded rolling history, designed as eight two-second
bins, rather than treating all boot-to-stop work as movie work. The phone
clip identifies which part of the movie overlaps that history. The oldest
bin may be partial; a stopped clock or a long gap can leave empty/invalid
bins. Empty bins do not prove that nothing happened.

An outer call can cross a bin boundary. Its body duration and its progress
counter deltas may be charged at different observation points, so one bin's
bytes divided by that bin's body time is not an independent throughput
measurement. Interpret adjacent bins and the complete valid window together.
The gesture interval is the end of observation, not evidence of a new movie
stall.

Clock validity is reported explicitly. Invalid clocks still leave useful
facts such as command mode, request sizes, progress and error counts; they
do not support CPU-time or bandwidth-per-second claims. Raw TMU modulo counts
are lower bounds where labeled, not exact long interrupt-mask durations.
Outer-body time excludes work outside its stated measurement boundary and
includes observer overhead. Nested GD/IRQ spans must not simply be added.
IRQ-delivered block share is not CPU usage or proof of useful decoder time.
The reports can identify costly masked loader work and the active read path;
they do not independently measure decoder execution or prove the cause of an
audio problem. Correlate them with the recorded picture and sound.

No console improvement is claimed by compiling or passing host checks. The
next change should follow the five clips and a valid, engaged observation
of the chosen scene, rather than another broad matrix.
