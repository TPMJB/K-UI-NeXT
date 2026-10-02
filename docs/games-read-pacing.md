# Games launcher: measured read pacing

## SCI follow-up experiment — 2026-10-01

**Initial console result:** the owner reports about **25 seconds** from Kasumi
selection to match start on `8d930310f79d` (earlier 29 seconds), noticeably
smoother combat and only slight slowdown during the first roughly ten seconds.
The earlier severe slowdown lasted about ten seconds too; severity improved
while that interval stayed similar. Audio remains smooth.
Storage throughput remains unchanged. A new return-counter photo and FMV
check remain outstanding; see the
[first batching result](evidence/sci-game-pacing-result-2026-10-01.md).

The owner's build `6cc2abb460b5` counter photograph shows zero enlarged
steps and approximately two game sectors per successful step. The prior
29-second Kasumi-to-first-fight observation remains the timed baseline;
the photograph contains launch-wide counters, not that load alone. See the
[grouped-CRC results and counter evidence](evidence/sci-grouped-crc-and-game-pacing-2026-10-01.md).

The new experiment permits up to **four sectors even when framebuffer
addresses change**, provided the measured cost predicts they fit within half
a video frame (about 8.3 ms at 60 Hz or 10 ms at 50 Hz). Unknown, invalid or
slow timing retains two sectors. The existing still-screen allowance of up
to eight sectors is unchanged. The estimate includes the previous step's
storage acquisition, transfer and cleanup; it follows slower measurements
immediately and faster measurements gradually.

This is a predicted allowance, not a deadline: an unexpected card stall can
overrun it, and the baseline two-sector floor can itself take longer. Reads
still mask interrupts while working, so gameplay, speech and FMVs require
console comparison. CRC, stream cleanup, memory/stack limits and the caller's
status restoration remain unchanged. Host models are not console speed results.

For this build, test the same DOA2 sequence: time Kasumi selection to the first
fight, check the first ten seconds of combat and an FMV, then photograph the
return counters. Another Storage soak is not needed for this pacing-only change.
`PACED STEPS` now includes the short allowance as well as still-screen steps.

Validation: 201 focused pacing checks pass with ASan/UBSan, plus an independent
optimized host build. Native normal/benchmark layout, stack and instruction
audits pass: SCI payload 11,168 bytes, end `0x8c00bae8` (24 bytes free),
stack 1,180/1,232 bytes. SCIF/IDE conservative stack bounds are 1,076/996
bytes. The menu-return heading is shortened to `GAME MENU RETURN` to fit the
policy within the unchanged resident reservation. No guard limit was changed.

The remainder describes the original, accepted SCIF pacing baseline; its
statement that changing buffers always retain two sectors is historical.

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

## Result: pinned as the Games baseline

The owner found this build the best so far: "This actually worked really
well... the only bad load time left was the first ten seconds of a fight were
pretty laggy. Otherwise it was extremely playable." It is pinned at
`baseline/doa2-pacing-2072b489c378` and is on `main`. A follow-up trial of
one-sector reads during play made DOA2's fight starts worse and was dropped;
see [the pin record](evidence/games-doa2-pacing-baseline-2026-09-26.md).
Serial-SD transport tuning is now closed.

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
| PACED STEPS | Steps granted a budget above two sectors; short request tails may read fewer |
| SPIN STEPS | Steps run because the game spun on `CHECK` |
| STEP CALLER SR | Caller's status register at the last read; bits 4–7 nonzero means interrupts were masked (usually a handler) |

FRAMES SEEN can miss wraps between samples, so these cumulative counters do
not establish an exact EXEC rate. Zero PACED STEPS means no successful read
was granted an enlarged budget; framebuffer changes are only one possible
reason, alongside timing validity and estimated cost. The screen stops the game and stays visible for
about 15 seconds at 60 Hz (18 seconds at 50 Hz), then reboots to K-UI. Start
recording before pressing the combination. Some games handle it as an internal
restart; the counter screen appears only when the game requests the BIOS menu.

## Original console test

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
