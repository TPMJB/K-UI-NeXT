<!-- SPDX-License-Identifier: GPL-3.0-only -->
# K-UI V2.0 RC13 "Tyrfing"

Tyrfing continues K-UI's cursed-sword release names. This guide covers the
**2.0 RC13 source integration candidate**, not a published final 2.0 release.
Its package prefix is `kui-2.0-rc13-tyrfing`. For the existing stable download,
use [1.8.5](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.8.5) and its
[installation guide](release-v1.8.5.md).

The candidate combines the established native reader with saved per-image
launch options and optional SCI WinCE CD music using four checked kernel/sound
adapters. Read the [preparation notes](release-v2.0-notes.md) for the console
evidence and remaining release checks, and the [DS-based release comparison](release-v2.0-ds-comparison.md)
for retained features, remaining gaps and deferred work.

## Update an existing independent K-UI installation

1. With the console off, back up the current `KUI` folder and keep the complete
   working package for rollback. Preserve a known-working `KUI/recovery.kui`.
2. Merge the candidate's **complete matching `KUI` folder** into the card root,
   replacing supplied files and retaining preferences, covers, game dumps and
   saved launch settings. Install `runtime.kui` and the complete
   `KUI/apps/games` payload folder together. Earlier partial test ZIPs are not
   a substitute for the matching release package.
3. Safely eject the card and cold boot. Check **V2.0 RC13** and the displayed
   build ID against the package's `build.json`. The illustration says V2.0;
   the live version label distinguishes this RC from a final release.

