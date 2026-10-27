<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution combined VMU + New candidate @BUILD@

This candidate restores virtual A2 and both real Maple C callbacks while
keeping New demand reads active. It moves repeated reads of immutable expected
values to isolated, read-only cached views.
The view is published after lazy SDK preparation finishes. Actual SDK, heap,
packet and MMIO reads keep their existing coherent access and ownership checks.
Controller TICK and prefetch remain off for this test.

The cec542aefa8a control passed twice with both C callbacks bypassed. The
1b2d79400abd delay control failed at the same black screen. This candidate
tests the cached metadata path with the virtual VMU restored; console
compatibility and the game's timing window still need this boot check.
This build accepts the matched Evolution 1 executable in NORMAL mode.

## Install and test once

Extract into a fresh folder and run there:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant combined
```

The installer accepts the existing 1b2d, cec542, 5fa97, 3f51 or 995 isolation
session and retains its original loader backups. No card preparation,
formatting, save reset or backup deletion is needed. The prepared card,
journal, identity, profile, GUI and launch settings stay in place. The
ordinary Standard module remains the exact original module while combined
is selected.

Safely eject and cold boot the same Evolution image with **SCI / Background
reader New (X) / Auto encoding / CD music Off / RAM patches Off**. Keep the
physical setup unchanged, with physical A2 empty. Check the startup banner
identifies the cached metadata combined candidate, and check that the game
recognizes the virtual VMU at A2.

Check whether it passes the same black-screen transition and plays the
opening FMV. If it boots, try **SAVE**, then **LOAD** when a save point is
available, using virtual A2 if the game offers a card choice. If it fails or an automatic error page
appears, report where it stops and photograph any error page. No capture
gesture or repeated Standard baseline is requested.

## Return to the passing build

From this same extracted folder:

```sh
python3 passed-bypass/install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

This returns directly to the exact cec542aefa8a modules that passed twice,
with virtual VMU off. Its included installer accepts this combined test's
receipt and keeps the initial backups. Use the main `--variant combined`
command above to return to this candidate.

## Restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores both exact original pre-isolation loaders from the retained
initial backup. The source and build evidence are included.
