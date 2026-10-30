# Skies Standard reader: opening progress and battle read comparison

This package changes only the ordinary game loader. **Keep the current GUI**;
there is no GUI installer or replacement in this ZIP. Every mode retains the
**72 KiB VMU worker**, split controller path and the same prepared virtual A2.

The owner tested paced build **d6d517fa133a** and completed Alfonso's second
fight, reached the save point, created a save, then rebooted and loaded that
save. The opening **Cupil hang remains**. This accepts the reported battle-read
and save/reboot/load sequence; it does not establish that the opening hang or
every game read is fixed.

The latest blue failure screen records an SD timeout (`SD=3`) during an image
read (`IMG=5`), with `STATE=0x1208` and 63 sectors already completed. That is a
partial accepted read, rather than a refusal before any disc data was supplied.
The next comparison targets SCI receive timing. It does not establish that the
smaller VMU reservation caused the failure.

The owner reached the save point with the build used before standalone VMU
work. This comparison holds the 72 KiB VMU, card and settings fixed and changes
SCI receive policy to isolate the newly failing path.

## Install the default paced receive mode

Extract the complete ZIP on the PC. Mount the Dreamcast microSD and run:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

Keep this existing **p0005** profile for every read comparison. Both installed
loaders used the same exact p0005 profile in the inventory report. Its card is
healthy with 53 free blocks and no reported filesystem problems. No new card
needs to be created or formatted.

The inventory did not identify a Skies save among the fifteen inventoried cards.
Seven card directories lacked a prepared profile; that is not evidence of a
corrupt card image. This package does not recover or promise an old Skies save.

The default is **paced**. It retains receive DMA and adds a bounded wait for
receive completion before the next transmitted byte. The PIO and original
unpaced receive modes are included for a controlled comparison.

All modes also include the CHECK-progress correction: changing or invalid video
geometry no longer repeatedly erases the CHECK-only reader's progress count.
It retains the existing normal read step and introduces no new busy-wait timer.
The owner's test still encountered the opening Cupil hang with this correction.

Safely eject. Use **SCI**, **Background Off (Standard)**, saved **Emulated VMU
Off**, CD music Off and RAM patches Off. This module supplies A2 itself. Keep
physical A2 empty and preserve the same accessories/settings for each comparison.
Record the build and selected policy at launch.

## Console checks

1. Let the opening intro continue through the point where it previously looped
   or stopped progressing. Report this separately from later gameplay.
2. Continue through **Alfonso's second fight, before the first save point**.
   Report whether the fight completes and whether the first save point can be
   reached. Photograph any blue fault page with all fields visible.
3. If the save point is reached, save without overwriting another save you want
   to keep. Wait until saving completes, then power cycle and load that new save.

A short pass or a failure identifies that tested scope. There is no need to
repeat the other games' already accepted tests for this comparison.

## Optional PIO comparison, then the unpaced control

If paced mode still fails, keep the same card and settings and select PIO:

```sh
python3 install.py /run/media/apollonius/Dreamcast --mode pio --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

PIO disables the SCI payload receive-DMA path. Loading speed may differ; compare
whether the same battle read completes and whether the same fault occurs.

The original unpaced receive-DMA path remains available as a control. It also
contains the CHECK-progress correction, so the intro test is not an old-build
comparison:

```sh
python3 install.py /run/media/apollonius/Dreamcast --mode unpaced --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

Use the same profile in every mode. Do not change the virtual card and receive
policy together when comparing a failure. `test-results.csv` has one row per
mode, with separate intro and second-fight fields.

## Card inventory report, saved to the PC Home folder

The current inventory has already been reviewed. If another report is needed
after testing, run:

```sh
python3 inspect-cards.py /run/media/apollonius/Dreamcast > "$HOME/kui-skies-cards.json"
```

The quoted output path explicitly saves `kui-skies-cards.json` in the **PC Home
folder**, not on the microSD. Send that file back for inspection. The scanner
reads every prepared card and profile, lists filenames/VMS descriptions, and
replays committed journal data only in RAM. It writes no card image, journal,
profile or assignment and does not reset recovery state.

## Exact restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --restore
```

This restores the exact ordinary loader present immediately before this package
was installed. Its new backup and receipt live in `KUI/skies-read-fix-loader/`.
All previous VMU/Skies loader and UI backup directories remain untouched. Keep
this ZIP and its new backup directory until restore is complete. Restore this
newer layer before invoking an older package's restore command.

The GUI, other loaders, assignments, card data, journals, profiles and saved
launch settings remain unchanged by installation. Saves written by a game remain
normal saves; restoring the loader does not roll card data back. A loader changed
by another installer is preserved by refusing to overwrite it.

All three modes use SCI native uncompressed image launching. The 72 KiB worker's
4 KiB Maple-list limit is retained; longer lists stop visibly rather than being
truncated. This package adds no physical-disc launch, DOA2 VMU admission,
WinCE/CDDA or compressed-image support. Complete source history, linked audits
and exact compiled artifacts are included; see `SOURCE.md`.
