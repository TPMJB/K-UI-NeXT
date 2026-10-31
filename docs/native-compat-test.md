# Compatibility test 5: early-driver groundwork (regression check)

The Background module now understands the Maple (controller and VMU) driver of
the early SDK that Sonic Adventure, The House of the Dead 2, Time Stalkers,
Elemental Gimmick Gear and Silver use. Those games do not get a virtual card
yet: they still need the early exit patch and, for The House of the Dead 2, a
fixed block of RAM. Until then they keep running on the standalone New reader,
as before.

The module's code changed for every game, though, so this test checks that the
games that already save still do.

| File | What it is |
|---|---|
| GUI | Test 4b's GUI, rebuilt with this build number. |
| `retail-native-dual.kui` | The Background module, card in **A2**, with the early-driver support. |
| `retail-native-dual-a1.kui` | The same module with the card in **A1**, for games set to A1. |
| Standalone New reader | Test 4b's, rebuilt with this build number. |

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

1. **A game that already saves** (Sonic Adventure 2, Jet Grind Radio or
   Skies): the loader shows `NEW READER / VIRTUAL A2 ON`. Load the save, save
   again, power off, boot and load.
2. **Sword of the Berserk**, slot A1: the loader shows `NEW READER / VIRTUAL A1
   ON`. The same check.
3. **Optional, Crazy Taxi 2**: it uses the fixed-RAM path. The same check.
4. **The House of the Dead 2 or Sonic Adventure**: still boots and plays as
   before, on the standalone New reader, without a card.
5. **Optional, Elemental Gimmick Gear** after the re-rip and recopy: if it
   still stops with "Invalid or unsupported data sector", press UP a few times
   on the Diagnostics page and photograph the two lines above the error.

A failure screen anywhere: photograph it. Record results in
`test-results.csv` or tell me in chat.

## What was and was not verified before delivery

- Host tests: the native compatibility suite (persistent bridge, controller
  fast lane, placement, lifetime, fixed-arena plan, exit family, coherence
  preparation, and the layout tests on real executables), the A1 suite, and the
  GUI's native-cache and launcher tests. Two checks did not pass here, as on
  the previous commit: the bottom-heap placement check needs a corpus proof
  built from original executables this machine lacks, and the personalization
  tool's own test already fails before this change.
- The structural probe gives byte-identical results before and after this
  change for the 16 other recognized executables in your library; only the
  five early titles changed, and they now resolve their driver layout.
- Nothing in this package has run on a console yet.
