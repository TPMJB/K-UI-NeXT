<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Favorites and recently played

In Games, select a game and press **START**. Library options retains the
existing browse, scan and test controls, then adds:

- **Favorites**: favorite games from `/Games`, in the normal alphabetical order.
- **Recently played**: prepared game launches from `/Games`, newest first.
- **Add selected favorite / Remove selected favorite**: saves that choice for
  the game highlighted before opening Library options.

An amber `*` identifies favorites in list, compact and gallery views. A disc
set has one favorite and one recent position; choosing it still opens its
ordinary disc chooser. Original and 2048-byte copies share the same activity
entry when they are a paired catalogue row. Folders cannot be favorites.
**Game library** returns to the complete list. X still scans for changes and Y
still changes the visual layout.

Opening game details, inspecting an executable or running a read test does not
update history. History is written only after game preparation succeeds,
before the launcher hands execution away. A history entry does not prove that
the game started or that gameplay succeeded; a final handoff refusal can still
leave the prepared launch in history. A failed history write is logged and
does not prevent launching the game.

The bounded collection keeps up to 128 activity records. When full, a new
record may replace the oldest nonfavorite history entry. Favorites are never
silently evicted; a collection containing 128 favorites refuses another new
record. Deleted images remain in the saved activity data but disappear from
filtered lists when a new catalogue no longer contains them. Moving or
renaming an ordinary image changes its path identity; disc sets use the
catalogue root and existing stable group key.

Activity is stored separately from the catalogue in
`KUI/games-library/activity-a.bin` and `activity-b.bin`. The writer alternates
slots, synchronizes and closes the replacement, then verifies every byte by
reread. A failed favorite save retains the previous in-memory choice and the
previous valid slot. A damaged newer slot falls back to the older valid
record. If neither existing slot is valid, writes are refused and the All
view remains available. Preserve both activity files in installation backups.

Validation covers codec corruption/truncation, path and group identity,
capacity and favorite retention, shell navigation, stale filtered results,
and actual FatFs FAT32/exFAT persistence, failed writes, disc grouping and
corrupt-slot recovery. Console acceptance remains pending for this combined
build.
