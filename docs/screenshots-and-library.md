<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Screenshots and Games conveniences

## K-UI screenshots

Hold **L + R**, then press **Y** to capture the current K-UI menu. Release Y
before taking another picture. Screenshots are written to `/KUI/screenshots`
on the selected SD volume as **640 x 480 BMP** files. Copy the BMP files to a
computer for release threads or other sharing.

**Settings → Screenshots** documents the shortcut, destination and format.
Opening this information page does not save or discard an unfinished Settings
draft. B returns to the same Settings row.

In-game screenshots are **not available yet**. The information page keeps this
future capability clearly unavailable; it does not expose an inactive toggle.
Menu capture does not add a screenshot service to the game loader.

## Planned return to K-UI

The current native VMU diagnostic shortcut is both triggers plus A+B+X+Y,
without Start, held across 180 completed controller polls. It deliberately
shows a terminal diagnostic page. A requested follow-up is to retain that
shortcut, display an intentional-exit screen briefly, and return to K-UI.

The existing BIOS-menu return handler reboots through the console boot ROM.
That starts K-UI when the K-UI bootstrap disc is still inserted; it does not
provide a direct SD reload, particularly when a physical game disc is present.
Direct return needs its own freestanding reload path, a checked runtime extent
map, safe code/stack placement, and orderly retirement of reader/DMA and VMU
transactions. The old KOS shell cannot resume after game handoff. Fatal error
pages should retain their evidence rather than inherit an automatic timeout.

## Planned in-game disc switching

Games catalogue grouping already selects a disc before launch. Runtime
switching needs a separate loader feature: prepare checked maps for the disc
set before handoff; model virtual lid-open/change/close; retire pending reads
and invalidate old prefetch/cache data; select the replacement map and update
TOC, position and disc-change responses. The same virtual VMU should remain
attached throughout the change.

Start with a native two-disc, non-CDDA profile such as Skies of Arcadia, using
a synthetic two-disc protocol harness before console testing. WinCE and CDDA
need their own interrupt/audio transition validation. Any new resident code
or disc maps must pass the existing complete lease, placement and stack checks;
the compact 72 KiB worker is part of an occupied 284 KiB reservation.
