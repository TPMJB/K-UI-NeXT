# K-UI V1.8.5 "Dáinsleif"

Version 1.8.5 expands Games image support, adds the Original/2048 copy picker,
keeps game lists in RAM, and fixes the native reader placement that blocked
Sonic Adventure and Grandia II. The owner also reports Skies of Arcadia works.
The splash and version display now identify 1.8.5. Read the
[release notes](release-v1.8.5-notes.md) for the tested scope and remaining limits.

Download **`kui-1.8.5-dainsleif-release.zip`** from the
[1.8.5 release](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.8.5).
The separate `kui-1.8.5-dainsleif-source.zip` contains corresponding source and
license records. Diagnostic and benchmark artifacts are development downloads.

## Update an existing independent K-UI installation

1. Back up the current `KUI` folder and keep the working package for rollback.
   Preserve any known-working `KUI/recovery.kui`.
2. **Merge the release's complete `KUI` folder into the card root**, replacing
   supplied files while keeping preferences, covers and game dumps. Install
   `runtime.kui` and all files under `KUI/apps/games` together; Games track maps
   and loader payloads from older packages cannot be mixed with this runtime.
3. Safely eject the card, boot K-UI and check **V1.8.5** and its build ID.
   `build.json` records the exact packaged source.

Keep your existing exFAT card. FAT32 remains supported too; no reformat,
reripping or game conversion is required. Runtime applications and Games use
exFAT/FAT32. The bootstrap's read-only ext4 loader does not provide ext4 app or
game-library access. See [filesystem support](storage-filesystems.md).

The device-root layout remains `/KUI`, `/Games` and optional `/Music` across
SCIF, SCI and experimental IDE/CF. There is no separate `/SCI` directory.

## Which boot CD to use

| Existing setup | Boot requirement |
| --- | --- |
| Independent K-UI with a compatible external SCIF bootstrap | Keep using that CD |
| SCI microSD with the SCI-capable graphical bootstrap from `6af5e11` or later | Keep using that CD |
| Old SCIF-only CD, changing to SCI | Burn the supplied `boot-cd/kui-v1.8.5.cdi` |
| DreamShell-based K-UI 1.0 | Burn the supplied CDI; that older CD cannot boot the independent runtime |
| Experimental IDE/CF | A compatible multi-transport bootstrap is required; the hardware remains untested |

This runtime update needs no new burn with your compatible CD. Burning the new
CD also updates the CD's own splash, boot menu and optical stop policy; updating
the card alone updates the runtime's presentation and behavior.

The graphical bootstrap offers Auto, SCIF, SCI and IDE/CF. Auto tries them in
that order. Holding **B** during startup selects the built-in CD path. A retained
`recovery.kui` is available when the normal runtime cannot load. See
[boot and recovery](boot-recovery.md).

## Launch games

Put each image and its referenced tracks in a folder under `/Games`. Games
directly launches supported **GDI, ISO, BIN/CUE, CDI v2/v3/v3.5 and standalone
BIN/IMG** layouts. Compressed **CSO/ZSO/CHD require PC import** before Games can
launch them. The [formats guide](games-formats.md) defines the supported layouts
and provides import commands.

1. Open **Games**, select the title with **A**, then inspect its details.
2. If matching Original and `Game-2048` folders exist, choose **Original** or
   **2048-byte copy** first. See [GDI conversion](gdi-2048-test.md).
3. Press **A** on details to open launch confirmation, then choose a reader:

| Button | Native reader |
| --- | --- |
| A | Standard reader; supported SCIF/SCI or experimental IDE/CF |
| X | Background SCI reader, 20 card blocks per call |
| Y | Background SCI reader, 25 card blocks per call |

Background readers require SCI microSD and a map fitting 64 shared track/extent
slots. Unsuitable native maps or transports use the standard reader, with the
reason logged. The standard reader has 160 shared slots, with up to 99 tracks.
Native CD confirmation also offers **L/R triggers: Plain or Scrambled** executable
encoding. Select Scrambled only for a known MIL-CD-scrambled executable;
the container's extension does not determine that choice.

