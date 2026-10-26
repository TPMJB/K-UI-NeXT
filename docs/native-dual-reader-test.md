<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native SCI New (X) / Legacy (Y) and virtual A2 candidate

Candidate **@BUILD@** combines the approved PIO read-ahead policy with
structural SDK discovery and the generic virtual-A2 loader. It installs a
matching Games UI and a separate native X/Y module. It does not replace the
ordinary, CE, CDDA or compressed-image loader files.

X selects the new reader; Y selects the legacy background transport. These
are the reader names under Advanced options, not new controller hotkeys:
the physical Y button still opens Options and X still inspects a game.
Both X and Y reserve 384KiB for this candidate's VMU/reader state. Y leaves
the extra read-ahead space unused; it is a transport comparison with the
same reservation, not a historical 192KiB-memory-footprint comparison.

The new reader is unchanged as a performance policy: at most two checked
physical blocks per eligible heartbeat, with timing admission for block 2,
SCI priority 1 and the existing CRC/loan/ownership limits. This candidate's
reader retains the eight software candidates and integrated selection from
the previous candidate. The follow-up changes are described below; Evolution 2
should not become faster than the previously approved build.

## Current console results and follow-up

The `f721551450e3` follow-up stopped before game startup for Evolution 2,
Lodoss and Rayman 2. The readable report shows stage `2`, session `-6`,
filesystem `-4` and filesystem detail `0x0f`: the shared exFAT boot-header
gate. That screen did not expose which header field failed. These are
console failures; the earlier software passes do not override them.

This candidate allows the exFAT VolumeDirty status bit to proceed through
the complete ownership scan. It preserves that bit and does not repair or
rewrite the boot header. Both boot checksums, volume geometry, current
prepared-file maps, allocation bitmap, namespace and cross-link checks are
still required before journal recovery can write. Media-failure and active-FAT
flags remain refused. A remaining preflight refusal now shows the exact boot
reject mask, flags and actual/expected geometry on one complete screen.
The owner subsequently confirmed the dirty-status bit caused this shared stop:
fsck reported the filesystem clean, and the old build passed preflight after
the status was cleared. Console confirmation of the new binary is still required.

On `af9e01c8a271`, Evolution 2 behaves as expected and Grandia II is flawless
according to the owner. Sonic Adventure 2 and Power Stone 2 are also reported
flawless. Skies stops at VMU guard `0x47`; Lodoss stops at guard `0x03`.
Evolution 1 reaches MPEG Sofdec and then black-screens before its opening movie. Treat Evolution 1
as a known failure in that candidate. Its earlier software admission pass
was not a console boot result. Rayman 2 freezes after bootstrap 2 with only
the build header. The initial eight-title round is complete; DOA2 admission is being investigated separately.

The previous follow-up added a repair for Lodoss's verified terminal-allocator table transition.
The original default allocator, reserved lease, full free-list guards and
unknown-table refusals remain required. The new terminal table is accepted
only while its checked fatal-handler hook is installed. Console confirmation
of this repair is still needed.

It also combines the UI's executable CRC and native VMU source read
into one pass where possible. It retains the full-file checks and the
stage's independent verification. It also exposes the already compiled
Evolution 1 trace through the matching two-file installer. The new SCI
reader policy and read-ahead producer are unchanged. The VMU worker's failure
pages now distinguish the individual comparisons behind guards `0x47` and
`0x03`, retaining the existing refusal and publication rules. Evolution 1
and Rayman still need their next captures; this is a Lodoss repair, one-pass
launch-preparation and diagnostic candidate. Console launch times have not yet
been measured.

## Install once

Run at the extracted ZIP's root:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --profile-runtime
```

Use your existing prepared profile; substitute its path only if different.
No `--game` selection is needed. The prepared card, journal, identity,
launch presets and game images are preserved. This profile selects the
same virtual 200-block A2 card for every tested game; it does not create a
separate card for each title. Leave physical A2 empty. Physical A1 remains
available. Use an unused save slot for new-save checks.

Safely eject, cold boot, and select **SCI / Background On / New (X) /
Auto encoding / CD music Off / RAM patches Off** for each game. In Advanced
options the old speed row is now **Background reader**. Saved X settings
select New; saved Y settings select Legacy. Background Off remains the
existing Standard path and is not the combined-reader test.

The candidate refuses non-SCI, unsupported boot encoding/CD images, altered
SDK contexts and maps needing more than 64 combined track/extent slots.
It reports a refusal rather than quietly changing to a different reader.
Unknown native SDK families remain experimental and can refuse safely.

## Skies and Lodoss guard captures

Retry these two titles with New (X). If either stops, photograph the complete
screen including DETAIL and AT/VALUE/EXPECT/CHECK/LEASE. The new CHECK value
identifies which comparison failed; the previous screenshots retained stale
values from earlier successful operations at these particular guards.
This instrumentation does not declare either game fixed or relax its checks.

## Rayman 2 cold-start capture

Record launch through the freeze. The follow-up stage prints numbered cold
checkpoints around verification, heap allocation, worker setup and reader
binding. Capture the last visible checkpoint, or the complete stop screen.
These labels add no per-frame reader work.

## Evolution 1 startup capture

After the other title tests, first retry Evolution 1 with **Legacy (Y)** on
the same build and prepared A2 card. Record whether it passes MPEG Sofdec.
Then install the existing exact-image trace mode and switch back to New (X):

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --evolution-trace
```

