# Toy Commander Maple observer — experimental, read only

This is a transport research test, **not virtual VMU saving**. It counts and
describes the actual Maple DMA schedules created by one exact Toy Commander
executable, and samples returned headers immediately before that game's normal
SDK response consumer. The authored observer injects no VMU commands and changes
no VMU responses. **The game's own physical VMU operations continue normally.**

The existing K-UI 2.0.0 release stays your normal installation. This ZIP replaces
only `KUI/apps/games/retail-boot.kui` temporarily; it does not contain a
replacement boot CD, shell, save files, game image, or ordinary release loader.
This is the normal native game-launch module used by the released Games app.

## Install and run

1. Back up the installed 2.0.0 `KUI/apps/games/retail-boot.kui` somewhere
   outside the `KUI` folder. Keep that original file so it can be restored
   after the test.
2. Copy this ZIP's `KUI/apps/games/retail-boot.kui` over the card's copy.
3. Launch your original, uncompressed Toy Commander image over **SCI**, using
   the **standard native reader** with **background loading Off** and **CDDA
   emulation Off** and **RAM patches Off**. Use the original raw-sector image
   for this initial test. Other titles and unsupported options are refused by
   the exact-title stage.
4. Insert a physical VMU in the first controller's VMU slot before starting.
   Navigate to the game's **LOAD/card menu** and let it read the card. Do not
   choose Save, Delete or Format. Physical VMU commands continue through the
   real hardware; this observer does not supply a virtual card.
5. The observer allows up to **18,000 attempted SDK DMA starts**: roughly five
   minutes at 60 schedules per second. It starts a shorter window upon the
   first subunit memory-card media/read/write/sync request, then stops after
   **1,200 additional attempted starts**. Ordinary device discovery alone does
   not start this shorter window. Timing depends on the game's schedule rate;
   either bound can stop the test. The final pending DMA is deliberately not
   started because the game has stopped for the diagnostic.
   It stops earlier if a schedule cannot be safely decoded, exceeds the
   32-descriptor observation bound, or has a receive buffer outside the
   admitted RAM ranges. In particular, it stops before DMA can read or
   overwrite the observer's allocation or its allocator header. An early
   report is useful: photograph its pages just like the normal report.
6. Photograph **all four report pages**. Each remains visible for about ten
   seconds before the next appears. Page numbers are zero through three and
   all numeric values are hexadecimal. Power the console off afterward.
7. Restore the backed-up 2.0.0 `KUI/apps/games/retail-boot.kui` before playing
   other games.

If it refuses the image, photograph the refusal screen. If it crashes or never
shows a report, note whether that happened before the title/menu appeared.
No virtual card is mounted by this test and no new virtual save is expected.

## What the pages prove

- Page 0 records the descriptor source-address range, maximum chain size,
  hard stop bound, per-port traffic, response-address range, parse/stack errors
  and main DEVINFO request count. Its completion counters are calls, matched
  schedules, calls without a pending schedule, and pending schedules replaced
  before a match; then pending descriptor count, busy/mask rejections and RX
  headers sampled. Main DEVINFO requests do not imply successful responses.
- Page 1 counts commands 0 through F, including DEVINFO, controller condition,
  memory information, block read/write, and final synchronization. It also
  separates main-unit requests from subunits 1 through 5.
- Page 2 records when the first subunit and memory-card requests appeared, the
  post-trigger window, subunit request count and stop reason. It shows the
  first four and last four subunit request records in chronological order.
  Each record contains descriptor ordinal, request header, up to the first two
  payload words, and receive address. Missing payload words are zero; the
  header gives the actual payload length. The memory-card function/location
  words are sampled; block-write save data after those words is never read or
  recorded. First/last lists overlap when there are fewer than eight requests.
