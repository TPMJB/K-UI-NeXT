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

## Diagnostics after a load

A+B+X+Y+Start makes most games ask the BIOS for its menu. The reader
intercepts that and shows counters since launch (hexadecimal):

| Line | Meaning |
| --- | --- |
| GD CALLS / EXEC CALLS | All calls into the reader / calls to `EXEC` |
| READ STEPS / SECTORS READ | Steps that read / game sectors delivered |
| FRAMES SEEN | Frames counted from the scanline register |
| PACED STEPS | Steps longer than two sectors (still screen) |
| SPIN STEPS | Steps run because the game spun on `CHECK` |
| STEP CALLER SR | Caller's status register at the last read; bits 4–7 nonzero means interrupts were masked (usually a handler) |

EXEC CALLS close to FRAMES SEEN means the game calls once per frame. PACED
STEPS of zero after a black-screen load would mean the game kept flipping
frames, so no pacing applied. The screen stops the game: power off and on
afterwards.

## Console test

Use the same card, DOA2 dump and boot CD. Merge this build's `sd-update`
`KUI` folder onto the card. The pinned baselines
`baseline/doa2-cmd18-ed31d522c847` and `baseline/doa2-sd-6c02bd8b22f4` remain
available for rollback.

1. Time **character select to the first stage** (baseline about 30–32 s).
   Also note the start-up time to the title screen.
2. Check that the stage introduction speech, the first seconds of the fight
   and an FMV feel **no worse** than before. They should be unchanged.
3. During the fight, press **A+B+X+Y+Start** and photograph the counter
   screen.
4. If there is time, do the same in Evolution 2 at a point where it feels
   slow, and say what was on screen (loading, menu, map or battle).

If a stop screen appears instead, photograph it. One run of each is enough.
