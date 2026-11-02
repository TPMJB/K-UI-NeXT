<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native session controls: hardware test draft

No physical-console result is claimed for this candidate. This protocol
separates library UI acceptance, return to K-UI, and a game's actual disc
handoff. Host protocol tests, linked placement checks and successful synthetic
disc transitions are prerequisites, not substitutes for these observations.

## Prepare

Back up the SD card's two native modules, prepared virtual card and journal
files, and `KUI/games-library/activity-a.bin` / `activity-b.bin`. Keep the same
game images and prepared cards used for the existing compact build.

Install the normal root `KUI/` update first. Then opt in by merging
`OPTIONAL-CONTROLS/KUI/` into the SD card's `KUI/`, replacing:

* `KUI/apps/games/retail-native-dual.kui` — default virtual A2.
* `KUI/apps/games/retail-native-dual-a1.kui` — virtual A1.

The runtime alone does not install the session-controls worker. Use matching
runtime and module builds. Safely eject, cold boot, and record the displayed
build identifier. Start with virtual A2; repeat the successful sequence with
A1 separately. Leave the corresponding physical slot empty.

Use **SCI / Background On / New (X) / Auto encoding / CD music Off / RAM
patches Off / virtual VMU On**. Do not use VMU Off as a controls test. Preserve
the ordinary compact modules for comparison and restore them to opt out.
No new card formatting or save reset is part of this protocol.

Record the catalogue's ordered disc paths and each disc's product, region,
revision and raw/2048-byte form. All members must pass launch preparation as
one checked set. If preparation refuses identity, fragmentation or mapping,
record the complete message and stop that set's test. Do not rename discs to
hide a refusal.

## 1. Verify the K-UI update

Before launching a game:

1. Check Games shows **All games**, **Favorites**, and **Recently played**.
   Press Y through all three tabs.
2. In All games, use X to favorite a game. Check its star and Favorites row.
   Change layout through START → Library options and confirm the tab and
   selection survive. Remove and restore a favorite, then reboot once to
   check persistence.
3. Confirm scanning and rebuilding are in Library options. Check Settings has
   no screenshot description row.
4. Hold L+R and press Y in K-UI. Verify a new 640 × 480 BMP appears in
   `KUI/screenshots` and opens correctly on a computer. Release Y between
   captures.

## 2. Verify intentional return

Use a game already known to launch and save with the matching compact
SCI/New module. A single-disc launch is sufficient for the first return.

1. Reach a stable game menu or safe gameplay point. Make an ordinary save,
   let the game finish, and verify it can load that save normally.
2. Hold **L+R+A+B+X+Y**, without Start, until the intentional diagnostic page
   appears. The threshold is 180 completed controller polls; it is not a
   five-second timer. Photograph the screen before continuing.
3. Release the controls. Check the page offers **START RETURNS TO K-UI**,
   then press Start once. Record any visible loading message, elapsed time,
   and the first K-UI screen reached.
4. In K-UI, verify controller navigation, Favorites, recent history and the
   screenshot shortcut. Launch the same game again and load the saved
   progress. A title-screen return alone does not prove save durability.
5. Repeat once from another safe point. Record a return-unavailable/refused
   message as a separate result rather than a successful reload.

Unexpected fault pages must remain available to photograph. Do not expect
Start or a timeout to dismiss them. Do not induce a fault by removing the SD
card, changing files during play, cutting power during a save, or corrupting
the runtime.

## 3. Skies of Arcadia, disc 1 → disc 2

Use a matched two-disc set and an existing save near the real disc boundary.
First verify both discs can launch individually under the existing compact
SCI/New profile and that the save is readable. This distinguishes an image
or base-reader problem from the new transition.

1. Opt in, open the grouped catalogue row and select disc 1. Confirm session
   preparation accepts the entire group. Launch and load the boundary save.
2. Play to the game's actual **insert disc 2** request. Let ordinary save
   activity finish. Photograph the request and note the starting disc.
3. Hold **L+R+Start**, with A/B/X/Y released, across 90 completed controller
   polls. Release the combination. Allow time for the game to observe the
   open lid and accept disc 2; do not repeatedly issue the shortcut.
4. Record whether the request clears, the expected scene begins, audio/video
   remain correct, and gameplay continues. Check that the same virtual VMU
   is still visible and that a new game save can be loaded.
5. Use intentional return, relaunch disc 2 from K-UI, and load the new save.
   Record this independently from the in-game disc transition.
6. If the game offers a legitimate request for disc 1 again, test the wrap
   from disc 2 to disc 1 there. Otherwise mark reverse handoff untested.

Switching maps at an arbitrary title screen does not establish that the
game's real disc-change protocol works. A visible lid transition alone is
also insufficient.

## 4. Resident Evil Code: Veronica

After a successful Skies disc 1 → disc 2 handoff and return/save checks,
repeat the same sequence with a matched Code: Veronica two-disc set and a
save at its real disc boundary. Use the game's own disc request and verify
the resulting scene, VMU save, return, relaunch and reload.

The current admission rule requires equal product, region and revision
metadata. If this set's real metadata differs and preparation refuses it,
report that result. It is a limitation of this candidate, not a reason to
edit the GDI or claim the title passed. A1 is a separate follow-up after the
default A2 sequence succeeds. Four-disc sets and other titles follow only
after these two-disc results; the four-disc bound is not a compatibility
claim for Shenmue or any other game.

## Report

For each result include:

| Observation | Record |
| --- | --- |
| Build and install | Runtime build ID, optional A1/A2 module, clean boot or returned session |
| Route | SCI, New, encoding, CD music, RAM patches, VMU On and selected virtual slot |
| Images | Ordered full paths, region/revision/product metadata, raw or 2048-byte form |
| Starting state | Selected disc, save used, exact game scene and disc request |
| Shortcut | Disc or return combination, whether controls were released, apparent hold duration |
| Transition | Screen messages, lid recognition, requested disc accepted, audio/video and elapsed time |
| Storage | VMU still visible, save completed, subsequent relaunch/load result |
| Failure evidence | Complete preparation message or photographs of every diagnostic page |

Keep failures grouped by stage: preparation, gesture recognition, virtual
lid observation, disc acceptance, return reload, and save/reload. Report
frame pacing or FMV regressions against the restored matching compact
module with the same game, card, route and scene.
