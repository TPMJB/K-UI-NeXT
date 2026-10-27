<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution completion-delay control @BUILD@

This control keeps New demand reads active, virtual VMU off, and both real
Maple C callbacks off. Submit retains the passing cec542aefa8a software
instruction path and MMIO ordering, with no submit delay helper. Completion
alone runs the existing read-only 76-TMU0-tick helper before the original
consumer continues. Controller TICK and prefetch remain off.

The 76 ticks correspond to about 97.3 microseconds in the admitted existing
game clock. This is a reference budget from the earlier instrumented PROFILE
capture, not an exact NORMAL callback duration. The control does not establish
physical interrupt phase or coverage of the game's scanline-zero window.
Invalid or frozen clocks produce a terminal diagnostic.

## Install and test once

Extract into a fresh folder and run there:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

The installer accepts the existing 5e4663, 1b2d, cec542, 5fa97, 3f51 or 995
isolation session and retains the original loader backups. No card
preparation, formatting, save reset or backup deletion is needed. The GUI,
prepared profile, card, journal, identity and launch settings stay in place.

Safely eject and cold boot the same Evolution image with **SCI / Background
reader New (X) / Auto encoding / CD music Off / RAM patches Off**. Keep the
physical setup unchanged, with physical A2 empty. Check the startup banner
identifies the completion-only delay control. A missing-memory-card warning
is expected because virtual VMU is off.

Report whether SD activity starts, whether it continues before the logo
disappears, and whether it passes the same Sofdec transition and plays the
opening FMV. If it stops, report where. If a clock-refused or other automatic
error page appears, photograph that page. No capture gesture, SAVE test or
repeated CEC baseline is requested.

## Return to the passing build

From this same extracted folder, if needed:

```sh
python3 passed-bypass/install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

This returns directly to the exact cec542aefa8a modules that passed twice,
keeping the initial backups. Use the main install command above to return
to this completion-delay control.

## Restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores both exact original pre-isolation loaders from the retained
initial backup. Full source and build evidence are included; console
acceptance of this control remains pending.
