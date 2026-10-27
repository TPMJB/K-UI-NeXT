<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native VMU performance: hardware finding and DreamShell design review

Reviewed 22 October 2026. K-UI measurement baseline is
`1e5b595f9618b2cfd1bfd10ac1fdcc3a48b5b0b3`; this document does not claim
hardware acceptance of the subsequent optimization. The source comparison is
read-only. No DreamShell source, machine code, signatures or implementation
translation is incorporated into K-UI by this review.

## What the console established

The owner's paired test reported substantially better gameplay with virtual
VMU disabled. The enabled Sonic Adventure 2 capture contains 1,200 poll pairs
over 19.9695 seconds. See the complete transcription, calculations and limits
in [the capture evidence](evidence/native-sa2-latency-2026-10-22.json).

| Measured C body | Calls | Mean per call | Captured wall time |
| --- | ---: | ---: | ---: |
| Heap release guard | 16,658 | 452.10 microseconds | 37.71% |
| Maple submission | 1,200 | 559.25 microseconds | 3.36% |
| Maple completion | 1,200 | 500.24 microseconds | 3.01% |
| VMU storage service | 0 | No calls | 0% |

These scopes execute with interrupts masked. Their nonoverlapping measured
total is 44.08% of the capture, including instrumentation overhead and
excluding assembly entry/exit. There are about 13.88 release checks per poll
pair. Thus work on an apparently unrelated game allocator path matters much
more than its individual submillisecond duration suggests.

This is direct evidence for reducing CPU work and masked duration before
changing storage policy. There was no VMU storage traffic to optimize in this
window. Instruction cache remained enabled at every captured sample. Neither
SCIF bandwidth nor a disabled instruction cache explains this measurement.
The control also omits the 192 KiB reservation, so a separate memory-budget
effect is not excluded by this pair alone.

## Implemented optimization and compiled evidence

The prepared worker now records immutable geometry, eight live allocator
identity witnesses and exact installed-hook expectations at admission. Each
release still checks the current candidate, every free node and the cursor.
Every poll and release still reads the current hook bytes. A halfword-aligned
12-byte veneer uses four aligned masked loads, covering precisely the same
bytes as the previous six halfword loads.

Protected SDK spans are validated before clipping to the managed arena, then
sorted and merged. A candidate bounded by that arena cannot overlap an
excluded span outside it. The reviewed SA2, Skies and Power Stone 2 arena
fixtures therefore need no SDK-interval loop on each free. The worker's own
lease remains protected independently. Full storage-boundary heap, layout,
runtime, DMA and bus checks remain in place.

The offline helper executes actual linked SH integer code with a synthetic
valid allocator, 60 SDK ranges outside its arena and one halfword-aligned
exit. See [the reproducible evidence](evidence/native-optimization-sh-2026-10-22.json)
for hashes, commands, all node counts and mutation results.

| Compiled scope | Before instructions | After instructions | Before / after private P2 writes |
| --- | ---: | ---: | ---: |
| Shared installed-hook check, three exits | 3,574 | 226 | 295 / 0 |
| Release callback including hooks, 64 free nodes | 15,612 | 4,211 | 865 / 143 |
| Additional free-list node | 133 | 59 | 7 / 2 |

These are executed instruction and data-access counts, not cycle estimates.
The callback comparison folds the common stack checker and legacy generic
reader into successful equivalent operations; their instructions are excluded.
Host sanitizer tests additionally cover live read failures, malformed lists,
alias variations, evolving allocator states and every protected patch bit.
Hardware gameplay, save and power-cycle acceptance is still pending.

## Upstream pin and scope

