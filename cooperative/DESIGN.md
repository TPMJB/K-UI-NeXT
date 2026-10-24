<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native loader: independent progress with bounded CPU work

## What the successful CE work actually changed

The owner-tested CE/CDDA sequence was `3e5fce65537a` followed by
`1aa16821209b`. The first shared a 32-pump physical-work budget across the
audio worker's whole turn, reducing its possible reception from about 75 KiB
to about 16 KiB and publishing checked partial audio before sleeping. The
second replaced volatile byte RAM copies with aligned integer word copies.
The owner described the remaining lag as almost imperceptible. See
`wince-cdda-worker-budget-test.md` and `wince-cdda-copy-test.md`.

The native reader already has aligned word `memcpy`. Its 2df1c301b26f capture
had 6506 foreground SD blocks and no IRQ-delivered blocks, with a maximum
8.22 ms delivery visit. VMU completion was at most 0.108 ms. That does not prove
the cause of a movement freeze; it makes independent data progress a useful
next experiment, rather than another reduction of CHECK supply.

## Lessons from Saturn and PlayStation

Sega's file-system manual separates immediate-return requests, look-ahead to
the CD buffer, and explicit server execution (`GFS_NwFread`, `GFS_NwCdRead`,
`GFS_NwExecOne/Server`). Sony's streaming library separates arrival, locked
ring-buffer data and consumer release. It also documents deferring a 2 KiB
streaming DMA from the interrupt to an application-chosen service point to
control bus occupation. These are producer/consumer and scheduling patterns;
they are not evidence that a lower-powered CPU makes arbitrary Dreamcast
software's I/O cheaper.

Primary manuals:

* https://www.infochunk.com/saturn/segahtml_en/prgg/prg1/file/hon/p08_14.htm
* https://psx.arthus.net/sdk/Psy-Q/DOCS/LIBOVR46.PDF (CD/Streaming Library,
  printed pp. 11-34 through 11-36)

K-UI cannot apply the PSX movie library's frame-discard policy to opaque game
reads. Every requested sector and the game's completion contract must remain
exact. The useful adaptation is independent production, short service visits,
and explicit publication of checked data.

## This candidate

The first prototype adds an opportunistic loader server after the existing
physical-controller completion proof. It keeps the movie-tested 20/25-block
foreground catch-up and true SCI IRQ cap 8. The independent server delivers
at most 2 blocks, retains the one-block PIO fallback, and never waits for DMA.
It can restart an idle run as well as consume an arrived block.

A new test reproduces a capped batch leaving an active request without a
receiver in flight; a future SCI completion cannot advance it. The server
then completes the same read without another game GD entry. This establishes
a liveness gap and recovery in the actual production reader model, not a
measured cause of the owner's console stall.

The service uses the existing worker-stack gateway under IMASK15, with a
separate read-only command. It cannot enter the save writer, acquire a new
bus, or issue virtual-card traffic. It runs only after a successful physical
controller completion, skips storage-in-flight/handling and diagnostic control
modes, respects the existing GD lock and native loan state, and preserves
the SDK caller's VBR/SCI context. Existing return trampolines retain their
normal re-arm role. No priority, timer setup or 192 KiB reservation changes.

The low resident remains below the original 0x8c007ba0 boundary. Its font is
bit-packed with all 252 decoded glyph rows tested against the original;
redundant cold-report labels and unused hook-count diagnostics make room for
the reader work and exact request timing. Guard and stack boundaries remain.

The 2-block limit bounds data work, not total elapsed time. Existing command,
token and recovery waits still have their original finite limits. The current
prototype is not a hard real-time scheduler. Additional controller service
also adds work to its completion hook, so console comparison is essential.

## Follow-on design, after measuring this producer

A 16–32 KiB read-ahead ring can fit within the already reserved lease tail.
Use low/high watermarks to refill gradually, reset speculative state on a
seek/cancel or image change, and prefetch only through validated immutable
game-map extents. Foreground catch-up should respond to actual queued data
and request progress. This avoids a title-based or movie/gameplay guess.

This cache is a proposal, not part of the candidate. It cannot hide a sustained
physical-bandwidth deficit or eliminate every game's synchronous asset load.
First verify that cooperative/IRQ blocks actually occur, compare complete
request times with the visible pauses, and retain the established FMV supply.

## Broad native VMU coverage

The separate Standard/A2 test route allows cold SDK-family discovery by
omitting `--game`. It opens investigation beyond the six cached images while
retaining unique discovery, lifetime, transport and ownership admission.
Unknown families remain explicit adapter work. Neither this experiment nor
an untested cold match is a claim that all native games can save correctly.
