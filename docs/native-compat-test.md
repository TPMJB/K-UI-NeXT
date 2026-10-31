# Compatibility test 7: The House of the Dead 2 saves on the virtual card

| File | What changed |
|---|---|
| Background module, card in A2 and in A1 | The House of the Dead 2 now runs on it, with the virtual card. The game has no memory allocator of its own, so the card's 284 KiB sit in a fixed part of RAM that your RAM-map sessions showed the game never writes, just below the loader. A check that would have stopped the game at its second card or controller transfer is fixed. |
| Standalone New reader | Back to the normal build: no RAM-map screens. Sonic Adventure, Time Stalkers, Elemental Gimmick Gear and Silver still run on it, without a virtual card. |
| GUI | Sends The House of the Dead 2 to the Background module, with Virtual VMU On or Off, instead of the standalone reader. |

This test includes checks 1 to 3 of test 6. If you have not run test 6, skip it.

## Install

Copy the `KUI` folder from this ZIP onto the root of the Dreamcast microSD,
merging it with the `KUI` folder already there (on macOS, hold Option while
dragging and choose Merge; Windows and Linux merge by default). It writes
exactly four files, whatever an earlier test put there:

- `KUI/runtime.kui`, the GUI;
- `KUI/apps/games/retail-native-dual.kui`, the Background module, card in A2;
- `KUI/apps/games/retail-native-dual-a1.kui`, the Background module, card in A1;
- `KUI/apps/games/retail-native-standalone.kui`, the standalone New reader.

Cards, saves, game assignments, settings (including each game's Virtual card
slot) and the other loaders are not touched. After booting, the top right of
the GUI should read this package's build number. Everything in `details/` is
evidence and source, not needed on the card.

Keep the virtual card's slot empty (A1 for Berserk, A2 for every other game)
and leave the jump pack out: vibration is not passed through yet.

## What to check

1. **The House of the Dead 2, Virtual VMU On** (card in A2, the default).
   1. Launch with **Background, New (X)**. While loading, the loader shows
      `NO SDK ALLOCATOR - REVIEWED RAM ARENA`, then `RAM ARENA LEASE FROM
      REVIEWED RAM MAP`, and the start screen shows `NEW READER / VIRTUAL A2
      ON`.
   2. The game should find a memory card in A2. The first time, it is this
      game's own empty card. Let the game create its save file.
   3. Play as far as you can, ideally the whole story, then Boss Mode and
      Training. Save whenever the game offers to.
   4. Power off, boot, launch again and check that the save loads.

   The card's RAM is only known to be free as far as you played in the RAM-map
   sessions (two in test 3, one to the middle of the second mission in test
   6). If the game freezes, shows wrong graphics, or the loader's failure
   screen appears, photograph the screen and note where in the game it
   happened.
2. **The House of the Dead 2, Virtual VMU Off.** Set Virtual VMU to Off for
   this game and put a physical card in A2. Launch with **Background, New
   (X)**: the game should see the physical card and play as before. This runs
   the Background reader in the same RAM without the virtual card.
3. **A game that already saves** (Sonic Adventure 2, Jet Grind Radio or
   Skies): the loader shows `NEW READER / VIRTUAL A2 ON`. Load the save, save
   again, power off, boot and load.
4. **Sword of the Berserk**, slot A1: the loader shows `NEW READER / VIRTUAL A1
   ON`. The same check.
5. **Optional, Crazy Taxi 2**: it uses the fixed-RAM path, which shares code
   with The House of the Dead 2's. The same check.
6. **Optional, Sonic Adventure or Time Stalkers** on **Background, New (X)**:
   they still run on the standalone New reader; the game should boot and play
   as in test 6, now without the RAM-map screens.

A failure screen anywhere: photograph it. Record results in
`test-results.csv` or tell me in chat. If The House of the Dead 2 does not
start at all, copying test 6's `KUI` folder back restores the previous
behaviour until the next package.

## What was and was not verified before delivery

- Host tests: the worker with the arena lease (its hooks, the witnessed
  borrowed literal, the poll of the arena's first words, and the refusal of
  every free and of a changed lease); the early driver, its exit pair and the
  arena lease together through whole save and load sessions at all 16
  interrupt levels the game's Maple server could run at; the live check
  against the full proof at every level for both drivers; the stage's whole
  route on your executable (no SDK allocator, the reviewed plan, its place
  below the loader, the exits, and all 60 spans the card's code protects lying
  outside the card's RAM); the cache record and the launcher's routing,
  including on your executable; and the native compatibility suite, the A1
  suite and the GUI's launcher tests. As on the previous commits, the
  bottom-heap placement check needs a corpus proof built from original
  executables this machine lacks, the personalization tool's own test already
  fails, and four artifact checks written for another build profile do not
  apply to this one.
- On the 21 executables of your library only The House of the Dead 2's
  resolution changes; every other game's record is byte-identical.
- The compact Background audit, with every changed function re-pinned after
  review, and the fixed-RAM audit, which executes the compiled arm for both
  Crazy Taxi 2's and The House of the Dead 2's routes.
- The RAM-map evidence covers only what was played in those sessions; it is
  not a proof that the game never uses that RAM.
- Nothing in this package has run on a console yet.
