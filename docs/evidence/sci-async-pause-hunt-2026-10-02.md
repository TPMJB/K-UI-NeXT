# SCI async: hunting the 2.5 ms pause

Recorded 2026-10-02. Follows the `bac1b152b4ac` console results
([stream record](sci-async-cmd18-stream-2026-10-02.md)): the streaming reader
reads at 1,202 KiB/s (1,323 without the test's own CRC32), but about once per
R run something holds the CPU for 2.5 ms.

## What was seen

- `bac1b152b4ac`: one SCI module reset in the CMD17 pass, with interrupts
  masked, took 2,508 us (`max_irq_masked_us`, site 3, stage 1); its `finish`
  call took 2,571 us. In the streaming pass one block's receive took 2,843 us
  and one `poll` 2,510 us.
- `3aa4554c44d9`: a streamed receive of 2,845 us and a masked window of
  about 2.5 ms.
- None of them came with an overrun, restart, retry or data error: the DMA and
  the card carried on, only the CPU was late.

Throughput barely notices (2.5 ms in a 2 to 3 s run). A game would: with
interrupts masked for 2.5 ms, its vertical blank, sound and drive interrupts
wait. Before the reader runs under a game, the pause must be shown not to be
the reader's, or fixed.

## What it is not

- An interrupt or another thread: the reset's window had interrupts masked.
- The clock: KOS's uptime (`__dreamcast_get_ticks`) re-reads TMU2 around its
  underflow flag and cannot be 2.5 ms off.
- Music: R pauses it before reading.
- The reset's own loops: each STBCR poll stops after 8 reads and each wait is
  a 64-iteration loop.

## Candidates

1. The SCI module stop and restart (STBCR MSTP0) sometimes stalls the CPU.
2. The console stalls the CPU whatever the reader does: in the core, or on the
   external bus (a cache miss waiting for another bus master).
3. For the unmasked cases (the streamed block), an interrupt handler or another
   thread.

The masked case landed in the module reset, a few microseconds of each 764 us
block in the CMD17 pass. A stall at a random time would far more likely have
landed in the receive or the framing, which had none (longest receive 408 us,
longest card wait 328 us). That points at (1), but one sample proves nothing.

## This build

**Pause log.** Every interrupt-masked window of 0.5 ms or more, every API
call of 1.5 ms or more and every gap of 1.5 ms or more between polls of one
request: the first 8 with site, stage and uptime, and the total count.

**Slowest module reset of the reads**, with its six steps timed by raw TMU2
reads (80 ns): checks, stop and confirm, standby wait, restart and confirm,
wait, register checks. Also its length in CPU cycles from the performance
counter KOS keeps running (PRFC0, 5 ns each; `cpu_counter_config` 49187,
0xc023, means it counts elapsed cycles). The same length on both clocks means
the CPU kept running but was stuck; far fewer cycles would mean its clock
stopped or TMU2 jumped.

**Reset loop** (1 s, after the streaming pass, reader still open): the
between-block handoff (deselect, module reset with interrupts masked,
re-initialization) back to back with the card deselected and no DMA: tens of
thousands of resets, against about 4,200 in the reads. It counts resets whose
masked window took 0.5 ms or more, keeps the first 8 uptimes and the slowest
reset's steps. Its windows stay out of the reads' pause log and
`max_irq_masked_us`.

**Idle CPU tests** (after the reader closed, 1 s each, no SCI activity):

- masked: interrupts masked in 20 ms windows, reading only TMU2 and the cycle
  counter, which are on-chip and need no bus;
- RAM: the same with one uncached main RAM read per loop, so a held external
  bus stalls it too;
- unmasked: interrupts and other threads run.

Each reports its longest gap between reads (and that gap in CPU cycles), when
it happened, how many gaps took 0.5 ms or more and the first 8 uptimes.

R takes about 4 s longer; screen updates stay paused throughout.

## Reading the result

| Seen | Means | Next |
| --- | --- | --- |
| Reset loop: many resets over 0.5 ms, the time in one step | The MSTP0 cycle stalls | Stop resetting the module between blocks: clear the overrun another way, or receive transmit-fed so no overrun ends a block |
| Masked idle tests have gaps (RAM only, or both) | The console stalls the bus or the core | Not the reader's; document it |
| Only the unmasked test and unmasked reads | Interrupts or another K-UI thread | Find which; none of them runs under a game |
| None anywhere | Rarer than once per run, or tied to reception | Run R again |

**Screen.** Lines 3 to 5 replace the per-block lines (still in the report):

| Line | Shows |
| --- | --- |
| 3 | Read pauses: count, longest, its site and stage |
| 4 | Reset loop: resets, how many took 0.5 ms or more, longest masked window |
| 5 | Idle CPU longest gap: masked, RAM, unmasked |

**Report.** `pause_count` and `pauses` (site 1 lease, 2 release, 3 module
reset, 4 DMA start, 5 DMA poll, 6 open, 7 long call, 8 between polls; stage 0
slow, 1 CMD17, 2 CMD18 test, 3 stream, 4 none, 5 reset loop),
`cpu_counter_config`, `reset_worst`, `reset_loop`, `spin_masked`, `spin_bus`
and `spin_unmasked`.

## Validation

The host model now has TMU2, the cycle counter, a 2.5 ms CPU stall that can be
injected at an uptime read and a 2.5 ms bus stall at a RAM read. Tests:

- a stall inside one module reset is logged at the reset (site 3, CMD17) and
  shows in step 0 and in the CPU cycles;
- the reset loop runs only on an idle reader after a fast request, counts its
  resets, leaves the reads' counters, pause log and slowest reset untouched,
  records a stall in its own log with the step, and leaves the reader able to
  read; a module that does not come back fails the reader;
- the idle test without RAM reads does not see a bus stall and the one with
  them does; without the cycle counter's configuration no CPU time is given;
- the R sequence runs the loop before closing and the three idle tests after;
  a failed loop fails the run through the normal recovery, with no idle tests.

Report and screen code were compiled on the host with every field at its
widest: the report is at most 15,279 of 16,384 bytes and every screen line
fits.

## Delivery

Source `ad0340e185285bb46af3154e60da83906e33d6b5` passed
[Diagnostic run 207](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37061143973):
host tests and the Dreamcast build. Install `KUI/runtime.kui` and
`KUI/apps/games/retail-boot.kui` from its `kui-1.5.1-dainsleif-sd-update`
artifact, run R and return the JSON and a photo of the result screen.

## Console results (ad0340e18528)

The owner ran R; it passed with all four passes returning the same data and
normal recovery verified ([report](sci-async-console-ad0340e18528-speed.json)).
Ordinary reader 1,094 KiB/s, async CMD17 653, CMD18 stream 1,188.

**There is no CPU pause.** The five logged pauses are one event per whole
second of uptime: they start at 34.9975, 35.9974 and 36.9975 s and end at
35.000001, 36.000150 and 37.000001 s (masked DMA polls of 2,505 us in the
CMD17 and stream passes, and the calls holding them). Everything timed
without KOS's clock saw no stall:

- reset loop: 73,273 module resets in 1 s, longest masked window 9 us, none
  over 0.5 ms;
- slowest module reset of the reads: 6.9 us on raw TMU2 reads, 7.0 us in CPU
  cycles;
- idle CPU, longest gap between reads: 1.4 us masked, 1.4 us masked with RAM
  reads, 52 us unmasked; no gap over 0.5 ms in 3 s.

**Cause: KOS's microsecond clock.** `timer_us_gettime64()` converts TMU2 ticks
at 80 ns each (`tv_nsec = ticks * 80`), but TMU2 runs at Pck/4, 49,874,880 / 4
= 12,468,720 Hz (80.2 ns), and KOS primes its one-second period with that real
rate. Within a second the reading therefore climbs only to 997,497.6 us; when
the second ends it jumps 2,502 us to the next whole second. Any interval
measured across a second boundary reads 2.5 ms long. The earlier 2,508 us
masked module reset, 2,843 us streamed receive and 2,662 us card wait were
such intervals. Rates averaged over seconds are unaffected (the jump makes up
for the slow count), so the KiB/s figures stand.

**Consequences.** The reader holds interrupts masked for microseconds only
(a module reset takes about 7 us); it needs no change for the game loader.
Other code timing short intervals with KOS's clock, such as the heartbeat and
the speed test's own timers, sees the same 2.5 ms whenever an interval spans a
whole second.

## Final timing (next build)

- The probe times with TMU2 ticks at their real length: KOS's
  `__dreamcast_get_ticks()` scaled by the TCOR2 period (a fixed-point factor
  computed at the first open). Its pause log, masked windows, call times and
  per-phase times no longer jump at whole seconds.
- The reset loop, the idle CPU tests and the CPU-cycle cross-check are
  removed; R is back to its earlier length. The pause log and the slowest
  module reset's steps stay.
- Screen: line 3 the stream's per-block times, line 4 the longest
  interrupt-masked window with its site and stage, line 5 the pause log ("No
  pauses" when no masked window reached 0.5 ms and no call 1.5 ms).
- Host model: TMU2 at 2 MHz, and KOS's `timer_us_gettime64()` modeled as on
  the console (2.5 ms jump per second) but not used by the probe. A new test
  reads across a whole second and requires no pause, no masked window over
  0.5 ms and no call over 1.5 ms.

Expected on the console: "No pauses" and a longest masked window of tens of
microseconds.

Source `9e6ccca3dd9e16edacd981830d6887c99372eca2` passed
[Diagnostic run 208](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37074912372):
host tests and the Dreamcast build. Install `KUI/runtime.kui` and
`KUI/apps/games/retail-boot.kui` from its `kui-1.5.1-dainsleif-sd-update`
artifact, run R and return the JSON and a photo of the result screen.
