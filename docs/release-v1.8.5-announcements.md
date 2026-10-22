# K-UI 1.8.5 announcement drafts

## Forum or Discord

**K-UI 1.8.5 “Dáinsleif” is out for Dreamcast.**

Sonic Adventure, Grandia II and Skies of Arcadia now work in my hardware tests
after moving the native reader out of memory used by game startup stacks.
Power Stone 1 and 2 also worked in the earlier command-compatibility fix.

Games now supports GDI, ISO, BIN/CUE, CDI and standalone BIN/IMG layouts. It
groups Original/2048 copies into one selectable entry, keeps recent game lists
in RAM, and loads artwork only for the selected title. The first scan still
takes a few seconds on my collection; navigating afterward is fluid.

The ripper can output GDI, BIN/CUE, CSO, ZSO or CHD. Compressed exports keep
the verified raw capture, and compressed games need the supplied PC import
tool before K-UI can launch them. Windows CE uses the background SCI reader
by default and remains experimental. Time Stalkers and some FMV stalls are
still unresolved, so this is not an all-games compatibility claim. SCI Wi-Fi
and W5500 FTP remain available with storage on SCIF; ESP32 board firmware
is flashed separately by USB.

K-UI is an independent KallistiOS shell with its own game loader and ripper.
It retains the GD-ROM and uses exFAT/FAT32 cards. Existing compatible K-UI
boot CDs can stay in use: replace the complete supplied KUI folder, keeping
your preferences and dumps. DreamShell-based K-UI 1.0 needs the new boot CD.

[Download and release notes](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.8.5)
· [Source](https://github.com/TPMJB/K-UI-NeXT)
· [Optional development support](https://ko-fi.com/tpmjb)

## Short post

K-UI 1.8.5 “Dáinsleif” is available. Sonic Adventure, Grandia II and Skies of
Arcadia work in my tests after a native-reader memory-placement fix. Games
adds GDI/ISO/BIN-CUE/CDI/raw-image support, an Original/2048 picker, RAM-cached
lists and selected-title artwork. The ripper adds BIN/CUE and verified
CSO/ZSO/CHD exports; compressed game boot still requires PC import. Windows CE
remains experimental, and Time Stalkers/some FMVs remain unresolved. Keep a
compatible K-UI CD and update the complete KUI folder on your exFAT/FAT32 card.

[Download K-UI 1.8.5](https://github.com/TPMJB/K-UI-NeXT/releases/tag/v1.8.5)
