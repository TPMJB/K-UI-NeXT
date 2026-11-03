<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Screenshots and Games conveniences

## K-UI screenshots

Hold **L + R**, then press **Y** to capture the current K-UI menu. Release Y
before taking another picture. Screenshots are written to `/KUI/screenshots`
on the selected SD volume as **640 x 480 BMP** files. Copy the BMP files to a
computer for release threads or other sharing.

Open **File Manager → X Actions → Screenshot gallery** to browse those saved
captures. Select a BMP and press A to view it; B returns to the gallery and
then to the folder you came from. The gallery action also works in an empty
File Manager folder. BMP decoding and folder reads run on the storage worker.

The screenshot shortcut works directly from K-UI. The old screenshot
description has been removed from Settings.

In-game screenshots are **not available yet**. No inactive toggle is exposed for this future capability.
Menu capture does not add a screenshot service to the game loader.

## Games library

Games now has visible **All games**, **Favorites**, and **Recently played**
tabs. Press **Y** to cycle tabs and **X** to favorite or unfavorite the
highlighted game. **START → Library options** contains scanning, rebuilding,
folder browsing, box art and layout selection. Layout changes keep the active
tab and selected row. See [Favorites and recently played](games-favorites-recent.md)
for storage and history behavior.

## Optional in-game controls

The separate native session-controls modules implement a first, bounded disc
switch and direct-return path for **SCI / Background New**, uncompressed
native GDI games with virtual VMU enabled. Installing the updated K-UI runtime
alone does not enable these controls. The ordinary compact modules remain
available; the optional modules use a larger resident reservation.

* **Next disc:** hold **L + R + Start**, with A/B/X/Y released, for 90 completed
  controller polls. Release before requesting another change. This cycles the
  checked catalogue disc set while retaining the virtual VMU.
* **Return:** hold the existing **L + R + A + B + X + Y** diagnostic gesture,
  without Start, for 180 completed controller polls. On the intentional exit
  screen, release the buttons, then press **Start** to reload K-UI from SD.
  There is no automatic five-second return. Unexpected fault reports remain
  diagnostic screens.

The poll counts are not fixed wall-clock timers. Save and read activity can
defer either transition. The implementation does not yet establish console
compatibility for any multi-disc title. See [Native session controls](native-session-controls.md)
for installation and limits, and the [hardware test draft](native-session-controls-hardware-test.md)
for the Skies of Arcadia, then Code: Veronica acceptance sequence.
