<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Games Flow view

Open **Games → START → Change layout** until **Flow** is selected. The selected
layout is saved alongside the existing List, Compact and Gallery choices.

Flow shows a vertical list with a large cover for the selected game. UP/DOWN
moves one game at a time across the underlying eight-row chunks. LEFT/RIGHT
moves one visible window. Opening Details and returning should retain the
selected game. Favorites, Recent, search/region/disc filters, letter jump and
multi-disc groups use the same library/query as the other layouts.

Viewport and highlight positions ease toward each new target over **110 ms**.
New input can retarget the motion. This is the configured animation duration;
it does not claim 110 ms input latency, a measured frame rate, or instant SD
reads on a physical Dreamcast.

## Bounded data and ownership

The UI retains the current eight-row page and at most two neighboring pages.
It addresses selections as positions in the filtered list, so crossing a warm
cache boundary can adopt the neighbor without a full-screen page replacement.
At a cold edge it keeps the outgoing rows visible while the sole storage
worker obtains the requested chunk. Fast repeat cannot authorize a second
filesystem owner or extend the cache without bound.

Idle worker requests copy the root, group, query, originating page and Flow
epoch. Results are applied only when that context is still current. Leaving
Games, changing a query/layout, starting a foreground job or requesting Stop
invalidates/discards stale work. A failed read in the same context is not
retried continuously every idle pass. Selected artwork retains the existing
selection-key publication fence.

Animation ticks and drawing use RAM snapshots; they do no filesystem I/O.
Library reads and title preparation remain on the existing storage worker.
This changes menu presentation and bounded prefetching, not the game resident
loader, CDDA/VMU workers, or an in-game runtime service.

## Implementation and validation pointers

- `include/kui/shell.h`: two-neighbor cache, row height, 110 ms duration,
  Flow snapshot/animation API.
- `src/core/shell.c`: cache admission, absolute selection, warm/cold boundary
  behavior, viewport/highlight interpolation and input handling.
- `src/dreamcast/main.c`: idle worker requests, query/epoch cancellation,
  UI-thread result publication and per-loop animation tick.
- `src/dreamcast/shell_draw.c`: vertical list/highlight and selected-cover draw.
- `tests/test_games_flow_dispatch.py`: executes the actual runtime helper C
  under context changes, foreground work, cancellation and failed reads; checks
  that only the UI publishes a worker result.
- `tests/test_shell.c` and `tests/render_shell.c`: shell navigation and visual
  fixtures for the same UI/render path. Host results do not establish console
  timing. `make test-v3-medium` runs the combined feature checks; the focused
  runtime scheduler check is `python3 tests/test_games_flow_dispatch.py`.

## Console acceptance

Use a library larger than three chunks and include a final partial chunk.
Try Normal and Fast repeat while holding DOWN, reversing to UP near a cache
boundary, and alternating LEFT/RIGHT windows. Record the selected game,
viewport/highlight behavior and time spent waiting for a cold edge.

Repeat in All/Favorites/Recent and with search, region/disc filters, letter jump
and multi-disc groups. Check the first/last item and final short chunk; ensure
the large cover always belongs to the highlighted game. Open Details, return,
and confirm the selection is preserved. Change layout, leave/re-enter Games
and reboot to check saved layout/selection behavior. Exercise B Stop during an
uncached read and confirm a subsequent normal browse still works.

Physical Dreamcast animation cadence, input response, SD-edge latency and
cover correctness remain acceptance items; they are not inferred from the
host scheduler or an animation constant.
