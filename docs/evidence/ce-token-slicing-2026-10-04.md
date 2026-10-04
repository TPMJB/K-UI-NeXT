# CE resumable token search — 2026-10-04

## Console evidence

Owner identifies photo `61835.jpg` as ARMADA, build `b6f55e1e0876`. The
exact run duration is unknown. Separately, Worms Armageddon is slightly
laggy but audio/video appear synchronized; no Worms counters were supplied.

| Counter | Hex | Decimal |
| --- | --- | ---: |
| Guard | 0 | 0 |
| Read steps / sectors | 50B1 / 4861 | 20657 / 18529 |
| IRQ blocks / call blocks | 103F9 / 4AF0 | 66553 / 19184 |
| Waits / EXEC / EXEC interrupt | E / 515A / 0 | 14 / 20826 / 0 |
| IRQ polled maximum / call maximum | 1 / 0 | 1 / 0 |
| PIO calls / requested bytes | 0 / 0 | 0 / 0 |
| DMA blocks / polled blocks | 10423 / 48D3 | 66595 / 18643 |
| Starts / kept / overruns | 49CD / 1F8 / 48D3 | 18893 / 504 / 18643 |
| CRC / token / foreign errors | 4 / 0 / 0 | 4 / 0 / 0 |
| Repair attempts / ahead | A6 / 0 | 166 / 0 |
| Incomplete: zero / 1–128 left | C / BB6 | 12 / 2998 |
| Incomplete: 129–384 / 385+ left | 3D82 / 35 | 15746 / 53 |
| CH2 late / DMAOR bad | 0 / 0 | 0 / 0 |
| Token bytes / maximum | 638E06 / C96 | 6524422 / 3222 |
| Stops | 49CC | 18892 |

Completed polled receptions are 21.87% of DMA plus polled receptions. All
successful polled receptions occurred in IRQ delivery visits, at most one
per visit. Incomplete bins reconcile exactly to overruns plus repair
attempts; 83.7% stopped with 129–384 DMA bytes left. Clean late snapshots
do not identify or exclude the cause of those earlier incomplete transfers.
The token maximum counts successful clocked bytes, not elapsed time. A
two-iteration IRQ cap therefore still permits a long synchronous token
search inside one iteration.

## Isolated change

Limit token searching to 256 clocked bytes per installed-CE GD or SCI IRQ
entry. The same allowance covers successive searches and the before/after
GD hooks. Exhaustion preserves the search and yields; it is neither a
token error nor a retry. Resume the same LBA without another CMD12/CMD18.
Keep the original total token-search limit across all slices so a card
that never returns a token still fails in a bounded way.

A paused search has no DMA in flight to generate its next SCI interrupt.
The CE integration must wake its driver explicitly, including physical
DMA destinations. It must not spend several fresh allowances in the same
masked entry or report completion before checked data is delivered. A
ready predecessor block remains available while its successor's token
search is paused.

Before CE's handler table is installed, token searching keeps its existing
synchronous behavior. Synchronous `PIO_TRANSFER` entries are also exempt:
their contract requires returning the requested bytes or an error. This
experiment does not establish a bound on all IRQ-masked work; payload
fallback, command response/busy polling and CRC remain unchanged.

The accepted per-byte payload receiver stays intact. No batched raw
receiver, timer ownership change, IRQ priority change, CRC relaxation,
queue enlargement or menu/music change accompanies this experiment.

## Local validation

Focused ASan/UBSan models pass: CE async 122,515 checks and native async
119,928 checks, plus native and CE stream suites. Added cases exercise
shared before/after allowance, successor-token pauses preserving the
predecessor, physical DMA wakeups without a pending SCI interrupt, unchanged
CMD18 continuation, cumulative token timeout, abort/reset/new-LBA cleanup,
and long-gap synchronous PIO/boot behavior. The yield-counter check excludes
repeated zero-byte probes. Local leak detection is disabled for the container.

Full SH GCC 15 proxy instruction, memory-layout and stack audits pass.
CE async payload is 16,260 bytes (+416), linked BSS 5,116 bytes (+32), ending
at `0x8c00d69c`: 356 bytes remain below the unchanged `0x8c00d800` limit.
Worst stack is 436 plus a 64-byte margin within 2,000 bytes; IRQ stack is
232 bytes. At a fixed build ID, all native resident/stage binaries and the
synchronous CE resident are byte-identical to `b6f55e1e0876`. Pinned CI and
the console run remain separate gates; these checks do not establish speed.

## Diagnostics and console test

`TOKYIELD` counts token-search pauses after clocking bytes and exhausting
the entry budget; zero-byte probes/deferrals do not increment it. `TOKBYTES`
and `TOKMAX` still describe entire searches, not individual slices. A
TOKMAX above 256 is expected when a long search spans multiple entries;
it does not mean the per-entry allowance was exceeded. Existing failure
and recovery counters remain available on the same photographable screen.

Cold boot and repeat ARMADA's intro with unchanged reader settings. Record
playback smoothness and capture the full menu-return screen, including the
build ID and TOKYIELD. Then cold boot and test Worms separately, noting boot,
lag and audio/video synchronization. A successful host model or nonzero
yield count alone does not establish a console speed or smoothness gain.
