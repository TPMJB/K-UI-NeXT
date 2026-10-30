# Compatibility test: Berserk, Crazy Taxi and Zombie Revenge

This package adds the three Shinobi 1.62zr ("round-toward-zero") games to the
Background reader. See `native-zr-family.md` in the source for the details.

| Game | Route after this update |
|---|---|
| Sword of the Berserk | Background New (X) **with** Virtual VMU |
| Crazy Taxi | Background New (X) through the standalone New module, no virtual VMU yet |
| Zombie Revenge | Background New (X) through the standalone New module, no virtual VMU yet |

Every previously supported game should behave exactly as before. Their
detection records are byte-identical, and the Background module's game-time
code is unchanged; only the one-time startup checks differ.

## Install

Extract the whole ZIP on the computer. With the Dreamcast microSD mounted,
from the extracted folder (replace `/path/to/SD`):

```sh
python3 install.py /path/to/SD
```

The installer changes exactly three files:

- `KUI/runtime.kui` — the GUI. It is your current Games/VMU-tools GUI plus the
  new detection.
- `KUI/apps/games/retail-native-dual.kui` — the 284 KiB Background module
  (72 KiB VMU, 192 KiB prefetch cache).
- `KUI/apps/games/retail-native-standalone.kui` — the standalone New reader,
  used automatically when the Background module cannot attach the VMU.

It keeps an exact backup of each, under `KUI/native-compat-test/`. Your cards,
saves, game assignments, settings, the Standard loader
(`retail-boot.kui`) and the VMU-Off loader (`retail-physical.kui`) are not
touched. Safely eject before booting.

## Settings for this test

| Setting | Value |
|---|---|
| Storage | SCI |
| Reader | **Background, New (X)** |
| Virtual VMU | On for Berserk (its own game card is created on first launch) |
| CD music | Off |
| RAM patches | Off |
| Physical A2 | Empty when testing the virtual VMU |

## What to check

1. **Sword of the Berserk** (New, Virtual VMU On). It should show
   `72 KIB VMU / 284 KIB BACKGROUND` and `NEW READER / VIRTUAL A2 ON`. Play
   through the opening, check that the game sees a memory card, save, power
   off after the save finishes, boot again and load it.
2. **Crazy Taxi** (New). The launch log should say it is using standalone New
   without a virtual VMU. Play a few rounds and note any loading stutter or
   audio problems.
3. **Zombie Revenge** (New). Same as Crazy Taxi: boot, play into the first
   stages, note stutter.
4. **One regression check:** a game that already worked (for example Skies or
   Sonic Adventure 2) on New with Virtual VMU On. It should behave as before.

Record results in `test-results.csv` or tell me in chat. For any failure, a
photo of the whole screen with every number on it is the most useful thing.

## Restore

From the same extracted folder:

```sh
python3 install.py /path/to/SD --restore
```

This puts back the exact three files that were there before. If you later want
to restore an older test package, restore this one first. Each installer
refuses to overwrite a file another package has changed since.

## What was and was not verified before delivery

- Host tests and linked checks: the SDK interrupt-family discovery test with
  Crazy Taxi as fixture (815,966 cases), the layout, heap, launcher-cache and
  standalone tests, and all 21 library startups.
- The complete compact Background audit, except its original-allocator section.
  That section needs the eight reviewed originals, including Grandia II and
  Lodoss, which were not available here.
- Nothing in this package has run on a console yet.
