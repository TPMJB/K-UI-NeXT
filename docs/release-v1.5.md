# K-UI V1.5 "Dáinsleif"

Version **1.5.0** is the final Dáinsleif release of the independent Dreamcast
launcher. Games can launch suitable native GD images beyond DOA2, and the
runtime includes the new crimson Dáinsleif splash. **DOA2 has confirmed
playable gameplay; Evolution 2 now boots but is severely slow.** Compatibility
and performance remain title-dependent. See the [release notes](release-v1.5-notes.md).

Download **`kui-1.5.0-dainsleif-release.zip`** from the
[1.5.0 release](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.5.0).
It contains the normal SD files, an optional boot CDI, installation and release
notes, checksums, license records and a source-package reference.

## Install on your existing SD card

1. Keep your working SD update or archived baseline ZIP for rollback.
2. **Merge the supplied `KUI` folder into the SD card's root**, replacing only
   matching supplied files. Preserve the existing folder, preferences, music
   selections and game dumps. The package contains no preference files.
   Update **both** `KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui`
   from this release.
3. Boot with your **current working CD**. Confirm `K-UI V1.5` and the source
   build ID shown by the launcher. The Dáinsleif splash is in the SD runtime.

`KUI/apps/music` contains the menu music; `KUI/apps/games` includes the normal
launch payload and the read probes used by Advanced diagnostics. The two `.db`
catalogues support optional dump comparison. Scan fixtures and the separate
Music demo are not included in this release package.

There is **no need to burn another CD or reformat your FAT32/exFAT card**.
`boot-cd/kui-v1.5.cdi` is provided for an optional replacement disc; burn it as
a disc image. It carries the original K-UI badge below the SEGA logo. Holding B
from power-on selects its built-in CD tools; R starts the existing benchmark
runner only when requested.

## Launch an existing game

Open Games, select an owned `.gdi` and press **A** to inspect it. On the detail
screen, press **A** for launch confirmation, then **A** again to launch.
**B** backs out before launch. The **Y** read probe is available for diagnosis
and is not required for normal launch.

Games reads the SD card without writing it. A running game may write saves
to an attached VMU. **Power off and on to return to K-UI.**

Further compatibility reports should identify the title and region, how far
it runs, and any audio, FMV, loading or VMU save/load issues. Photograph a stop
screen if one appears. Slow loading and streaming are known limitations;
speed optimization is deferred beyond this release.

## Build and source records

`build.json` identifies the exact source and package contents; `SHA256SUMS`
covers the delivered files. The accompanying
`kui-1.5.0-dainsleif-source.zip` provides corresponding source and dependency
records; see `SOURCE.txt` for the exact source revisions.

For development, workflow artifacts additionally provide `sd-update`
(diagnostic fixtures and demo music), `bootstrap-cd`, `sd-benchmark` and
`diagnostic` (full build outputs and source records). The benchmark package
replaces normal game launch and is not the normal release installation.
