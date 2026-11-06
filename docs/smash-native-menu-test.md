# Smash Pack normal-menu Genesis save test

This integration starts Sega Smash Pack's original menu and carries its exact
Genesis selection into a fresh native engine launch. Phantasy Star II and
Shining Force use the same existing shared Smash Pack virtual card in A1.
The Genesis engine's controller settings have their own save file on that card.

## Install and launch

1. Copy the release ZIP's complete top-level `KUI` folder onto the SD card,
   replacing the matching files together. Keep the existing card backing files
   and the same original Sega Smash Pack image.
2. Remove physical VMUs. Enable Virtual VMU. Select Native/Default boot and
   leave background reading, CD music and RAM patches off.
3. Launch Sega Smash Pack normally. Select **Phantasy Star II** in the original
   Smash Pack menu. Use the same route for **Shining Force** afterward.

The launcher prepares complete native and menu-return capsules before the menu
starts. Their contents and allocation are checked before use. Record the time
from each game selection to its first usable screen, including the second and
third launches below. Console timing has not been measured for this revision.

## Check successive selections

1. Select **Shining Force**, confirm A1 is present, and save an identifiable
   change. Wait for the game's save success message and for SD activity to end.
2. Press **A+B+X+Y+Start** to return to the original collection menu. Select
   **Phantasy Star II** and confirm A1 is still present. Save a different
   identifiable change, then return to the collection menu again.
3. Select **Shining Force** again and load its earlier save. Return once more
   and select **Phantasy Star II**, confirming its own save remains available.
4. Continue with the power-off checks below. Menu return alone does not prove
   that either save survived loss of power.

Each return must restore the collection adapter and each selected game must
acquire a fresh worker over the same existing card. Record any missing card,
unexpected save prompt, diagnostic, stalled return or unusually slow launch.

## Check each RPG separately

1. Reach a game save opportunity and make an identifiable change, such as
   a new character name, location or progress point.
2. Use the game's own save action. If the Genesis engine presents **Save Game**,
   complete that save too. Wait for its success message and for SD activity to
   finish.
3. Power off. Restart K-UI and Sega Smash Pack, select the same RPG through the
   original menu, and load the saved game. Check the identifiable change.
4. Repeat the complete save, power-off and load sequence for the other RPG.

This tests persistent cartridge saves. Returning to the title menu without
removing power does not establish that the save survived on the SD card.

## Check controller settings separately

Change a visible Genesis controller mapping, use the original settings save
action, wait for success and SD activity to finish, then power off. Relaunch
through the original menu and confirm the mapping loads. The engine stores
these settings as `SMASH_01.CFG`, with a 1,024-byte payload requiring four VMU
blocks. This path is separate from cartridge SRAM and matters even for a game
without a persistent cartridge save.

## Other Genesis games and diagnostics

The same native backend accepts all ten original Genesis menu selections,
including each selection's original mode, skip and sound options. Individual
high-score persistence remains unclassified. The reviewed engine enables
**Save Game** only when a cartridge declares supported SRAM and marks it dirty;
SRAM eligibility alone does not show that a game stores its scores there.
See [the save-scope audit](smash-genesis-save-scope.md) for the available evidence
and the small header-only collector for the remaining cartridge metadata.

If a blue diagnostic appears, capture every cycling page and record the last
game action and whether the SD LED was active. Use the specified normal-menu
sequence and return combination first; other reset combinations need their
own hardware results.

## Review boundary

The linked audit binds the original-menu outer loader, low reader, raw reader,
surviving handoff, native child module and their prepared markers and seeds.
It combines exact linked artifact checks with bounded source and instruction
models. Source and host checks do not establish console boot, peripheral
timing, game compatibility or durable saves; the steps above establish those
hardware results.

The audit walks the actual low-reader and handoff compiler graphs together.
It distinguishes the writer's mapping callback from its new handoff redirect,
so the redirect's C stack use is included in the original 352-byte private
reader budget. The later capsule loader uses its own guarded 2 KiB stack.
The linked C model covers both RPG selections, opaque zero/ROM-valued C0
restoration, malformed arguments, a busy raw-reader probe, a bad capsule CRC
and the original non-native fallback. An optional original GIN input also
executes the actual redirect callback's four admission cases and the compiled
return preparation, publication, worker binding and return gate against the
original source. A busy return waits in the still-live SDK context for a
bounded interrupt-enabled retry; it cannot resume the old cleanup caller.

Delivery also requires actual linked cached discovery and table-based CRC
execution, grouped capsule reads and a fresh bounded FAT read window. These
checks establish the intended software paths; the recorded console timings
determine the practical loading result.

```sh
PATH="$PWD/.deps/sh-elf/bin:$PATH" python3 tools/check_smash_menu_loader.py \
  build/smash-menu-loader build/smash-native-menu \
  --gin /path/to/original/GINXFER.BIN --output build/smash-menu-loader/architecture-audit.json
```

Final packaging requires clean compile provenance for the matching outer,
child and runtime modules. The release includes their hashes and the audited
source change; it includes no original game executable or cartridge payload.
