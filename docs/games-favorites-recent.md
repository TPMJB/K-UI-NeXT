<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Favorites and recently played

Games opens with three visible tabs: **All games**, **Favorites**, and
**Recently played**. Press **Y** to move to the next tab; the tab bar always
shows which list is active. Tab changes start at the first page of `/Games`.

Highlight a game and press **X** to add or remove its favorite star directly.
An amber `*` identifies favorites in List, Compact, Gallery and Flow layouts. The
highlighted game is retained when it remains in the resulting list. Folders
cannot be favorites; a clear message explains that refusal.

A disc set has one favorite and one recent position; A still opens its ordinary
disc chooser. Original and 2048-byte copies share activity when they form a
paired catalogue row. **Recently played** is ordered newest first.

**Games → START** opens **Library options** with the less frequent actions:
open the full Game library, browse other SD folders, scan box art, run developer tests, scan
for changes, rebuild the saved library, or change between List, Compact,
Gallery and [Flow](games-flow.md) layouts. Changing layout keeps the active tab
and highlighted row.
A normal rescan seeks the remembered game again after discovery completes.

The same Library options menu now includes **Search and filters**, **Edit
display choices**, and **Verify stable identity**. Search matches titles and
display names; region and disc-number filters can be combined with any of the
three tabs. Choose Apply filters to publish the edited query, or choose a
letter and Jump to letter. B leaves the active query unchanged. The Games
header identifies an active text, region or disc filter.

Display choices can set a title alias and a PNG/JPEG cover path without
renaming a game or its folder. A opens the selected field's keyboard; choose
Save display choices to write the draft. Empty fields restore the original
title or automatic cover. X reloads the saved choices and discards the draft.
Changes and results are tied to the exact selected game and request generation.
Verify stable identity is a separate, confirmed full-image read; it can take
several minutes and B can stop it.

Opening game details, inspecting an executable or running a read test does not
update Recently played. Recent/activity history is written only after game
preparation succeeds, before the launcher hands execution away. A Recent entry
does not prove that the game started or that gameplay succeeded; a final
handoff refusal can still
leave the prepared launch in Recently played. A failed activity-history write
is logged and does not prevent launching the game.

This is the **Recent/activity** list, separate from
[virtual VMU save history](vmu-history.md). Save checkpoints have their own
verification and launch-refusal rules; the paragraph above does not weaken them.

The bounded collection keeps up to 128 activity records. When full, a new
record may replace the oldest nonfavorite Recent/activity entry. Favorites are
never silently evicted; a collection containing 128 favorites refuses another new
record. Deleted images remain in the saved activity data but disappear from
filtered lists when a new catalogue no longer contains them. Moving or
renaming an ordinary image changes its path identity unless an explicit
[stable-identity verification recovers the moved association](games-search-editing.md).
Disc sets use the catalogue root and existing stable group key. Display aliases
and cover assignments do not change these real paths.

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
