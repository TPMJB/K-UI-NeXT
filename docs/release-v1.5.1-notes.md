# K-UI V1.5.1 "Dáinsleif" — release notes

Version **1.5.1** is a point release of Dáinsleif. It updates the SD runtime
and the Games payload, and keeps the 1.5 boot CD, splash and optical capture
engine. Follow the [installation guide](release-v1.5.1.md) to update an
existing FAT32/exFAT SD card.

## Changes since 1.5.0

### Games: loading and launching

- **Longer reads on still screens.** While a game shows an unchanging picture,
  such as a black or static loading screen, the in-game reader serves each
  request until shortly before the second vblank instead of stopping after two
  sectors. While the game draws, requests stay at the 1.5 size. Every SD read
  is now a single multi-block stream with the same per-block CRC checks.
  [Details](https://github.com/TPMJB/K-UI-NeXT/blob/v1.5.1/docs/games-read-pacing.md).
- **Faster launches.** Where each track lies on the card now comes from the
  allocation table, instead of one SD read per allocation cluster. The two
  handoff screens before a game pause for half a second instead of three. The
  boot executable is read once, by the stage, which checks every sector's
  sync, mode and address against its position.
  [Details](https://github.com/TPMJB/K-UI-NeXT/blob/v1.5.1/docs/games-launch-speed.md).
- **Games with CD-audio music run silently.** CD-audio play, pause and release
  requests are accepted without sound instead of stopping the game.
- **Return to K-UI from a game.** A+B+X+Y+Start makes most games ask for the
  BIOS menu; the reader then shows its counters for about two seconds and
  restarts the console, which boots the K-UI disc again.

### Games: box art, titles and views

- **Advanced > Scan box art** records each game's disc title and cover in
  `KUI/covers`: the owner's PNG or JPEG named after the game's folder, else
  the disc's own `0GDTEX.PVR`, else the title alone. Later scans read only new
  or changed games. Game folders are only read.
- **Y** switches between List (names beside a large cover), Compact (two
  columns of small covers) and Gallery (four covers across); the choice is
  saved. Image details show the large cover.
- The `.PVR` texture reader is independent K-UI code following KallistiOS's
  documented texture format. Owner images are decoded by the pinned
  `stb_image` 2.30 (MIT).
  [Details](https://github.com/TPMJB/K-UI-NeXT/blob/v1.5.1/docs/games-covers.md).

### Music

- The menu music and startup chime are stored as Ogg Vorbis, decoded by one
  shared decoder, and **Harbor Lights** joins the menu rotation. A missing
  `.ogg` still falls back to 1.5's `.wav` file.

## Games compatibility

Rows that changed since 1.5; all other requirements and limits are as in
the [1.5 notes](https://github.com/TPMJB/K-UI-NeXT/blob/v1.5.1/docs/release-v1.5-notes.md).

| Requirement or limitation | 1.5.1 behavior |
| --- | --- |
| CD audio | Play requests are accepted without sound; music on CD-audio tracks is absent |
| Returning from a game | A+B+X+Y+Start restarts into K-UI when the game asks for the BIOS menu; otherwise power cycle |
| Box art | 16-bit disc textures (twiddled, stride or VQ) or owner PNG/JPEG up to 3 MB and 1.2 megapixels; other games show their title and a placeholder |

Passing preparation checks still means only that an image fits the reader.
Loading speed remains bounded by the serial SD interface.

## Hardware evidence

| Area | Observation |
| --- | --- |
| Still-screen read pacing, DOA2 | Owner: "This actually worked really well." Some longer loads between fights; the first roughly ten seconds of a fight remain laggy |
| Still-screen read pacing, Evolution (first game) | Owner: mostly responsive; the few FMVs stutter |
| Faster launches, silent CD audio, restart combination, box art | Release-candidate console test pending |

These are owner observations, not measured timings. The read-pacing
observations come from build `2072b489c378`, whose reader this release
carries unchanged except for the CD-audio and restart handling above. See the
[pin record](https://github.com/TPMJB/K-UI-NeXT/blob/v1.5.1/docs/evidence/games-doa2-pacing-baseline-2026-09-26.md).

For rollback, the 1.5.0 release remains available, and the tested pacing
build is preserved on branch `baseline/doa2-pacing-2072b489c378`. The earlier
`baseline/doa2-cmd18-ed31d522c847` and `baseline/doa2-sd-6c02bd8b22f4`
branches are unchanged.

## Package and source

`kui-1.5.1-dainsleif-release.zip` contains the normal runtime, Games payloads,
Ogg menu music, catalogues, optional boot CDI, splash preview and source and
license records. It excludes scan fixtures, demonstration Music files, box art
and SD preferences. Merge its supplied files without replacing the existing SD
directory wholesale.

Full corresponding source, dependency pins and license records accompany the
release in `kui-1.5.1-dainsleif-source.zip`, as identified by `SOURCE.txt`.
The 1.5.0 notes and installation guide remain in the repository.
