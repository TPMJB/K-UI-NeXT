# Saved Games library and disc groups — @BUILD@

This GUI update saves the prepared Games list between boots and groups clearly
named discs under one game. It replaces the menu runtime only. Existing native
reader modules, VMU cards, journals, game images and launch preferences remain
on the card.

## Install

Extract the ZIP, open its folder in a terminal, and run:

```bash
python3 install.py /path/to/mounted/SD
```

The installer checks the package, backs up the menu it replaces and verifies
the installed runtime. To restore that menu, run the same command with
`--restore`. Safely eject the card before testing on the Dreamcast.

## Saved list

The first opening builds a complete list. Let it finish once. Later boots load
the prepared list from the card; the home screen also warms it in idle time.
Opening a valid saved list does not walk the game folders or check every image.
Artwork loads separately, after the text rows become usable.

- **X — Scan** checks for added, moved or removed games.
- **START → Scan for changes** does the same for `/Games`.
- **START → Rebuild library** makes a fresh catalogue and provides recovery
  from damaged cache metadata.
- **Scan box art** remains a separate action.

Changes made on a computer become visible after a scan. K-UI invalidates its
saved catalogue before directory-changing rip, file-manager and FTP operations.
A stopped or failed replacement scan keeps the previous complete list where
one is available. Catalogue files live under `KUI/games-library`; they contain
menu metadata, not game payload or VMU data. The selected image still goes
through normal inspection and launch checks.

Cold card initialization and the first scan still take time. Console opening
latency has not been measured for this build; compare it with your current menu.

## Disc grouping

For example, these two existing folders appear as one game:

```text
SKIES_OF_ARCADIA_D1
SKIES_OF_ARCADIA_D2
```

Select **SKIES OF ARCADIA**, then **Disc 1** or **Disc 2**. If a disc has an
existing normal/2048 pair, its version chooser follows. B returns to the disc
list, then to the selected game. Game files do not move, and each selected disc
keeps its exact launch path and saved options.

Explicit `Disc 1`, `Disk 1`, `CD1` and separate `D1` markers are recognized in
folder or image names. Remaining region/revision text must match. Bare sequel
numbers are not disc numbers: Crazy Taxi 2 and Sonic Adventure 2 stay separate.
Conflicting duplicate disc numbers remain separate entries. Ambiguous names
are left alone; add explicit disc markers if needed. Automatic grouping does
not merge existing virtual cards or rewrite per-game assignments.

This build provides a menu disc chooser. Switching discs while a game is
running is a later resident-loader feature.

## Console checks

1. Open Games, let the initial list finish, leave it, then open it again.
2. Reboot with the same card. Compare time to usable rows, page turns and art.
3. Open Skies and select each disc; check the normal/2048 chooser if present.
4. B back through details, versions, discs and the parent list; confirm that
   the selected game is retained.
5. Add or rename a game on a computer. Check that X Scan updates the list and
   that another reboot retains the update.
6. Stop a scan; confirm the previous list is usable. Check that unrelated
   games, the ripper, existing VMU assignments and per-disc launch options still
   behave as before.

The package includes the exact source changes and host test evidence.
`SOURCE.md` identifies the base commit and explains how to load the source bundle
into a repository clone. Hardware responsiveness and successful game launches
still need console acceptance.
