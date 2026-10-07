# K-UI V1.8.5 "Dáinsleif"

An independent Dreamcast shell built directly on upstream KallistiOS, retaining
the original GD-ROM and using exFAT/FAT32 storage.

**Version 1.8.5 fixes the native reader placement that blocked Sonic Adventure
and Grandia II; the owner also reports Skies of Arcadia works.** Games adds
GDI/ISO/BIN-CUE/CDI/raw-image support, an Original/2048 picker, RAM-cached lists
and selected-title artwork. Disc Ripper adds BIN/CUE and verified CSO/ZSO/CHD
exports. SCI Wi-Fi integration and separate ESP32-C5/C6 firmware are included.
The splash identifies 1.8.5 and retains the Dáinsleif artwork.

Download the [1.8.5 release](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.8.5),
then read the [installation guide](docs/release-v1.8.5.md) and
[release notes](docs/release-v1.8.5-notes.md). Install the runtime and complete
Games payload folder together. Existing exFAT cards, game dumps and compatible
independent K-UI boot CDs stay usable. DreamShell-based K-UI 1.0 requires the
supplied new CDI. No reformat, reripping or 2048 conversion is required.

Windows CE defaults to its background SCI reader and remains experimental;
audio/FMVs and title compatibility are limited. Time Stalkers and some native
FMV stalls remain unresolved. Compressed games require PC import before launch.
IDE/CF remains untested synchronous PIO; ATA DMA and CE on IDE/SCIF are future
work. ext4 is available only to the read-only bootstrap, not runtime apps or
Games.

The historical [1.7 notes](docs/release-v1.7-notes.md) and
[1.5.1 notes](docs/release-v1.5.1-notes.md) remain available.

The current SD runtime adds **raw-track GDI capture, saved-file verification and
controlled resume** to the independently booting hardware diagnostic. It stays
in RAM after you swap discs and uses FAT32/exFAT through the standard external
serial SD adapter. Keep the reusable CD and update `/KUI/runtime.kui`.

**Disc reading is done and verified. For the full state, read
[docs/HANDOFF-disc-reader.md](docs/HANDOFF-disc-reader.md) first.** A whole GD-ROM now rips in
about 20 minutes (it was over 100 with the original settings) and has been proven byte-exact
against the TOSEC catalogue on two discs: Sword of the Berserk (3 tracks) and MDK2 (31 tracks,
27 of them audio), on the console and again on a PC. MDK2 was also interrupted twice mid-disc and
resumed to a verified finish.
The capture engine also passes host-image tests. Earlier SD runtime launch and
disc/exFAT probes passed on a physical console. The user also
confirms B selects CD fallback and reports successful cold boots; the boot count
was not specified. Missing-file detection, checksum rejection and restoring the
good runtime have now passed by user report; see the
[hardware evidence](docs/hardware-evidence.md) for remaining coverage.
New K-UI shell code uses GPLv3; the Wi-Fi firmware/link library is MIT, and
dependencies retain their own licenses. There is no
separate contribution or commercial-relicensing agreement.

## The ten-app shell

The SD runtime opens ten apps, in this order on Home: **Games**, **Disc Ripper**,
**VMU Manager**, **File Manager** (new since 1.5.1), **Music Player**, **GD Play**,
**Memory Test**, **Network**, **Diagnostics** and **Settings**. New captures use a selectable
parent folder, defaulting to `/Games`, with title-based folders and GDI filenames.
The established raw acquisition engine remains in place; new format exports
have separate host validation and still need console acceptance.

Coming from DreamShell-based K-UI 1.0? Burn the supplied 1.8.5 CDI; that older
CD cannot boot this independent runtime. For an existing independent K-UI
installation, use its compatible bootstrap CD and merge the release package's `KUI` folder
onto the SD card as described in the [installation guide](docs/release-v1.8.5.md).
Update the runtime and Games payload together; optional original menu music
is included.
See [the current app acceptance round](docs/apps-round-five.md) and
[app boundaries and the later executable-loader plan](docs/app-architecture.md).
These new app paths still need physical-console acceptance.

