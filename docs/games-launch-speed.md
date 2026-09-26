# Games launcher: faster launches and compatibility

Five owner-approved changes on top of the pinned pacing build
(`baseline/doa2-pacing-2072b489c378`). The in-game read path is unchanged
except for the CD-audio commands and the menu return below.

## What changed

1. **Track maps come from the allocation table.** Preparation used to read one
   SD sector per allocation cluster of every track to learn where each file
   lies on the card: roughly 8,000 reads for a 1 GB track on exFAT with
   128 KiB clusters, 32,000 on FAT32 with 32 KiB clusters. FatFs fast seek
   (`FF_USE_FASTSEEK`, `CREATE_LINKMAP`) now lists each file's contiguous
   cluster runs from the FAT alone; exFAT files marked contiguous need no FAT
   reads at all. Bounds, aliasing and the 128-extent limit are checked as
   before. No other FatFs user sets a link map, so its behavior is unchanged.
2. **Half-second handoff screens.** The two stage screens before bootstrap 2
   and before the game each paused about three seconds; now about half a
   second. A failure still leaves its last screen for a photograph.
3. **No executable pre-read.** Preparation read the whole boot executable to
   checksum it, and the stage then read it again. Now only the stage reads it,
   as raw sectors, and checks every sector's sync pattern, mode and header
   address against its LBA, which proves the file map pointed at the right
   sectors; per-block SD CRCs cover the transfer. EDC is deliberately not
   required, because region, VGA and translation patches commonly edit
   `1ST_READ.BIN` without regenerating it; such games launch as before. The
   stage takes the executable's CRC32 after loading for its existing check
   that bootstrap 2 left it unchanged. The IP keeps its checksum. Header
   addresses beyond 99 minutes follow `recovery_sector.c` (122 minutes is
   `0xc2`).
4. **CD-audio commands are accepted silently.** PLAY, PLAY2, PAUSE and RELEASE
   used to stop the game on "GD REQUEST REJECTED". They now complete at once
   without sound, reads or state changes, so games whose music is CD audio
   run without it.
5. **A+B+X+Y+Start restarts K-UI.** When a game asks the BIOS for its menu,
   the reader shows its counter screen for about two seconds, then jumps to
   the boot ROM (as KOS `arch_reboot()` does). The console restarts and boots
   the K-UI disc. Fault screens still stop and wait for a power cycle.

## Validation

Host: the retail GD service tests cover the silent CD-audio commands and
parameter checks; the image tests cover sector headers, including addresses
past 99 minutes and every framing error; the FAT32/exFAT launch-preparation
tests now inject their bad-sector, aliasing and cancellation faults into the
link map and check that no executable bytes are read before launch. Native:
the resident still fits its reserved area and stack bound; the stage and the
resident pass the linked instruction audit.

## Console test

Merge this build's `sd-update` `KUI` folder onto the card; keep the boot CD.

1. Launch DOA2 and time **Play to the game's first screen**. The pinned build
   took about 14 s to the bootstrap screen plus 12 s to the game.
2. In the game, press **A+B+X+Y+Start**: the counter screen should appear
   briefly, then the console should restart into K-UI from the boot disc.
3. If you own a game with CD-audio music (for example Crazy Taxi or Sega
   Rally 2), check that it now runs, silently, instead of stopping.

If a stop screen appears, photograph it. The pinned build stays available for
rollback.
