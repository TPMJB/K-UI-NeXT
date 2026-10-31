# Compatibility test 3: Berserk in A1, and RAM maps

This package has three parts:

| File | What changed |
|---|---|
| Background module | The virtual card answers as **A1** instead of A2, for Sword of the Berserk, which only ever saves to A1. |
| Standalone New reader | Records which parts of RAM a game never writes (the RAM map). This is the first step toward a virtual card for Sonic Adventure, The House of the Dead 2, Time Stalkers and Elemental Gimmick Gear. |
| GUI | Same as test 2, plus a clearer message when a disc image is too fragmented for the Background reader. |

While this test is installed, every game that runs the Background module with
Virtual VMU On finds its card in **A1**. Games that already saved in A2 search
every slot, so they should find it there too. To go back to A2, copy test 2's
`KUI` folder over again.

## Install

Copy the `KUI` folder from this ZIP onto the root of the Dreamcast microSD,
merging it with the `KUI` folder already there (on macOS, hold Option while
dragging and choose Merge; Windows and Linux merge by default). It replaces
exactly three files, whatever an earlier test put there:

- `KUI/runtime.kui`, the GUI;
- `KUI/apps/games/retail-native-dual.kui`, the 284 KiB Background module
  (72 KiB VMU, 192 KiB prefetch cache), now with the card in A1;
- `KUI/apps/games/retail-native-standalone.kui`, the standalone New reader with
  the RAM map.

Your cards, saves, game assignments, settings, the Standard loader
(`retail-boot.kui`) and the VMU-Off loader (`retail-physical.kui`) are not
touched. Everything else in the ZIP (`details/`) is evidence and source, not
needed on the card. Safely eject before booting.

## Controller setup

- **A1 (the front slot of the controller in port A) must be empty.** The virtual
  card lives there now. A real device in A1 stops the game on a failure screen
  at its first memory card check, before anything is saved.
- A real VMU in A2 still works.
- **No jump pack.** Vibration is not passed through yet, in any slot. The first
  rumble would stop the game on a failure screen.

## Test 1: Berserk saves

1. Launch Sword of the Berserk with **Background, New (X)** and Virtual VMU On.
   The loader shows `NEW READER / VIRTUAL A1 ON`.
2. The game should find a memory card in A1. Save, power off after the save
   finishes, boot again and load it.
3. **One game that already saved** (Sonic Adventure 2, Jet Grind Radio or
   Skies): it should find its card in A1 and save and load as before.

## Test 2: RAM maps

For Sonic Adventure, The House of the Dead 2, Time Stalkers and Elemental
Gimmick Gear:

1. Launch with **Background, New (X)** as before. Just before the game starts,
   the loader shows `RAM MAP TEST - FREE PAGES MARKED` for two seconds.
2. Play the way you normally would, but cover as much as you can: stage loads,
   cutscenes, menus, the attract mode. Twenty minutes or more is ideal.
3. Finish with **A+B+X+Y+Start**. If that only takes you to the title screen,
   press it again there. The return screen shows a row labelled `RAM` with
   four eight-digit numbers for about 15 seconds. **Photograph it.**
4. Two or three sessions per game, covering different parts, are best. A part
   of RAM only counts as free if every session leaves it unused.

Each bit in those numbers stands for 128 KiB of RAM; set means the game never
wrote there. That is where its virtual card can go.

**Elemental Gimmick Gear** refused with "map exceeds 64 slots" in test 2. That
means one of its track files is split into many pieces on the card, more than
the Background reader's 64-entry map holds. The message now names the track
and the number of pieces. Copy the game's folder off the card and back so its
files are stored in one piece, then launch it again.

Record results in `test-results.csv` or tell me in chat. For any failure, a
photo of the whole screen with every number on it is the most useful thing.

## What was and was not verified before delivery

- The A1 module is the test 2 Background module with controller A's two slots
  exchanged. Built the default way, the same source is byte-identical to the
  test 2 module. In the A1 build, only the stage's text and seven functions
  differ; every other changed function moved without changing.
- Host tests with the slots exchanged: split controller and stream guard
  (1,607,463 checks), persistent bridge (2,085,252 checks) and the Maple chain
  builder. The default builds of the same tests pass too.
- RAM-map fill and report host test, launcher test (478,745 checks).
- The compact audit's byte pins describe the A2 module, so they were not run on
  the A1 variant; the checks above stand in for them.
- Nothing in this package has run on a console yet.
