<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native session controls

This optional candidate adds in-game disc switching and direct SD return to
K-UI to the native virtual-VMU worker. Console acceptance is pending. Host
tests and a successful build do not establish title compatibility or save
durability on a Dreamcast.

## Install and opt in

Install the bundle's normal `KUI/` update first, including `KUI/runtime.kui`.
That provides the Games tabs and favorite shortcut, removes the screenshot
description from Settings, and prepares checked session data at launch. Keep
the existing prepared virtual cards, journals and library activity files.

To enable the new in-game controls, merge the bundle's
`OPTIONAL-CONTROLS/KUI/` directory into the SD card's `KUI/` directory. This
replaces only these selected native modules:

| Bundle path | SD destination | Virtual slot |
| --- | --- | --- |
| `OPTIONAL-CONTROLS/KUI/apps/games/retail-native-dual.kui` | `KUI/apps/games/retail-native-dual.kui` | A2, the default |
| `OPTIONAL-CONTROLS/KUI/apps/games/retail-native-dual-a1.kui` | `KUI/apps/games/retail-native-dual-a1.kui` | A1 |

Back up those two existing compact modules before the override. Restore both
from the matching normal update to opt out; keep the updated runtime. Do not
rename a module for one virtual slot into the other slot's filename. A loose
`retail-native-session-controls.kui` file is not a dispatch target and does
not opt in. Installing only `KUI/runtime.kui` also leaves the compact modules
in use.

For the first bench pass choose **SCI**, **Background On / New (X)**, native
GDI, Auto encoding, CD music Off, RAM patches Off, and **virtual VMU On**.
New (X) is the reader's option label; the in-game shortcut below uses Start.
Keep the selected virtual slot free of a physical VMU.

## Disc switching

Open a catalogue disc-set row and choose its starting disc normally. Launch
prepares the entire set in numeric order. At the game's disc-change prompt,
hold **L + R + Start** with **A, B, X and Y released**. After 90 successfully
completed physical controller polls, the worker requests the next disc.
Release the combination before requesting another change. The last disc
wraps to the first.

The transition waits for the reader and pending VMU operations. It presents
a virtual open lid, waits for the game to observe that state and for at least
30 further controller completions, then closes the lid with the next checked
disc map. Old pending reads and prefetch state are retired before rebinding.
The current virtual VMU remains attached. There is no in-game selection
overlay or screenshot capture.

The hold is measured in controller completions, not seconds. Games that
stop polling the controller, stop observing disc status, or stop reaching an
idle reader state can defer the change indefinitely. A successful shortcut
outside the game's requested disc-change scene is not a compatibility test.

## Return to K-UI

Hold the existing **L + R + A + B + X + Y** diagnostic combination, without
Start, for 180 completed controller polls. Pending VMU work and live reader
I/O settle before the intentional exit report. The exit request takes
priority over a disc change, so a game that stops probing the virtual lid
can still reach the return screen.
When the screen says **RELEASE KEYS / START RETURNS TO K-UI**, release the
buttons and triggers, then press **Start**.

This performs one direct SD reload into a fresh KOS/K-UI instance. It does
not depend on the bootstrap disc being present and does not resume the old
shell. It also does not save the game's progress: use the game's ordinary
save function first. A normal error report does not enter this menu and has
no automatic return timer.

If the screen says **RETURN UNAVAILABLE**, **RETURN REFUSED** or
**K-UI RETURN FAILED**, photograph the complete report and power off. A failed
attempt cannot safely resume the game or retry a partially overwritten runtime.

## Current limits and bounds

* The opt-in path is **SCI / Background New / native GDI / virtual VMU On**.
  Legacy, Standard, SCIF, IDE, Windows CE, CDDA, compressed images, physical
  disc loading and VMU Off are outside this candidate's acceptance scope.
  Selecting an unsupported route can refuse preparation; it does not enable
  the services on that route.
* Disc sets contain **two to four** distinct images on the same SD partition.
  Preparation requires matching product, region and revision fields. Every
  member must fit the 64-slot compact track/extent map, and high-density CDDA
  tracks are excluded. Admission is checked before handoff; renaming an image
  cannot repair an identity or mapping refusal.
* Single-disc launches can use return without a disc-switch set. Return
  requires a readable, valid `KUI/runtime.kui`, at most **32 physical file
  extents**, payload at most **4 MiB**, and runtime memory at most **8 MiB**.
  The complete live worker and its stacks must lie outside the incoming
  payload. An otherwise supported game can therefore show return unavailable.
* Launch validates the runtime header and complete payload CRC, then retains
  its physical extents and partition bounds. The terminal loader rereads the
  pinned extents, checks the unchanged header and payload CRC, and patches
  the selected SCI boot transport only in RAM. It performs no filesystem
  operations and never rewrites the runtime file. Leave the SD card and its
  files in place throughout the game.
* The return path retires reader/stream activity, refuses live DMA ownership,
  disables terminal Maple triggering, and turns caches off before loading
  through P2. Failure preserves a terminal diagnostic state rather than
  jumping to unverified bytes or acknowledging an unfinished save.

The optional layout currently uses a **112 KiB worker inside a complete
324 KiB reservation**, including the unchanged 192 KiB read-ahead cache.
The compact baseline uses 72 KiB inside 284 KiB: the optional reservation is
40 KiB larger. The final linked worker, private stacks and all retained data
must fit that layout; the package's build checks are authoritative. These
services add bounded work to existing proved controller completions. Their
effect on console frame pacing still needs measurement.

See the [hardware test draft](native-session-controls-hardware-test.md) for
acceptance order and the observations to report.

## Module build mapping

The two build variants produce the same basename in separate build folders.
Package them under their exact dispatch filenames:

| Build output | Optional package destination |
| --- | --- |
| `build/native-session-controls/retail-native-dual.kui` | `OPTIONAL-CONTROLS/KUI/apps/games/retail-native-dual.kui` |
| `build/native-session-controls-a1/retail-native-dual.kui` | `OPTIONAL-CONTROLS/KUI/apps/games/retail-native-dual-a1.kui` |
