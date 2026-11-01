# Compatibility test 7b: The House of the Dead 2 saves on the virtual card

Test 7 stopped The House of the Dead 2 while loading, on the screen with
`PHASE 0000000A / DETAIL 00000007`. Everything before that step had worked.
The game's memory was set aside for the virtual card (`ENTRY HEAP BEGIN
8CDB9000`, `BYTES 00047000`), and the card's code had installed. The step that
failed connects the reader to the game's interrupt code. This game's SDK
starts its interrupts through a second handler: the SDK's own option that
moves interrupts to their own stack. The loader only knew the first one, so it
stopped, as it is meant to when something unexpected is found.

| File | What changed since test 7 |
|---|---|
| Background module, card in A2 and in A1 | The interrupt check also accepts that second handler. It does so only when the SDK code that installs it and the handler itself match the reviewed code exactly. Both handlers call the reader in the same way. |
| Standalone New reader | Behaves as in test 7. It shares the image check with the Background module, which now also recognizes the second handler, but this reader does not use it yet. |
| GUI | Behaves as in test 7. The same shared image check gives every game the same result as before. |

As in test 7, The House of the Dead 2 runs on the Background module with the
virtual card. The game has no memory allocator of its own, so the card's 284
KiB sit in a fixed part of RAM, just below the loader. Your RAM-map sessions
showed the game never writes there. The GUI sends the game to the Background
module, with Virtual VMU On or Off.

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
      REVIEWED RAM MAP`, then `SDK SCI ROUTE READY / LEVEL 1` (test 7 stopped
      just before this line), and the start screen shows `NEW READER / VIRTUAL
      A2 ON`.
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

- On console (test 7): The House of the Dead 2 loaded up to the interrupt
  step, with the virtual card's memory set aside and its code installed.
  Nothing after that step has run on a console yet.
- The fix: host tests that rebuild the SDK's install code and both handlers
  from your executables. They refuse every changed byte of the second
  handler and of the install code, every changed address word, every changed
  live code byte, and every failed memory read. They admit both handlers on
  all five games that share this SDK: The House of the Dead 2, Sonic
  Adventure, Time Stalkers, Elemental Gimmick Gear and Silver. Every other
  game in your library gets the same result as before. The reader's existing
  interrupt tests pass unchanged.
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
  review (the interrupt check grew; every other changed word is a moved
  address), and the fixed-RAM audit, which executes the compiled arm for both
  Crazy Taxi 2's and The House of the Dead 2's routes.
- The RAM-map evidence covers only what was played in those sessions; it is
  not a proof that the game never uses that RAM.
- This package itself has not run on a console yet.
