# Compatibility test 2: early Shinobi and round-toward-zero games

This package adds seven games to the Background reader. Four are early Shinobi
(1.41-1.43) games, newly added in this test. Three use Shinobi 1.62zr
("round-toward-zero"), as in the first compatibility test. See
`ZR-FAMILY.md` for the details of both SDK families.

| Game | Route after this update |
|---|---|
| Sword of the Berserk | Background New (X) **with** Virtual VMU |
| Crazy Taxi | Background New (X) through the standalone New module, no virtual VMU yet |
| Zombie Revenge | Background New (X) through the standalone New module, no virtual VMU yet |
| Sonic Adventure | **New in this test:** Background New (X) through the standalone New module, no virtual VMU yet |
| The House of the Dead 2 | **New in this test:** same as Sonic Adventure |
| Time Stalkers | **New in this test:** same as Sonic Adventure |
| Elemental Gimmick Gear | **New in this test:** same as Sonic Adventure |

Silver and Blue Stinger are not in this test. Silver fails the check that
proves where its program ends, and Blue Stinger uses a different SDK interrupt
setup.

Every previously supported game should behave exactly as before. Their
detection records are byte-identical. The Background module's game-time code
(worker, both residents, read-ahead) is byte-identical to the first test except
for the embedded build number. Only the one-time startup checks and the
standalone module's late bind differ.

## Install

Extract the whole ZIP on the computer. If the first compatibility test
(`K-UI-Compat-Test-8a5a4aa9de80`) is still installed, restore it first with its
own installer, from its folder:

```sh
python3 install.py /path/to/SD --restore
```

This installer refuses to run while another test package is installed, and
changes nothing in that case. Once the first test is restored, this installer
removes the record it left, as long as all three files are exactly the
originals it recorded. Then, with the Dreamcast microSD mounted, from this
extracted folder (replace `/path/to/SD`):

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

The standalone games have no virtual VMU yet. Use a physical VMU if you want
to save in them.

## What to check

1. **Sonic Adventure, The House of the Dead 2, Time Stalkers and Elemental
   Gimmick Gear** (New). The launch log should say it is using standalone New
   without a virtual VMU. Play through loading-heavy parts (Sonic Adventure:
   stage loads, cutscenes and the Adventure Fields; House of the Dead 2: chapter
   loads and the cutscenes between rooms). Note any loading stutter, audio or
   video skips, or a hang.
2. **Sword of the Berserk** (New, Virtual VMU On), if not already done with the
   first test. It should show `72 KIB VMU / 284 KIB BACKGROUND` and
   `NEW READER / VIRTUAL A2 ON`. Play through the opening, check that the game
   sees a memory card, save, power off after the save finishes, boot again and
   load it.
3. **Crazy Taxi** and **Zombie Revenge** (New), if not already done: boot, play
   a few rounds or into the first stages, note stutter.
4. **One regression check:** a game that already worked (for example Skies or
   Sonic Adventure 2) on New with Virtual VMU On. It should behave as before.

Record results in `test-results.csv` or tell me in chat. For any failure, a
photo of the whole screen with every number on it is the most useful thing.

## Restore

From the same extracted folder:

```sh
python3 install.py /path/to/SD --restore
```

This puts back the exact three files that were there before, then removes this
test's backup record, so any other test package can be installed next. Each
installer refuses to overwrite a file another package has changed since.

## What was and was not verified before delivery

- Host tests and linked checks:
  - the SDK interrupt-family discovery test with Crazy Taxi as fixture
    (820,138 cases) over all 21 library startups;
  - the early-family test, once with each of the five early games as fixture
    (368,993 cases each);
  - the standalone structural (69,808 cases) and demand-model tests;
  - the launcher-cache test (39,135 cases), the installer tests (14 cases,
    including the switch from the first test) and the structural audit of all
    21 library startups.
- The complete compact Background audit, except its original-allocator section.
  That section needs the eight reviewed originals, including Grandia II and
  Lodoss, which were not available here.
- Nothing in this package has run on a console yet.
