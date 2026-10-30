<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Spawn: find a persistent runtime memory interval

Spawn's New reader is recognized, and its Maple layout now resolves through
the reviewed tail thunk. Its linked SDK allocator has no encoded initializer
reference, so there is no proved allocator-backed lease for a virtual VMU.
This test measures which upper-RAM pages remain untouched during play. It
does not enable virtual saving or choose a RAM interval in advance.

Use the **RAM-map variant** of the current general package, installed with
its supplied installer. The ordinary package does not collect this evidence.
The generic test is described in [native-ram-map-test.md](native-ram-map-test.md).

## Console test

1. Fully power off, then boot K-UI. Select the original **Spawn: In the
   Demon's Hand** GDI, **SCI**, **New (X)**, and **CD music Off**. Leave the
   other options at their defaults. The startup screen must include
   **RAM MAP TEST - FREE PAGES MARKED**. If it does not, report the screen
   before spending time on the run.
2. Let the intro and menus run, then play normally. Include a stage or scene
   transition and several minutes of action. This standalone diagnostic
   provides the New reader; it does not provide a virtual VMU.
3. Press **A+B+X+Y+Start** to leave. A first press may return to the game's
   menu; press again if needed to reach the reader's return screen. Photograph
   the whole screen, including the build ID and all four **RAM UNUSED** words.
   Report which character/stages or modes you used and roughly how long.
4. Fully power off and repeat with a different selection of character,
   stages or mode, again including an actual transition. Capture the second
   return screen. A soft reset between these runs does not start a fresh map.

If the diagnostic stops at a refusal, hangs, or changes normal gameplay,
photograph the available screen and describe the point reached. That result
is useful; no further startup upload is needed for this exact executable.

## How the results are used

Each bit describes one 128 KiB page. A set bit means every word still holds
the diagnostic's address-derived pattern at the terminal capture. Changed
words, including dirty CPU-cache data and DMA/reader writes, make the page's
bit clear. The
executable's pages, the diagnostic stage's own pages, and the last stack page
are deliberately not marked. A zero bit is not evidence that any of those
unmarked areas were used by the game.

The two captures can be decoded with the included helper:

```sh
python3 tools/decode_native_ram_map.py WORD0 WORD1 WORD2 WORD3
```

Use the four hexadecimal words exactly as displayed, in left-to-right order.
The photographs are sufficient if typing the words is inconvenient.

Only an interval untouched in both sessions becomes a candidate for further
review. A fixed-arena implementation would still need to bind the exact game,
reserve a correctly sized interval, start the game's own allocator at a
reviewed initialization point, and retain the ordinary live ownership and
lifetime checks. A successful map alone does not authorize virtual-card writes.
