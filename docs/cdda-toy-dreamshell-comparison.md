# Toy CDDA comparison and implementation provenance

Review date: 2026-10-08 UTC. This review concerns the separate Toy Commander
pilot. It does not change the ordinary 1.8.5 reader or establish compatibility
for other games.

## Reference pin and license

The comparison fetched DC-SWAT/DreamShell master at
`e4a81bcba87861c87bd7eaf07b79044db2c73e09`, committed 2026-10-07 at
15:31:56 UTC, with tree `4868be98caf9456a29f10e249c95a775407a9d13`.
The reference checkout remains outside K-UI's source and packages.

At this pin, [LICENSE](https://github.com/DC-SWAT/DreamShell/blob/e4a81bcba87861c87bd7eaf07b79044db2c73e09/LICENSE)
is PolyForm Noncommercial 1.0.0.
[NOTICE](https://github.com/DC-SWAT/DreamShell/blob/e4a81bcba87861c87bd7eaf07b79044db2c73e09/NOTICE)
identifies SWAT and a separate commercial-licensing contact. K-UI's
GPL-3.0-only license, or a historical GPL DreamShell reference, does not
relicense this pinned source. No reference source, binaries, license text,
allocator signatures, refill algorithm, or interrupt implementation is
imported into this pilot.

This is source-aware comparison, **not a formal clean-room process**.
Reviewing another implementation to identify missing behavior does not by
itself prove independent authorship or provide a legal guarantee. Implementation
inputs and notices remain subject to [K-UI's provenance policy](../THIRD_PARTY.md)
and the boundary in [the reader design](cdda-reader-design.md).

## Architectural comparison

| Area | Pinned DreamShell | K-UI Toy pilot |
| --- | --- | --- |
| Playback | Continuous stereo hardware loop; refill the opposite buffer half | Continuous planar stereo ring admitted through Toy's own ARM queue; guarded paired cursor observations own refill halves |
| Service | GD polling fallback and optional VBlank/AICA-DMA interrupt service | Two updater visits and admitted pause retries; elapsed-time-limited refill quanta |
| SD input | Incremental software asynchronous read progression; distinct from sound transfer | Synchronous single-sector SCI transactions, interrupt restoration between quanta, retained partial sector; separate optional status-sample reuse |
| Sound transfer | AICA DMA or SQ/PIO paths depending on configuration | Bounded PIO into a cursor-admitted inactive ring half |
| Channel conflicts | Check channel settings and restore or select another channel | Fixed, excluded ports 62/63; fail closed on unexpected settings |
| Audio status | Explicit logical track/audio/drive state | Newly projects the worker's logical state into existing GD responses |
| Transitions | Audio stop handling for abort, GD reinit, and data requests | Audio abort handling is added; data reads retain priority over filling |
| Blocking | Several lock, FIFO, and DMA waits have no deadline | New pilot bus operations have poll/time limits and terminal fault handling |

Primary reference files:

- [CDDA implementation](https://github.com/DC-SWAT/DreamShell/blob/e4a81bcba87861c87bd7eaf07b79044db2c73e09/firmware/isoldr/loader/cdda.c):
  buffer setup, direct channel programming, channel checks, audio commands,
  and refill service.
- [GD syscalls](https://github.com/DC-SWAT/DreamShell/blob/e4a81bcba87861c87bd7eaf07b79044db2c73e09/firmware/isoldr/loader/syscalls.c):
  logical audio status, command completion, audio abort, and data transitions.
- [ASIC service](https://github.com/DC-SWAT/DreamShell/blob/e4a81bcba87861c87bd7eaf07b79044db2c73e09/firmware/isoldr/loader/asic.c):
  service opportunities independent of a game's sound updater.

The comparison identifies requirements; it is not a template for implementing
them. The earlier finite pilot restarted every 16,384 stereo frames and
introduced a retirement guard at every handoff. This revision starts one
32,768-frame stereo ring and refills its halves after guarded cursor
observations. It retains dependence on the game's service opportunities.
Hardware looping alone cannot establish a refill deadline; stale audio can
repeat if the SH-4 no longer services the worker.

## Reviewed independent changes

The GD adapter uses K-UI's existing protected map and scalar command encoder.
It projects logical playback across prefill and bank exchange, distinguishes
pause/end/fault states, clamps position to the admitted track, and revokes an
outstanding audio mailbox when its handle is aborted. Protocol status values
come from the documented KallistiOS GD contract already recorded in
[THIRD_PARTY.md](../THIRD_PARTY.md). This is not DreamShell's syscall handler.

The queue acknowledgment change retains the submitted four-word packet and
driver/request generations. It recognizes dispatch followed by native slot
reuse, rather than requiring the worker to observe a transient empty header.
Its implementation follows the exact Toy driver contract documented in
[the independently inspected driver evidence](evidence/cdda-toy-driver-command-static-2026-10-07.md).
The DreamShell reference directly controls channels and supplies no equivalent
Toy SDK packet acknowledgment implementation.

The cache change follows the exact Toy startup constant and its movie cache
invalidation behavior: it admits only that constant, selects P1 write-through
before native initialization, and retains both caches. The pause wrapper keeps
the original pause attempt and result. On its documented busy result, an
independently authored follow-up validates the native SDK context, visits the
worker, and calls the game's existing guarded retirement routine. It does not
fabricate SDK completion. The native routine borrows the validated original
game stack for its GD scratch destination, then restores the private worker
stack. The bridge stays claimed, callback targets are restricted, and the
protected GD map now admits only the exact verified interrupt scratch spans described below. Validated pending file owners defer the
authored pump so the existing VBlank path can retire them within the retry
budget; the pilot never invokes their completion callbacks.
Retry time/attempt limits and exact SR restoration
bound returning attempts; they cannot preempt a native callback that hangs.
These changes follow the privately inspected Toy bootstrap/SDK contracts,
not DreamShell cache or interrupt code. Their numerical admission is recorded
in [the admission helper](../src/loader/toy_pilot_admission.h) and
[pause adapter](../src/loader/toy_pilot_pause.c); game bytes are not published.

Review covered the working changes against local base
`9ef2c818128438dc4199428ce02801ec4fb73f8f`, including the new GD adapter/status
helper, packet acknowledgment, cache admission, pre/post service bridge, pause
adapter, and tests. Manual review found no copied or closely translated
reference implementation. A supplementary scan of all seventeen changed/new
C, header, and assembly files against thirteen reference source files found no
identical added substantive code lines of at least 40 characters. Short
24-token matches occur in conventional SH-4 register stack save/restore
instructions; no reference-specific algorithm was found in those matches.
These checks cannot prove absence of copying or translation. This review does
not cover every historical project revision or establish legal clearance.

## Recording follow-up

The new report establishes a rejected native CHECK scratch mapping, documented
in the [recording/IRQ review](evidence/cdda-toy-recording-irq-2026-10-08.md).
The correction is independently derived from Toy's native veneer, captured
PR/SP and our own protected-map policy. It has no DreamShell counterpart.

The pinned reference's continuous looping pair avoided the earlier pilot's
repeated finite START/retirement gaps. This revision independently removes
that repeated restart through the exact Toy SDK queue contract. The
reference's interrupt options create service opportunities
outside a game's sound updater. TMU use in the inspected CDDA path is position
measurement; the shown TMU interrupt handler is commented out, so this review
does not describe it as timer-interrupt-driven. SD software progression,
IDE/G1 DMA and AICA sound DMA are distinct mechanisms. Enabling an IRQ does
not make the SCI card interface asynchronous.

Our bounded refill burst and sector reuse reduce specific overhead without
duplicating a reference algorithm. The continuous ring adds an independently
authored cursor-age and half-ownership rule, with explicit command and
deadline handling. These provide checked refill admission when the worker
runs; they do not provide an independent streaming deadline. Toy's
RequestEvent remains a host notification, not an ARM-side stop watchdog.
Looped playback can retain stale sound when SH-4 servicing stops.

The optional SCI build only reuses a checked receive-progress status sample
for its next TDRE decision. Missing TDRE uses the same fresh bounded poll;
framing, receive counts, DMA errors, CRC and timeout checks remain. This
change is separate from the ring worker. Its linked worker payload must
match the baseline, while each variant's actual resident, stage and complete
runtime pass the usual package audits. The DMA progression model removes
511 redundant ready-status reads per 512-byte block and exercises delayed
transmit readiness, error flags and receive counts. That is an instruction
path result, not a measured console bandwidth gain. The status-read order
follows the TDRE contract in sections 15.2.7 and 15.4 of the
[Renesas SH7750 hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).

The continuous-ring and optional SCI changes follow K-UI's own numerical
ownership model, the supplied Toy driver contract and existing SCI pacing.
No DreamShell ring/refill, IRQ or DMA implementation is copied or translated.
The earlier similarity scan described above covers its stated historical
change set; it is not represented as a scan of all new ring or SCI code.

The user confirmed Start/menu recovery on baseline `6bb04d248b35`, as
recorded in the [continuity follow-up](evidence/cdda-toy-continuity-2026-10-08.md).
The ring test must preserve that result while improving audible continuity.
Actual movie speed and uninterrupted playback still need console comparison;
host tests cannot establish either outcome.
