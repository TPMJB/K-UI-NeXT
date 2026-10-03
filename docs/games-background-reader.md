# Games: background SCI reader (test build)

The game reader normally reads each EXEC's sectors on the spot, with the
game's interrupts masked: about 2 sectors (9 card blocks, roughly 4 ms) per
EXEC, and DOA2 calls EXEC from its vertical-blank interrupt. The background
reader streams the request from the card while the game runs instead.

It is chosen per launch: on the **Launch game** confirmation, **A** launches
with the standard reader, **X** with the background reader whose EXEC tops
reading up to the standard reader's step, and **Y** with the background
reader whose CHECK does so too (see *How long a GD call waits*). It needs SCI microSD and a launch map of at most 32 file extents (a
freshly copied game has a handful); otherwise the launch uses the standard
reader and the log says why. The stage screen shows
`BACKGROUND READER X - EXEC TOP UP` or `BACKGROUND READER Y - EAGER` when it
is installed.

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
  working (36 repairs). About 5% of repairs failed their CRC on the console;
  the likeliest cause, the channel's last write still held off the bus when
  the reception is stopped, is now ruled out by an uncached read of main
  memory first (the DMAC goes before the CPU on the bus). If two repaired
  blocks fail their CRC, repair is switched off for the session and
  overruns restart the stream as before; one alone leaves it on.
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

* **X:** an EXEC makes sure of 10 blocks (the standard reader's step) since
  the previous EXEC: it waits for the ones the interrupt did not deliver, and
  not at all when the interrupt delivered 10 or more. A game is never slower
  than with the standard reader. In DOA2's fourth run 4,611 EXECs still
  waited, about 10 blocks (4.4 ms) each.
* **Y (eager):** a CHECK tops up like an EXEC, counting blocks since either.
  More data per frame while the interrupt is held off, and more masked time.
  (The fifth build's Y waited for at most one block per call instead; DOA2's
  loads and intro took far longer, so the lag there is the game waiting for
  data, not the waits themselves.)

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
+0x600 hold the receive areas and the reader's state
(`src/loader/retail_async.h`).

### Memory

The background resident is a fourth low resident (`resident-scia`). To fit
the receive areas it holds 32 extents instead of 128, has no ordinary block
reader, and keeps a 384-byte private stack instead of 1,280: its worst-case
stack depth comes from GCC's call graph (`-fcallgraph-info=su`,
`tools/check_retail_stack.py`) rather than a sum of every frame. The worst
path is a GD call into the service core at about 220 bytes (CI compiler),
plus a 64-byte allowance, against 336 available. The releasing entries and
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

## Console test (DOA2)

1. Install `KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui` from the
   build's `sd-update` artifact. Storage must be SCI microSD.
2. Games, select DOA2, A to inspect, A again for the confirmation, then **X**.
3. The stage screen should say `BACKGROUND READER X - EXEC TOP UP`.
4. Time character select to fight start (standard reader: about 25 s) and
   note smoothness in the first seconds of the fight, as before.
5. Play a fight or two. Then A+B+X+Y+Start for the counters screen (about
   15 seconds) and photograph it.
6. The same with **Y** (`BACKGROUND READER Y - EAGER`) to compare load time
   and lag.
7. For comparison, launch again with **A** (standard reader).

If it stops, photograph the last screen: `IMAGE READ FAILED` shows the GD
request, the card block and the stream's error counters.

### The counters screen

| Row | Meaning |
| --- | --- |
| `SECTORS READ` | Sectors delivered to the game |
| `IRQ BLKS` / `CALLBLKS` | Blocks delivered by the reader's interrupt / by the game's GD calls |
| `WAITS` | GD calls that waited for blocks (EXECs, and with Y CHECKs, short of 10 since the previous one) |
| `IRQS` / `FAILURES` | Interrupt entries; retried stream failures |
| `EXECS` / `EXEC INT` | EXEC calls during reads; those made from an interrupt handler (caller IMASK above 0) |
| `STALLED` | GD calls that found a block already ended: its interrupt was held off |
| `HOOKS` / `RELEASES` | Vector installs per read; installs again by a GD call after a release |
| `REHOOKS` | Installs again by the trampoline as an interrupt handler returned |
| `REL 100` / `REL 400` / `REL 600` | Events released at VBR+0x100 (exceptions), +0x400 (TLB misses), +0x600 (interrupts) |
| `VBR CHGS` | GD calls that found the game had moved to other vectors |
| `DMA BLKS` / `POLLED` | Blocks received by DMA / by programmed transfers (channel busy) |
| `STARTS` / `STOPS` | CMD18 starts and CMD12 stops |
| `CONTINUE` / `KEPT` | Blocks that continued the stream / shared blocks reused |
| `OVERRUNS` / `CRC ERRS` / `TOKENERR` / `FOREIGN` | Stream errors, all retried |
| `MAXTOKEN` | Longest wait for a data token, in bytes |
| `REPAIRED` | Mid-block overruns resumed in place, the lost byte rebuilt |

Values are hexadecimal.
