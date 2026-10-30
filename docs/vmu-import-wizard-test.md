# Per-game VMU cards and save versions — GUI test

This package installs the new GUI and a separate physical-VMU Standard reader
at `KUI/apps/games/retail-physical.kui`. It keeps Background72 build 1734830d08df,
your existing VMU-only/standalone readers, and their earlier backups unchanged.
You can resume the standalone game test separately.

The new plain Standard module serves Background Off / Virtual VMU Off without
using an old VMU-only module's embedded card. It has no VMU worker or virtual-card
reservation and supports the ordinary SCIF, SCI and IDE transports.

The import wizard keeps captured source cards separate. For each selected game,
you choose its save files and get a new 200-user-block virtual card. Selecting
files from another source keeps that version on another card; versions are not
merged. You can also explicitly choose a blank game card. Existing virtual cards
and unselected game choices remain stored.

## Install the GUI

Extract **K-UI-VMU-Import-Wizard-@BUILD@.zip** and open its extracted
**K-UI-VMU-Import-Wizard-@BUILD@** folder. Run:

```sh
python3 install.py /path/to/SD
```

Replace `/path/to/SD` with the mounted SD root. The installer checkpoints the
exact current GUI and any existing physical Standard module before replacement.
If the physical module was absent, that absence is recorded for restore. It accepts existing valid game modules,
including Background72 and the earlier General reader, and also supports a card
without game modules. It changes only those two target files and its own checkpoint. It never
replaces the existing ordinary/VMU-only or Background module, prepares a game
profile, changes a save, or changes reader settings. Safely eject before booting.

## Import and review each game's files

1. Open **Home → VMU tools → Import saves**. Choose the physical slot with
   **L/R Slot**, then **A Import**. The physical VMU is read only. Add another
   physical VMU if desired; each captured source stays separate, including
   differing saves with the same internal filename.
2. Press **START Games**. Select a game with **A Choose saves**. In **Choose
   saves for this game**, use **A Pick save**, **L/R Source**, and **Left/Right
   Page**. Review the filenames, descriptions and block counts. Suggestions are
   hints only; they do not select files for you. Include related files together.
3. Press **START Keep** to retain that game's choices. Each source with picked
   files becomes a separate 200-user-block game card. For a new game without
   imported progress, use **X Blank/remove** on the game list to choose a blank
   card explicitly. Selecting another game creates its own card rather than
   sharing the first game's capacity.
4. Press **START Review**. Use **Left/Right** to check every review page: selected
   games, version/card counts, picked saves and blank choices. Then confirm
   **A Create and assign cards**. Sources are kept as
   full archives. Fresh playable versions get new pairs; existing card data is
   kept, and confirmed results can be reused on retry. No PC preparation command
   is needed for supported combined-reader launches.

A session captures up to eight source cards and lists up to 256 distinct game
products from `/Games`. Different disc images with the same product number use
the same game's choices. Each version has its own 200-user-block capacity.
Capacity errors or invalid mini-game combinations are reported before that
card is published; at most one mini-game is allowed on a version. A blank card
starts with all 200 user blocks free.

## Choose a save version at launch

Open **Games → Options → Virtual VMU card** to inspect or change a game's card.
With multiple versions and no saved default, the same picker opens before
launch. Use **Up/Down** to select a card, then:

- **A Use card** chooses it for this launch without saving a default. If the
  picker opened during launch, A continues that launch automatically.
- **X Set default** saves the highlighted card as the default and stays in the
  picker. Press A afterwards to use it or continue the waiting launch.
- **Y Always ask** clears the saved default and stays in the picker. A can then
  use the highlighted card once; a future launch asks again.
- **B Back** returns without launching or changing the current selection.

An explicit default is retained when another version is added. Use Y if you
want a prompt instead. A sole card needs no launch prompt. Older single-card
assignments expose only A/B until they are part of the version catalogue.
Changing a choice or default does not delete any version or rewrite its saves.

## Console checks

Use the current Background reader with Virtual VMU On for imported game-card
checks. This GUI does not extend a loader's compatibility. To resume the
standalone virtual-VMU test with this GUI, use **Background Off / Virtual VMU
On** with the existing paced SCI 72 KiB `retail-boot.kui` from d6d517fa133a or
1734830d08df. The GUI prepares the selected card's profile in RAM; it never falls
back to a baked-in profile. Missing or unrecognized modules refuse this route,
including the other comparison variants. This package preserves the installed
module but does not recreate a missing one; Background72 remains independently
available. The old setting with both preferences Off now selects the plain
physical reader.

Also check **Background Off / Virtual VMU Off** with a physical VMU. It should
use the new plain Standard reader and physical saves, with no virtual card or
VMU-worker reservation. Background72 with Virtual VMU Off retains its existing
72 KiB worker and full 284 KiB reservation; those are different reader routes.

- Import two games from one source. Confirm they have different game-card pair
  IDs and independent free space, with only their picked files on each card.
- Pick versions of one game from two sources. Confirm the source versions stay
  separate and the launch card chooser selects the intended progress. Test a
  one-time choice, a saved default, and clearing the default to ask again.
- Save on one chosen version, wait for completion, power off, and load it again.
  Check the other version and the second game's card remain unchanged.
- Recheck the original physical VMUs and an unselected game's previous card.
  Their contents and choices should remain unchanged.

Record results in `test-results.csv`. In **Virtual VMUs → Details**, record
**Storage path / Storage ID** for each new card. Keep source physical cards and
source archives until these checks pass. Host tests, linked GUI provenance and the new physical Standard module's
instruction/layout/stack audits are included. No virtual worker was rebuilt by
this package; console behavior is still a hardware test.

## Cancellation, retry and restore

Before publication, return to the game/file choices to correct them. Once
publication begins, the choices are locked for that import. Cards confirmed
ready are reused if later catalogue/default publication fails, and the game's
choices publish together after all required cards are ready. A late I/O failure
after a card was saved but before its final readback was confirmed can leave an
unassigned copy; retry may create another pair. Those copies and the full source
archives remain intact. Retry the same review rather than editing card files by
hand. Starting a new import preserves anything already published.

Restore the exact GUI and physical-reader state that preceded this build with
the same extracted package:

```sh
python3 install.py /path/to/SD --restore
```

Restore preserves all new cards, save versions, defaults, assignments and
existing game modules. If this package created the physical Standard module,
restore removes only that exact new module; an existing predecessor is restored
byte for byte. A later valid loader update does not prevent independent GUI restore.
A later GUI replacement is protected: restore that later GUI package first.
Every wizard build has its own checkpoint under `KUI/vmu-wizard/checkpoints/`;
earlier GUI and loader backups remain untouched. `SOURCE.md` explains recovery
of the exact GUI source and full repository history without a network connection.
