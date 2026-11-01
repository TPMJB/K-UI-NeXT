# Compatibility test 7d: a report page for The House of the Dead 2's freeze

Test 7c got The House of the Dead 2 running on the Background module with the
virtual card: it found the card, saved, and the save loads after a power cycle.
It then froze during the first boss battle in Original mode, with the sound
still playing, and there was some lag that test 6 did not have. Your two
follow-up runs narrowed it down:

- **Not memory.** The RAM map through the first boss in Original mode
  (`681CFFF0`) left 0x8CC80000-0x8CE00000 untouched. The virtual card's RAM
  (0x8CDB9000-0x8CE00000) lies inside that.
- **Not the reader.** With Virtual VMU Off, the same Background reader, with
  the same background reading and prefetch in the same RAM, got through.

So the freeze comes from the virtual card's code. With the card On, that code
answers the card's requests on every controller transfer. It also reads and
writes the card file on the SD card, which it shares with the reader. Several
causes fit, so this package adds a way to look instead of guessing.

| File | What changed since test 7c |
|---|---|
| Background module, card in A2 and in A1 | A report gesture: hold **both triggers fully, plus A, B, X and Y**, for about three seconds. The virtual card's code then stops the game and shows its page of counters. Nothing else changed. |
| Standalone New reader | Unchanged. |
| GUI | Unchanged. |

The gesture works in any game on the Background module. It needs all four face
buttons and both triggers held together for three seconds, which no game asks
for; Start is not part of it.

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

1. **The House of the Dead 2, Virtual VMU On, Original mode.** Launch with
   **Background, New (X)** and play to the first boss, as in test 7c.
2. **When it freezes:** hold **both triggers fully + A + B + X + Y** for
   about three seconds.
   - If a blue page appears (`VMU / A2`, `SCI JOURNAL`, `DETAIL 0000004F`),
     photograph the whole page, then power off.
   - If nothing appears after holding for about five seconds, write that
     down. It means the card's code is no longer called at all, which is a
     different cause.
3. **If it does not freeze:** play through the boss, then use the gesture
   anywhere in play and photograph the page. That gives the normal counters to
   compare with.
4. **One question about your last run:** with Virtual VMU Off, was the lag
   also there, or only with the card On?

The page shows how many controller transfers were sent and finished, and
how many card reads, write steps and saves there were. It also shows how
often the card found the SD card busy and asked the game to send again, its
last SD loan result, and the card's last request and reply. Those numbers
distinguish the likely causes.

Optional regression checks, as in test 7: a game that already saves (Sonic
Adventure 2, Jet Grind Radio or Skies) and Sword of the Berserk in A1 should
load and save as before.

## What was and was not verified before delivery

- On console (test 7c): The House of the Dead 2 boots, finds the virtual
  card, saves, and the save loads after a power cycle. It froze once at the
  first boss in Original mode with the card On, and not with the card Off.
- The gesture: a host session of the actual worker shows the page on exactly
  the 180th consecutive held transfer, on both the controller-only and the
  full path. Releasing restarts the count. A trigger at 199, or any one face
  button released, never shows it, even held for 200 transfers. The worker's
  whole persistent session suite passes with it.
- The compact Background audit, with the changed worker functions re-pinned
  after review: the completion grew by the gesture check, the 36-byte
  gesture helper is pinned for the paths that execute it, and its 4-byte
  counter is in the reviewed state inventory. Every other changed word is a
  moved address. The fixed-RAM audit executes the compiled arm for both
  arena routes.
- Native compatibility suite, A1 suite and the other host checks: the same
  results as test 7c. As before, the bottom-heap placement check needs a corpus
  proof built from original executables this machine lacks, the
  personalization tool's own test already fails, and four artifact checks
  written for another build profile do not apply to this one.
- This package itself has not run on a console yet.
