# K-UI V1.5.1 "Dáinsleif"

Version **1.5.1** is a point release of Dáinsleif. It updates the SD runtime
and the Games payload: steadier game loading, faster launches, silent
CD-audio games, a button combination that returns to K-UI, box art with disc
titles, and Ogg menu music. The boot CD, the Dáinsleif splash and the optical
capture engine are unchanged. See the [release notes](release-v1.5.1-notes.md).

Download **`kui-1.5.1-dainsleif-release.zip`** from the
[1.5.1 release](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.5.1).
It contains the normal SD files, an optional boot CDI, installation and release
notes, checksums, license records and a source-package reference.

## Install on your existing SD card

1. Keep your current SD files or an archived package for rollback.
2. **Merge the supplied `KUI` folder into the SD card's root**, replacing only
   matching supplied files. Preserve the existing folder, preferences, music
   selections, box art and game dumps. The package contains no preference files.
   Update **both** `KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui`
   from this release.
3. Boot with your **current working CD**. Confirm `K-UI V1.5.1` and the source
   build ID shown by the launcher.

The menu music in `KUI/apps/music` is now Ogg Vorbis. The runtime plays each
`.ogg` and falls back to 1.5's `.wav` file only when the `.ogg` is missing, so
the old `.wav` menu tracks can be deleted once the new files are on the card.
The two `.db` catalogues support optional dump comparison.

There is **no need to burn another CD or reformat your FAT32/exFAT card**.
`boot-cd/kui-v1.5.1.cdi` is provided for an optional replacement disc; burn it
as a disc image. It carries the original K-UI badge below the SEGA logo.
Holding B from power-on selects its built-in CD tools; R starts the existing
benchmark runner only when requested.

## Box art

Open Games, press **START** and choose **Scan box art** once. The scan records
each game's disc title and cover in `KUI/covers`: your own PNG or JPEG named
after the game's folder when you add one, otherwise the cover that many discs
carry (`0GDTEX.PVR`). **Y** switches between List, Compact and Gallery views.
Game folders are only read. See
[box art, titles and views](https://github.com/TPMJB/K-UI-NeXT/blob/v1.5.1/docs/games-covers.md).

## Launch an existing game

Open Games, select an owned `.gdi` and press **A** to inspect it. On the detail
screen, press **A** for launch confirmation, then **A** again to launch.
**B** backs out before launch. The **Y** read probe is available for diagnosis
and is not required for normal launch.

Games reads the SD card without writing it. A running game may write saves
to an attached VMU. To return to K-UI, press **A+B+X+Y+Start** in the game: the
reader shows its counters for about two seconds, then restarts the console,
which boots the K-UI disc again. A game that does not respond to that
combination, or a stop screen, needs a power cycle.

Compatibility reports should identify the title and region, how far it runs,
and any audio, FMV, loading or VMU save/load issues. Photograph a stop screen
if one appears.

## Build and source records

`build.json` identifies the exact source and package contents; `SHA256SUMS`
covers the delivered files. The accompanying
`kui-1.5.1-dainsleif-source.zip` provides corresponding source and dependency
records; see `SOURCE.txt` for the exact source revisions.

For development, workflow artifacts additionally provide `sd-update`
(diagnostic fixtures and demo music), `bootstrap-cd`, `sd-benchmark` and
`diagnostic` (full build outputs and source records). The benchmark package
replaces normal game launch and is not the normal release installation.
