<!-- SPDX-License-Identifier: GPL-3.0-only -->
# K-UI 2.0.0 “Tyrfing”

This guide covers the final 2.0.0 package. Read the
[cumulative release notes since 1.7.5](release-v2.0-notes.md) for changes,
hardware evidence and experimental boundaries.

[Downloads](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v2.0.0) ·
[Screenshots and posting images](release-v2.0-image-index.md) ·
[Reddit/forum drafts](release-v2.0-announcements.md)

## Update an existing independent K-UI installation

1. With the console off, back up `KUI` and retain the complete working package
   for rollback. Preserve a known-working `KUI/recovery.kui` and its matching
   Games payloads separately.
2. Merge the release's **complete matching `KUI` folder** into the card root,
   replacing supplied files while retaining preferences, covers, dumps and
   saved launch choices. `runtime.kui` and all of `KUI/apps/games` must come
   from the same build. Do not assemble the final installation from partial
   test ZIPs.
3. Safely eject the card and cold boot. Check **V2.0.0** and its build ID
   against the package's `build.json`.

Keep your existing **exFAT card**. FAT32 is supported too. No reformat,
reripping or 2048-byte conversion is required. The root layout stays `/KUI`,
`/Games` and optional `/Music`, including SCI storage. ext4 is read-only
bootstrap support, not runtime app/game-library access. See [filesystems](storage-filesystems.md).

Compatible legacy JSON launch presets migrate only when both new binary
settings slots are absent. Existing binary profiles and original JSON files
are preserved. Retired comparison modes are skipped with a log; oversized or
invalid legacy collections are explicitly refused rather than truncated.

## Which boot CD to use

| Existing setup | Boot requirement |
| --- | --- |
| Compatible independent K-UI SCIF installation | Keep the working CD |
| SCI microSD with a compatible SCI-capable independent bootstrap | Keep the working CD |
| Old SCIF-only bootstrap, changing to SCI | Burn the supplied current CDI |
| DreamShell-based K-UI 1.0 | Burn the supplied current CDI; its CD cannot boot this runtime |
| Experimental IDE/CF | Compatible multi-transport bootstrap; hardware remains unvalidated |

