# Games launcher: read pacing on still screens

After 1.5 the retail game reader was reviewed again for speed, without the
2048-byte track conversion. The serial SD transfer is at its hardware limit;
this change targets **when** the reader works, not how fast each byte moves.
It is a console experiment: the numbers below come from host models, not
hardware timings.

## Why loads were slow

The reader is synchronous. A game's `EXEC` call reads a fixed two game
sectors (about 10 ms of SD work) and returns. A game that calls `EXEC` once
per video frame therefore gets two sectors per frame, however idle it is:
about 240 KiB/s at most, against about 420 KiB/s when the reader works
continuously.

The owner's CMD18 build made each transfer about 23% faster, yet DOA2's
character-select-to-stage load stayed at roughly 30–32 seconds of black
screen. If transfer speed limited that load, it would have shortened by about
a fifth. So the load is limited by how often the game calls the reader, while
the game itself waits.

## What changed

1. **Longer steps only while the picture is still.** The reader samples three
   PowerVR registers on every call: the current scanline, the vblank line and
   the displayed framebuffer address. It never writes them. After the
   displayed framebuffer has stayed the same for 12 frames, as on a black or
   static loading screen, one `EXEC` may read until shortly before the
   *second* vblank from now: up to 8 sectors, sized from how long recent steps
   took on this card. Crossing one vblank only delays that interrupt, so a
   game that reads from its vblank handler gets about two frames' worth of
   reading per call instead of two sectors.
2. **Unchanged while the game is drawing.** As soon as the game shows a new
   frame, steps return to the accepted two sectors. Gameplay, FMVs and
   animated loading screens keep the baseline behavior.
3. **Spinning on status.** If the game calls `CHECK` 16 times within about one
   scanline with no `EXEC` between them, it is waiting on the read, so the
   reader runs one step for it. The status reply itself is unchanged.
4. **One SD stream per step.** Every physical read, including one-block
   tails, is now a single CMD18 stream of up to 64 blocks instead of 10. The
   owner's comparison measured two-block CMD18 reads level with CMD17
   (392 vs 395 KiB/s) and longer ones faster. The in-game reader no longer
   contains CMD17 at all, which freed the space and stack for this work.
   CRC checks on every block and CMD12 cleanup on every exit are unchanged.

The model in `tests/test_retail_pace.c` runs a scanline-level loop for each
video timing. When the game reads from its vblank handler on a still screen,
throughput rises from 2 to about 3.2 sectors per frame on NTSC (roughly 1.6×),
2.5 on VGA with a card 25% slower than the owner's, and 3.9 on PAL. With
flips, even at one frame in eight, every step stays at two sectors. For a
main loop that does its own work after each read and then waits for vblank,
the model finds no work amount where pacing reads less than the fixed
two-sector steps. None of this is a hardware measurement.

Unchanged: resident and stack addresses, the private stack and entry lock,
masked interrupts during a step with the caller's exact SR restored, CRC
checks, the one-block cache, and destination cache handling. No timer, DMA,
interrupt handler or video register is written.

## Console result — still-screen pacing

Build `2072b48` ([CI run 36209735323](https://github.com/TPMJB/K-UI-NeXT/actions/runs/36209735323),
`sd-update` artifact 10895048305) with the same DOA2 dump, card and boot CD.
The owner reported: "This actually worked really well. Some longer load times
between fights but really the only bad load time left was the first ten
seconds of a fight were pretty laggy. Otherwise it was extremely playable."
No counter photo or timings were recorded; Evolution 2 was not yet tried.
The earlier pinned builds described the same early-fight lag (about 5–8 s).

## Early-fight lag: in-play trial build

The start of a fight still streams data while the game draws every frame.
Each two-sector step costs about 10 ms of a 16.7 ms frame, so a game whose own
frame needs more than about 6.7 ms falls to 30 fps (DOA2's game speed drops
with it). A smaller step leaves the game more of each frame but takes longer
to deliver the same data. Whether one-sector steps keep DOA2 at full speed
depends on its own frame time, which the reader cannot see. This build
measures it on the console:

1. **Trial.** While the picture moves, `EXEC` steps alternate every 120
   frames (2 s) between one sector and the usual two. Still screens keep the
   pacing above. For this test, animated loading screens are slower half the
   time.
2. **Spin steps fit before the next vblank.** While the picture moves, a step
   run for a game spinning on `CHECK` reads only what fits before the next
   vblank-in, or nothing. Previously it could run past the vblank and cost the
   game a frame. On still screens it is unchanged.
3. **Fight counters.** Counting restarts whenever the picture starts moving
   after a still screen, so in DOA2 the counters cover the current fight.

Host models cover the trial alternation, counter resets, vblank crossings and
a game that spins on `CHECK` after drawing: with light frames the spin steps
read in its idle time, with heavy frames none run, and none ever passes the
vblank. The in-game reader dropped its old cumulative counter lines and the
unused "NATIVE GD IMAGE" banner to make room.

### Reading the counter screen

A+B+X+Y+Start makes most games ask the BIOS for its menu; the reader shows
these counts since the fight began and stops the game (power off and on
afterwards). Each line holds two hexadecimal counts: the first four digits,
then the last four.

| Line | First count / second count |
| --- | --- |
| 1 SECTOR FRAMES/FLIPS | Frames spent in one-sector periods / new pictures shown in them |
| 2 SECTOR FRAMES/FLIPS | The same for two-sector periods |
| SECTORS 1/2 | Sectors read in one-sector / two-sector periods |
| EXEC CALLS/STEPS | Calls to `EXEC` / steps that read (spin steps included) |
| SPIN STEPS/CROSSINGS | Steps run for a game spinning on `CHECK` / steps that ran past the next vblank-in |

Flips divided by frames is the game's speed: 1.0 is 60 fps, 0.5 is 30 fps. If
one-sector periods run near 1.0 and two-sector periods near 0.5, one-sector
steps during play are the fix. If both are low, the lag needs a finer step or
comes from something else, and the other counts show which. EXEC calls near
the frame count mean the game calls once per frame.

## Console test

Use the same card, DOA2 dump and boot CD. Merge this build's `sd-update`
`KUI` folder onto the card. Build `2072b48` and the pinned baselines
`baseline/doa2-cmd18-ed31d522c847` and `baseline/doa2-sd-6c02bd8b22f4` remain
available for rollback.

1. Start a fight and watch the first ten seconds: does it alternate between
   smoother and laggier roughly every two seconds?
2. About **six seconds** into the fight, while it is still laggy, press
   **A+B+X+Y+Start** and photograph the counter screen.
3. If there is time, power cycle and repeat, pressing it about **twenty
   seconds** in (after the lag), for the total data the fight start needs.
4. Optionally do the same in Evolution 2 where it feels slow, and say what was
   on screen (loading, menu, map or battle).

If a stop screen appears instead, photograph it.
