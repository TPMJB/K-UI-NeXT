# Games: background SCI reader (test build)

The game reader normally reads each EXEC's sectors on the spot, with the
game's interrupts masked: about 2 sectors (9 card blocks, roughly 4 ms) per
EXEC, and DOA2 calls EXEC from its vertical-blank interrupt. The background
reader streams the request from the card while the game runs instead.

It is chosen per launch: on the **Launch game** confirmation, **A** launches
with the standard reader and **X** with the background reader. It needs SCI
microSD and a launch map of at most 32 file extents (a freshly copied game
has a handful); otherwise the launch uses the standard reader and the log
says why. The stage screen shows `BACKGROUND READER - SCI TEST BUILD` when it
is installed.

## How it works

* A read REQUEST starts a CMD18 stream at the request's first card block.
  Each block is received by DMA on channel 1 exactly as the SCI async probe
  proved on the console: 513 bytes by DMA, the last CRC byte from RDR, the
  receiver stopping on the gap byte's overrun, then deselection, an SCI
  module reset and re-initialization, and reselection, where the card resumes
  with the next block's token.
* When a block's DMA completes (DMTE1), or the receiver overruns mid-block
  (SCI ERI), the reader's interrupt checks the block's CRC, starts the next
  block, and copies the bytes into the game's destination while that block
  arrives. Mode1 headers are checked as before; the cursor
  (`src/core/retail_cursor.c`) produces exactly the bytes the standard reader
  produces.
* The game's GD calls also deliver whatever has arrived. An EXEC waits for
  blocks itself (up to 10, the standard reader's step) when no interrupt can
  deliver: before the game has installed its own exception vectors, or when
  no interrupt came since the previous EXEC although a block was in flight.
  So a game is never slower than with the standard reader.
* The stream stays open between requests (the card waits, deselected), so
  sequential requests continue without a new CMD18; a block two requests
  share is kept rather than read again. An extent or track change, or a
  non-sequential request, stops the stream (CMD12) and starts a new one.
* CRC errors, overruns, missing tokens and a busy DMA channel are retried at
  the same block (a busy channel is read by programmed transfers). Eight
  failures in a row at one block, or a bus fault, end the read with the
  usual `IMAGE READ FAILED` screen.

### The interrupt

The reader puts its own vector table in front of the game's only while a
block is in flight with its interrupt enabled, and gives the game its VBR and
interrupt levels back whenever the stream is idle:

* VBR+0x100 and VBR+0x400 jump straight to the game's vectors.
* VBR+0x600 takes INTEVT 0x660 (DMTE1), 0x4E0 (SCI ERI) and 0x500 (SCI RXI,
  masked by SPTR.EIO during DMA) and passes every other interrupt to the
  game's vector unchanged; R0 is parked in DBR, which games do not use.
* The interrupt level is the game's own DMAC level if it set one, otherwise
  the lowest (1), for both the DMAC and the SCI. No hook is made while the
  boot VBR (0x8C00F400) is still in use.
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
reader, and keeps a 512-byte private stack instead of 1,280: its worst-case
stack depth comes from GCC's call graph (`-fcallgraph-info=su`,
`tools/check_retail_stack.py`) rather than a sum of every frame. The worst
path is a GD call into the service core at about 260 bytes, against 464
available. The standard residents are unchanged.

## Known risks

* A game that installs new exception vectors while a block is in flight
  would receive the reader's interrupt itself. The reader re-installs at the
  next GD call (`VBR CHANGES` counts this), but the game's own handler sees
  one unknown event first. Games normally set their vectors once at start.
* A stream restart (new CMD18) waits for the card's first token inside the
  interrupt or GD call, as the standard reader always does (usually 1-2 ms).
* Untested on hardware: whether DOA2's main loop runs with interrupts open
  enough for the interrupt to deliver most blocks (`IRQ BLKS` against
  `CALLBLKS` on the menu-return screen answers this).

## Console test (DOA2)

1. Install `KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui` from the
   build's `sd-update` artifact. Storage must be SCI microSD.
2. Games, select DOA2, A to inspect, A again for the confirmation, then **X**.
3. The stage screen should say `BACKGROUND READER - SCI TEST BUILD`.
4. Time character select to fight start (standard reader: about 25 s) and
   note smoothness in the first seconds of the fight, as before.
5. Play a fight or two. Then A+B+X+Y+Start for the counters screen (about
   15 seconds) and photograph it.
6. For comparison, launch again with **A** (standard reader).

If it stops, photograph the last screen: `IMAGE READ FAILED` shows the GD
request, the card block and the stream's error counters.

### The counters screen

| Row | Meaning |
| --- | --- |
| `SECTORS READ` | Sectors delivered to the game |
| `IRQ BLKS` / `CALLBLKS` | Blocks delivered by the reader's interrupt / by the game's GD calls |
| `EXECWAIT` | EXEC calls that waited for blocks (no interrupt delivering) |
| `IRQS` | Interrupt entries |
| `FORWARDS` | Interrupts passed to the game while the stream was idle |
| `FAILURES` / `MAXRETRY` | Retried failures; longest run at one block |
| `HOOKS` / `VBR CHGS` / `BOOT VBR` | Vector installs; game VBR changes seen; calls made before the game had its own vectors |
| `DMA BLKS` / `POLLED` | Blocks received by DMA / by programmed transfers (channel busy) |
| `STARTS` / `STOPS` | CMD18 starts and CMD12 stops |
| `CONTINUE` / `KEPT` | Blocks that continued the stream / shared blocks reused |
| `OVERRUNS` / `CRC ERRS` / `TOKENERR` / `FOREIGN` | Stream errors, all retried |
| `MAXTOKEN` | Longest wait for a data token, in bytes |

Values are hexadecimal.
