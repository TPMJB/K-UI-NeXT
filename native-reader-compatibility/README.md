<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native reader compatibility test sheet

This sheet tests two separate candidates. Start with the new Evolution 1/2
SCI Background PIO-pair candidate, then use the generic Standard/A2 package
for the wider title list. Each result belongs to its exact title, executable
revision, image format, reader and build. A Standard success does not establish
PIO-pair compatibility for that title.

The earlier Evolution 2 PIO-pair build, d0aac421584c, produced 2,230 checked
blocks during its capture, with zero cache-miss delivery visits, zero
abandonments and zero measured soft-window overruns. The user reported nearly
perfect movement. This new compatibility candidate adds reviewed Evolution 1
admission; Evolution 1 still needs its own console acceptance. The pair remains
limited to two fresh optional blocks per heartbeat, with timing admission for
the second block. Those measurements are not a worst-case execution bound.

## Packages and scope

Candidate checkpoint: **af401496961a**. The unified ZIP contains two installation
directories; each has its own installer, base module and tools.

| Route | Directory | Displayed build | Games for this batch |
| --- | --- | --- | --- |
| SCI Background PIO pair, virtual A2 | `reader-pair/` | `af401496961a` | Admitted Evolution 1 and Evolution 2 images |
| SCI Standard, generic virtual A2 | `native-a2/` | Record this separate module's own displayed build | Grandia II, Sonic Adventure 2, Power Stone 2, Skies of Arcadia; then fresh discovery for other native families |

Keep the two extracted directories intact. Both installers write the same
loader destination, `KUI/apps/games/retail-boot.kui`. Run the command from the
directory of the package being tested. Changing the Background menu setting
alone does not switch between the two installed modules.

Keep the successful d0aac421584c package available as a comparison. The new
candidate is the next test; the instructions do not require reinstalling an
older build before it.

## Preparation and result labels

Use the existing prepared p0005 card/profile in its current location. A
read-only copy of the complete p0005 folder to the computer provides a useful
checkpoint; do not move, rename, replace or re-prepare the original backing
image, journal, identity or profile for this matrix. Leave physical A2 empty.
The prepared virtual card should appear as A2. Start with one controller and
the same accessories used for the preceding successful run.

Use an empty, disposable in-game save slot for new-save tests. Name it clearly
where the game permits. Do not overwrite a required save on virtual A2 or
physical A1. If a game has only an occupied slot, lacks free space, or requests
formatting, record that condition and skip the write test. Loading an existing
save and observing A1 recognition remain separate checks. Do not delete saves
or accept a format prompt to make a test pass.

Record these once for each game/build combination:

```text
Game / region / revision:
Package route / displayed build:
Image path and format: original raw-2352 GDI / supported cooked-2048 / other
Install selection: evolution1 / evolution2 / fresh discovery
Transport / reader / speed / encoding:
Video mode: 50 Hz / 60 Hz / unknown
Physical A1 device / physical A2 empty:
Prepared profile path:
```

Use `PASS`, `FAIL`, `REFUSED`, `SKIPPED`, or `NOT RUN` for each numbered test.
A refusal is useful evidence of the admission boundary, not save/load success.
For every failure or refusal, photograph the complete first diagnostic screen,
including build, PHASE, DETAIL and any address/error rows. Note the earliest
visible symptom, SD LED behavior, and whether music continued while movement
or video stopped. Preserve the first result before changing a setting.

## Install the Evolution 1/2 PIO-pair candidate

Open a terminal at the extracted ZIP's root. For Evolution 2:

```sh
cd reader-pair
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2 --profile-runtime
```

