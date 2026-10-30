# Skies: card inventory, launch preparation and controller comparison

The owner reported the SCI VMU-only batch working except Skies of Arcadia:
the old save is missing, the game sometimes freezes or goes black, and pressing
A takes about 20 seconds to reach the first bootstrap. These are three distinct
observations. This package does not claim that the crash is fixed.

The older Skies timer-callback correction is retained. New host tests exercise
Skies' uniform receive layout, dynamic timer registration and durable saves in
both controller paths. Console comparison is still needed.

## 1. Locate the actual save without changing any card

With the microSD mounted on the PC, run from the extracted package:

```sh
python3 inspect-cards.py /run/media/apollonius/Dreamcast > ~/kui-skies-cards.json
```

Send `kui-skies-cards.json` back for inspection. It lists every prepared card,
profile, filename, VMS description and installed module binding. A committed
journal is replayed into memory before listing the effective card. No card,
journal, profile or assignment is written. The output path above is on the PC.

`p0005` is the known Evolution test card; its current contents must establish
whether it contains the Skies save. A different card's contents must not be
inferred from its number. If the save is found elsewhere, use that same profile
for both comparison modes below.

## 2. Test the shorter preparation path

The included GUI is based on the approved disc-recovery/session-resume source.
It calculates executable CRC during the existing native VMU source/SHA pass
instead of reading Skies' 5,460,080-byte executable twice. The original
one-sector reads, exact CRC, SHA, live ownership and fallback checks remain.
This removes one complete read; it does not promise an exact console duration.

```sh
python3 install-launch-ui.py /run/media/apollonius/Dreamcast
```

This replaces only `KUI/runtime.kui`, retaining its exact previous bytes in a
separate backup. Time from pressing A to Bootstrap 1 on the next Skies launch.
Restore this GUI independently with:

```sh
python3 install-launch-ui.py /run/media/apollonius/Dreamcast --restore
```

## 3. Compare only Skies, using the same card and settings

The two SCI modes share the same source revision, Standard reader, 112 KiB VMU
reservation, prepared card and durable save handling. The baseline retains the
split controller path; the control calls the retained full-submit path instead.

For the currently selected p0005 card:

```sh
python3 install.py /run/media/apollonius/Dreamcast --mode baseline --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

Keep SCI, Background Off (Standard), the saved Emulated VMU preference Off,
CD music Off and RAM patches Off. The module supplies virtual A2 itself. Keep
physical A2 empty; physical A1 may remain attached. Repeat the scene or action
where Skies froze, then switch only the controller path:

```sh
python3 install.py /run/media/apollonius/Dreamcast --mode full-submit --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

The control displays `FULL SUBMIT CONTROL`. Use the same profile in both modes.
If inventory identifies another Skies card, substitute that profile in both
commands. Do not change the card and controller mode together when comparing a
crash. There is no need to repeat the other games' accepted tests.

Report the mode, card profile, whether a VMU is detected, whether the old save
loads, when any freeze occurs, and how many attempts or minutes were tested.
A successful short run is evidence for that scope, not full-game certification.

## Restore the exact previous VMU test loader

```sh
python3 install.py /run/media/apollonius/Dreamcast --restore
```

This package uses `KUI/native-vmu-only-skies/` and restores the exact loader
that was installed before this comparison. It preserves the original
`KUI/native-vmu-only/` backup and receipt. Restore this comparison first before
using the original VMU-only ZIP's restore command. Card saves made by a game
remain normal saves; restoring loaders does not roll card data back.
