# K-UI 1.7 announcements

## Reddit / forum title

K-UI 1.7 Dáinsleif: from the DreamShell-based 1.0 to a standalone Dreamcast environment

## Reddit / forum post

**K-UI 1.7 "Dáinsleif" is out! If the last version you saw was my
DreamShell-based K-UI 1.0, quite a lot has changed.**

K-UI means Katana User Interface. I started with DreamShell, which gave me a
valuable foundation for improving the tools I wanted on my Dreamcast. Since
then, I've moved K-UI into a standalone project built directly on upstream
KallistiOS, with its own bootloader, shell, disc capture engine and resident
GDI game loader.

DreamShell also uses KallistiOS; the difference is K-UI's own application
and loader layer above it.

The current shell is a focused C implementation with a fixed renderer and
statically linked tools, replacing the earlier SDL/Lua/XML application
framework. I've kept original K-UI branding, music and selected independently
authored helpers and policies, with their provenance recorded. KOS and the
other dependencies keep their own credits and licenses.

Some tools will look familiar to 1.0 users, but they're now running on this
independent foundation. **Windows CE from SCI microSD is the big headline:
ARMADA and Worms Armageddon have booted and run on my Dreamcast.** The SCI
reader also uses DMA, multi-block streaming and background delivery, checking
one block while the next arrives. DOA2's tested background-reader setting is
the most fluid I've had so far.

What you get:

- **Games:** native GDI launch, cover art, disc titles and three browsing views,
  plus the experimental SCI Windows CE path.
- **Disc Ripper:** raw-track dumps, stop/resume, verification, reference
  comparison and separate damaged-disc salvage. Established Sword of the
  Berserk and 31-track MDK2 captures took about twenty minutes each and matched
  TOSEC on Dreamcast and PC.
- **VMU Manager and File Manager:** verified save backups/restore and card file
  operations. File Manager's console acceptance is still pending.
- **W5500 FTP:** local-network transfers with completion-safe uploads and
  progress on the console. Tested W5500/SCIF builds reached around 0.8 MiB/s
  uploads and 0.5 MiB/s downloads.
- **Music, audio CD, diagnostics, settings and graphical boot/recovery**, with
  fresh crimson artwork. The original GD-ROM stays installed.

This is still a growing independent loader: compatibility varies, CE is
SCI-only with audio/FMV/sync limitations, and image-backed CD audio is silent.
It doesn't yet replace DreamShell's full compatibility layer or BIOS/region
write tools. ATA PIO is prepared for my upcoming IDE/CF board, but hardware
testing and ATA DMA remain ahead.

Coming from the DreamShell-based 1.0? Use the supplied 1.7 boot CDI for the
independent runtime.

Installation instructions, interface previews and exact limits:
https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.7

Source: https://github.com/TPMJB/K-UI-NeXT

Title/region, transport, reader choice and any audio, FMV or VMU save/load
reports are welcome.

By **TPMJB**. If you'd like to support development: https://ko-fi.com/tpmjb

## Discord / short forum post

**K-UI 1.7 "Dáinsleif" is out!**

If you remember my DreamShell-based K-UI 1.0, this is the next big step:
a standalone KallistiOS-based project with its own bootloader, C shell,
disc capture engine and resident GDI loader. Original branding, music and
selected authored helpers carry forward; DreamShell deserves credit as the
starting point.

**ARMADA and Worms Armageddon now run from SCI microSD**, alongside improved
native-game reading, covers, verified ripping/resume, VMU tools, File Manager,
W5500 FTP and a graphical recovery menu. The original GD-ROM stays installed,
and there's fresh crimson artwork.

CE is experimental/SCI-only with audio and FMV limits. Compatibility varies;
this doesn't replace every DreamShell feature. Image CD audio is silent;
BIOS/region writes, ATA hardware testing and ATA DMA remain future work.

Coming from 1.0? Use the supplied 1.7 boot CDI.

Download, screenshots + notes:
https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.7

By TPMJB • Support: https://ko-fi.com/tpmjb
