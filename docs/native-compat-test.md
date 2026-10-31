# Compatibility test 4: a virtual card slot for each game

Sword of the Berserk saved in A1 with test 3. This test makes the slot a
per-game choice instead of a module swap:

| File | What it is |
|---|---|
| GUI | Each game's **Advanced** page has a new **Virtual card slot** row, A2 by default. If a game's boot executable cannot be read, the log now names the sector and shows its raw header. |
| `retail-native-dual.kui` | The Background module with the card in **A2**: the same code as test 2. |
| `retail-native-dual-a1.kui` | The same module with the card in **A1**: the same code as test 3. Games set to A1 use it. |
| Standalone New reader | The ordinary build again. The RAM maps are done, so it no longer marks RAM. |

Test 3 put every game's card in A1. With this test, every game is back in A2
unless you set it to A1.

## Install

Copy the `KUI` folder from this ZIP onto the root of the Dreamcast microSD,
merging it with the `KUI` folder already there (on macOS, hold Option while
dragging and choose Merge; Windows and Linux merge by default). It writes
exactly four files, whatever an earlier test put there:

- `KUI/runtime.kui`, the GUI;
- `KUI/apps/games/retail-native-dual.kui`, the Background module, card in A2;
- `KUI/apps/games/retail-native-dual-a1.kui`, the Background module, card in A1;
- `KUI/apps/games/retail-native-standalone.kui`, the standalone New reader,
  without the RAM map.

Cards, saves, game assignments, settings and the other loaders are not
touched. After booting, the top right of the GUI should read this package's
build number. Everything in `details/` is evidence and source, not needed on
the card.

## Setup

1. **Sword of the Berserk:** Games, select it, Options, Advanced, **Virtual card
   slot**: press A to switch it to **A1**. It saves automatically.
2. Keep the virtual card's slot empty: A1 for Berserk, A2 for every other game.
   A real device in that slot stops the game on a failure screen before
   anything is saved.
3. **No jump pack.** Vibration is not passed through yet.

## What to check

1. **Berserk:** the loader shows `NEW READER / VIRTUAL A1 ON`. Load the save
   from test 3, save again, power off, boot and load.
2. **A game that already saved** (Sonic Adventure 2, Jet Grind Radio or Skies),
   slot left at A2: the loader shows `NEW READER / VIRTUAL A2 ON` and it loads
   and saves as before.
3. **Elemental Gimmick Gear:** if it still stops with "Invalid or unsupported
   data sector", press Y on the Diagnostics page to save the log. Just above
   that error the log has a line starting `Retail boot: executable sector LBA`
   and one starting `Retail boot: its first 24 raw bytes`. Send a photo of
   them or the saved log, plus the text of EGG's `.gdi` file (open it in any
   text editor; it is a few short lines).
4. **Optional, Sonic Adventure's boot time.** Do this last. From pressing A on
   the game, time two moments with a phone stopwatch: when the loader's screen
   says `ENTERING OWNER BOOTSTRAP 2`, and when the SEGA logo appears. Then do the
   same with the Standard reader: VMU tools, **Use virtual VMU**: Off; Sonic,
   Options, **Background loading**: Off. Set both back afterwards. The second
   pair of times shows whether the wait after bootstrap 2 is the card's speed or
   the way the New reader paces its reads.

Record results in `test-results.csv` or tell me in chat.

## What was and was not verified before delivery

- `retail-native-dual.kui` is byte-identical to the audited test 2 module apart
  from the build number; `retail-native-dual-a1.kui` is the test 3 module
  rebuilt from the same source.
- Host tests: launch settings (v4 slot record, v3 kept when every game is A2),
  the Games options shell (slot row, launch request, saved choice), the
  launcher, the A1 resident tests with the slots exchanged and the standalone
  reader's structural tests.
- Nothing in this package has run on a console yet.
