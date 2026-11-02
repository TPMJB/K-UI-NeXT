<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Favorites and recently played

Games opens with three visible tabs: **All games**, **Favorites**, and
**Recently played**. Press **Y** to move to the next tab; the tab bar always
shows which list is active. Tab changes start at the first page of `/Games`.

Highlight a game and press **X** to add or remove its favorite star directly.
An amber `*` identifies favorites in list, compact and gallery layouts. The
highlighted game is retained when it remains in the resulting list. Folders
cannot be favorites; a clear message explains that refusal.

A disc set has one favorite and one recent position; A still opens its ordinary
disc chooser. Original and 2048-byte copies share activity when they form a
paired catalogue row. **Recently played** is ordered newest first.

**START → Library options** contains the less frequent actions: open the full
Game library, browse other SD folders, scan box art, run developer tests, scan
for changes, rebuild the saved library, or change between List, Compact and
Gallery layouts. Changing layout keeps the active tab and highlighted row.
A normal rescan seeks the remembered game again after discovery completes.

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