RC13 retains the orange Tyrfing runtime/header artwork introduced in RC11 and a matching optional
CDI with the new boot splash, orange controls and sword badge. Burn that CDI
to replace artwork on the boot disc; the normal runtime update can continue
using your existing compatible independent K-UI CD. To preview the new boot
menu without burning, copy only the separate bootstrap package's
`KUI/tools.kui` to the card, pause the existing CD startup and choose
**Card tools**. Check **Card boot / recovery**, RC13 and the build ID, then
choose **Start K-UI**. The under-SEGA badge is stored on the disc and changes
only when that disc is replaced. See [the card preview guide](bootloader-refresh.md#check-the-final-artwork-without-reburning).

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
Standard LZ4 and explicitly selected historical LZO1X ZSO have separate
experimental native SCI Standard readers, with console acceptance pending; see
[ZSO scope](games-zso.md). CSO and CHD require PC import; see
[format boundaries](games-formats.md).

1. Open **Games** and select the title. Where paired copies exist, choose
   **Original** or **2048-byte copy**.
2. Press **Y Options** to inspect or change **Background loading** and
   **CD music**. **Advanced** contains boot encoding, background speed, image
   information and developer tests. Choices save automatically for this image;
   Original and converted copies have separate image paths and settings.
   For ZSO, select its known codec under **Options > ZSO codec** before
   inspection. If initial inspection fails, **Y Select ZSO codec** opens that
   choice; LZ4 is the default and no decoder fallback is attempted.
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

| Adapter established from | Recorded scope |
| --- | --- |
| Armada | Exact US runtime, T40301N 00; console audio evidence |
| Bust-A-Move 4 | Exact US runtime, T-8117N; console audio evidence |
| Worms Armageddon | Exact US runtime, T40601N; console audio and converted2048 cold-boot evidence |
| Worms World Party | Fourth kernel/sound adapter; scoped successful owner report, still Experimental |

These profiles require original **raw2352 AUDIO** tracks. Their DATA tracks
may be original raw or supported converted2048 copies. Adapter selection checks
the complete kernel identity and the runtime's sound/memory contracts. Other
images sharing an admitted implementation can be attempted as Experimental;
a matching title name or audio-track list alone does not establish admission
or successful gameplay. Unknown kernels are refused before handoff. Turn
**CD music Off** to attempt launch with the ordinary CE reader.
See [admission and evidence limits](experimental-cdda.md).

The latest Worms hardware report describes almost imperceptible gameplay lag
with music after the worker-budget and RAM-copy improvements. This is a scoped
owner report, not a zero-overhead or universal compatibility result. Earlier
Armada/BAM4/Worms tests retain documented audio/FMV limitations. The WinCE path
does not resolve Toy Commander's native FMV stalls.

## VMU and system tools

- In **VMU Manager → R Backups**, press **Y Import save file**, choose an
  external `.vms`, `.vmi` or `.dci`, review its name and metadata, then confirm
  staging a verified SD bundle. This step needs no attached VMU. See
  [format limits and filename rules](vmu-import.md). Use the existing separate
  restore preview/confirmation if you want to put the imported save on a VMU.
- In **L Actions**, choose **Copy all saves to VMU**, select the destination
  with Left/Right and review the whole batch before confirmation. Both cards
  must remain connected and SD backups must succeed. Conflicting names or
  inadequate capacity refuse the batch before writing. See
  [bulk-copy behavior and partial results](vmu-copy-all.md).
- In **VMU Manager**, open **L Actions → Backup card image** to save the
  selected formatted VMU as a verified 131072-byte raw VMD. An empty formatted
  card is supported. The backup never writes to the VMU.
- Open **R Backups**, select a `.vmd`, then use **X Extract selected** or
  **Y Extract all**. Extraction needs no attached VMU and creates new verified
  save bundles. To inspect an external raw image, place it under a folder such
  as `/KUI/backups/vmu/A1-0001/imported.vmd`. See [accepted paths and image
  limits](vmu-restore.md#full-card-vmd-backup-and-read-only-image-tools).
- In **Settings → System Tools**, choose **Compare settings flash** or
  **Compare visible BIOS** and select an image with A. Directories are browsed,
  not selected. The required sizes are 131072 and 2097152 bytes respectively.
  The result reports an exact match or differing-byte count and first offset.
  See [comparison scope](system-backups.md#compare-an-existing-image).

These additions are host-tested and await console acceptance. VMD extraction and external import
create bundles for the existing separately confirmed single-save restore workflow. Whole-card restore/format/overwrite and flash
programming are not provided. No DreamShell implementation code is reused.

## Existing tools and hardware boundaries

Disc Ripper, VMU tools, file operations, music, networking, diagnostics and
settings retain their existing roles. The
[1.8.5 guide](release-v1.8.5.md#rip-a-disc-in-another-format) describes retained
ripper exports. [Wi-Fi setup](wifi.md) and [FTP setup](ftp.md) remain applicable.
SCI networking uses SCIF storage because SCI storage reserves that port.
For the separate [PC capture destination](network-rip.md), the worker first
loads its local configuration, disconnects local storage, then writes directly
to an authenticated PC FAT32/exFAT volume image. Its helper is included under
`tools/`; local capture remains the default and console adapter acceptance
is pending.

IDE/CF remains experimental synchronous PIO without console validation.
ATA DMA and Windows CE on IDE/SCIF are not implemented. This candidate makes
no new Wi-Fi, Ethernet or storage-throughput claim. Time Stalkers, some native
FMVs and universal CDDA remain unresolved. Both native ZSO codec paths
still need console acceptance; CSO and CHD resident decoding remain
unimplemented. Direct CHD loading has its own [implementation plan](games-chd.md).

## Rollback and source

Restore the archived runtime and matching Games payload folder together, then
cold boot. Keep game dumps and preferences. A recovery runtime may also need
its matching external Games payloads.

Use the package's source/build records and checksum manifests to identify its
exact contents. A prepared package must include corresponding source and
licenses. New K-UI shell code is GPL-3.0-only except where stated; the separate
Wi-Fi firmware/link library is MIT and dependencies retain their own notices.
See [THIRD_PARTY.md](../THIRD_PARTY.md).
