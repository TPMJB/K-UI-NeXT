# Toy Commander Maple observer — experimental, read only

This is a transport research test, **not virtual VMU saving**. It counts and
describes the actual Maple DMA schedules created by one exact Toy Commander
executable. The authored observer injects no VMU commands and changes no VMU
responses. **The game's own physical VMU operations continue normally.**

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
4. Let the game start normally. Keep any physical card whose saves you want to
   use in its ordinary slot; it remains a real card. To avoid game-authored card
   writes during this observation, use an empty physical slot and do not save.
5. The observer intentionally stops after **1200 attempted SDK DMA starts**.
   This is roughly 20 seconds if one schedule is submitted each video frame;
   actual timing depends on the game. The final pending DMA is deliberately not
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
  per-port traffic, response-address range, and parse/stack errors.
- Page 1 counts commands 0 through F, including DEVINFO, controller condition,
  memory information, block read/write, and final synchronization.
- Page 2 records main/subunit addressing, initial schedule pointers, the last
  descriptor words, and a rolling FNV fingerprint of descriptor headers.
- Page 3 records live trigger/enable/busy register values, the inherited CPU
  interrupt mask, and the game's live Maple register-base field. Nonzero
  "NONSW", "NONONE", "NONZERO", or "NONMAPLE" counters need investigation.

These counts provide evidence for a future pre-submit hook. They do not prove
complete SDK call coverage, memory-card response timing, or durable virtual
save/load in the game. The virtual-card packet/storage implementation is tested
separately in the standalone harness.

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
store on schedules which return normally.

The instruction model does not simulate caches, interrupts, bus timing or
the real Maple controller. The bootstrap's cache handling and memory guards
have been reviewed, but console results are still required before treating
this hook as a route to in-game virtual saves.