Windows CE confirmation uses **A** for its **background SCI reader**. The broken
regular CE reader is no longer offered. CE requires SCI microSD and a supported
map; oversized background maps fail with an explanation. Its payload retains
the historical name `ce-probe.kui`. CE audio, FMVs and compatibility remain
experimental.

The first Games listing can still take several seconds to discover a large
collection. Recently visited lists stay in RAM, rows appear before artwork,
and only the selected game's cover loads. **X Refresh** rebuilds the catalogue
after collection changes or card replacement. **Y** changes the Games view;
**Start > Scan box art** obtains covers. See [covers](games-covers.md).

Games reads image storage without writing it; normal saves may write to a VMU.
**A+B+X+Y+Start** restarts K-UI when the game invokes the BIOS menu. Otherwise
power cycle. Keep the boot CD inserted for that restart.

## Rip a disc in another format

In Disc Ripper, press **Start > Capture settings > Output format**, select
**GDI, BIN/CUE, CSO, ZSO or CHD**, then save with **A** before starting New.
GDI remains the default. Resume and Verify use the job's recorded format.

Compressed exports are created after raw capture verification and checked by
decoding against that capture. They retain raw tracks and `.capture.gdi` for
resume, verification and reference hashes, so allow space for both. CSO/ZSO
contain one cooked high-density data track; CHDv4 includes the captured data
and audio mainchannel tracks. These exports do not add compressed game booting.
See [ripper controls](ripper-controls.md) and [formats](games-formats.md).

Explicit optical apps can spin the disc when needed. At the runtime boot
screen K-UI requests STOP; idle title detection stays parked until an observed
disc removal/insertion. A compatible old CD can start the motor before the
new runtime reaches that screen. Actual motor timing depends on the drive.

## Wi-Fi and wired networking

The release preserves the SCI Wi-Fi integration for **XIAO ESP32-C5** (2.4/5 GHz)
and **ESP32-C6** (2.4 GHz), with separate K-UI board firmware. Flash that firmware
through USB from a computer first; the SD runtime does not program the board,
and console firmware updating is not exposed. See [Wi-Fi setup](wifi.md) and
[USB flashing](wifi-flash-arch.md). Board firmware downloads come from the
separate **Wi-Fi firmware** workflow, rather than the K-UI runtime ZIP.

With networking on SCI and storage on SCIF, open **Network > Start (Wi-Fi)**,
select a network and enter its password. The **Bands** row uses Left/Right for
Auto, 5 GHz only or 2.4 GHz only on C5; C6 stays on 2.4 GHz. The board retains
the network/band setting. **Network > Y** starts FTP after joining.

W5500 FTP remains available for the supported **W5500 on SCI + storage on
SCIF** arrangement. Open **Network > Y (FTP Server)** and use the address,
user and password shown. Startup payloads are protected; update those with a
computer's card reader. See [FTP](ftp.md).

## Remaining limits

IDE/CF remains experimental synchronous PIO with no hardware performance claim;
ATA DMA and Windows CE on IDE/SCIF are not implemented.

Time Stalkers still returns to K-UI in the reported test. Some other titles
stall or skip FMVs with repeated audio in both Original and 2048 mode. These
remain unresolved. CDDA playback, Mode 2 Form 2 and subchannel emulation are
unsupported by the resident game reader. Compatibility is title-dependent.

## Rollback, checksums and source

Restore the archived `KUI` runtime and matching Games payloads together to roll
back. Preserve your game dumps and preferences. A recovery runtime may also
require its matching external game payloads.

`SHA256SUMS` identifies files inside the installation bundle;
`SHA256SUMS.txt` covers the downloadable ZIPs. `SOURCE.txt` and `build.json`
record the packaged source and dependency pins. New K-UI shell code is
GPL-3.0-only; the separate Wi-Fi firmware/link library is MIT, and dependencies
retain their original licenses.
