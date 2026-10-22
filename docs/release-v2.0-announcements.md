<!-- SPDX-License-Identifier: GPL-3.0-only -->
# K-UI 2.0.0 “Tyrfing” — announcement drafts

Ready-to-post copy for use once v2.0.0 and its downloads are public. These are
drafts; saving this file does not publish them. Choose images from the
[press image index](release-v2.0-image-index.md). Keep interface renders'
example/host-render labels and board renders' development-preview labels.
The [release notes](release-v2.0-notes.md) retain detailed evidence and limits.

## Reddit — title

K-UI 2.0.0 “Tyrfing” — Dreamcast SD loading, optional CD music, better game browsing and more

## Reddit — body

I've brought the work since 1.7.5 together as **K-UI 2.0.0 “Tyrfing.”** I didn't
give 1.8.5 a separate public release post, so those changes are included too.

K-UI is my independent Dreamcast environment built on KallistiOS, with its own
shell, game loader and disc ripper. It keeps the original GD-ROM installed and
uses exFAT/FAT32 storage. Keep your compatible independent K-UI boot CD and
existing dumps: update the complete supplied `KUI` folder on the card.

The main changes:

- **Native compatibility and FMVs:** reader placement and GD-command fixes
  cover working Sonic Adventure, Grandia II, Skies of Arcadia and Power Stone
  1/2 tests. The final RC restores the accepted Skies/Time Stalkers behavior
  and Evolution/Evolution II opening-FMV improvement; my priority retest works.
- **A better Games library:** always alphabetical, remembered folder/highlight,
  cached browsing, selected-title covers, grouped Original/2048 copies and
  saved launch options per image.
- **More formats:** supported GDI, ISO, BIN/CUE, CDI and raw BIN/IMG layouts.
  Batch conversion makes separate 2048-byte copies. Direct LZ4/LZO ZSO is
  experimental on native SCI Standard; CSO/CHD use the PC importer.
- **Optional SCI CD music:** checked CE adapters established from Armada,
  Bust-A-Move 4, Worms Armageddon and Worms World Party, plus native Toy
  Commander/MDK2 bindings. Worms input lag is much improved in my tests.
  Music defaults Off; exact runtime matters, and World Party and unfamiliar
  matching images remain Experimental.
- **More ripper tools:** GDI/BIN-CUE/CSO/ZSO/CHD/cooked-GDI outputs, exact job
  selection, folder labels, retries, strict Mode 1 checks, repair-to-copy and
  reread evidence. An experimental authenticated PC-volume destination and
  declared single-session CD capture have their own hardware-test limits.
- **More VMU tools:** verified VMD backups, image browsing/save extraction,
  supported VMS/VMI/DCI import and previewed Copy all between VMUs. System
  Tools compares settings-flash/visible-BIOS images without programming.
- **Presentation/networking:** orange pixel Tyrfing artwork, completed Home
  icons, K-UI/version on the VMU, retained W5500 FTP, C5/C6 Wi-Fi settings and
  compatible card-supplied adapter update support.

Compatibility is still title-dependent. Toy Commander's intro FMV stutter is
unresolved; CD music isn't universal. New ZSO, VMU, recovery, PC/CD and wireless
update paths have individual hardware-test limits. SCI networking needs SCIF
storage because SCI storage uses the same port. IDE/CF is unvalidated PIO;
ext4 is bootstrap-only. The full notes distinguish console reports from host tests.

I've also included a preview of the separate custom internal board.
**Getting that hardware working is my intended version 3 milestone.** The
render is a design preview, not a released or electrically validated board;
2.0.0 uses the existing supported hardware.

