<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Tyrfing compared with DS-based K-UI 1.0.1

Audit: 2026-10-15. Independent runtime source inspected at
`9e1efbf` (`2.0-rc10`), updated for the independent RC12 maintenance additions
and retained RC11 artwork. RC12 source/build records identify its final commit.
Legacy baseline: [K-UI_DS release `k-ui-1.0.1`](https://github.com/TPMJB/K-UI_DS/releases/tag/k-ui-1.0.1),
commit `2a5309298dde8fb100da1e2e4e10517695c9780f`, including GD Ripper 2.2.5
and Bootloader 3.3. That repository was previously named DreamShell_NeXT;
the existing legacy audits inspect the same source commit.

This is a comparison of shipped/reachable workflows and present independent
source. It is not a new hardware test. A legacy option establishes that the
old application offered it, not that every title or peripheral worked. In
particular, the [old compatibility record](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/docs/compatibility.md)
records a Bust-A-Move 4 serial-SD black screen, unvalidated IDE/CF hardware,
and incomplete individual VMU/flash test coverage.

## Release decision

**Tyrfing has enough implemented scope for a 2.0 release after acceptance of
the complete candidate. Full DreamShell feature parity is not a prerequisite.**
The largest remaining everyday gaps are full-card VMU restoration/broader save imports,
BIOS/region programming, direct CSO loading and broader storage/loader
compatibility. They require independent implementation rather than another
merge of existing work. Several smaller library and peripheral features also
remain below.

The release gate is the assembled package: its build, linked loader/layout,
source/licence and checksum checks, followed by the owner's console test.
Earlier feature ZIPs do not test the final combination. Keep experimental
capabilities labelled with their actual scope; do not describe unknown
CDDA/ZSO/IDE combinations as working because their UI or source exists.

## Implemented work that should not remain on a missing-features list

| Area | Present in Tyrfing | Evidence and remaining acceptance |
| --- | --- | --- |
| Games browsing | Alphabetical ordering across all pages/cache visits; saved folder/highlight by name; list/compact/gallery views; extracted/user artwork; grouped equivalent descriptors and Original/2048 copies | `4d79d63`, `81a5452`; [library behaviour](games-library.md), `src/apps/games.c`, `src/apps/games_covers.c`, `src/core/shell.c`. The ordering/remembered-selection addition still needs integrated console acceptance. |
| Per-image launch tools | Saved reader/music/encoding/codec choices, detected defaults, temporary unsaved baseline, Check executable, durable preparation reports, two existing bounded one-time native boot patches | `3114cc2`, `4d79d63`; `src/apps/game_launch_report.c`, `src/core/game_launch_settings.c`, `src/core/game_ram_patches.c`, [launch options](games-launch-options.md). New tools are source/host implemented; preparation success does not prove gameplay. |
| WinCE/native CD music | Broader checked runtime selection; four CE kernel/sound adapters established from Armada, BAM4, Worms Armageddon and World Party; native Toy/MDK2 executable/driver bindings | `53dc10f`; `src/apps/games_retail.c`, `include/kui/wince_cdda_profiles.h`, `src/core/games_cdda_profile.h`, [admission/evidence](experimental-cdda.md). World Party and unfamiliar matching images remain Experimental. This is a substantial serial-storage capability beyond the old release's recorded BAM4 failure, not proof of universal superiority. |
| Compressed ZSO first slice | Independent standard LZ4 and explicitly selected historical LZO1X readers, separate packages and validation | `d406863`, `4d79d63`; `src/core/zso.c`, `src/core/retail_zso.c`, [ZSO limits](games-zso.md). Native SCI Standard, one cooked track, 2048-byte blocks and at most 31 physical fragments; no CE/CDDA/background/patch combination. Console acceptance remains pending. |
| GD Ripper | Existing resumable/verified capture, reference catalogues and salvage; exact saved-job selection, custom folder label, retries, strict Mode 1 acquisition checks, cooked GDI export, opt-in single-session CD capture, direct PC destination, repair-to-copy and suspect reread evidence | `f502d29`, `4d79d63`; [ripper audit](audit-legacy-ripper.md), [controls](ripper-controls.md). All identified ripper workflow additions are implemented. The newly integrated operations need console acceptance, not another feature implementation. |
| VMU management | Selected/all-save SD backups, selected-save VMU copy, managed delete and verified restore to a new filename; startup LCD branding; RC12 verified raw 128 KiB VMD backup, read-only browsing and selected/all-save extraction | `9508dde`, `8b82f04`, `1b6f809`; `src/apps/vmu.c`, [restore](vmu-restore.md), [startup](vmu-startup.md). The recorded THPS2 backup/delete/restore round trip passed in the game; two-VMU copy, the new LCD presentation and RC12 image tools need their own acceptance. RC12 FAT32/exFAT tests also round-trip extracted bundles through existing restore. |
| System comparison | RC12 read-only comparison of exact-size SD images with all 128 KiB settings flash or the currently visible 2 MiB BIOS bank; exact byte-difference count/first offset and two-pass stability checks | `src/apps/maintenance.c`, [system tools](system-backups.md). Real FAT32/exFAT tests prove card images remain unchanged; console acceptance remains pending. No chip identification, bank switch or programming is inferred. |
| Files, music and diagnostics | Checked local copy/move/rename/delete/mkdir and file viewing; menu/custom WAV/Ogg playback and audio CD; allocated-buffer RAM test; storage Quick/Compare/Soak, history/baselines and reports | `2fc9453`, `8b82f04`, `eddc0d4`; [files](files.md), [app acceptance](apps-round-five.md), [storage tests](storage-testing.md). File Manager's new native-console workflow remains unaccepted. Storage speed testing is present. |
| Networking | W5500 and XIAO ESP32-C5/C6 FTP; Wi-Fi scan/join/band selection and saved adapter credentials; compatible card-supplied adapter firmware checks/install | `75e6776`, `cbdc383`, `3e3f86c`; [FTP](ftp.md), [Wi-Fi](wifi.md), [console updates](wifi-console-update.md). Final wireless timing, firmware update and direct-PC-rip adapter paths still need console acceptance; firmware 0.1.3 is built separately. |

## Actual remaining differences

Priorities mean follow-up value, not automatic 2.0 blockers. Deferred features
stay deferred even where the old application offered them.

| Area | Remaining difference from the old release | Priority and concrete next work |
| --- | --- | --- |
| VMU full-card and imported saves | Old VMU Manager offers full `.vmd` image creation/browsing/restoration, format/overwrite and broader external save inputs. RC12 now creates verified raw 128 KiB VMD images and browses/extracts saves from structurally valid standard images without a connected VMU. Current restore uses its verified `.vms` + `.dir` + `.crc` bundles under `/KUI/backups/vmu`, with no overwrite or full-card format. Broader standalone VMS/VMI/DCI import and bulk direct VMU-to-VMU Copy all are absent. | Useful post-2.0 parity work: independently add broader save-format inspection/import, then validate full-card restoration with a recoverable hardware setup. Old `applications/vmu_manager/`, `utils/README.vmu-manager.md`; new `src/apps/vmu.c`, [restore contract](vmu-restore.md). |
| BIOS and permanent region maintenance | Old BIOS Flasher offers chip/bank identification, complete-image compare and erase/program on supported programmable chips; Region Changer offers persistent factory/settings changes. Current System Tools exposes inventory, verified settings-flash/visible-BIOS backups and RC12 read-only complete-image comparison. Chip/bank identification and permanent changes remain absent. Original Sega mask ROM is not programmable. | Separate hardware project, not an RC defect. Identify the actual flash part/bank wiring and implement from its datasheet with a recoverable test setup. Old `applications/bios_flasher/`, `applications/region_changer/`, `modules/bflash/`; new `include/kui/maintenance.h`, `src/apps/maintenance*.c`, [boundaries](independent-app-parity.md), [backups](system-backups.md). |
| Compressed game containers | The old ISO-loader/ISOFS path has direct CSO and historical ZSO support. Tyrfing has the narrower experimental ZSO slice above; **CSO still needs PC import**. Broader block sizes, other transports and CE/CDDA/background combinations are not implemented for direct ZSO. | High-value later loader work: bounded CSO DEFLATE decoder and a separately proven resident layout, followed by measured console comparison. `src/core/zso.c`, [roadmap](games-parity-roadmap.md). **Direct CHD is a requested enhancement, not a demonstrated loss from 1.0.1**; see [CHD design](games-chd.md). |
| Retail compatibility and runtime controls | Time Stalkers still returns to K-UI; Toy Commander and some other FMVs stall/skip. Unknown native sound drivers and CE kernel/sound contracts need new adapters. Old automatic/manual OS, alternate executable, boot-method/placement, IRQ/async, region and CDDA controls are not interchangeable parameters for this independent loader. | Preserve the tested SCI/native baseline. Work from one reproducible failing title/trace at a time; do not add old numeric knobs without an implemented meaning. [Current evidence](release-v2.0-notes.md), `src/apps/games_retail.c`, `src/loader/retail_*`, [legacy Games audit](audit-legacy-games.md). These failures are known limits, not evidence that every corresponding title worked in 1.0.1. |
| Gameplay VMU/screenshots/new patch features | Old Games exposes SD-backed VMU emulation and screenshot hotkeys; more flexible old RAM-patch controls are outside the two existing one-time executable patch slots. | **Explicitly deferred by the owner**, together with alternate executable selection, additional boot methods and per-game region override. None is a Tyrfing blocker. See [independent design/sequence](games-parity-roadmap.md). |
| Games library conveniences | No flat recursive catalogue/category filters, selected-game external audio preview, Home game shortcuts, mouse pointer or Games-specific theme editor. Last selection, alphabetical sorting, artwork and three views already exist. | Optional post-2.0 usability work. Audio previews must yield to foreground I/O; a flat catalogue needs deterministic grouping and bounded refresh. Old `applications/games_menu/modules/{menu,preset,system_menu,module}.c`; new `src/apps/games.c`, `src/core/shell.c`. |
| Storage/filesystem breadth | Legacy apps can expose SD/IDE/PC and other device locations. Current apps mount one selected FAT32/exFAT volume; FAT16 is rejected. IDE/CF is implemented as experimental synchronous PIO but lacks console validation; ATA DMA and CE on IDE/SCIF are absent. ext4 loads bootstrap images only. | Keep exFAT as the working setup. G1-board work remains separate; later prove hardware coexistence, then DMA/background ownership. Multi-volume apps and writable ext4 need backend/extent work. `src/core/storage_probe.c`, `src/core/diskio.c`, `src/loader/retail_storage.h`, [transports](storage-transports.md), [filesystems](storage-filesystems.md). Old IDE paths also lacked documented physical acceptance, so do not claim an established IDE regression. |
| File Manager scope | Single selected-volume browsing/folder picker rather than the old two-pane/multi-device workflow; no general archive manager, script/module execution or old open-with framework. | Basic checked local operations are implemented. Cross-device copy should follow a multi-volume storage design; archive/script support is optional scope. Old `applications/filemanager/`; new `src/apps/files.c`, [files](files.md). |
| Network breadth | Old Network exposes general FTP/HTTP services and Ethernet/dial-up settings, including saved startup/NTP preferences. Current file service is W5500/Wi-Fi FTP; BBA/LAN provides inspection/temporary connection tests rather than that FTP backend. No HTTP server, PPP/dial-up, static-address UI or persistent Dreamcast NTP sync. Wi-Fi DNS/adapter UTC reading exists, so it is not correct to call all name/time access absent. | Optional later integrations. Preserve SCI bus ownership: networking on SCI needs storage on SCIF; SCI storage cannot share the port with W5500/Wi-Fi in the current implementation. `src/apps/{ftp_server,network_connect,network_w5500,network_wifi}.c`, [network test](network-connection-test.md), [Wi-Fi](wifi.md). |
| GD Play | Current app exits through KOS shutdown into the stock BIOS; normal BIOS region/autostart policy applies. It is not a general executable-loader replacement. | Validate disc boot/return on the owner's setup. [Independent handoff](independent-app-parity.md), `src/apps/gd_play.c`. The older app's loader machinery is not imported. |
| Memory/peripheral tools | Current RAM test checks only its allocated CPU-memory buffer, up to 4 MiB; it does not reproduce old selectable VRAM/AICA/OCRAM/full-region modes. Hardware keyboard/mouse input and the Dreameye app are absent. The virtual keyboard is present. | Optional tools; no game-loading blocker. `src/apps/memory_test.c`, `src/dreamcast/main.c`; old `applications/{memtest,dreameye}/`. No physical-memory sweep should borrow live game/shell memory. |
| General app ecosystem/arcade | Static independent apps do not replace DreamShell's Lua commands, dynamic modules/application API, launch-app framework or all legacy media handlers. Arcade, Cart Ripper and MIE JVS were included in the old package but target separate arcade hardware. | Deliberately different architecture and platform scope. These are not missing retail-release requirements. Old `applications/Makefile`, root `Makefile`, `utils/package_release.py` establish actual package inclusion, not merely source-tree presence; new [app architecture](app-architecture.md). |

## Migration and distribution differences

- **New bootstrap once when moving from DS-based K-UI.** The old DS boot disc
  does not load the independent `KUI/runtime.kui` contract. Existing compatible
  independent K-UI CDs can be retained for normal updates. The new boot artwork
  requires a new CD or the documented card-bootstrap preview path; changing
  the runtime alone cannot alter a burned CD. The old `DS/boot.cfg` device
  order, arbitrary `.BIN` paths, delay and debug/emulator core selection are
  not migrated; the independent menu uses fixed checked `.kui` paths and its
  Auto/SCIF/SCI/IDE source choices. This is a different boot contract, not a
  drop-in replacement for DreamShell core files.
- **Complete matching runtime and Games payloads.** Update the supplied `KUI`
  tree together. Retain a known-working recovery image and its matching Games
  payloads separately. Recovery is explicit; automatic rollback/update is not
  implemented. FTP protects startup files, so it is not an unrestricted runtime
  updater.
- **Keep exFAT and the existing images.** Compatible raw GDI files can be
  inspected/launched; no reformat, reripping or mandatory 2048 conversion is
  required. Old DS presets/settings/covers/module layouts are not automatically
  imported. Old `rip.state` is not an independent K-UI resume checkpoint;
  current `checkpoint-a.bin`/`checkpoint-b.bin` records are a different contract.
  An imported GDI's scan/repair support does not convert old partial jobs into
  resumable captures. The same distinction applies to standalone old save files versus
  the current verified restore bundle. RC12 can browse/extract structurally valid
  raw VMD images into that bundle format; it does not restore the whole image.
- **Different CD distribution.** Legacy 1.0.1 includes a self-contained full
  shell/apps CDI alongside its bootloader. The independent distribution is a
  bootstrap/recovery diagnostic CD plus storage runtime; there is no matching
  complete shell/apps standalone-on-CD package. This does not remove the
  primary SD/exFAT workflow.
- **No package-size superiority claim.** Legacy's 100,333,734-byte ZIP includes
  different core variants/CD content. Compare installed scope before inferring
  code or resident-RAM efficiency; see [the earlier size analysis](independent-app-parity.md).

## Concrete work before final publishing

1. Build the complete candidate from its final artwork/source commit and pass
   the ordinary host/filesystem, native/CE/ZSO layout/stack/instruction and
   package/source/licence gates. Ship all four CE music modules, the native
   modules and both ZSO codecs from that same commit. `tools/package.py`
   explicitly inventories these payloads; no historical test ZIP is a build input.
2. On the owner's console, confirm the updated splash/Home/VMU branding and
   matching build identity; reopen Games after adding a title to verify order
   and saved selection, then test an established native game and the accepted
   WinCE/music baseline. Preserve raw/2048 choices and a usable recovery path.
   Check that an unsupported music profile is refused clearly.
3. Smoke-test normal local rip/Stop/Resume/Verify and the new UI controls.
   For RC12, back up a formatted VMU as VMD, browse/extract a save, and compare
   the console with its own verified settings-flash/visible-BIOS backup. Record
   actual console acceptance separately for PC destination, CD capture,
   repair/evidence, ZSO and adapter firmware paths if exercised. Features left
   untested retain their explicit experimental/acceptance labels; do not turn
   them into general compatibility promises.
4. Publish accurate notes. The stale three-profile/broader-selection wording
   and old statement that console firmware updating was unavailable have been
   corrected with this comparison. Keep World Party scoped and Experimental;
   retain the named Time Stalkers/FMV/IDE/ext4/compression limitations. After
   owner acceptance, prepare the final 2.0 version/artwork/package together;
   publishing the RC does not itself authorize a final-release claim.

No DreamShell loader or flash-driver code was copied during this audit.
Future parity work must continue to use independently implemented runtime
code and preserve dependency licences/provenance. Read-only comparison of the
old behaviour is not permission to transplant its implementation.