Current DreamShell master was
[`c55ebd19030d7527b936172bddc0fae38cd0f081`](https://github.com/DC-SWAT/DreamShell/commit/c55ebd19030d7527b936172bddc0fae38cd0f081).
It is one commit beyond the earlier comparison pin
`f3cb6cd8f11cece3461244e2b6c92f1c539983ab`; that commit adds a CRT test
application and does not change `firmware/isoldr`. The inspected Maple and
CDDA sources therefore retain the behavior described in the
[broader feature comparison](vmu-feature-comparison.md).

This review examines reachable source behavior, not measured DreamShell
performance. It does not establish per-title compatibility or a throughput
advantage on any transport.

| Primary source at the current pin | Git blob SHA |
| --- | --- |
| [Maple handler](https://github.com/DC-SWAT/DreamShell/blob/c55ebd19030d7527b936172bddc0fae38cd0f081/firmware/isoldr/loader/maple.c) | `668992b1a956ab615fd940a4e79b264ff6424bd7` |
| [Allocator adapter](https://github.com/DC-SWAT/DreamShell/blob/c55ebd19030d7527b936172bddc0fae38cd0f081/firmware/isoldr/loader/malloc.c) | `e18580405ab483caa7d325c2492ae106767c07b0` |
| [ASIC dispatcher](https://github.com/DC-SWAT/DreamShell/blob/c55ebd19030d7527b936172bddc0fae38cd0f081/firmware/isoldr/loader/asic.c) | `cf5e99f6a813065f8fe3d08c61c429ddd6c89fca` |
| [CDDA engine](https://github.com/DC-SWAT/DreamShell/blob/c55ebd19030d7527b936172bddc0fae38cd0f081/firmware/isoldr/loader/cdda.c) | `d913e513cb68053474267804d00da41c7d0cf32c` |
| [GD syscall service](https://github.com/DC-SWAT/DreamShell/blob/c55ebd19030d7527b936172bddc0fae38cd0f081/firmware/isoldr/loader/syscalls.c) | `e145bcf42ab68e7472193e8f00cb41bbb8fcb8e4` |
| [Build policy](https://github.com/DC-SWAT/DreamShell/blob/c55ebd19030d7527b936172bddc0fae38cd0f081/firmware/isoldr/loader/Makefile.cfg) | `7bc4b73e3cc884ce297d588b9e6a2a2a242e6584` |
| [Cache/stack startup](https://github.com/DC-SWAT/DreamShell/blob/c55ebd19030d7527b936172bddc0fae38cd0f081/firmware/isoldr/loader/startup.s) | `39a00ab7d5deefc2ddd253a1260ee36dca24158c` |

The root [LICENSE](https://github.com/DC-SWAT/DreamShell/blob/c55ebd19030d7527b936172bddc0fae38cd0f081/LICENSE)
is PolyForm Noncommercial 1.0.0. Some individual files, including startup
assembly, carry an explicit GPLv3 notice. Those mixed notices do not justify
assuming the current entire loader is GPL. This is an architectural comparison
with independently stated product requirements, not a determination of rights
to reuse upstream implementation.

## Concrete differences and independent lessons

| Observed source behavior | K-UI implication and evidence boundary |
| --- | --- |
| The normal Maple handler processes the completed hardware chain on admitted Maple IRQs; its traversal has a 24-descriptor limit. It does not rediscover a title's SDK submit/completion functions on each transfer. | Make ordinary controller work proportional to the current transfer. An interrupt observer could eventually broaden SDK coverage, but it introduces vector, original-handler, lifetime and completion-order contracts. It is a separate design project. |
| The allocator adapter remembers a matching allocator signature and uses known entry offsets. No every-game-free interception was found in the inspected allocator/Maple paths. | K-UI's lifetime proof has a stronger explicit purpose, but repeating invariant layout construction and hook encoding on every free is avoidable. Prepare immutable geometry once, then retain the full live candidate, allocator-root and free-list validation. Do not infer that removing our guard is safe because the reference lacks it. |
| Normal code targets P1 and startup uses a P1 stack. Maple DMA buffers use explicit uncached aliases. The build enables LTO. | Cached immutable code is already established in K-UI. Reduce function-pointer dispatch, repeated P2 configuration loads and stack traffic where the compiled SH listing proves a gain. Mutable cached aliases or blanket LTO require fresh coherence, relocation, ABI and call-graph audits. Copying another CCR setting is not justified by this capture. |
| VMU read, write and sync handlers immediately return AGAIN while image transfer is busy. Once admitted, they perform synchronous file operations. | A common owner can retry a VMU request without waiting behind an unrelated read inside a masked hook. This helps future image/CDDA/save contention; it does not fix the measured controller-only regression. Retry schedules must be bounded and preserve acknowledged-write semantics. |
| The ASIC path services CDDA from AICA, VSync, Maple and GD events. The CDDA engine advances through read/wait/prepare/output states, often returning while another operation progresses. | Independently structure combined VMU/CDDA work as bounded progress with audio refill deadlines. Preserve original IRQ routing and reentry rules. The reference also contains busy waits, including an AICA restore case; it is not evidence that every path is nonblocking. |
| The Maple handler has an unfinished physical-VMU command suppression item. It edits replies after physical DMA. | Retain K-UI's pre-DMA exclusion of virtual-slot commands and explicit real-card conflict refusal. A shorter path must not send virtual writes to a physical card. |
| VMU writes go directly to the backing file; sync requests filesystem sync. The inspected handler has no ordered redo-journal and full-block readback contract. | Retain K-UI's stronger explicit per-block durability and bus-restoration contract. It does not make a whole multi-block game save atomic and is not a general power-loss guarantee. |

Relevant source regions are `maple.c` 157-260, 293-474; `malloc.c` 26-72;
`asic.c` 40-83; `cdda.c` 1429-1540; `syscalls.c` 900-925 and 1103-1120;
and `startup.s` 32-89. These references describe observations, not reusable
upstream implementation instructions.

## Order of work

1. **Address the measured release cost first.** Prepare and coalesce immutable
   exclusions, clip them to the admitted allocator arena, and prepare hook
   witnesses once. Keep complete dynamic free-list and candidate validation.
   The capture's SA2, Skies and Power Stone 2 fixtures have no SDK exclusions
   intersecting their admitted allocator arena, making repeated checks against
   every external SDK range unnecessary. Check the emitted SH code as well as
   host read counts.
2. **Remeasure before another architectural change.** Use the same gameplay
   sequence for no-VMU, production VMU and instrumented VMU. Report total/max
   release, submit and completion times, poll frequency and observed gameplay.
   Repeat save, power-cycle and load after the performance candidate passes.
   A lower host read count is supporting evidence, not hardware acceptance.
3. **Then reduce the ordinary controller path.** A candidate design can pass
   an eligible all-physical chain through without constructing a replacement
   chain, retain an exact bounded response snapshot, and perform only admitted
   parent-presence edits at completion. It must still prove no virtual command
   reaches DMA, preserve unrelated peripheral responses, detect real-slot
   conflict, and refuse ownership/lifetime drift. This is a future optimization,
   not an implemented or validated path in this review.
4. **Budget combined storage work.** Introduce transport-independent ownership
   and bounded retries before enabling writable VMU with CDDA or SCIF. Measure
   controller latency, audio underruns, game reads and durable-commit time
   together. Save traffic is bursty; ordinary gameplay should perform no VMU
   backing I/O. Raw link bandwidth alone does not settle serial feasibility.
5. **Optimize startup separately.** Exact executable-bound discovery records
   can skip CPU searches. They must not cache allocator addresses, current card
   ownership, a storage loan or recovery authority. Display phase timing so
   discovery, ownership reads and recovery are distinguishable.

## Product targets beyond a speed comparison

Useful advantages are measurable: near-control controller latency, repeatable
boots, explicit collision handling, verified durable block commits, stable
per-game or shared-card selection, and a console wizard importing physical
VMU saves with checked capacity and readback. Broader SDK families and legal
Maple schedules should be added from independent fixtures and real console
results. Neither an advertised LCD/clock bit nor a successful save in three
games establishes universal emulation.

Physical GD-ROM play with serial save backing is a distinct launch/service
attachment problem. It must preserve hardware GD-ROM command/IRQ ownership
and prove resident lifetime and serial access while the disc reader runs.
It should not be enabled by relaxing the existing image-loader gates. None of
these software investigations requires opening the console first.
