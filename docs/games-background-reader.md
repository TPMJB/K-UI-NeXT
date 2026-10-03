# Games: background SCI reader (test build)

The game reader normally reads each EXEC's sectors on the spot, with the
game's interrupts masked: about 2 sectors (9 card blocks, roughly 4 ms) per
EXEC, and DOA2 calls EXEC from its vertical-blank interrupt. The background
reader streams the request from the card while the game runs instead.

It is chosen per launch: on the **Launch game** confirmation, **A** launches
with the standard reader, **X** with the background reader whose EXEC and
CHECK calls each top reading up to 20 card blocks (about twice the standard
reader's step), and **Y** with the same reader topping up to 25 (see *How
long a GD call waits*). It needs SCI microSD and a launch map of at most 32
file extents (a freshly copied game has a handful); otherwise the launch
uses the standard reader and the log says why. The stage screen shows
`BACKGROUND READER X - 20 PER CALL` or `BACKGROUND READER Y - 25 PER CALL`
when it is installed.

## How it works

* A read REQUEST starts a CMD18 stream at the request's first card block.
  Each block is received by DMA on channel 1 exactly as the SCI async probe
  proved on the console: 513 bytes by DMA, the last CRC byte from RDR, the
  receiver stopping on the gap byte's overrun, then deselection, an SCI
  module reset and re-initialization, and reselection, where the card resumes
  with the next block's token.