The card update changes runtime artwork. New pixel boot artwork and the
under-SEGA badge require the matching new CD; compatible old CDs still boot
the updated runtime. To preview the boot menu without burning, use the
separate bootstrap package's `KUI/tools.kui` and the CD's **Card tools** choice.
See [bootloader preview](bootloader-refresh.md#check-the-final-artwork-without-reburning).

The graphical bootstrap selects Auto, SCIF, SCI or IDE/CF. Auto tries those
transports in order. Hold **B** during startup for the built-in CD path.
See [boot and recovery](boot-recovery.md).

## Launch games

Put each image and its referenced tracks in a folder under `/Games`. Direct
layouts include supported GDI, ISO, BIN/CUE, CDI and standalone BIN/IMG.
LZ4/LZO1X ZSO readers are experimental native SCI Standard paths; CSO/CHD need
the supplied PC importer. See [formats](games-formats.md).

1. Open **Games** and select a title. Where copies are paired, choose
   **Original** or **2048-byte copy**.
2. Press **Y Options** for **Background loading** and optional **CD music**.
   **Advanced** holds encoding, background speed, image information and
   preparation tools. Settings save per image; Original/2048 paths can differ.
3. Use **START** from Options, or **A Launch** from details/confirmation.
   Keep the card inserted while playing.

For ZSO, choose its known codec in **Options → ZSO codec** before inspection.
If inspection fails, **Y Select ZSO codec** opens that choice. LZ4 is the
default; no codec guessing occurs. The ripper's historical LZO output needs
the explicit LZO choice. ZSO cannot combine with background loading, CE or CD
music and still needs console acceptance.

Native games have Standard and SCI Background readers; Advanced offers
Normal/Aggressive background speed. **CE requires Background on SCI.**
Use **CD music Off** for ordinary CE loading.

Games remains alphabetical after additions and remembers the last folder and
highlight. **X Refresh** rebuilds discovery after card/collection changes.
**Start → Scan box art** obtains covers. First discovery can take several
seconds; recent catalogue visits and selected-title artwork are cached.

Games does not change the stored image bytes; normal saves may write to a VMU.
**A+B+X+Y+Start** restarts K-UI when a game invokes the BIOS menu; otherwise
power cycle. Keep the boot CD inserted for that restart.

### Optional CD music

For an admitted image on **SCI microSD**, enable **Background loading** and
**CD music**. New images default to music Off. Original **raw2352 AUDIO** is
required; supported DATA can be raw or converted2048.

CE adapters were established from US Armada, Bust-A-Move 4, Worms Armageddon
and Worms World Party. Admission checks runtime identity and sound/memory
contracts. World Party and unfamiliar matching images remain Experimental.
Native adapters retain the checked Toy Commander/MDK2 runtime bindings.
Unknown combinations are refused with music On; turn it Off to attempt the
ordinary path. Toy Commander's FMV stutter remains unresolved. See
[music scope and evidence](experimental-cdda.md).

## Rip and verify discs

Disc Ripper **A** opens New confirmation, **X** resumes and **Y** verifies
saved files. **Start** opens Advanced. New uses a free disc-title folder under
`/Games` by default. Resume/Verify use the job's recorded format.

**Capture settings** selects GDI, BIN/CUE, CSO, historical LZO ZSO, CHD or GDI
2048, plus hashes, saved-file readback, bounded retries and optional strict
Mode 1 checks. Exports retain and verify raw files; allow space for both.
Advanced offers exact saved-job selection and optional new-folder labels.

Separate salvage, repair-to-copy and reread evidence tools retain originals.
Single-session CD capture is an explicit declaration, not session detection.
The experimental PC destination writes to an authenticated FAT32/exFAT volume
image via the bundled helper, not a shared folder. These new paths retain
individual console-test limits. Read [ripper controls](ripper-controls.md) and
[PC destination setup](network-rip.md).

Saved-file verification establishes consistency with recorded hashes; only
**FULL TRACK MATCH** establishes all-track agreement with an independent
catalogue. Check a complete capture on a PC with:

```sh
python3 tools/verify_dump.py "/path/to/card/Games/MDK2"
```

## VMU and system tools

- **VMU Manager → L Actions → Backup card image:** verified raw 128 KiB VMD,
  including an empty formatted card.
- **R Backups → X Extract selected / Y Extract all:** browse VMDs and create
  verified save bundles without a connected VMU.
- **R Backups → Y Import save file:** preview supported VMS/VMI/DCI and stage
  a verified bundle. Physical restore remains a separate confirmed action.
  Bare VMS data requires a correct short filename.
- **L Actions → Copy all saves to VMU:** preview destination, capacity and
  conflicts. All source saves are backed up before writes; keep both cards
  connected. Failed/stopped batches report verified completed copies.
- **Settings → System Tools → Compare settings flash / Compare visible BIOS:**
  compare exact-size images with stable reads: 131072 bytes for settings flash,
  2097152 for the visible BIOS. No programming occurs.

These additions have host/fault/filesystem checks; game acceptance does not
establish every maintenance workflow. Whole-card restore, format/overwrite,
BIOS programming and permanent region changes are not supplied. Read
[VMU limits](vmu-restore.md), [imports](vmu-import.md), [bulk copy](vmu-copy-all.md)
and [system comparison](system-backups.md).

## Networking and firmware

W5500 Ethernet/FTP and XIAO ESP32-C5/C6 Wi-Fi use SCI with **storage on SCIF**.
SCI storage owns the same port. C5 supports 2.4/5 GHz; C6 uses 2.4 GHz.
**Network → Start** opens scan/join/band settings; **Y** starts FTP after join.

Adapter firmware is separate. The Wi-Fi page checks/installs compatible
card-supplied app updates after confirmation; initial programming/recovery
still uses USB. New timing/update paths retain console-test limits. See
[Wi-Fi](wifi.md), [updates](wifi-console-update.md) and [FTP](ftp.md).
FTP protects startup files; use a card reader for runtime updates.

## Rollback, build records and limits

Restore the archived runtime and matching Games payloads together, then cold
boot. Keep dumps/preferences. Recovery may need matching external payloads too.

`build.json`, source records and checksum manifests identify the exact files.
Final packages include corresponding source, dependency pins and licenses.
The RC15 owner retest was accepted on October 15, 2026 (America/Chicago);
optional ZSO, maintenance, recovery/CD/PC and wireless-update paths retain the
limits in the [release notes](release-v2.0-notes.md).

IDE/CF remains experimental synchronous PIO without physical acceptance.
ATA DMA, CE on IDE/SCIF, runtime ext4, universal CDDA and some FMV fixes remain
future work. The separate board preview targets version 3 after hardware works;
it is not required for 2.0.0.

New shell code is GPL-3.0-only except where stated. Separate Wi-Fi firmware and
kwlink are MIT; dependencies retain their notices. See [THIRD_PARTY.md](../THIRD_PARTY.md).
