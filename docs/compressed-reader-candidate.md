# Compressed reader test candidate

This is a separate K-UI 2.0 development build. Identify it by the exact build
ID on screen and in build.json. Keep the known-working installation for rollback.

Copy the supplied `KUI/runtime.kui` and the complete `KUI/apps/games` folder to
the card together. Keep the directory names and filenames as supplied, including
`cso-boot.kui`; no renaming is needed. Use the existing compatible boot disc.

Use one known-working native game as an independent 2048-byte CISO v1 CSO.
Select its `.cso` entry in Games, inspect it, then launch with Standard reader,
Background loading Off, CD music Off and RAM patches Off, on SCI microSD.
Compare boot, a real gameplay load and continued reads with the original
uncompressed image. Record the build, title/image variant and any failure screen.
Normal menu return reboots directly; a read fault keeps its diagnostic screen.

[GAMES-CSO.md](games-cso.md) gives the exact profile and fragmentation limits.
The separate LZ4/LZO ZSO options remain experimental. The ordinary native,
WinCE and CDDA loaders retain their own admission rules and release baseline.

CHD now has an on-console preparation path: it expands and verifies a reusable
GDI cache before launching through the normal reader. Parentless v4/v5 images
and the default CD codecs are supported within the stated optical profile.
It needs expanded SD space; it does not stream compressed hunks during gameplay.
Use the runtime-only CHD overlay to preserve the personalized VMU loader.
See [GAMES-CHD.md](games-chd.md).

ATA DMA remains future work. The target is a shared standards-based driver for
ordinary IDE/CF mods and the K-UI board, with board activation kept separate.
See [ATA-DMA-PLAN.md](ata-dma-plan.md).