* Every reception ends on the receiver's overrun, after the block or mid-block
  (the game's own DMA holding the bus), and that raises the SCI's receive
  error interrupt (ERI). The reader's interrupt checks the block's CRC,
  starts the next block, and copies the bytes into the game's destination
  while that block arrives. The DMA channel raises no interrupt of its own.
  Mode1 headers are checked as before; the cursor
  (`src/core/retail_cursor.c`) produces exactly the bytes the standard reader
  produces.
* The game's GD calls also deliver whatever has arrived, and may wait for
  more (see below).
* Within a run, the next block's reception starts as soon as a block has
  arrived, before that one is checked (bit order, CRC) and copied, so the
  card streams while the CPU works (`src/loader/retail_async.c`, `deliver`).
  The two receive areas then hold the block being checked and the one
  arriving; the block kept for a following request gives way.
* The stream stays open between requests (the card waits, deselected), so
  sequential requests continue without a new CMD18; a block two requests
  share is kept rather than read again. An extent or track change, or a
  non-sequential request, stops the stream (CMD12) and starts a new one.
* When the game's own DMA holds the bus long enough for the receiver to
  overrun mid-block (2 to 4% of blocks in DOA2), exactly one byte is lost
  and the card stops where the clock stopped. Back on the bus, the channel
  takes the byte RDR held, so the lost byte is the one at the channel's
  count (if the channel was still held off when the reception was stopped,
  RDR keeps that byte and the next one is lost). The reader resumes the
  block from the byte after the lost one and rebuilds that byte from the
  block's CRC16 (the CRC is linear, so undoing the shifts of the bytes after
  it leaves the lost byte's own contribution). A second fault in that block
  is still detected 255 times in 256. Only a lost CRC byte or a second
  overrun in one block restarts the stream. Resuming relies on the card
  continuing mid-block after a deselection, which DOA2's fourth run showed
  working (36 repairs).
* About 4% of repairs failed their CRC on the console (2 of 55 in the sixth
  run, which then switched repair off: its other 1,869 overruns each
  restarted the card). The likeliest cause is a transfer the channel had
  begun when the reception was stopped (RDR read, its write held off the
  bus by the game's DMA): stopping the channel does not cancel it, so the
  count was a byte short while RDR was already empty, the lost byte was
  taken to be one early, and the resumed bytes landed one place early. The
  reader now waits for the bus (an uncached read of main memory; the DMAC
  goes before the CPU there) both before stopping the channel and after it,
  before counting; the sixth build waited only before. The host model
  reproduces that failure with the old order and repairs it with the new.
* Whatever the cause, a resumed reception that ran a byte ahead ends with
  the card's gap byte (or next token) where the block's second CRC byte
  belongs. Such a block is fetched again without a rebuild (a rebuild of
  shifted bytes would pass 1 time in 256) and counted as `AHEAD`; it does not
  count against repair. If two repaired blocks fail their CRC for any other
  reason, repair is switched off for the session and overruns restart the
  stream as before; one alone leaves it on.
* An overrun the reader's interrupt finds is not resumed there: the SCI is
  handed back, the card waits deselected with nothing in flight, and the
  next GD call's fetch of that block resumes it in place (`DEFERRED`). In
  the eighth run every repair a GD call started succeeded (43 of 43 with
  Y), while those started from the interrupt are the likeliest source of
  the failures that kept switching repair off.
* CRC errors, other overruns, missing tokens and a busy DMA channel are
  retried at the same block (a busy channel is read by programmed
  transfers). From the second failure in a row at a block, it is read by
  programmed transfers (masked, about 0.7 ms), which cannot overrun however
  long the game holds the bus. Nine failures in a row at one block, or a bus
  fault, end the read with the usual `IMAGE READ FAILED` screen.

### How long a GD call waits

Whatever the interrupt has not delivered, the game's own GD calls must, and
a call that waits keeps the game's interrupts masked meanwhile: that is what
shows as lag.

* **X:** an EXEC or a CHECK makes sure of 20 blocks since the previous one
  of either: it waits for the ones the interrupt did not deliver, and not at
  all when the interrupt delivered 20 or more. This is the seventh build's
  Y, the best so far in DOA2 (10 blocks lagged more: seventh run).
* **Y:** the same with 25 blocks, up to about 9 ms per call. 30 (the
  eighth build's Y) read faster but stalled DOA2's frames more: its waits
  sit in the game's vertical-blank handler.

### The interrupt

The reader puts its own vector table in front of the game's only while a
block is in flight, and gives the game its VBR and SCI level back whenever
the stream is idle:

* VBR+0x600 takes INTEVT 0x4E0 (SCI ERI) and 0x500 (SCI RXI, masked by
  SPTR.EIO during DMA). Every other interrupt, and every exception at
  VBR+0x100 and +0x400, first gives the game its VBR and SCI level back and
  then enters the game's vector with all registers unchanged (R0 is parked
  in DBR, which games do not use; R1..R3 on the interrupted stack meanwhile:
  `kui_retail_release_*` in `src/loader/retail_resident.S`). A game's handler
  therefore never runs under the reader's VBR, and the reader's interrupt
  cannot reach the game's vectors. DOA2 crashes if its handlers do run under
  the reader's VBR (the second test build's Y).
* For an interrupt, the releasing entry also keeps the interrupted PC and
  stack (SPC and SGR, up to three pending) and points SPC at
  `kui_retail_rehook`, so the game's handler returns there. The trampoline
  raises SR.BL, takes the newest pending return (interrupt handlers return
  newest first), installs the reader's VBR and SCI level again if the stream
  still runs and the VBR is still the one released to (`REHOOKS`), and
  resumes the interrupted code with RTE and its own SR. Exceptions are only
  released (their handlers may read SPC); the next GD call installs the
  reader again (`RELEASES`). A game that switched stacks under a pending
  return (threads) would stop the trampoline rather than resume the wrong
  code. Both X and Y work this way since the fifth build.
* Only the SCI's interrupt level changes, to the lowest (1); the game's DMAC
  and other levels are never touched. DOA2 keeps the bootstrap's VBR
  (0x8C00F400) for the whole game; it is hooked like any other.
* The handler runs with SR.BL set on the reader's private stack (GD calls run
  masked, so the two never meet), saves bank-1 R0..R7, PR and MAC, and ends
  with RTE. It typically runs about 50 us per block: CRC, the next block's
  token and DMA start, and the copy.

The vectors sit in the resident's BSS: the gaps between VBR+0x100, +0x400 and
+0x600 hold the receive areas and the reader's state, the SCI bus's own state
included (`src/loader/retail_async.h`).

### Memory

The background resident is a fourth low resident (`resident-scia`). To fit
the receive areas it holds 32 extents instead of 128, has no ordinary block
reader, and keeps a 352-byte private stack instead of 1,280: its worst-case
stack depth comes from GCC's call graph (`-fcallgraph-info=su`,
`tools/check_retail_stack.py`) rather than a sum of every frame. The worst
path is a GD call into the service core at about 224 bytes (CI compiler),
plus a 64-byte allowance, against 304 available. The releasing entries and
the trampoline use the interrupted stack instead (12 and 16 bytes). The
standard residents are unchanged.

## Known risks

* A game that installs new exception vectors while a block is in flight
  would receive the reader's interrupt itself. The reader re-installs at the
  next GD call (`VBR CHGS` counts this), but the game's own handler sees one
  unknown event first. Games normally set their vectors once at start.
* A stream restart (new CMD18) waits for the card's first token inside the
  interrupt or GD call, as the standard reader always does (usually 1-2 ms).