Launch Evolution 1 with New (X) and record startup through the failure. The
trace can show a small hexadecimal checkpoint strip while the game owns a
supported display surface. If blue trace pages appear, record a complete
cycle, ending when the first heading repeats. This trace stops automatically
only if an intercepted guard or its reviewed wait-loop limit fires. It does
not provide the profile mode's trigger+B capture gesture. If it stays black,
keep the startup video showing the last visible checkpoint strip and record
whether sound continues and whether the SD-reader light stays on. A report's
saved exception state is a checkpoint, not automatically the fault PC.

This exact Evolution 1 trace is for diagnosis, not performance comparison.
Before continuing the other games, reinstall with the `--profile-runtime`
command above. No new card preparation or image conversion is needed.
Your results on the earlier `af9e01c8a271` build remain useful.

## Priority retries for this follow-up

1. Evolution 2: first confirm it passes the shared virtual-card preflight;
   then check the familiar movement route and an FMV for a short regression.
2. Lodoss: retry startup. If it boots, test movement/transitions, A2 load, an
   unused-slot save and cold-boot reload. Capture any new stop screen in full.
3. Skies: retry New (X) and capture the full new guard report if it stops.
4. Rayman 2: record startup and the last numbered cold checkpoint or stop screen.
5. Evolution 1: compare Legacy (Y), then record New (X) with the exact trace mode
   above. Return to `--profile-runtime` before performance comparisons or other games.
6. Run the computer-side DOA2 filesystem probe at the end of this guide.

The earlier four successful titles are the regression baseline. Their results
remain recorded in RESULTS.csv under `af9e01c8a271`. This follow-up has not yet
been run on the console. Do not replace a failure entry with a software-only pass.

## Broader checks after startup succeeds

The eight joint source-family, actual SDK callback, Maple transport and
384 KiB allocator checks remain required. They do not override a console
failure. Use the following longer checklist once the startup retries pass.

| Order | Title | Main exercise |
| --- | --- | --- |
| 1 | Evolution 2 | Familiar movement area and opening engine sequence |
| 2 | Evolution 1 | Opening, movement, area/battle transitions |
| 3 | Grandia II | Movie, engine dialogue, field/battle transitions |
| 4 | Skies of Arcadia | Opening, town/field/ship transitions, physical A1 |
| 5 | Power Stone 2 | Repeated matches, menus, character/arena loads |
| 6 | Sonic Adventure 2 | Opening, stage/camera movement, repeated stage loads |
| 7 | Rayman 2 | Opening, sustained movement, level transitions |
| 8 | Record of Lodoss War | Opening, movement/combat, repeated area loads |

For each title on New (X):

1. Record title/revision, raw 2352 or cooked 2048 format, build and settings.
2. Cold boot; record any refusal screen in full, including PHASE/DETAIL.
3. Watch an available FMV without skipping it; note sound sync or repeats.
4. Watch an in-engine scene/dialogue separately from the FMV.
5. Play continuously for 5–10 minutes, moving, turning and using menus.
6. Make at least 10 room/area/battle transitions, returning to old areas.
7. Repeat a short forward/backward route three times to exercise seeks.
8. Load an existing A2 save and verify recognizable progress.
9. Make a clearly named disposable save in an unused slot; wait for completion
   and continue playing for two minutes. Skip writing if no unused slot exists.
10. Power off completely, wait 10 seconds, then cold boot and reload that save.
11. Repeat with physical A1 attached and A2 physically empty; check both slots.
12. Repeat the cold boot/A2 load twice more to check consistency.
13. Make the required separate diagnostic capture described below.
14. Repeat an already supported alternate image format if available; do not
    alter sector declarations or force an unsupported image to launch.

Then switch **Advanced → Background reader → Legacy (Y)** and repeat the
same movement/scene route, A2 load/save/power-cycle check and diagnostic
capture. Record X and Y separately in RESULTS.csv. A slower Y result is
useful comparison evidence; do not discard an X failure after a successful Y run.

The 26-program analysis and first refusal gates are included in the evidence
folder. The other 18 supplied programs currently fail at least one reader
or VMU family gate. Do not spend a long playback test on a title that refuses
at startup; photograph the full screen. Other titles/revisions can be tried
with the same installer and settings: admission is structural, not a title list.

## Captures are a separate run

Keep `--profile-runtime` installed. Long play needs no capture gesture.
After completing save/persistence checks, enter the test area, hold both
triggers plus B for about 1 second, release, and continue the same activity.
The capture intentionally stops gameplay after roughly 20 seconds.

For New (X), record **CRC VALIDATED PHYSICAL READ AHEAD** through the next
appearance of that same heading. Include every intervening page. Use the
repeated heading as the end signal, not a fixed 90-second duration.
For Legacy (Y), record from the first repeating report heading through its
next appearance; it has no active read-ahead cache page and its legacy
read/age counters are deliberately not interpreted as request durations.
Power off after recording before starting another run.

Report approximately how many visible hiccups occurred, the longest pause,
whether music continued, whether a movie recovered, and the first action that
reproduced a failure. The single biggest priority is verifying that New (X)
keeps Evolution 2's approved behavior and extends it to the other seven titles.

## DOA2 filesystem probe

DOA2 is not yet admitted by this virtual-VMU candidate. Its application uses
fixed sound/graphics/resource partitions rather than the reviewed allocator.
The provided metadata probe reads the filesystem and exact startup identity
without modifying the SD card or copying game payloads into its output.

From this extracted folder on your computer, run:

```sh
python3 kui-doa2-filesystem-probe.py /run/media/apollonius/Dreamcast --output DOA2-filesystem-metadata.zip
```

Upload the resulting ZIP with the other test results. If more than one exact
DOA2 image is found, use the `--gdi` form in DOA2-PROBE.md to select the image.
The metadata helps prove the resource partition bounds; generating it does
not enable the new reader or virtual VMU for DOA2.
