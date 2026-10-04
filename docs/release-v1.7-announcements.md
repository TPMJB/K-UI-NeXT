# K-UI 1.7 announcements

## Reddit / forum title

K-UI 1.7 Dáinsleif: Windows CE from SCI microSD, faster game reading, FTP and a full Dreamcast toolkit

## Reddit / forum post

**K-UI 1.7 "Dáinsleif" is out. Windows CE from SCI microSD is the headline,
but this has become a much bigger Dreamcast project than a game launcher.**

K-UI means Katana User Interface. It's my independent Dreamcast environment,
built directly on KallistiOS, with its own shell, disc capture engine and game
loader. It keeps the original GD-ROM in the console. Boot K-UI from CD, keep
the runtime on your card, and update it by copying files.

Since 1.5.1, a lot of work has gone into how the SH-4 actually receives and
delivers data: bounded SCI DMA, multi-block streaming, checking a completed
block while the next arrives, interrupt handling, recovery, and CE's virtual
memory and disc-driver contracts. The reward is **ARMADA and Worms Armageddon
booting and running from SCI microSD on my Dreamcast**, plus much better
native-game results in the tested DOA2 configuration.

There are useful numbers behind it, too. An optimized SCI filesystem soak
verified 160 MiB with zero reported errors at about **1,068 KiB/s reads and
1,201 KiB/s writes**. The separate async streaming diagnostic verified its
data at **1,202 KiB/s**. Those are storage tests, not game frame rates, but
they show how far the reader has come.

What you get in 1.7:

- **Games:** native GDI launching, cover art, disc titles and three browsing
  views, with SCI background-reader choices and experimental Windows CE launch.
- **Disc Ripper:** raw-track GDI dumps, stop/resume, verification, reference
  comparison, named destinations and damaged-disc salvage tools. The established
  Sword of the Berserk and 31-track MDK2 captures each took about twenty minutes
  and matched TOSEC on both Dreamcast and PC.
- **VMU Manager:** save browsing, verified backups, restore and managed copy/delete.
- **File Manager:** card browsing and checked file operations, with startup files
  protected. Console acceptance for this newer app is still pending.
- **W5500 FTP:** copy files over your local network, with completion-safe uploads,
  download resume and progress on the console. Tested transfer builds have reached
  around **0.8 MiB/s up and 0.5 MiB/s down** using W5500 on SCI and storage on SCIF.
- **Music, audio CD, memory/storage diagnostics and settings**, plus a graphical
  boot/recovery menu and fresh crimson cyberpunk artwork.

The CE path is experimental and SCI-only. It still has FMV, audio and sync
limitations, and title compatibility varies. I kept the better prior 256-byte
SCI token-search setting for this release after the newer 512-byte experiment
made audio worse. Image-backed CD audio remains silent. This release also
prepares bounded ATA PIO reads for my upcoming IDE/CF board, but that hardware
is still untested and ATA DMA is future work.

**Download:** https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.7

**Source:** https://github.com/TPMJB/K-UI-NeXT

Existing exFAT/FAT32 cards can be updated in place: merge the package's `KUI`
folder and keep your games and settings. Existing compatible CDs remain usable;
SCI/IDE needs the multi-transport bootstrap. Runtime and game payloads must come
from the same package. Installation steps and exact compatibility limits are
included.

If you try it, title/region, storage transport, reader choice and where the game
gets to are useful reports—especially any FMV, audio or VMU save/load behavior.
This release has been shaped by repeated testing on real hardware, and those
specific reports help move the next piece forward.

Made by **TPMJB**. K-UI source is GPLv3; dependencies retain their own licenses.
If you'd like to support the project: https://ko-fi.com/tpmjb

## Discord / short forum post

**K-UI 1.7 "Dáinsleif" is out!**

Big step for my independent KallistiOS-based Dreamcast environment:
**ARMADA and Worms Armageddon now boot and run from SCI microSD**, alongside
faster SCI storage and the tested DOA2 background reader.

- Native GDI launcher with covers and three views
- Raw-track disc ripping, resume, verification and reference comparison
- VMU backup/restore tools and new File Manager
- W5500 FTP, tested around 0.8 MiB/s up / 0.5 MiB/s down with SCIF storage
- Graphical boot/recovery menu, music, diagnostics and new crimson artwork
- Original GD-ROM stays installed

CE is experimental and SCI-only; audio/FMV/sync limits remain. ATA PIO is
prepared but hardware-untested, and CD-audio tracks in game images are silent.
Existing cards update in place—merge `KUI` and keep your games/settings.

Download + notes: https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.7
Source: https://github.com/TPMJB/K-UI-NeXT
By TPMJB • https://ko-fi.com/tpmjb
