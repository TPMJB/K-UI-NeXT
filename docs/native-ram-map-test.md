<!-- SPDX-License-Identifier: GPL-3.0-only -->
# RAM-map test for games without an SDK heap

Virtual VMU needs 384 KiB of game RAM that the game never touches. Games
that start the Katana SDK heap give it up through their own allocator. Crazy
Taxi 2 never starts that heap and works from fixed RAM buffers
(`docs/evidence/native-ct2-heap-finding-2026-10-27.json`), so the only way
to find such an interval there is to watch what the game writes.

`Makefile.native-standalone-ram-map` builds the structural New standalone
reader with `KUI_NATIVE_RAM_MAP`. It installs in the same place
(`KUI/apps/games/retail-native-standalone.kui`) and so serves every game the
combined module refuses on a **New (X)** launch.

## What it does

- **Before the game starts.** The stage's last step, with both caches off,
  writes `address ^ 0x4b55494e` to every word of every 128 KiB page from the
  end of the loaded executable to RAM end. It skips its own image and BSS
  (from `0x8ce00000`) and the last page, which holds its stack. The stage
  screen shows `RAM MAP TEST - FREE PAGES MARKED`.
- **On menu return or a GD stop.** After the usual rows, the resident purges
  each cache line (OCBP), reads all 16 MiB uncached and shows one row,
  `RAM` (`RAM UNUSED` before the early-family standalone left no room for the
  longer label), of four words. Bit `i` of word `w` is the page at
  `0x8c000000 + (32w + i) * 0x20000`; it is set only if every word of the page
  still holds the pattern.
- `tools/decode_native_ram_map.py` turns the four words into address ranges.

Any write clears a page's bit for the rest of the session: by the CPU
(cached or not), by DMA, or by the reader on the game's behalf. A set bit
means one session never wrote that page. It reserves nothing, and only
repeated sessions across a game's modes make an interval a candidate.

## Room in the resident

The resident's image and BSS end exactly at its guarded stack (`0x8c007ba0`,
no spare bytes); the scan shares the existing report routine. The worst
private-stack path is now the GD stop report at 228 bytes, against 240
allowed (304 less the 64-byte margin). The reader's GD, interrupt and
stream code is the structural build's.

## Host evidence

`make -f Makefile.native-standalone-ram-map ram-map-host-tests` runs the
linked fill and report in an SH-4 interpreter over 16 MiB of RAM with a
write-back data cache that P2 bypasses. For five executable sizes (Crazy
Taxi 2's, page-boundary cases and the largest the stage accepts) it checks
that the fill marks exactly the expected pages and nothing else, that an
untouched session reports every marked page, and that each kind of write
clears only its page: a cached store still dirty in the cache, an uncached
store, DMA under a stale clean line, a single byte, a page's first and last
word. With OCBP removed, the dirty store goes unseen, so the purge matters.
