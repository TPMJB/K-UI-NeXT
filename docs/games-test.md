# Games foundation: GDI browsing and inspection

This is the G1/G2 foundation for the [Games plan](games-milestone-plan.md).
It adds a ninth launcher app in the existing K-UI style. It does not launch
retail games yet. The original-fixture resident proof has since passed, and the
selected-image/vector test is the current pending gate. The normal optical
reader is unchanged.

**Hardware update — 2026-09-24:** runtime `57d53841c1ea` inspected ARMADA three
times with identical five-track/boot metadata, reading 7,199 bytes each time.
A stopped Bust-A-Move 4 inspection was followed by successful ARMADA operations.
That accepts the demonstrated exFAT inspection path; no repeat is requested.
The exact browsing route and other unlogged controls are not inferred. See
[the evidence](evidence/games-armada-inspection-2026-09-24.json).

## Install

For the current next test, follow **[Selected-image GD request probe](games-image-probe.md)**.
It adds `KUI/apps/games/image-probe.kui` to the SD update. The browsing checklist below is
retained for G1/G2; accepted ARMADA inspection does not need repeating.

Finish any active operation, shut down, and replace `/KUI/runtime.kui` with the
new `sd-update` copy. Keep the current boot disc, games, settings and backups.
No new game files, full rip, verification scan or benchmark is needed.

## Controls

| Screen | Controls |
| --- | --- |
| Home | Select Games and press A |
| Games | Up/down select, A open/inspect, left/right page, X refresh, Start Advanced |
| Advanced | Choose Game library (`/Games`) or Browse SD folders (`/`) with A; B returns |
| Image details | A Test image reads after valid inspection; X inspect again; B returns to the list |
| Image test confirmation | A starts the handoff; B returns to details |
| During I/O | B requests Stop; navigation resumes when work ends |

The library includes immediate folders and `.gdi` files. A folder containing
exactly one visible GDI opens that image directly. Empty or ambiguous folders
remain browsable. Numbered duplicates such as `MDK2 (2)` stay separate entries.
Directory order is the card's order, with eight entries per page. Hidden/system
and dot-prefixed names are omitted. Missing `/Games` produces an explanation
and an Advanced browsing route; browsing does not create the folder.

Inspection reads the small descriptor and bounded boot metadata, checks every
track file's existence/length and reports title, product, region, boot file,
track counts and total stored bytes. A K-UI manifest is not required. This
checks image layout, not each sector's integrity or retail compatibility.
Advanced CRC remains the separate full saved-file scan.

First-stage support is raw **2352-byte, zero-offset GDI tracks**, with a
high-density data track and Mode 1 boot metadata. ISO, CDI, compressed images,
2048-byte GDI tracks and other filesystem/sector forms are not implemented.
Directory paths are limited to 127 UTF-8 bytes; individual filenames to 127
bytes, with longer full GDI paths supported. Track names in the GDI are safe
same-folder ASCII names, including quoted spaces. Unsupported/malformed images
produce an inspection error; they are never executed.

## Focused console acceptance

1. Open **Games**, select an existing named dump such as Dead or Alive 2, and
   check its displayed title and track count. Record the product/region and
   boot filename. Returning to Games should remain responsive.
2. If MDK2 is already on the card, inspect it too: its multiple audio/data
   tracks exercise the same saved-image mapping without reading the whole game.
   Use an existing numbered duplicate if convenient; no new rip is requested.
3. Open **Advanced > Browse SD folders** and select an existing GDI outside
   `/Games`, if available. A GDI-only folder should work without a K-UI manifest.
4. Try B during a browse/inspection if it lasts long enough, then refresh and
   open another image. Do not modify or unplug the card while the runtime owns it.
5. Save the log with **Diagnostics > Y**. Successful inspections log selected
   path, title, tracks, boot extent and metadata bytes read. For an error, send
   the log and the small `.gdi` descriptor; whole track uploads are unnecessary.

The production renderer's `home-games`, `games`, `games-detail`, `games-error`
and `games-advanced` previews are included in the full diagnostic artifact.

Host validation passed 659 image checks, 73 metadata scenarios and 52 real
FAT32/exFAT cases. Each filesystem case preserved the entire card image by
SHA-256 comparison. These are host results; console acceptance is limited to
the logged ARMADA inspections above. See [the validation record](evidence/games-foundation-host-2026-09-24.json).

## Implementation boundary

`src/core/game_image.c` is a portable read-only sector service with file
callbacks; `game_metadata.c` parses IP.BIN and bounded ISO9660 boot metadata.
`src/apps/games.c` owns mounted SD operations through the existing single worker.
All files close and the volume unmounts before returning to the UI. Existing
RAM music can keep playing; no optical command is issued by Games inspection.

The browser remains linked into `runtime.kui`. Resident test executables now
live separately under `/KUI/apps/games/`: the original synthetic `probe.kui` and
the selected-image `image-probe.kui`. These test payloads do not launch retail
games. IDE/CF sources remain unavailable until a backend is implemented and tested.