For Evolution 1, remain in `reader-pair/` and install the same candidate with
its own mapping:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --profile-runtime
```

If the card has a different mount or prepared-profile path, substitute that
existing path. Keep `--profile-runtime` in every install command for this batch.
Safely eject and cold boot after installing.

Use **SCI**, **Background On**, **Normal speed**, **Auto/default encoding**,
**CD music Off**, and **RAM patches Off**. Leave the game's normal audio enabled.
Keep physical A2 empty. Expect **SDK SCI ROUTE READY / LEVEL 1**, followed by
**BOUNDED READ AHEAD READY / 192 KIB CACHE**, before gameplay starts. A different
executable revision may be refused; changing `--game` does not bypass admission.

Record roughly how long launch spends in bootstrap 2, executable/lifetime
verification, and the interval from the route-ready messages to the title
screen. Photograph any refusal instead of repeatedly launching it with
different speed or encoding settings.

## Long runs and diagnostic captures are different runs

The same `--profile-runtime` installation supports both. For ordinary long
play, do not hold the capture gesture. That allows the 5–10 minute movement,
transition and save checks to finish. Perform saves before any diagnostic
gesture, and wait for the game's save-complete indication.

For each admitted game, make one complete baseline diagnostic recording,
including when long play appears smooth. If a later run has a repeatable
problem, collect an additional complete cycle through that activity. For
each diagnostic run, enter the chosen area, hold both triggers fully
plus B for about one second, release, and continue the same activity. Collection
intentionally stops gameplay after roughly 20 seconds. This stop is expected;
the run cannot continue through later gameplay or save checks.

When reports appear, wait for **CRC VALIDATED PHYSICAL READ AHEAD**. Record one
complete cycle, stopping when that exact heading returns. Include the cache,
exit counters, all request timings, VMU timings and any failure/phase pages.
At 60 Hz, the preceding Evolution 2 runs cycled in about 66 seconds without
failure snapshots or about 74 seconds with separate FIRST/LAST snapshots.
Evolution 1 can include additional phase pages, and 50 Hz takes longer. Use the
repeated heading as the stop signal, rather than a fixed time limit. Repeated
pages show the same frozen capture.

On the cache page, retain both **PIO WORK MAX BAD** and **SECOND SKIP ONE MAX
OVER**. They distinguish block attempts, whole producer-visit time,
second-attempt admission, individual block time and measured soft-window
overruns. A larger cache-hit count alone is not a timing or compatibility pass.
After the report recording, power off before beginning the next run.

## Priority A: Evolution 2 regression and Evolution 1 acceptance

Do A01 first, then A02. If Evolution 1 reaches gameplay, complete its remaining
checks before repeating the same long-play cases on Evolution 2. Append the
game code to every result, for example `A04-E1` or `A04-E2`.

| ID | Priority and action | Record |
| --- | --- | --- |
| A01-E2 | Cold boot Evolution 2 once with the new compatibility candidate. Reach the familiar movement area and play for two minutes without the gesture. | Correct build and ready messages; boot timing; any regression from d0aac421584c. |
| A02-E1 | Install `--game evolution1`, cold boot, and reach the title screen and first controllable area. | Ready messages or full refusal; virtual A2 visibility; initial responsiveness. |
| A03-E1/E2 | On a fresh boot, watch the opening FMV or another available movie uninterrupted, then watch an in-engine cutscene/dialogue sequence. Do not skip it on this run. | Separate FMV and engine-scene results; audio continuity, sync, freezes, and whether the scene recovers. Mark a type unavailable if the chosen sequence has none. |
| A04-E1/E2 | Play a familiar area continuously for 5–10 minutes. Move, turn, interact and use ordinary menus. Do not start capture yet. | Approximate hiccup count, longest visible pause, input response, and whether the problem grows with time. |
| A05-E1/E2 | Make at least ten ordinary room/area/battle transitions, including returning to an earlier area where possible. | Which transition failed or slowed; repeatability; music during loading. |
| A06-E1/E2 | Travel a short route forward, immediately return, then repeat it three times. Re-enter a previously visited room/scene; revisit the same menu or dialogue. | Sequential versus reverse/repeated access; first pass versus later passes; any stale, missing or corrupt assets. |
| A07-E1/E2 | Before starting a diagnostic, load a known existing A2 save and verify a recognizable location, character, inventory or progress marker. | Correct card and save selection, loaded progress, and any unexpected card prompt. |
| A08-E1/E2 | If a disposable empty slot is available, create a new A2 test save after a recognizable progress change. Wait for save completion, then another ten seconds. Continue playing for two minutes. | Test slot/name, save-complete indication, progress marker, post-save streaming/input behavior. Otherwise report `SKIPPED` and why. |
| A09-E1/E2 | After A08, fully power off, wait ten seconds, cold boot the same module/settings, and load only that disposable A2 test save. | Exact saved marker restored; card detection; no formatting or recovery refusal. Do not overwrite another slot to repeat it. |
| A10-E1/E2 | Power off, attach the usual physical VMU in A1, leave A2 empty, and cold boot. Confirm A1 remains recognized while the virtual A2 save remains selectable. Play for five minutes. | A1 detection/LCD behavior where the game uses it; A2 load; any accessory conflict. Do not write a required A1 save. |
| A11-E1/E2 | After the long checks, make the required separate baseline diagnostic run through the same movement/transition route. Trigger capture only after reaching that area. | Full CRC-heading-to-repeat recording and a short description of the activity during its 20-second window. Repeat for any later reproducible problem. |
| A12-E1/E2 | Repeat a successful cold boot and A2 load twice more, fully powering off between runs. | Three-boot consistency including the earlier run; earliest inconsistent phase. |
| A13-E1/E2 | Only if the loader already supports both supplied variants of this exact revision, repeat A02/A03/A06/A11 using the legitimate cooked-2048 variant after the original raw-2352 GDI. | Format of each run, admission, playback, access pattern and full report. Do not rename tracks or alter sector declarations to force acceptance. |

Do not erase a failing result after a successful retry. Report both, with the
one changed variable. If an existing lifetime/reset guard stops a return to a
menu, photograph it and report the menu action; a full power cycle is the next
independent run, not evidence that the in-game reset path passed.

## Priority B: broader titles through the separate Standard/A2 package

Open a terminal at the extracted ZIP's root and install the **generic
Standard/A2** route with fresh discovery:

```sh
cd native-a2
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --profile-runtime
```

Omitting `--game` requests a fresh SDK-family scan. This is the generic
Standard route; it does not install the PIO-pair interrupt route for these
titles. Use **SCI**, **Standard / Background Off**, **Auto/default encoding**,
**CD music Off**, and **RAM patches Off**. Safely eject and cold boot. Keep
physical A2 empty and use A1 only for coexistence checks.

The Standard timing report does not have the read-ahead **CRC VALIDATED** page.
For each admitted game, collect its required separate baseline diagnostic
cycle. Use the same gesture and continue for roughly
20 seconds until the intentional stop. Record from its first report heading
(normally **NATIVE VMU LATENCY CAPTURE**) until that same heading returns,
including any phase pages. Keep `--profile-runtime` installed for its ordinary
long runs too, without triggering the gesture during those runs.

Run these in order. An older successful Standard result is background context;
each listed build/revision still needs this batch's own outcome.

| ID | Title | Concrete ordinary run before the separate diagnostic |
| --- | --- | --- |
| B01 | Grandia II | Cold boot; watch an available movie and an engine dialogue/scene; play for ten minutes with several area/battle transitions; revisit the same route; perform the safe A2 save/power-cycle/load sequence. |
| B02 | Sonic Adventure 2 | Cold boot; watch the opening; play a familiar stage for ten minutes, including movement/camera changes and several loading transitions; repeat a stage or section through the game's ordinary menus; perform safe A2 persistence checks. |
| B03 | Power Stone 2 | Cold boot; play several matches/arenas for ten minutes with repeated character/menu/arena loads; check available movie playback; perform safe A2 persistence checks. |
| B04 | Skies of Arcadia | Cold boot; watch the opening; play for ten minutes across available town/field/ship/scene transitions; revisit a route; check physical A1 recognition/LCD where used while loading virtual A2; perform safe A2 persistence checks. |

For each B test, record individual outcomes for boot, A2 recognition, movie,
engine scene, movement, transitions, reverse/repeated access, save, complete
power-cycle/load, physical A1 coexistence and diagnostic capture. Mark an
unavailable sequence `SKIPPED`; do not infer it passed from another sequence.
Disc switching in Skies is a separate test and is not required for this batch.
Collect another complete diagnostic cycle for any later reproducible failure.

If cached discovery is compared after a successful fresh scan, use only a
mapping actually listed by that package's `--list-games`, record that change,
and retain `--profile-runtime`. A cache record is tied to an exact executable;
it cannot authorize an unknown revision or a different reader route.

## Priority C: unknown native families and useful refusals

Use the generic **Standard/A2** installation above, without `--game`, for these
probes. Do not install the Evolution PIO-pair module and substitute another
title's cached selection. These families may stop at a supported-admission
boundary. A clear refusal is an expected possible outcome and supplies the
next adapter's evidence; it does not establish that virtual A2 works there.

| ID | Probe | First objective |
| --- | --- | --- |
| C01 | Blue Stinger | Original native GDI, fresh scan; record the exact discovery/admission outcome and first screen reached. |
| C02 | Crazy Taxi 2 | Fresh scan; capture any allocator/initializer-family refusal rather than treating a discovered allocator as complete admission. |
| C03 | Jet Grind Radio / Jet Set Radio, identifying the actual region/revision | Fresh scan; record layout/terminal/lifetime admission or the full refusal. |
| C04 | Phantasy Star Online, identifying the exact version | Fresh scan; record whether layout and terminal-entry discovery complete; no online-service test is required. |
| C05 | Another available native family, such as Time Stalkers, Silver, Soul Reaver or RECV | Confirm the original disc's native classification, then collect its exact outcome. Prior title-specific success does not imply generic-family support. |

If a probe is admitted and reaches gameplay, first check A2 recognition and an
existing save load. Continue with the B-style movement/transition and safe
persistence checks when available, and collect one complete Standard baseline
report cycle. Record an admitted boot separately from a
completed save/load result. If refused, stop that run at the diagnostic screen;
do not disable ownership checks, change a declared sector size, or format a
card to bypass the refusal.

Collect the original startup for the same refused region/revision. From the
generic package directory, using its included collector:

```sh
python3 collect-native-startup.py /path/to/game.gdi --output Game-startup.zip
```

Use a new output name for each image, for example `Crazy-Taxi-2-startup.zip`.
The collector reads the original native raw-2352 GDI and extracts its boot
metadata/executable, not the whole disc. It does not modify the input tracks.
For a cooked-2048 console test, collect from that revision's original raw GDI.
Send the refusal photo, startup ZIP, image/revision and test ID together.
If the collector identifies Windows CE, record that classification and use
the separate CE investigation; do not force the native collector to accept it.

## CE, CDDA and other transports

Windows CE, CDDA-enabled playback, physical-disc service, SCIF and IDE are
separate routes. This batch makes no compatibility claim for them. Keep their
existing packages/settings and results separate. Turning CD music Off is part
of these native SCI tests; a successful run does not validate combined CDDA
and virtual-VMU operation.

## Copyable result record

One compact record per ID is enough; append a video/photo filename where
relevant. Include `SKIPPED` checks rather than omitting them.
The supplied `RESULTS.csv` has matching IDs; its Capture column should contain
the required baseline recording for A11, each admitted B/C game, and additional
failure recordings where needed. A refused boot uses its photo/startup bundle
instead because it cannot reach the runtime gesture.

```text
ID / game / revision:
Route / build / install selection:
Image format / 50-or-60-Hz / physical A1:
Result: PASS / FAIL / REFUSED / SKIPPED / NOT RUN
Boot / A2 recognition / existing-save load:
FMV / engine scene / movement duration:
Transitions / reverse-repeated route:
New-save disposable slot / saved marker:
Full power-cycle reload / physical A1 coexistence:
Capture heading repeated? / recording filename:
First symptom or PHASE+DETAIL / audio continuing? / SD LED:
Changed variable on retry / retry outcome:
```

The minimum first return is A01-E2, A02-E1, one complete Evolution 1 long run
if admitted, and its full diagnostic cycle. The rest is an ordered backlog:
finish the admitted Evolution checks, then B01–B04, then the C-family probes.
Report an early refusal immediately; the independent Standard tests can still
continue without waiting for a new PIO-pair family adapter.
