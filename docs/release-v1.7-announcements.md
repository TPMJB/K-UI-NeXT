# K-UI 1.7 announcements

## Forum title

K-UI 1.7 Dáinsleif: from the DreamShell-based 1.0 to a standalone Dreamcast environment

## Forum post — BBCode

```bbcode
[b]K-UI 1.7 "Dáinsleif" is out! If you last saw my DreamShell-based K-UI 1.0, here's what's changed.[/b]

I've moved K-UI into a standalone project with its own bootloader, shell, disc capture engine and resident GDI loader. Both DreamShell and K-UI use KallistiOS; the change is the application and loader layer above it. My focused C shell uses a fixed renderer and statically linked tools in place of the earlier SDL/Lua/XML framework. DreamShell's framework and game reader are not inputs to this build.

DreamShell was a valuable starting point. Original K-UI branding, music and selected independently authored helpers carry forward with recorded provenance. KOS and other dependencies retain their credits and licenses.

[i]Images below are rendered previews of the release UI with example data.[/i]

[img]https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/home-games.png[/img]

[b]Games[/b]

Native GDI launching includes covers, titles and three views. SCI reading uses DMA, streaming and background delivery; DOA2's tested background setting is my most fluid result yet. [b]ARMADA and Worms Armageddon have also booted and run from SCI microSD through the experimental Windows CE loader.[/b]

[img]https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/games-gallery.png[/img]

[b]Tools[/b]
[list]
[*]Disc Ripper: raw-track dumps, stop/resume, verification, reference comparison and separate salvage. Sword of the Berserk and 31-track MDK2 captures took about twenty minutes each and matched TOSEC on Dreamcast and PC.
[*]VMU backups/restore and checked File Manager operations. File Manager's console acceptance is pending.
[*]Music, audio CD, diagnostics, settings and graphical boot/recovery, with fresh crimson artwork. The original GD-ROM stays installed.
[/list]

[img]https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/ripper.png[/img]

[b]W5500 FTP[/b] adds local-network transfers with completion-safe uploads. Tested W5500/SCIF builds reached around 0.8 MiB/s uploads and 0.5 MiB/s downloads.

[img]https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/ftp-busy.png[/img]

Compatibility varies. CE is SCI-only with audio/FMV/sync limits; image-backed CD audio is silent. K-UI does not cover every DreamShell loader feature. BIOS/region writes, ATA hardware testing and ATA DMA remain future work.

Coming from 1.0? Burn the supplied 1.7 CDI and install its KUI folder, preserving compatible game dumps. The old DreamShell CD cannot boot this runtime.

[url=https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.7]Download, installation and release notes[/url]
[url=https://github.com/TPMJB/K-UI-NeXT]Source[/url]
[url=https://ko-fi.com/tpmjb]Support TPMJB on Ko-fi[/url]
```

## Reddit title

K-UI 1.7 Dáinsleif: standalone Dreamcast tools, Windows CE from SCI microSD, and W5500 FTP

## Reddit post — Markdown

**K-UI 1.7 "Dáinsleif" is out! If you last saw my DreamShell-based K-UI 1.0,
there's a new foundation underneath the familiar name.**

I've moved K-UI into a standalone project with its own bootloader, shell,
disc capture engine and resident GDI loader. Both projects use KallistiOS;
the difference is K-UI's application and loader layer. Its focused C shell
uses a fixed renderer and statically linked tools in place of the earlier
SDL/Lua/XML framework. DreamShell's framework and game reader are not inputs
to this build.

DreamShell was a valuable starting point. Original K-UI branding, music and
selected independently authored helpers carry forward with recorded
provenance. KOS and other dependencies retain their credits and licenses.

**What's here:**

- **Games:** native GDI launching, covers, titles and three views. SCI reading
  uses DMA, streaming and background delivery. DOA2's tested background setting
  is my most fluid result yet. **ARMADA and Worms Armageddon have also booted
  and run from SCI microSD through the experimental Windows CE loader.**
- **Disc Ripper:** raw-track dumps, stop/resume, verification, reference
  comparison and separate salvage. Established Sword of the Berserk and
  31-track MDK2 captures took about twenty minutes each and matched TOSEC on
  Dreamcast and PC.
- **VMU and file tools:** verified save backups/restore and checked File Manager
  operations. File Manager's console acceptance is pending.
- **W5500 FTP:** local-network transfers with completion-safe uploads. Tested
  W5500/SCIF builds reached around 0.8 MiB/s uploads and 0.5 MiB/s downloads.
- Music, audio CD, diagnostics, settings, graphical boot/recovery and fresh
  crimson artwork. The original GD-ROM stays installed.

**UI previews on GitHub:** [Home](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/home-games.png),
[Games Gallery](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/games-gallery.png),
[Disc Ripper](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/ripper.png),
[FTP Server](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/ftp-busy.png).
These are rendered previews of the release UI with example data.

Compatibility varies. CE is SCI-only with audio/FMV/sync limits; image-backed
CD audio is silent. K-UI does not cover every DreamShell loader feature.
BIOS/region writes, ATA hardware testing and ATA DMA remain future work.

Coming from 1.0? Burn the supplied 1.7 CDI and install its `KUI` folder,
preserving compatible dumps. The old DreamShell CD cannot boot this runtime.

[Download, installation and notes](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.7)
• [Source](https://github.com/TPMJB/K-UI-NeXT)
• [Support TPMJB on Ko-fi](https://ko-fi.com/tpmjb)

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

## Direct image links

All six screenshots are rendered release UI previews with example data.
These immutable links identify the committed assets used above.

| Asset | Direct link |
| --- | --- |
| Home / Games selected | [home-games.png](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/home-games.png) |
| Games Gallery | [games-gallery.png](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/games-gallery.png) |
| Disc Ripper | [ripper.png](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/ripper.png) |
| VMU Manager | [vmu.png](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/vmu.png) |
| FTP Server | [ftp-busy.png](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/ftp-busy.png) |
| Windows CE confirmation | [games-ce-probe.png](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/release-v1.7/games-ce-probe.png) |
| Dáinsleif release banner | [release-v1.7-banner.jpg](https://raw.githubusercontent.com/TPMJB/K-UI-NeXT/7800e886dbab6795e6248469a5adb5c78261dc86/resources/branding/release-v1.7-banner.jpg) |
