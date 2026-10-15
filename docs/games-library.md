# Games library

Games lists folders and image files together, alphabetically by their full
folder or file name. ASCII case is ignored for ordering, with a deterministic
tie break. The same order applies to every page, cached visits and large
folders. Adding files to a card no longer puts them at the end of the list
because of their FAT directory insertion order. Equivalent descriptors and
original/2048-byte pairs retain their existing grouping rules.

The last folder and highlighted row are remembered by name. Opening Games
again restores that location, including the correct page if added games have
shifted the row. Going to a parent highlights the folder just left. Refresh
also keeps the highlighted name when it still exists. A removed selection
falls back to the first row; a removed remembered folder falls back to
`/Games`. This never starts a game automatically.

The storage worker saves the selection after a short idle interval and before
a game handoff. Two small CRC/sequence records at
`KUI/apps/games/selection-a.bin` and `selection-b.bin` retain the prior complete
selection when a save is interrupted. No selection files are created merely
by loading preferences. Browsing remains usable when selection saving fails.
Per-image launch options and the saved view use their existing separate files.

For a `.zso` image, Options offers **ZSO codec**. When inspection fails, Y
opens the same chooser directly. Choose Standard LZ4 or Historical LZO1X,
then A saves that exact image's choice and reinspects it. The choice is loaded
before detail metadata reads and bulk embedded-art extraction. Both formats share the `ZISO` header, so no codec is
guessed and a failed decode never triggers a second decoder. Missing older
preferences default to LZ4; v1/v2 launch records migrate when next saved.
A failed preference read/save leaves the explicit choice usable for this
inspection without overwriting an unread collection. Bulk scans refuse an
item whose preferences cannot be read. Because old cover records do not store
a codec, an explicit bulk scan rechecks ZSO artwork; selected-art caches retain
their normal warm behaviour. Native compressed-launch
limits and console acceptance still apply; see [ZSO scope](games-zso.md).

Options > Advanced > **Check executable** performs the same bounded image,
exact boot-byte, launch-package and map preparation as Play, then frees the
staged package and stays in the menu. Stop remains available. It uses the
current choices, including the selected codec, and logs the precise rejection
reason in Diagnostics. Only the preparation report may be written; game and
package files stay unchanged. A passed check does not verify every sector,
execute the game or prove console compatibility. The report's state remains
“prepared before handoff; game start unconfirmed.”

**Temporary baseline** in Advanced captures the current choices and uses the
inspected image's defaults: detected reader, automatic boot encoding, music
Off and RAM patches Off. It preserves the explicit ZSO codec. Changes made
while the baseline is active, including Restore defaults, are not saved.
Select Temporary baseline again to restore the previous choices and resume
automatic saving. Reinspection or leaving the image also ends the temporary
mode; persistent presets are never replaced merely by enabling it.