* The interrupt waits whenever the game masks interrupts or runs its own
  handlers (the reader's vectors are released then): in DOA2's fourth run it
  delivered 41% of blocks, during boot almost none (the fifth run).
* Game code that reads VBR (rather than its own handlers) while a block is
  in flight sees the reader's. DOA2 has not minded.

## First console run (80687b46fde8)

DOA2 loaded in about 25 seconds with lag for about 7 seconds into the fight,
no better than the standard reader. The counters explained it: `HOOKS 0`,
`IRQS 0` and `BOOT VBR` 43,241: DOA2 never leaves the bootstrap's VBR, and
that build did not hook it, so every block (149,307) was read inside the
game's calls, ten per EXEC (`EXECWAIT` 15,050), as the standard reader does.
6,073 blocks (4%) also overran mid-block, each costing a CMD12, a new CMD18
and a token wait of up to 3,085 bytes (about 3 ms) masked. No CRC or token
errors; 32,400 sectors delivered correctly. The next build hooks the boot
VBR and repairs overruns in place.

## Second console run (da60895201d3)

The background reader crashed and rebooted the console right after the
bootstrap, at the game's first reads; the standard reader still worked.
That build had hooked the boot VBR for the first time, and on that first
hook it had also raised the DMAC's interrupt level from 0 to 1 (IPRC) and
enabled channel 1's completion interrupt. Two suspects, both reset-class on
an SH4 (an unexpected event or a bad handler address in exception state):

1. Raising the DMAC level unmasks every DMA channel's completion and address
   error interrupt the game left enabled but masked; those went to the
   game's handler, which had never expected them.
2. The game's own handlers ran under the reader's VBR when an event was
   passed on, which breaks a handler that locates its data from VBR.

The next build removed the first suspect from both launches (no DMA
interrupt, no IPRC change; the SCI's ERI ends every block) and offered the
second as the choice between X (releases its hook, so no game handler ever
runs under the reader's VBR) and Y (kept it).

## Third console run (7462d64cec22)

X ran DOA2 without trouble; Y crashed, which confirms the second suspect:
DOA2's handlers must not start under the reader's VBR. With X there was
still a lot of lag before character select and at the start of the fight
(the models already on screen); once the fight had started it ran smoothly.
The counters:

* `IRQ BLKS` 2,019 against `CALLBLKS` 66,419: 3% of blocks by interrupt.
  `RELEASES` 6,243 against `HOOKS` 417 and only 2,133 interrupts: DOA2's own
  interrupts come so often that the vectors were nearly always handed back
  before the next block ended, so EXEC read almost everything itself
  (`EXECWAIT` 6,908), masked, as the standard reader does.
* `OVERRUNS` 1,676 (2.5% of 68,346 DMA blocks), all of them restarts:
  `REPAIRED` 0. The repair assumed RDR still held the byte before the lost
  one, but by the time the reader looks, the channel has taken it. Each
  restart costs a CMD12, a CMD18 and a token wait of up to 3,086 bytes
  (about 2 ms) masked.
* No CRC or token errors; 14,863 sectors delivered.

The next build re-hooks with Y as each interrupt handler returns, and
repairs an overrun with RDR empty.

## Fourth console run (c86877c0dbb9)

Y (release and re-hook) ran DOA2 well: "textures popped in way faster" at
the title screen, and character select to the first fight took a little
under ten seconds (about 25 before). There was still lag for 6 or 7 seconds
after the first character spoke in the intro, then the fight ran with no
lag; the next fight's FMV stuttered slightly and its first section (Kasumi
against her clone) lagged, the rest smooth. The counters:

* `IRQ BLKS` 31,684 against `CALLBLKS` 45,503: 41% by interrupt (3% before).
  `REHOOKS` 6,025 against `REL 600` 6,034, and no exceptions: the trampoline
  re-hooked after nearly every game interrupt.
* `EXECWAIT` 4,611, about 10 blocks (4.4 ms masked) each: the remaining lag.
  `RELEASES` 3,386, `HOOKS` 509 (about 33 sectors per read).
* `REPAIRED` 36, then a repaired block failed its CRC (`CRC ERRS` 2) and
  repair switched itself off; `OVERRUNS` 1,578 restarted the card.

The fifth build keeps that reader for both launches. X keeps EXEC's top-up,
Y waits for at most one block per call, repair survives one failure, and new
counters show when and from where EXEC is called and when the interrupt was
held off.

## Fifth console run (40e7e2b71c61)

Y (at most one block per call) was much slower: the first try still had not
reached the title screen after 20 seconds (counters then: 101 of 13,141
blocks by interrupt, `IRQS` 120, `WAITS` 13,021 single blocks, `EXECS`
6,548 of which `EXEC INT` 2,172 from interrupt handlers, `STALLED` 1,987).
The second try took 30 s to the title, 10 s from Kasumi to the fight, then
30 s of intro lag before the fight started (6 to 7 s with the fourth
build). So during boot DOA2 keeps interrupts masked and only its GD calls
move data, and the intro lag is the game waiting for data. That run later
stopped with `IMAGE READ FAILED` during an EXEC: 1,941 overruns (4%), 61
repairs before two failed, and nine failures in a row at one block. The
sixth build reads a block by programmed transfers from its second failure,
fences the channel's last write before counting, and makes Y an eager X.

## Sixth console run (5136651bba37)

Y (eager, 10 blocks per EXEC or CHECK) was "substantially better": about
ten seconds from selecting Kasumi until the textures loaded, then at most
five seconds until the first fight started (six to seven of intro lag with
the fourth build), the fight without lag. The second fight's FMV, then a
second or two of lag as that battle was about to start; the third fight's
FMV lagged a second or two at its start, and skipping it with Start choked
for two to three seconds. "Pretty damned close to parity." The counters:

* 99,307 blocks delivered, 27,736 (28%) by interrupt; `WAITS` 7,318 at
  9.8 blocks each (each waiting call waited for nearly all of its 10).
  `EXECS` 4,560, `EXEC INT` 3,630 of them (80%) from interrupt handlers;
  `STALLED` 1,800. `REHOOKS` 5,993 of `REL 600` 6,031, no exceptions, `HOOKS`
  716, `RELEASES` 3,271.
* `OVERRUNS` 1,869 (1.9% of 98,951 DMA blocks), all restarts: repair stopped
  after 55 (`REPAIRED` 55, `CRC ERRS` 2: both failed repairs) early in the
  run. `POLLED` 194 (a block's second failure in a row), `FAILURES` 1,871,
  no read failure. `STARTS` 2,074, `KEPT` 164, `MAXTOKEN` 3,086.

So the fence before stopping the channel did not fix the failed repairs,
and without repair every overrun cost a card restart, most in the heavy-DMA
phases (texture uploads, FMV) where the remaining lag is. The seventh build
fences after the stop as well, refetches byte-ahead blocks without
switching repair off, makes the sixth build's Y the new X and tries 20
blocks per call as Y. To fit, it drops `IRQS` and `FAILURES` (they repeated
`IRQ BLKS` and the stream's error counters) and `STOPS`, and keeps the SCI
bus's state in the reader's region.

## Seventh console run (cbfa8f74f05b)

Y (20 blocks per EXEC or CHECK): "A lot less lag. Reasonable even." The
counters:

* 65,063 blocks delivered, 7,860 (12%) by interrupt; `WAITS` 3,322 at 17.2
  blocks each. `EXECS` 2,002, `EXEC INT` 1,976 of them (99%) from interrupt
  handlers; `STALLED` 458. `REHOOKS` 2,701 of `REL 600` 2,802, no
  exceptions, `HOOKS` 384, `RELEASES` 1,760.
* Overruns fell to 0.56% of DMA blocks (365 of 64,975; 1.9% in the sixth
  run): with the game held in its GD calls longer, its own DMA met the
  stream less often.
* Repair still stopped early: `REPAIRED` 13, `CRC ERRS` 2, `AHEAD` 0. So the
  failed repairs are not a byte ahead, and the fence after the stop did not
  cure them; the other 352 overruns restarted the card (about half a second
  in all). `POLLED` 7, `STARTS` 464, `KEPT` 83, `MAXTOKEN` 3,222.

X (10 blocks per call) was "largely the same" as before: smoother in some
ways, more lag in others, and a rough three seconds of lag as the textures
appeared when a fight started. Its counters: 60,951 blocks, 17,197 (28%) by
interrupt; `WAITS` 4,456 at 9.8 blocks; `EXECS` 2,733, `EXEC INT` 1,806
(66%); `STALLED` 648; overruns 1.26% (767 of 60,804), `REPAIRED` 36 with
`CRC ERRS` 2 and `AHEAD` 0 (repair off again), `OVERRUNS` 731, `POLLED` 74,
`STARTS` 844. So the larger target wins twice: more data per call, and
fewer overruns (the game's DMA meets the stream less often). 4 of 49
repairs failed in the two runs, none a byte ahead.

The eighth build makes 20 blocks the new X and tries 30 as Y, and starts
each next block before checking and copying the one that arrived (the
check took the card's time before). `STALLED` is gone to make room.

## Eighth console run (effc7b43fe5d)

The owner: "I think X was superior to Y this time", and X "was very doable
as far as gameplay goes"; it is pinned as the known-good build (see
`docs/HANDOFF.md`). The counters:

* **X (20 per call):** 106,071 blocks, 7,938 (7.5%) by interrupt; `WAITS`
  5,474 at 17.9 blocks; `EXECS` 3,436, `EXEC INT` 3,081 (90%); overruns 0.50%
  (534 of 105,846), `REPAIRED` 24 with `CRC ERRS` 2 and `AHEAD` 0 (repair off
  again), `OVERRUNS` 510 restarts, `POLLED` 40, `STARTS` 719, `KEPT` 189.
* **Y (30 per call):** 91,726 blocks, only 20 by interrupt; `WAITS` 3,262 at
  28.1 blocks; `EXECS` 1,997, `EXEC INT` 1,985 (99%); `REPAIRED` 43 with no
  failure, so no overrun restarted the card (`OVERRUNS` 0, `STARTS` 152,
  `POLLED` 0).

Y moved more data per EXEC (19.9 KB against 13.4), so its reads ended
sooner, but each of its waits is longer and nearly all of them run inside
DOA2's vertical-blank handler: the game's frames stall more. Around 20
blocks per call is the balance for DOA2.

The repairs are the new lead (acted on in the ninth build: see the bullet
on interrupt overruns under *How it works*). In every run with interrupt deliveries, two
repairs failed early and switched repair off; in Y, where the reader's
interrupt delivered almost nothing and so nearly every repair started inside
a GD call, all 43 succeeded. The likely cause is a repair started from the
reader's interrupt, at the overrun itself, while the game's bus traffic
that caused it is still going on; a repair started from a GD call comes
later. (0 failures in 43 at the earlier rate would happen about 1 time in
9 by chance.)

## Console test (DOA2)

1. Install `KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui` from the
   build's `sd-update` artifact. Storage must be SCI microSD.
2. Games, select DOA2, A to inspect, A again for the confirmation, then **X**.
3. The stage screen should say `BACKGROUND READER X - 20 PER CALL`.
4. Time character select to fight start (standard reader: about 25 s) and
   note smoothness in the first seconds of the fight, as before.
5. Play a fight or two. Then A+B+X+Y+Start for the counters screen (about
   15 seconds) and photograph it.
6. The same with **Y** (`BACKGROUND READER Y - 25 PER CALL`) to compare load
   time and lag.
7. For comparison, launch again with **A** (standard reader).

If it stops, photograph the last screen: `IMAGE READ FAILED` shows the GD
request, the card block and the stream's error counters.

### The counters screen

| Row | Meaning |
| --- | --- |
| `SECTORS READ` | Sectors delivered to the game |
| `IRQ BLKS` / `CALLBLKS` | Blocks delivered by the reader's interrupt / by the game's GD calls |
| `WAITS` | EXEC and CHECK calls that waited for blocks (short of 20, or with Y 30, since the previous one) |
| `EXECS` / `EXEC INT` | EXEC calls during reads; those made from an interrupt handler (caller IMASK above 0) |
| `HOOKS` / `RELEASES` | Vector installs per read; installs again by a GD call after a release |
| `REHOOKS` | Installs again by the trampoline as an interrupt handler returned |
| `REL 100` / `REL 400` / `REL 600` | Events released at VBR+0x100 (exceptions), +0x400 (TLB misses), +0x600 (interrupts) |
| `VBR CHGS` | GD calls that found the game had moved to other vectors |
| `DMA BLKS` / `POLLED` | Blocks received by DMA / by programmed transfers (channel busy, or a block's second failure in a row) |
| `STARTS` | CMD18 starts (each but the first after a CMD12) |
| `KEPT` | Shared blocks reused rather than read again |
| `OVERRUNS` / `CRC ERRS` / `TOKENERR` / `FOREIGN` | Stream errors, all retried (`OVERRUNS`: those that restarted the card) |
| `REPAIRED` | Mid-block overruns resumed in place, the lost byte rebuilt |
| `AHEAD` | Repaired blocks that ran a byte ahead: fetched again (also in `CRC ERRS`) |
| `DEFERRED` | Of the repaired, those found by the interrupt and resumed by the next GD call |

Values are hexadecimal.