- Page 3 records live trigger/enable/busy register values, the inherited CPU
  interrupt mask, and the game's live Maple register-base field. Nonzero
  "NONSW", "NONONE", "NONZERO", or "NONMAPLE" counters need investigation.
  It then shows the completion entry SR/mask bitmap, retained main-A DEVINFO
  response header and RX address, latest subunit request/response headers,
  its RX address and descriptor ordinal, and completion context diagnostics.
  The main-A DEVINFO header is retained across later controller polls so its
  peripheral-presence bits remain visible. Response headers are raw words:
  for example, `FFFFFFFF` is not evidence that a card replied successfully.

Stop reason values on page 2 are `1` unsafe schedule, `2` hard limit, `3`
memory-card capture window, `4` stack guard, and `5` owner reset/terminal entry.
Schedule counts and request records are captured **before submission**, including
the final unsent schedule. Only schedules allowed to reach the original DMA-start
instruction become pending for completion matching. The completion hook reads
one returned header word per pending descriptor, with no response payload or
response writes, before the normal SDK consumer runs. It requires the exact
caller, unchanged DMA source, idle software-triggered DMA, enabled Maple,
interrupt mask 15, the admitted SDK manager/ready/TAS-lock state, and valid
private bounds. Failed checks skip RX sampling and retain the pending metadata;
the game's original consumer still runs. This normal SDK path does not cover
the title's separate light-gun transfer path.

The page-3 completion context flags are sticky hexadecimal bits: `01` caller PR,
`02` DMA source, `04` busy DMA, `08` interrupt mask, `10` SDK state, `20`
trigger/enable mode, and `40` private stack/count bounds. `CTXBAD` counts calls
with any context flag other than the separately counted busy/mask conditions.
Orphans and overwrites expose unmatched calls or schedules; they do not by
themselves establish that every transfer uses this consumer.

These samples can establish physical-card traffic and matched returned headers
on the admitted path. They do not prove complete SDK coverage, response payload
contents, or durable virtual save/load. The virtual-card packet/storage
implementation is tested separately in the standalone harness.

## If an image read fails

Photograph the entire `IMAGE READ FAILED` screen. Its two extra rows retain
the read failure and observer state, even if the four Maple pages never appear.
All values are hexadecimal.

| Fields | Meaning |
| --- | --- |
| `ACQ`, `SD`, `IMAGE` | Storage-acquisition result, final SD result after stream cleanup, and image-reader result. Zero means success. `IMAGE = FFFFFFFF` means acquisition failed and the image read never ran. The SD result preserves the first read error if cleanup also fails. |
| `REQLBA`, `REQCOUNT`, `DEST`, `DONE` | Start sector, total sector count and starting destination of the whole game request, followed by bytes completed before the failure. These identify the request rather than the failed step's exact sector. |
| `BOOTERR`, `LEASE`, `CCR` | `BOOTERR` packs bootstrap state in its upper 16 bits and error in its lower 16 bits. State 1 is armed; 2 is initializing; 3 is leased; 4 is installed; 5 is failed. The installed lease begins at `8CFD0000`. `CCR` is the CPU cache-control register. |

The storage results are captured through the normal acquire/read/stop/release
sequence. This compact page does not include latched serial or DMA fault-time
registers; the read-only register shown is the current CPU cache control.

## Build checks and their limits

The package audit binds the embedded reader, worker, stage and runtime module
to their actual linked ELF bytes. It checks code and stack placement, compiler
stack reports, absence of FPU instructions, and the exact protected P2 data
layout. It also executes the linked hook's actual SH4 integer instructions
against randomized inherited register states and a C callee that clobbers
caller-saved state. That check requires one observer call, complete register
and SR restoration, and exactly one replay of the game's original DMA-start
store on schedules which return normally. The completion trampoline receives
the original SR and caller PR, preserves all other inherited integer state,
restores the original consumer target, and performs no DMA-start store.

The instruction model does not simulate caches, interrupts, bus timing or
the real Maple controller. The bootstrap's cache handling and memory guards
have been reviewed, but console results are still required before treating
this hook as a route to in-game virtual saves.
