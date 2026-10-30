# Compact Background reader with the 72 KiB VMU

The recommended installation updates only the compact Background reader and its
matching GUI. It needs no existing prepared card or p0005 profile. You can import
a physical VMU through the GUI and start Skies from scratch.

Background supports New (X) and Legacy (Y) with Virtual VMU On or Off. Its
reservation is 284 KiB, including the 72 KiB VMU worker and existing 192 KiB
reader cache. Off retains that worker, full reservation and shared reader hooks;
it disables virtual card service. This is different from the worker-free
structural reader, whose prefetch behavior differs. The existing asynchronous
SCI stream engine remains separate from Standard's paced receive-DMA path.

The compact worker accepts Maple command lists up to 4 KiB, matching the
standalone 72 KiB bound. Longer lists stop visibly before mutation; they are
never truncated. The reader cache remains 192 KiB and worker stacks remain
4 KiB. These bounds do not establish compatibility with every game.

The package also includes the paced standalone 72 KiB reader. Build d6d517fa133a
passed Alfonso's second fight, the save point, save, reboot and load; the Cupil
intro hang remains. Previously accepted games do not need a complete retest.
Host audits establish layout and bindings; this compact Background build still
needs the console checks below.

## Install Background and the matching GUI

Extract the entire ZIP on the computer. Replace `/path/to/SD` with the mounted
SD root. From the extracted folder, run:

```sh
python3 install.py /path/to/SD --background-only
python3 install-ui.py /path/to/SD
```

The first command installs only `KUI/apps/games/retail-native-dual.kui`; it leaves
the current Standard loader and its backups untouched. The second installs only
`KUI/runtime.kui`, whose parser recognizes the new compact reservation. Both
preserve exact originals before replacement. No prepared profile is required:
the combined module carries its validated generic template, and the GUI binds
the game card at launch when Virtual VMU is On.

The installers preserve all cards, journals, preparation profiles, assignments,
saved settings and earlier package backups. They do not delete, reset, format
or prepare cards. Clearing old cards, if desired, is a separate user action.
Safely eject before booting.

## Import and assign a fresh virtual card

Open **VMU tools → Import saves**. Use **L/R Slot** to choose the physical VMU,
then **A Import all saves**. Press **START Games** to open **Choose games for
this card**, select only the matching Skies release/product with **A**, then
**START Review → A Create and assign cards**. Check the source and product before
confirming. The physical VMU is read only; existing files are copied, with no
promise that saves from another release are compatible.

No existing Skies save or prepared profile is required. You can start a new game
on this imported card. Alternatively, Background On with Virtual VMU On can
create a blank per-game card on its first supported launch without an import.

To record the selected card, return to **Virtual VMUs**, find the card whose
games list contains that Skies product, and open **Details → Storage path /
Storage ID**. Game options shows a friendly label rather than pNNNN. The launch
log also reports `Virtual card: pNNNN for <product>`. Keep this assigned card
through the Background comparisons. Do not edit `import.bin`, `game.bin`, card
images or assignment records by hand.

Background Virtual VMU On uses the GUI's game-card assignment. An installer's
embedded prepared profile does not override that assignment. Restoring loaders
or GUI never rolls back console card assignments or saves.

## Console checks

Use SCI, original native disc images, CD music Off, RAM patches Off and the same
boot encoding/settings as the successful standalone run. Physical A1 may remain
inserted; keep physical A2 empty when testing virtual A2.

1. **Background On / New (X) / Virtual VMU On.** Confirm the imported/assigned
   card, start a new Skies game, and pass Alfonso's second fight. Letting the
   Cupil intro play is an optional separate check. At the save point, save without overwriting
   another wanted save. Wait for completion, power off normally, then load that
   new save again.
2. **Background On / New (X) / Virtual VMU Off.** This exercises the same compact
   module without virtual card service. Virtual cards and assignments remain
   stored; physical VMUs are used normally. A virtual save will not appear on a
   physical card. Loading/saving is optional when no suitable physical save is
   available.
3. **Optional controls:** repeat the relevant Background case with **Legacy (Y)**
   or AutoPrefetch as needed. The standalone artifact remains available as
   described below; there is no need to repeat every previously accepted game.

Record reader, Virtual VMU preference, pair ID, Cupil intro behavior, Alfonso's
second fight, and save/reboot/load results in `test-results.csv`. Photograph any
blue failure screen with every numeric field. Successful installation is not a
claim of a console crash fix.

## Optional paired installation with an existing prepared profile

If you later want to replace Standard too, use a canonical prepared profile
that already exists on this SD; replace the example path with its actual path:

```sh
python3 install.py /path/to/SD --profile KUI/vmu-test/pNNNN/retail-persistent-profile-NNNN.bin
```

This installs both modules and binds their embedded profiles to that existing
card. It refuses an ambiguous auto-selection rather than guessing. Standard /
Background Off uses its embedded profile and supplies virtual A2 itself: set the
saved **Virtual VMU Off** for this route. Background Virtual VMU On continues to
use the GUI assignment. If those profiles refer to different cards, later saves
are independent. The tool does not create a missing preparation profile or
require one for the recommended Background-only route.

## Read-only card report on the computer

After shutting down safely and mounting the SD on the computer, from the
extracted folder create a report in the computer's Home folder:

```sh
python3 inspect-cards.py /path/to/SD > "$HOME/skies-background72-cards.json"
```

It reads prepared cards and all file/VMS descriptions, replaying valid committed
journals only in RAM. It never changes the SD. Newly imported cards without a
prepared profile may be reported as missing preparation; that alone does not
mean their card image is corrupt. Keep the report private because it can include
card identity and personal save information.

## Restore independently

Restore the recommended Background-only installation and GUI in either order:

```sh
python3 install.py /path/to/SD --background-only --restore
python3 install-ui.py /path/to/SD --restore
```

If you also installed Standard with the optional paired command, restore both
loaders with `python3 install.py /path/to/SD --restore`. Using `--background-only
--restore` restores only Background and leaves Standard as it is.

New backups live only under `KUI/skies-background72/` and
`KUI/skies-background72-ui/`. Earlier standalone, controller comparison,
launch-GUI, next-package and SCI read-fix receipts/backups are retained. If a
target was changed by a later installer, restore that later package first;
this installer refuses to overwrite the changed target. No restore deletes
cards or rolls back saves, settings or assignments. `SOURCE.md` explains offline
source recovery; `artifacts/` and `evidence/` contain the exact builds and checks.
