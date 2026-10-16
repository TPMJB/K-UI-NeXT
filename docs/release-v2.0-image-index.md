<!-- SPDX-License-Identifier: GPL-3.0-only -->
# K-UI 2.0.0 Tyrfing — release image kit

These images are ready to upload to Reddit, Dreamcast forums and release posts.
Start with the [visual contact sheet](../resources/release-v2.0/09-press-contact-sheet.png).
The downloadable press ZIP retains these repository paths, so the links in this
index also work after extraction.

## Suggested post gallery

Use the square announcement or landscape hero first, then the UI overview,
Games and optional CD-music cards. Add the VMU, disc and network cards according
to the audience. The board preview is separate development work and is not a
2.0.0 hardware feature.

| File | Size | Suggested use |
| --- | --- | --- |
| [Accepted Tyrfing artwork](../resources/release-v2.0/00-tyrfing-artwork.jpg) | 1280 × 720 | Original accepted orange pixel artwork; embedded title is V2.0 |
| [Release hero](../resources/release-v2.0/01-release-hero.png) | 1920 × 1230 | Landscape announcement; exact 2.0.0 version band |
| [Square announcement](../resources/release-v2.0/02-release-square.png) | 1600 × 1600 | Reddit gallery cover or forum lead image |
| [UI overview](../resources/release-v2.0/03-ui-overview.png) | 1920 × 1600 | Home, Games, VMU Manager and Network together |
| [Games](../resources/release-v2.0/04-games.png) | 1920 × 1080 | Alphabetical library, Original/2048 pairing and saved options |
| [Optional WinCE CD music](../resources/release-v2.0/05-wince-cd-music.png) | 1920 × 1080 | SCI opt-in with compatible runtime checks; World Party remains experimental |
| [Disc preservation](../resources/release-v2.0/06-disc-preservation.png) | 1920 × 1080 | Resumable ripping, export, verification and recovery tools |
| [VMU saves](../resources/release-v2.0/07-vmu-saves.png) | 1920 × 1080 | Card-image backups, extraction, import and batch-copy previews |
| [Network](../resources/release-v2.0/08-network.png) | 1920 × 1080 | FTP and wired/wireless adapter software |
| [Contact sheet](../resources/release-v2.0/09-press-contact-sheet.png) | 1600 × 1800 | Quickly choose the composed images |

## Individual UI views

All sixteen views below are 640 × 480 PNGs from the **production K-UI renderer**
using host-side sample state. They are **software-rendered previews, not console
screenshots**. The header says `Build UI SAMPLE`. Cover thumbnails are abstract
placeholders; game names, save entries, addresses, firmware strings, capacity,
progress and rates are sample fixture data. They do not establish compatibility,
radio performance or successful execution of a particular console workflow.

| View | File |
| --- | --- |
| Home | [home.png](../resources/release-v2.0/ui/home.png) |
| Games list | [games-library.png](../resources/release-v2.0/ui/games-library.png) |
| Games gallery | [games-gallery.png](../resources/release-v2.0/ui/games-gallery.png) |
| Original / 2048-byte selection | [games-variants.png](../resources/release-v2.0/ui/games-variants.png) |
| Saved launch options | [games-options.png](../resources/release-v2.0/ui/games-options.png) |
| Optional WinCE CD music | [wince-cd-music.png](../resources/release-v2.0/ui/wince-cd-music.png) |
| Disc Ripper settings | [disc-ripper.png](../resources/release-v2.0/ui/disc-ripper.png) |
| Disc Ripper tools | [ripper-tools.png](../resources/release-v2.0/ui/ripper-tools.png) |
| VMU Manager | [vmu-manager.png](../resources/release-v2.0/ui/vmu-manager.png) |
| VMD image browsing | [vmu-image.png](../resources/release-v2.0/ui/vmu-image.png) |
| Save import preview | [vmu-import.png](../resources/release-v2.0/ui/vmu-import.png) |
| Network | [network.png](../resources/release-v2.0/ui/network.png) |
| Wi-Fi | [wifi.png](../resources/release-v2.0/ui/wifi.png) |
| File Manager | [file-manager.png](../resources/release-v2.0/ui/file-manager.png) |
| System Tools | [system-tools.png](../resources/release-v2.0/ui/system-tools.png) |
| Audio CD | [audio-cd.png](../resources/release-v2.0/ui/audio-cd.png) |

The new VMU and ripper workflows are implemented and host-tested; their
integrated console acceptance remains pending. CD music is optional on SCI,
limited to compatible runtime contracts and image layouts; unknown profiles
remain experimental. These boundaries are included on the relevant cards.
See the [full 2.0.0 release notes](release-v2.0-notes.md) and
[installation guide](release-v2.0.md) for the complete scope.

## Board development preview

Board images and their CAD provenance are under
[`resources/release-v2.0/board/`](../resources/release-v2.0/board/).

| View | File | Size |
| --- | --- | --- |
| Ready-to-post isometric view | [g1-board-hardware-preview.png](../resources/release-v2.0/board/g1-board-hardware-preview.png) | 2384 × 2384 |
| Transparent isometric view | [g1-board-hero-transparent.png](../resources/release-v2.0/board/g1-board-hero-transparent.png) | 2384 × 2384 |
| Top | [g1-board-top-transparent.png](../resources/release-v2.0/board/g1-board-top-transparent.png) | 1488 × 2776 |
| Underside | [g1-board-bottom-transparent.png](../resources/release-v2.0/board/g1-board-bottom-transparent.png) | 1488 × 2776 |

See the [render notes and suggested captions](../resources/release-v2.0/board/README.md)
for source identity and component-model omissions.

These depict the separate board under development. They are CAD/design renders,
not photographs or proof of a working manufactured board. The working board is
the owner's tentative milestone for a future 3.0 release; no delivery date or
2.0.0 hardware support is promised by these images.

## Reproduce the software images

From the repository root, with Pillow, DejaVu Sans fonts and the host build
dependencies installed:

```sh
python3 tools/build_release_press.py
```

The script builds `build/render-shell`, renders the `press-` sample modes in
`tests/render_shell.c`, and composes the cards with the accepted
`resources/branding/release-v2.0-banner.jpg`. It does not modify runtime code or
board renders. `--skip-render` reuses the existing UI PNGs. No retail game images,
cover art or sound data are included in this image kit.
