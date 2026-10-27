<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution combined controller-split candidate @BUILD@

This candidate enables **New demand reads and virtual A2**. The controller
submit path uses a short controller capture; the completion path performs
the live ownership, packet and response checks before publishing virtual A2
presence. Both real Maple callbacks remain enabled. Controller TICK, prefetch
and the separate metadata-cache control are off.

This is a new combined implementation for the matched Evolution 1 executable
in NORMAL mode. The latest 99b post-start control was reported as not working;
its exact failure point was not confirmed. The split is not a hardware timing
or speed claim, and console acceptance is pending.

## Install and test once

Extract into a fresh folder and run there:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant combined
```

The installer accepts the existing 99b session and its earlier isolation
backups. No card preparation, formatting, save reset or backup deletion is
needed. It preserves the prepared profile, card, journal, identity, GUI and
launch settings. The inactive Standard module is restored to its original
bytes.

Safely eject and cold boot the same Evolution image with **SCI / Background
reader New (X) / Auto encoding / CD music Off / RAM patches Off**. Keep the
physical setup unchanged, with physical A2 empty. Check the startup banner
identifies the combined controller-split candidate and check whether the game
recognizes the virtual VMU at A2.

Report SD activity before the logo disappears, whether it passes the Sofdec
transition, and whether the opening FMV plays. If it boots, try **SAVE**, then
**LOAD** when a save point is available, using virtual A2 if the game offers a
card choice. If it stops, report where; photograph any automatic error page.
No capture gesture or repeat of a passing baseline is requested.

## Return to the passing CEC build

From this same extracted folder, if needed:

```sh
python3 passed-bypass/install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

This returns directly to the exact cec542aefa8a modules that passed twice,
with virtual VMU off, while retaining the initial backups. Use the main
`--variant combined` command above to return to this candidate.

## Restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores both exact original pre-isolation loaders from the retained
initial backup. Full source and build evidence are included.