[Download 2.0.0 and read the changelog](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v2.0.0)
· [Source](https://github.com/TPMJB/K-UI-NeXT)
· [Screenshots/press images](https://github.com/TPMJB/K-UI-NeXT/blob/main/docs/release-v2.0-image-index.md)
· [Optional development support](https://ko-fi.com/tpmjb)

## Forum — BBCode

```bbcode
[b]K-UI 2.0.0 “Tyrfing” for Dreamcast[/b]

I've brought the work since 1.7.5 into one public update, including the 1.8.5 changes I didn't separately announce.

K-UI is my independent KallistiOS-based environment with its own shell, game loader and disc ripper. The original GD-ROM stays installed. It uses exFAT/FAT32 cards. Keep a compatible independent K-UI boot CD: replace the complete supplied KUI folder, keeping preferences and dumps.

[b]What's changed[/b]
[list]
[*]Native reader/GD-command fixes, with working Sonic Adventure, Grandia II, Skies of Arcadia and Power Stone 1/2 tests. The final RC restores accepted Skies/Time Stalkers behavior and Evolution/Evolution II opening-FMV improvement; my priority retest works.
[*]Alphabetical Games lists, remembered folder/highlight, cached browsing, selected-game artwork, Original/2048 grouping and saved per-image launch choices.
[*]Supported GDI, ISO, BIN/CUE, CDI and raw BIN/IMG layouts; optional batch 2048-byte conversion. Direct LZ4/LZO ZSO is experimental on native SCI Standard. CSO/CHD use the PC importer.
[*]Optional SCI CD music through checked adapters established from Armada, Bust-A-Move 4, Worms Armageddon and Worms World Party, plus native Toy Commander/MDK2 bindings. Worms responsiveness is much improved in my tests. Music defaults Off; World Party and unfamiliar matching images remain Experimental.
[*]Ripper output choices, exact saved-job selection, folder labels, retries, strict Mode 1 checks, repair-to-copy, reread evidence, declared single-session CD capture and an experimental direct PC-volume destination.
[*]Verified VMD backups, image browsing/extraction, supported VMS/VMI/DCI import, previewed Copy all between VMUs and read-only settings-flash/BIOS comparison.
[*]Orange pixel Tyrfing artwork, completed Home icons, K-UI/version on the VMU, retained W5500 FTP, C5/C6 Wi-Fi and compatible card-supplied adapter update support.
[/list]

[b]Scope[/b]
Compatibility is title-dependent. Toy Commander's intro FMV stutter remains unresolved. CD music is not universal. New ZSO, maintenance, recovery, PC/CD and firmware-update paths have individual hardware-test limits. SCI networking needs SCIF storage. IDE/CF remains unvalidated PIO; ext4 is bootstrap-only. See the full notes.

[b]Looking ahead[/b]
The separate custom internal board is a design preview. Getting it working is my intended version 3 milestone; it is not a 2.0.0 requirement or a validated shipping board.

[url=https://github.com/TPMJB/K-UI-NeXT/releases/tag/v2.0.0]Download and changelog[/url]
[url=https://github.com/TPMJB/K-UI-NeXT]Source and documentation[/url]
[url=https://github.com/TPMJB/K-UI-NeXT/blob/main/docs/release-v2.0-image-index.md]Screenshots and press images[/url]
[url=https://ko-fi.com/tpmjb]Optional development support[/url]
```

## Short post

K-UI 2.0.0 “Tyrfing” combines the Dreamcast work since 1.7.5: native compatibility/
FMV improvements, alphabetical cached Games browsing, Original/2048 selection,
saved per-image choices, optional checked SCI CD music, expanded ripper and VMU
tools, and orange pixel artwork. Keep a compatible independent K-UI CD and
update the complete KUI folder on exFAT/FAT32. The notes cover title-dependent
compatibility, experimental paths and known FMV limits.

[Download and changelog](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v2.0.0)

## Press introduction

**K-UI 2.0.0 “Tyrfing” brings a substantial software update to the Dreamcast
while retaining its original GD-ROM drive.** The independent KallistiOS-based
environment combines its own game loader and verified/resumable disc ripper
with VMU management, file tools, music, networking and diagnostics.

The cumulative update since 1.7.5 includes native compatibility and FMV
improvements, alphabetical Games browsing with saved launch choices,
Original/2048-byte selection, expanded image support and optional checked SCI
CD music. Ripper and VMU workflows expand alongside new orange pixel artwork.
The notes distinguish console reports from host-tested/experimental features,
including the remaining Toy Commander FMV issue and hardware-specific storage
boundaries.

A separate internal G1 microSD / Wi-Fi carrier is in development, designed to
retain the original GD-ROM and provision optional BIOS circuitry. **Working
board hardware is the intended version 3 milestone.** Its native KiCad render depicts the design in
development, without establishing manufacture, fit, electrical operation or
performance. The 2.0.0 software continues to support existing setups.

[Full-resolution board preview and captions](../resources/release-v2.0/board/README.md)

Source, installation, checksums and downloads:
https://github.com/TPMJB/K-UI-NeXT/releases/tag/v2.0.0

## Image captions

- **Tyrfing artwork:** K-UI 2.0 artwork; live version/build labels identify the
  installed software.
- **Interface renders:** K-UI interface rendered on a host with example titles,
  metadata and counters; not a photograph or gameplay capture.
- **Board preview:** custom board design in development; intended future
  version 3 milestone, with hardware validation still ahead.

The image index supplies exact filenames, origins and recommended posting
order. Keep hardware evidence separate from promotional feature cards in captions.
