# K-UI V1.7 "Dáinsleif"

K-UI 1.7 brings the work since 1.5.1 into one release: faster SCI microSD
storage, background native-game reading, experimental Windows CE launching
on SCI, W5500 FTP, File Manager, a graphical boot/recovery menu and new crimson
artwork. K-UI is an independent Dreamcast environment built on upstream
KallistiOS. Read the [release notes](release-v1.7-notes.md) for compatibility
and hardware evidence.

Download **`kui-1.7-dainsleif-release.zip`** from the
[1.7 release](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.7).
Use this installation package for normal operation. The separate
`kui-1.7-dainsleif-source.zip` contains corresponding source and license
records; workflow diagnostic and benchmark artifacts serve development.

## Update an existing card

1. Keep the current working package for rollback. Preserve any known-working
   `KUI/recovery.kui` already on the card.
2. **Merge the supplied `KUI` folder into the storage root**, replacing matching
   supplied files. Keep preferences, covers, music selections and game dumps.
   Update `KUI/runtime.kui`, `KUI/apps/games/retail-boot.kui` and
   `KUI/apps/games/ce-probe.kui` together from this package.
3. Finish the copy and safely eject the card. Boot K-UI and check **V1.7**,
   the source build ID and the selected transport. `build.json` identifies the
   exact packaged source.

Keep an existing exFAT card as it is; no reformat or reripping is required.
Existing FAT32 cards also remain supported. Current runtime applications use
exFAT/FAT32. The bootstrap's read-only ext4 loader does not provide full ext4
application or game-library support.

The normal layout is `/KUI`, `/Games` and optional `/Music` at the device root.
SCI and IDE/CF use that same layout; there is no separate `/SCI` directory.

## Which boot CD to use

| Your setup | Boot requirement |
| --- | --- |
| Existing external SCIF SD adapter | Your working K-UI CD can continue to boot the updated runtime |
| SCI microSD | Use the SCI-capable graphical bootstrap from `6af5e11` or later, or the supplied 1.7 CDI |
| IDE/CF | Requires a compatible multi-transport bootstrap; hardware support remains experimental and untested |
| New crimson CD artwork and graphical recovery menu | Burn the supplied image in `boot-cd` as a CDI disc image |

If you already use the compatible SCI-capable CD, this runtime update needs
no new burn. An older SCIF-only CD cannot gain SCI/IDE drivers from a runtime
file update.

The graphical CD menu starts automatically after three seconds; input pauses
the countdown. Left/Right selects Auto, SCIF, SCI or IDE/CF for this session.
Auto tries SCIF, then SCI, then IDE/CF. **Start K-UI** loads the normal runtime
and tries `recovery.kui` on that device if needed; **Recovery** requests only
the retained recovery image. Holding **B** during startup opens the built-in
CD path and bypasses an optional card boot override. See
[boot and recovery](boot-recovery.md).

Power off before changing adapters, wiring or IDE/CF hardware. Keep the
selected storage device connected during use.

## Launch native games

1. Open **Games**, select your raw-track `.gdi` image and press **A** to inspect.
2. Press **A** on the detail screen to open launch confirmation.
3. Choose the reader on confirmation:

| Button | Native-game reader |
| --- | --- |
| A | Standard reader for SCIF, SCI or experimental IDE/CF |
| X | Background SCI reader with 20-block call batches |
| Y | Background SCI reader with 25-block call batches; the owner's preferred DOA2 setting |

Background reading requires SCI microSD and a map that fits 64 shared track
and file-extent slots. An unsuitable transport or a larger map falls back to
the standard reader; the log records why. The standard reader has 160 shared
slots and accepts up to 99 tracks. Passing preparation checks does not
establish game compatibility.

Games reads storage without writing it. Normal saves can write to an attached
VMU. **A+B+X+Y+Start** returns through the reader's report and console restart
when the game invokes the BIOS menu; otherwise power cycle. Keep the boot CD
in the drive if you want that restart to boot K-UI again.

For covers, press **START** in Games and choose **Scan box art**. **Y** on
the game list switches List, Compact and Gallery views. Game images remain
read-only during scanning.

## Try Windows CE games on SCI

Windows CE launching is **experimental and SCI-only**. ARMADA and Worms
Armageddon have run on the owner's Dreamcast; audio, FMV and broader title
compatibility remain limited.

1. Open Games and inspect a Windows CE `.gdi` with **A**.
2. Press **A** for its Windows CE launch/boot-test confirmation.
3. Press **X** to use the background SCI reader retained for 1.7. **A** selects
   the standard synchronous reader for comparison.

The matching `ce-probe.kui` is required despite its historical filename. The
release retains the accepted **256-byte token-search allowance** from
`6f14bc529472`; the later 512-byte comparison is excluded after the owner's
audio regression report. This does not establish perfect audio/video sync.
For troubleshooting, record the title, region, reader, scene and build ID,
and photograph any stop or menu-return report.

## Transfer files with a W5500

With a supported W5500 wired to SCI and storage on the external SCIF adapter,
open **Network → Y (FTP Server)**. Connect a computer's FTP client to the
address shown, port 21, user `kui`, and the displayed password. The password
is retained in `KUI/ftp-password.txt`. Use plain FTP on your local network.

Uploads become their final filenames only when complete. Existing files are
retained until the replacement is ready; startup payloads are protected.
Up to three clients can connect. B stops the server. Update K-UI's protected
startup files with a computer's card reader. See [FTP](ftp.md).

SCI storage and the W5500 occupy the same port in this release and cannot
operate together there. The hardware-tested network setup is W5500 on SCI
with storage on SCIF.

## Rollback and source

Restore the archived runtime and matching Games payloads together to roll
back; keep your game dumps and preferences. A retained `recovery.kui` can
boot a working runtime, but external app payloads may still need restoration
from the matching package.

`SHA256SUMS` covers the installed bundle; the release's `SHA256SUMS.txt`
covers its downloadable ZIPs. `SOURCE.txt` and `build.json` identify the exact
source and dependency records. K-UI code is GPL-3.0-only; dependencies retain
their licenses.
