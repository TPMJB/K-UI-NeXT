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
benefit is broader admission and integrated selection. Evolution 2 should
not become faster than the previously approved build.

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

## Test order and what to record

Start with Evolution 2 for a short regression, then test the other titles in
the table. All eight passed the joint source-family, actual SDK callback,
Maple transport and 384KiB allocator checks. Console boot, live startup
ordering, playback and save persistence still need your results.

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