- Select with D-pad or stick and open with A. B returns home while idle.
- The startup splash is capped at three seconds; B skips the shortened startup cue.
  Home Y cycles volume/off; L/R on Home and Ripper select songs. The header names the current song.
- GD Play exits through normal KOS shutdown to the stock BIOS. Music Player
  caches PCM16 WAV or Ogg Vorbis songs up to 6 MiB for background playback across apps.
  The developer `sd-update` includes an original one-minute WAV/Ogg sample.
  Music Player supports custom-song cycling, explicit cache clearing and a separate audio-CD player.
  Since 1.5 the menu songs ship as Ogg Vorbis and share one decoder, and Harbor
  Lights joins the rotation as a sixth song. All six cached use 1.06 MiB of RAM,
  against 4.46 MiB for the five 1.5 WAVs, which cards without the Oggs still play.
  The startup chime is embedded as Ogg too ([details](docs/music-round-five.md#bundled-menu-songs-as-ogg)).
- Ripper: A confirms a new dump, X resumes the newest matching job, Y verifies.
  Start opens Advanced, **Destination folder** and **Capture settings**.
  Output choices are GDI, BIN/CUE, CSO, DreamShell-LZO ZSO and CHDv4; compressed
  exports retain the verified raw capture for resume and verification.
  Advanced also offers explicitly confirmed Quick resume (sizes only), preserving
  full Resume and its saved-byte checks. During work, B requests Stop. Reports still save automatically. Idle insertion
  detection shows the disc title; moving capture/verification phases show percentage and ETA.
  Current retry attempts and the cumulative job total are labelled separately.
- System Settings: 640x480 TV timing with reversible preview, memory display,
  music enabled and volume, startup chime/app, local clock, TV safe area, menu sounds
  and restore-defaults. System Tools adds inventory, verified flash/visible BIOS backups and Restart.
  Capture hashes/readback remain inside the ripper. New file dates follow the RTC.
- VMU Manager: read saves, make verified SD backups, and preview/confirm restore
  into a free filename with readback. Managed copy/delete require confirmation and
  a verified restorable SD backup. No overwrite or format. Memory Test checks only its allocated RAM.
- Ripper Advanced CRC scans completed saved jobs and reports hash/Mode 1 sector
  errors separately. Already-read track sizes and CRCs are compared with the
  Redump/TOSEC catalogues, including GDI-only folders without a manifest. Only a
  full reference match establishes all-track agreement; unavailable/partial
  references retain the limited structural result.
  Separate Salvage jobs add durable bad-sector queues, optional zero filling and
  bounded repair passes; unresolved holes never receive a complete-dump claim.
- File Manager browses every folder and file on SD and opens games, music and
  pictures. Copy, move, rename and delete are checked and confirmed first;
  copies are read back and compared before they take their name, nothing is
  replaced, and the files K-UI needs to start are protected
  ([details](docs/files.md)). It has not yet been tried on a console.
- Network includes a temporary DHCP/address-conflict/gateway-ping test; it
  does not claim Internet reachability or change saved network settings. With a
  W5500 wired to the SCI port (a modification), Network also finds that adapter
  and runs an FTP server for the SD card: uploads take their name only once
  complete, and K-UI's start-up files are protected
  ([details](docs/ftp.md)). Earlier overlapping-transfer builds reached about
  0.8 MiB/s uploads and 0.5 MiB/s downloads in the tested W5500/SCIF setup;
  these are historical results, not new 1.8.5 measurements.
  A XIAO ESP32-C5/C6 with the separate K-UI firmware provides SCI Wi-Fi;
  Network Start opens scan/join and band selection. Storage remains on SCIF
  while networking owns SCI. Flash firmware by USB from a computer; console
  firmware updating is not exposed ([setup](docs/wifi.md)).
  Diagnostics retains disc/SD probes, log export,
  mstats and benchmarks. RAM remains visible in the ripper.
- Music keeps playing from RAM during menu actions and capture; uncached song
  changes wait until the storage worker is idle. Console continuity still needs checking.
- Repeated drive failures can retain PIO until reboot. The ripper now keeps that
  warning visible; the accepted DMA stop/lid policy is unchanged.

Defaults now select **CRC32, automatic end readback Off, DMA and 2 Hz busy redraws**:
these are the policy choices used by the accepted fast captures. Explicit
`bench.cfg` keys override saved preferences, and the log prints the effective
options. Resume keeps the existing job's hash mode. **Y Verify always rereads saved
files**. Only an independent **FULL TRACK MATCH** gives the stream CRC badge a
green result; partial matches and saved-file verification remain separate.

A title such as MDK2 produces `/Games/MDK2/MDK2.gdi`; another New uses
`/Games/MDK2 (2)/MDK2.gdi` without overwriting the first. Resume/Verify select the
greatest numbered matching-disc checkpoint in the chosen parent, with legacy
`/KUI/dumps` fallback. Existing jobs keep their format. See
[ripper controls](docs/ripper-controls.md) and [the capture format](docs/capture-format.md).

The reusable CD's built-in diagnostics remain available by holding B during
startup. Missing or invalid runtime packages fall back automatically. Keep using
the working CD; this UI update needs no new burn. See [SD bootstrap](docs/sd-bootstrap.md)
and [hardware evidence](docs/hardware-evidence.md) for the established boot checks.

## Build and test

On Ubuntu 24.04 / a compatible Linux or WSL installation:

```sh
sudo apt-get install build-essential git curl wget patch python3 bison flex \
  texinfo gettext libgmp-dev libmpfr-dev libmpc-dev libisl-dev \
  meson ninja-build pkg-config libisofs-dev libpng-dev libjpeg-dev \
  dosfstools exfatprogs mtools ffmpeg
python3 tools/fetch_deps.py --fatfs-only
make test test-images
bash tools/setup_kos.sh
source .deps/kos/environ.sh
make diagnostic
python3 tools/package.py
```

Packaging requires a clean committed source tree, so the source archive matches
the executable. The initial compiler build is substantial; subsequent builds
reuse it. Dependencies are pinned in [dependencies.json](dependencies.json).
The scripts use upstream KOS's stable SH-4 compiler profile: GCC 15.2.0,
Binutils 2.45.1 and Newlib 4.6.0.20260123. No DreamShell checkout is needed.

Host tests run the same portable C command, TOC, block-device and filesystem code
as the console app. Filesystem tests use disposable regular image files and
independent `mkfs`/`fsck` tools. They do not operate on physical disks.

## Milestone 1

The complete goal remains: boot independently, use FAT32/exFAT serial SD storage,
capture every supported retail GD-ROM track, and verify and resume saved dumps.

| Stage | Status |
| --- | --- |
| M1.0: source/build foundation and visible boot diagnostic | **Done.** Diagnostic runs and display fix confirmed; dozens of cold boots with no controller problems |
| M1.1: raw-disc and FAT32/exFAT capability probes | **Done.** Disc probes (Sword of the Berserk, MDK2) verified; exFAT and FAT32 both verified end to end. On FAT32 the runtime loads, the storage test passes and a whole disc rips byte-exact, about 2.4% slower than exFAT ([evidence](docs/evidence/fat32-sword-dma-2026-09-20.json)) |
| M1.2: validated runtime loading from SD | **Done.** exFAT handoff, B-selected fallback, missing-file rejection and good-runtime restoration confirmed; all five malformed fixtures rejected on hardware, each with its own reason, with a usable fallback ([evidence](docs/evidence/m12-runtime-rejection-2026-09-20.json)) |
| M1.3: full-track GDI capture | **Done.** Sword of the Berserk and MDK2 (31 tracks, 27 audio) captured and verified against TOSEC on the console and on a PC; Sword ripped three ways, identical by SHA-256 ([handoff](docs/HANDOFF-disc-reader.md)) |
| M1.4: SHA-256 capture verification and controlled stop/resume | **Done.** MDK2 stopped twice mid-disc and resumed to a finish verified on the console and against TOSEC; Sword track 3 PC-verified. The lid opened mid-capture stopped cleanly and resumed to a TOSEC-verified finish on hardware (Omikron). A scratched disc retried a fixed 10 times, named the bad sector and stopped with the partial job kept. A B stop and a lid-open in the same boot both resume on DMA ([evidence](docs/evidence/dma-stop-fix-confirmed-2026-09-20.json)). **Open:** a card that fills mid-capture, host-tested only |
| M1.5: hardware acceptance and minimal UI refinement | Launcher and non-Games app round five implemented: named dumps, salvage, VMU copy/delete/restore, Ogg/CD audio, settings/tools and network diagnostics; latest app hardware acceptance pending. Full-card failure remains host-tested only |

See [the research scope](docs/milestone-1-research.md),
[the implementation decisions](docs/diagnostic-design.md), and
[dependency provenance](THIRD_PARTY.md).

## Games

Games directly launches supported GDI, ISO, BIN/CUE, CDI v2/v3/v3.5 and
standalone BIN/IMG layouts. The supplied PC importer expands CSO/ZSO and
extracts CHD; resident compressed boot decoding is not implemented. See
[format and import limits](docs/games-formats.md).

Matching `Game` and `Game-2048` folders collapse into one Games entry. Choose
**Original** or **2048-byte copy** after selecting it. The
[batch converter](docs/gdi-2048-test.md) creates separate copies, preserves
originals and reports each conversion, skip or failure. Conversion does not
guarantee compatibility or replace an archival raw dump.

Recent lists remain in RAM. Rows appear before artwork, and only the selected
game's cover loads. The owner reports the first Games entry takes about
5–7 seconds on the tested collection and navigation afterward is fluid.
**X Refresh** rebuilds the cache after library/card changes. See
[covers and views](docs/games-covers.md).

Native confirmation uses **A** standard, **X** background SCI/20 and **Y**
background SCI/25. Windows CE uses **A** for its background SCI reader; its
broken regular reader is no longer offered. The native reader now stays below
the IP image, clearing startup-stack collisions without changing game code.
The owner confirms Sonic Adventure, Grandia II and Skies of Arcadia working
on the tested layout. Power Stone 1/2 worked after earlier GD command fixes.
These are scoped console results, not full playthroughs or an all-games claim.

Time Stalkers still returns to K-UI, and some titles stall/skip FMVs in both
Original and 2048 mode. CDDA, Mode 2 Form 2 and subchannel emulation remain
unsupported. See the [current release notes](docs/release-v1.8.5-notes.md),
[native placement evidence](docs/evidence/native-low-resident-2026-10-06.md),
and [historical gameplay development](docs/games-retail-test.md).

## License and contribution policy

New K-UI shell code is **GPL-3.0-only**, unless a file states otherwise.
The separate Wi-Fi firmware and shared kwlink library are **MIT**; source
headers and [THIRD_PARTY.md](THIRD_PARTY.md) identify the applicable licenses. Contributions
use the applicable existing file license. Contributors retain their copyrights;
there is no separate CLA or grant of proprietary relicensing rights.

This is a fresh repository, not a DreamShell fork. Requirements and hardware
observations can inform new implementations. Do not import upstream-derived DreamShell source or binaries by changing their names.
Independently authored additions from our earlier fork can be reused after checking
their origins, dependencies and intended license; see [the reuse inventory](docs/prior-work-reuse.md).
Original project artwork and the five original synthesized music loops are reused with recorded provenance; inherited assets keep their own terms.
Independently licensed upstream KOS contributions remain attributed and usable,
including contributions by people who also work on other projects.
