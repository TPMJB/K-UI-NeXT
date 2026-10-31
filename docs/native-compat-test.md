# Compatibility test 6: early exits, and a long House of the Dead 2 RAM map

| File | What changed |
|---|---|
| Background module, card in A2 and in A1 | It now recognizes the two BIOS exits of the early SDK that The House of the Dead 2 and Time Stalkers use, and how to hook them. Those games still run on the standalone New reader: they also need a place in RAM for the card, which this test helps find. The hook check runs for every game, so this is again a regression check. |
| Standalone New reader | The RAM-map build from test 3, rebuilt: it records which parts of RAM a game never writes. |
| GUI | Test 5's GUI, rebuilt with this build number. |

If you have not run test 5 yet, skip it: this test covers the same checks.

## Install

Copy the `KUI` folder from this ZIP onto the root of the Dreamcast microSD,
merging it with the `KUI` folder already there (on macOS, hold Option while
dragging and choose Merge; Windows and Linux merge by default). It writes
exactly four files, whatever an earlier test put there:

- `KUI/runtime.kui`, the GUI;
- `KUI/apps/games/retail-native-dual.kui`, the Background module, card in A2;
- `KUI/apps/games/retail-native-dual-a1.kui`, the Background module, card in A1;
- `KUI/apps/games/retail-native-standalone.kui`, the standalone New reader
  with the RAM map.

Cards, saves, game assignments, settings (including each game's Virtual card
slot) and the other loaders are not touched. After booting, the top right of
the GUI should read this package's build number. Everything in `details/` is
evidence and source, not needed on the card.

While this test is installed, every game that runs on the standalone New
reader (Sonic Adventure, The House of the Dead 2, Time Stalkers, Elemental
Gimmick Gear, Silver) runs on the RAM-map build. It plays the same; it only adds
the two screens described below. The next test replaces it with the normal
build.

Keep the virtual card's slot empty (A1 for Berserk, A2 for every other game)
and leave the jump pack out: vibration is not passed through yet.

## What to check

1. **A game that already saves** (Sonic Adventure 2, Jet Grind Radio or
   Skies): the loader shows `NEW READER / VIRTUAL A2 ON`. Load the save, save
   again, power off, boot and load.
2. **Sword of the Berserk**, slot A1: the loader shows `NEW READER / VIRTUAL A1
   ON`. The same check.
3. **Optional, Crazy Taxi 2**: it uses the fixed-RAM path. The same check.
4. **The House of the Dead 2, long RAM map.** Its virtual card would sit in a
   part of RAM the game never writes. Test 3's two sessions both left the same
   1.4 MiB free, but the second session used much less RAM than the first, so
   longer play has to confirm it.
   1. Launch with **Background, New (X)** as before. Just before the game
      starts, the loader shows `RAM MAP TEST - FREE PAGES MARKED` for two
      seconds.
   2. Session 1: play as far as you can, ideally the whole story (continues
      are fine), with its cutscenes, bosses and the ranking or name entry at
      the end.
   3. Session 2: the other modes: Boss Mode, Training, the options, and let
      the attract mode run once.
   4. Finish each session with **A+B+X+Y+Start**. If that only takes you to
      the title screen, press it again there. The return screen shows a row
      labelled `RAM` with four eight-digit numbers for about 15 seconds.
      **Photograph it.**

   More sessions are welcome. A part of RAM only counts as free if every
   session leaves it unused.

A failure screen anywhere: photograph it. Record results in
`test-results.csv` or tell me in chat.

## What was and was not verified before delivery

- Host tests: the exit discovery tests, including the new exit family on
  synthetic images (every opcode, both literal pools, the fill, stray
  references into either exit) and on the 15 executables in your library that
  resolve their exits; the patch head at both alignments; every bit of the
  20-byte joint patch in the worker's hook check; the native compatibility
  suite, the A1 suite, and the GUI's native-cache and launcher tests. As on the
  previous commits, the bottom-heap placement check needs a corpus proof built
  from original executables this machine lacks, the personalization tool's own
  test already fails, and four artifact checks written for another build
  profile do not apply to this one.
- The exits now resolve in The House of the Dead 2 and Time Stalkers; every
  other title's exit result is unchanged.
- The compact Background audit, with the hook check's changed functions
  re-pinned after review, and the RAM-map build's SH-4 execution test.
- Nothing in this package has run on a console yet.
