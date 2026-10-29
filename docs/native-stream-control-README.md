<!-- SPDX-License-Identifier: GPL-3.0-only -->
# PSO and Spawn intro playback control

Build: **@BUILD@**. This package keeps the combined ripper and game-card
source from **c2e6f07cb52e**. Code Veronica and PSO have positive console
reports on that build; PSO's intro is slow, and Spawn's intro is slow with
distorted audio.

Spawn now uses the normal structural New reader, with existing SD stream
counters displayed at return. It does not fill RAM with diagnostic patterns.
Spawn still uses the reader without a virtual VMU. PSO keeps the combined
virtual-A2 reader and **Prefetch Auto**, with its existing return-screen
format. The added counters are only in Spawn's standalone reader. This is
a comparison build, not a claimed playback fix.

## Preserve the currently installed build

With the card mounted on the PC, run this Bash block once. Continue when it
finishes without an error. It preserves an existing archive with this name.

```bash
(
  set -C
  tar -c -C /run/media/apollonius/Dreamcast \
    KUI/runtime.kui \
    KUI/apps/games/retail-native-dual.kui \
    KUI/apps/games/retail-native-standalone.kui \
    KUI/native-general/state.json \
    > "$HOME/K-UI-before-@BUILD@.tar"
)
```

## Install

Extract the complete ZIP, open a terminal in its package folder, and run:

```bash
python3 install.py /run/media/apollonius/Dreamcast \
  --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin \
  --runtime-checkpoint "$HOME/K-UI-before-@BUILD@.tar"
```

The installer verifies the four-file checkpoint, updates the three modules
and receipt, and preserves card images, journals, profiles and game settings.
Safely eject, then boot K-UI. The runtime build ID must be **@BUILD@**.

## Compare the same intros

Use each game's original GDI, **SCI**, **CD music Off**, and **RAM patches
Off**. Start each comparison from a full power-off and use the same intro
and stopping point. Begin with Spawn, then repeat with PSO.

| Game | Background | What to record |
| --- | --- | --- |
| Spawn | New (X) | Video smoothness, audio distortion, approximate duration, and the complete blue return screen with SD counter rows. |
| Spawn | Off | Whether the same video and audio are better, unchanged or worse. |
| PSO | New (X) | Video and audio quality, approximate duration, and the complete blue return screen with its existing service rows. |
| PSO | Off | Whether the same intro is better, unchanged or worse. |

Use **Off** for the intro comparison. Use **New (X)** for virtual-A2 saving.
An Off run does not expose the virtual card.

After each New run, press **A+B+X+Y+Start** to reach the return screen. If
the first press reaches the game's menu, press again. Photograph the whole
screen so the build ID and all rows are visible. Spawn's counters cover the
session through return, including any menu round trip. PSO retains its earlier
service rows and does not gain SD counter rows in this control.

The hexadecimal counters describe these existing totals, in left-to-right
order:

| Row | Values |
| --- | --- |
| SD DMA PIO CMD KEEP | DMA blocks, PIO blocks, CMD18 starts, retained-block uses |
| SD OVR CRC TOK FOR | Overruns, CRC errors, token errors, foreign channel events |
| SD FIX AHEAD DEFER | Overrun repairs, repairs received a byte ahead, deferred repairs |

They help identify PIO fallback and stream errors or repairs. They do not
measure frame rate or transfer rate by themselves. The earlier RAM-map
captures remain recorded; no further RAM-map run is needed for this comparison.

## Return to the exact previous build

With the card mounted on the PC:

```bash
tar -xf "$HOME/K-UI-before-@BUILD@.tar" \
  -C /run/media/apollonius/Dreamcast
```

This restores only the three modules and old receipt. New saves, journals,
cards and caches remain. The installer's `--restore` instead returns its
first General backup, which may be much older than this checkpoint.

The package includes both source histories, build evidence and SHA-256 sums.
