# Per-game VMU cards and save versions — GUI test

This package installs the new GUI and a separate physical-VMU Standard reader
at `KUI/apps/games/retail-physical.kui`. It keeps Background72 build 1734830d08df,
your existing VMU-only/standalone readers, and their earlier backups unchanged.
You can resume the standalone game test separately.

The new plain Standard module serves Background Off / Virtual VMU Off without
using an old VMU-only module's embedded card. It has no VMU worker or virtual-card
reservation and supports the ordinary SCIF, SCI and IDE transports.

The wizard matches captured saves to the games in `/Games` automatically.
Each game gets a separate 200-user-block card containing its own related save
files. Different physical sources retain separate versions, including two PSO
characters. Only ambiguous, unsupported or unidentified save sets need an answer.
Full source snapshots remain preserved, including backup-only files.

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

## Import automatically

1. Open **Home → VMU tools → Import saves**. Choose the physical VMU with
   **L/R Slot**, then **A Import**. Add the other physical VMU the same way.
   Reading never writes to the physical cards.
2. Press **START Match saves**. The wizard identifies game names, reviewed save
   families, product metadata and related files together. Most saves need no
   manual selection. One game's saves never share another game's card capacity.
3. If **Saves that need your help** appears, use **A Choose game** for a save
   set. Choose its game by title; when two releases are plausible, only those
   releases are listed. **X Backup only** keeps the files in the full source
   backup without attaching them to a game. Repeat only for the remaining
   questions. Nothing gets silently discarded.
4. **Ready to import saves** shows only the planned game cards, their free
   blocks, and separate version counts. It does not list unrelated games as
   “Unchanged.” Use **Left/Right** for more review pages and **A Import saves**
   to create the cards. **X Change matches** opens the optional manual editor.

A 10-block save leaves 190 blocks free for that game. Two PSO versions become
two independent 200-block cards and remain selectable at launch. Games without
imported saves can receive their own blank card on their first supported launch;
the automatic import does not manufacture empty cards for every library entry.

If a saved assignment refers to a removed virtual-card folder, the wizard asks
**Use imported saves** before overriding that missing reference for selected
games. **B Cancel** starts no publication. Present but damaged or identity-mismatched
cards, storage errors, and damaged assignment records remain distinct errors;
they are not reported as a full card-choice list. Existing cards, healthy
choices, defaults, and the old assignment records are retained.

A session captures up to eight source cards and scans up to 256 game products.
All save-file grouping stays within its original physical source. Related files
remain together; several installed regions or versions with indistinguishable
metadata require a game choice. An unsupported game can remain backup-only.
Choosing backup-only for every unresolved set can still save all full sources
without creating playable cards.

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
