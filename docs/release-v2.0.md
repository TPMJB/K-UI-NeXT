<!-- SPDX-License-Identifier: GPL-3.0-only -->
# K-UI V2.0 RC10 "Tyrfing"

Tyrfing continues K-UI's cursed-sword release names. This guide covers the
**2.0 RC10 source integration candidate**, not a published final 2.0 release.
Its package prefix is `kui-2.0-rc10-tyrfing`. For the existing stable download,
use [1.8.5](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.8.5) and its
[installation guide](release-v1.8.5.md).

The candidate combines the established native reader with saved per-image
launch options and optional SCI WinCE CD music for three admitted US runtime
profiles. Read the [preparation notes](release-v2.0-notes.md) for the console
evidence and remaining release checks.

## Update an existing independent K-UI installation

1. With the console off, back up the current `KUI` folder and keep the complete
   working package for rollback. Preserve a known-working `KUI/recovery.kui`.
2. Merge the candidate's **complete matching `KUI` folder** into the card root,
   replacing supplied files and retaining preferences, covers, game dumps and
   saved launch settings. Install `runtime.kui` and the complete
   `KUI/apps/games` payload folder together. Earlier partial test ZIPs are not
   a substitute for the matching release package.
3. Safely eject the card and cold boot. Check **V2.0 RC10** and the displayed
   build ID against the package's `build.json`.

Keep your existing **exFAT card**. FAT32 remains supported. No reformat,
reripping or 2048-byte conversion is required. ext4 remains a read-only
bootstrap capability; it does not provide runtime or game-library access.
The card layout remains `/KUI`, `/Games` and optional `/Music`, including when
using SCI storage. See [filesystem support](storage-filesystems.md).

Keep a **compatible independent K-UI boot CD**. An existing SCI installation
requires its SCI-capable bootstrap; an older SCIF-only CD needs replacement
only when changing to SCI. DreamShell-based K-UI 1.0 cannot boot this independent
runtime. Updating the card does not change artwork or behavior stored on the
boot CD. See [boot and recovery](boot-recovery.md).

## Launch games

Place each image and all its referenced tracks in a folder under `/Games`.
Supported GDI, ISO, BIN/CUE, CDI and standalone BIN/IMG layouts remain available.
CSO/ZSO/CHD require PC import; see [format boundaries](games-formats.md).

1. Open **Games** and select the title. Where paired copies exist, choose
   **Original** or **2048-byte copy**.
2. Press **Y Options** to inspect or change **Background loading** and
   **CD music**. **Advanced** contains boot encoding, background speed, image
   information and developer tests. Choices save automatically for this image;
   Original and converted copies have separate image paths and settings.
3. Use **START** from Options, or **A Launch** from details/confirmation, to
   launch with those saved choices. Keep the card inserted while playing.

Native games retain the standard reader and the SCI background reader, with
Normal/Aggressive background speed under Advanced. Windows CE requires the
background SCI reader. Use **CD music Off** for the ordinary CE loader.

### Optional Windows CE CD music

For a matching profile on **SCI microSD**, set **Background loading On** and
**CD music On**. New images default to music Off. The toggle is offered to
inspected WinCE GDIs containing high-density audio tracks; the loader still
checks the selected runtime before handoff.

| Current admitted runtime | Profile scope |
| --- | --- |
| Armada | Exact admitted US runtime, T40301N 00 |
| Bust-A-Move 4 | Exact admitted US runtime, T-8117N |
| Worms Armageddon | Exact admitted US runtime, T40601N |

These profiles require original **raw2352 AUDIO** tracks. Their DATA tracks
may be original raw or supported converted2048 copies. A matching title name
or audio-track list alone does not establish admission. Other regions,
revisions and titles need compatible runtime adapters; an unsupported music
profile is refused before handoff. Turn **CD music Off** to attempt launch
with the ordinary CE reader. See [profile scope](wince-cdda-next-titles.md).

The latest Worms hardware report describes almost imperceptible gameplay lag
with music after the worker-budget and RAM-copy improvements. This is a scoped
owner report, not a zero-overhead or universal compatibility result. Earlier
Armada/BAM4/Worms tests retain documented audio/FMV limitations. The WinCE path
does not resolve Toy Commander's native FMV stalls.

## Existing tools and hardware boundaries

Disc Ripper, VMU tools, file operations, music, networking, diagnostics and
settings retain their existing roles. The
[1.8.5 guide](release-v1.8.5.md#rip-a-disc-in-another-format) describes retained
ripper exports. [Wi-Fi setup](wifi.md) and [FTP setup](ftp.md) remain applicable.
SCI networking uses SCIF storage because SCI storage reserves that port.

IDE/CF remains experimental synchronous PIO without console validation.
ATA DMA and Windows CE on IDE/SCIF are not implemented. This candidate makes
no new Wi-Fi, Ethernet or storage-throughput claim. Time Stalkers, some native
FMVs, universal CDDA and compressed resident game booting remain unresolved.

## Rollback and source

Restore the archived runtime and matching Games payload folder together, then
cold boot. Keep game dumps and preferences. A recovery runtime may also need
its matching external Games payloads.

Use the package's source/build records and checksum manifests to identify its
exact contents. A prepared package must include corresponding source and
licenses. New K-UI shell code is GPL-3.0-only except where stated; the separate
Wi-Fi firmware/link library is MIT and dependencies retain their own notices.
See [THIRD_PARTY.md](../THIRD_PARTY.md).
