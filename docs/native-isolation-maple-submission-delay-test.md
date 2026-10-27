<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution submission-delay control @BUILD@

The completion-only 629e7fe96c08 control passed the Sofdec transition. This
matching control places the reference delay only at submit: the existing
59-TMU0-tick helper runs before the original MDST store. Completion retains
the passing cec542aefa8a software instruction path and MMIO ordering, with no
completion delay helper. New demand reads remain active; virtual VMU, both
real Maple C callbacks, controller TICK and prefetch remain off.

The 59 ticks correspond to about 75.5 microseconds in the admitted existing
game clock. This is a reference budget from the earlier instrumented PROFILE
capture, not an exact NORMAL callback duration. The control does not establish
physical interrupt phase or coverage of the game's scanline-zero window.
Invalid or frozen clocks produce a terminal diagnostic.

## Install and test once

Extract into a fresh folder and run there:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

The installer accepts the existing 629e7fe96c08 session and its earlier
isolation backups. No card preparation, formatting, save reset or backup
deletion is needed. The GUI, prepared profile, card, journal, identity and
launch settings stay in place.

Safely eject and cold boot the same Evolution image with **SCI / Background
reader New (X) / Auto encoding / CD music Off / RAM patches Off**. Keep the
physical setup unchanged, with physical A2 empty. Check the startup banner
identifies the submission-only delay control. A missing-memory-card warning
is expected because virtual VMU is off.

Report whether SD activity starts, whether it continues before the logo
disappears, and whether it passes the same Sofdec transition and plays the
opening FMV. If it stops, report where. If a clock-refused or other automatic
error page appears, photograph that page. No capture gesture, SAVE test or
repeat of the passing 629e control is requested.

## Return to the passing CEC build

From this same extracted folder, if needed:

```sh
python3 passed-bypass/install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

This returns directly to the exact cec542aefa8a modules that passed twice,
keeping the initial backups. Use the main install command above to return
to this submission-delay control.

## Restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores both exact original pre-isolation loaders from the retained
initial backup. Full source and build evidence are included; console
acceptance of this control remains pending.
