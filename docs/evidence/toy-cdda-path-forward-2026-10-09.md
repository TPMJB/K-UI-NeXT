<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Toy Commander CDDA: next experiments, 2026-10-09

## Decision

**Updated after R and C hardware tests:** neither cache profile produced a
noticeable improvement. Their normalized RAW and GD timings are essentially
the same. The next controlled build is an explicit synthetic-source profile
that preserves PCM16 playback and the CDDA timeline while removing only audio
RAW card reads. The revised priority is bounded SCI occupancy and independently
serviced prepared PCM, with packed DATA as a distinct bandwidth experiment.
See [R/C results and revised decision](toy-cdda-transport-decision-2026-10-09.md)
and the [silent-source checklist](../cdda-toy-synthetic-source-test.md).

The following sections preserve the reasoning before those tests; private
cache isolation is now tested and is not the selected performance hypothesis.

Keep the restored, continuous-audio `7b55156aafa2` runtime as the console
baseline. Investigate private-state cache isolation first, then compare
passive ARM snapshot batching and AICA DMA as separate factors. A later
storage experiment must include an independently serviced PCM consumer;
another sector queue alone is not the selected next build.

This is an engineering plan, not a new runtime or a claim that video is fixed.
The rollback source is native commit `e763fd3e3928`; the preserved GitHub
rollback is `12b2ad6fda2e`. Both have tree
`8f1675566ace066717292b9ecae3e516c73233f9`. The 30 existing Toy pilot suites
passed on that restored source.

## What the evidence establishes

| Observation | Consequence |
|---|---|
| User heard continuous music in the clean `7b55156aafa2` intro run, with visual holds still present. | Preserve its audio behavior while testing each performance factor. Extended gameplay remains unverified. |
| That run averaged 4.4420 ms per synchronous raw-sector callback; maximum was 9.8752 ms. | Storage callbacks can consume substantial time even when music sounds correct. |
| Maximum worker service gap was 81.2966 ms; maximum visit was 21.6474 ms. | Average throughput alone cannot establish refill deadlines. |
| Failed `71fe85e01bbb` report recorded three `COPY_RESERVE` recoveries and a nominal 123.796 ms maximum service gap. Raw, queue, and transport errors were zero. | The failure includes late refill/recovery activity, not a demonstrated corrupt-sector diagnosis. |
| Toy worker reads the original ARM's position table in sound RAM. | It does not perform the competing hardware channel-selector polling process described by SWAT. |
| PCM copies in `toy_pilot_bus.c` are bounded CPU PIO; no G2 DMA is submitted. | SWAT's DMA concern applies to the actual transfer path. The dominant video cost is still unproven. |
| Loader substitutes the game's cache startup policy `0x105` with `0x101`. | The game runs P1 with write-through to protect injected state against native movie cache invalidation. Its performance effect has not been isolated. |

At 75 raw CDDA sectors/s, extrapolating the clean callback mean gives
333.15 ms of callback elapsed time per second. This is a conditional workload
estimate, not a measured whole-game CPU utilization percentage. The failed
asynchronous report's 129.203 ms maximum raw delivery latency includes time
between game visits and must not be compared as if it were synchronous CPU
blocking time.

Evidence: [clean console run](cdda-eight-block-console-clean-2026-10-08.md),
its [exact report pages](cdda-eight-block-console-clean-2026-10-08.json),
[cursor contract](cdda-toy-driver-cursor-static-2026-10-07.md),
[lifecycle/cache contract](cdda-toy-lifecycle-static-2026-10-08.md), and
[rollback](toy-queued-sci-rollback-2026-10-09.md). The failed report's decoded
counters come from the nine user-supplied `71fe85e01bbb` photos, matched to
the version-11 report schema retained in that commit.

## 1. Protect injected state without changing the game's cache policy

This is the strongest newly identified independent performance hypothesis.
The original startup policy selects P1 copy-back; native movie paths at
`0x8c04c238` and `0x8c06da62` discard operand-cache tags. Simply restoring
`0x105` could lose dirty injected state and suspended stack frames. The
existing write-through substitution is a correctness workaround, not an
accidental setting that can safely be removed in isolation.

Investigate keeping authored code in cached P1 while placing persistent
mutable state and private stacks exclusively in uncached P2 aliases of the
same reserved physical RAM. P2 access avoids this tag-discard hazard;
software must still manage alias and DMA coherence. The Renesas manual
documents these properties; it does not predict an FMV improvement.

The audit must cover:

- Low resident request state, manifest/configuration copies, reader state,
  report state and bridge anchors.
- High audio worker owner/model/mailbox, ring bookkeeping, raw and planar
  buffers, audio stack and independent GD stack.
- Optional SCI state, saved vector/return context, IRQ stack and exported
  pointers, even though that transport is off in the baseline profile.
- Assembly saves made on inherited game stacks before switching to a private
  stack. Moving C globals alone does not protect those suspended frames.
- Linker VMA/LMA, physical exclusion ranges, exports, installer checks and
  stack canaries. Alias changes must not invalidate address admission.
- One-time purge before alias transition, and absence of later accidental
  cached references to the same mutable physical storage.

The native GD payload mapper already uses `OCBP` and writes through P2;
preserve that mechanism. Direct raw audio output bypasses that mapper and
must receive its own coherent private destination. Do not add a speculative
global flush or alter native buffers indiscriminately.

Use two successive comparisons: first relocate the private state while
retaining `0x101`; only after continuity and lifecycle checks pass compare
the same relocated binary with native `0x105`. That separates the cost of
uncached private state from the benefit or cost of restoring game copy-back.
Do not switch CCR back and forth during playback.

## 2. Reduce passive G2 observation cost without reducing proof opportunities

The original ARM remains the only hardware channel-position monitor owner.
Our reads at `0xa08014a4`, `0xa08015e8`, and `0xa08015ec` observe its published
flags and cursors. Repeated admission, FIFO draining, stable-word reads and
template validation can nevertheless cost bus and IRQ-masked time.

Add a bounded three-word snapshot primitive with one DMA-idle admission,
one exact-SR save/restore scope and one aggregate budget. Retain per-word
stability and FIFO checks. The result is sequentially observed data, not an
atomic stereo snapshot; keep the existing independent ARM publication and
phase allowance. A ten-word template snapshot is another separate candidate.

Preserve the pre-read baseline, two independent position changes per channel,
freshness limits, and every per-plane ownership/deadline/generation gate.
Do not cache templates across visits until all possible native writers are
accounted for. Do not lengthen an IRQ-masked scope merely to get fewer calls.

This differs from the already rejected poll-thinning prototype: omitting a
post-raw probe passed 59 timing profiles but lost nine previously passing
profiles. The earlier read-ahead prototypes also traded old passing cases
for new ones. Those results are in the
[continuity investigation](cdda-toy-continuity-2026-10-08.md).

## 3. Implement aligned PCM staging, then evaluate AICA DMA

Use independently authored G2 transfer code while retaining Toy's ARM
firmware, native SFX, leased sound RAM and reserved ports 62/63. General KOS
sound initialization is not a drop-in integration: this is a native game,
not a homebrew application that owns the whole sound system.

KOS documents G2 DMA separately from SH-4 DMAC, so the SCI channel does not
automatically conflict with AICA DMA. It also documents 32-byte alignment
for source, destination and transfer length. Those hardware/API facts do
not establish ownership of Toy's DMA registers or completion events.

| PCM unit | Mono plane bytes | Duration at 44.1 kHz | Use |
|---|---:|---:|---|
| 588-frame raw CDDA sector | 1,176 | 13.333 ms | Length leaves 24 bytes modulo 32; sector-by-sector destinations drift out of alignment. |
| 512-frame prepared chunk | 1,024 | 11.610 ms | First small aligned candidate. |
| 1,024-frame prepared chunk | 2,048 | 23.220 ms | Separate size comparison after ownership is proven. |
| 4,096-frame logical block | 8,192 | 92.880 ms | Existing bookkeeping unit; not a reason to hold the bus for a whole block. |

An assembler spanning sector boundaries should emit multiples of 16 frames.
Never round a transfer beyond the admitted inactive ring span. Preserve
finite-track tails and looping sample order.

Before submission, trace native G2 register writers, ASIC completion event
masks/acknowledgements and any transfer-manager ownership. An idle register
check is not a persistent lease. This contract has not yet been admitted in
the repo.

After establishing it, a short, bounded, polled transfer is the first isolated
PIO-versus-DMA comparison. It may reduce CPU copying and FIFO overhead but
does not overlap CPU movie decoding. Genuine asynchronous DMA comes later,
through a proven transfer manager or narrowly admitted completion hook.

Prepared stereo staging stays immutable until both physical transfers finish.
Only then commit the stereo fill. STOP, reset and shutdown must keep that
storage and the sound-heap lease alive until DMA is quiescent. A generation
change does not cancel physical writes. On timeout, neither reuse staging
nor fall back to PIO while the DMA might still run.

If no safe lease can be established, keep PIO and evaluate aligned PCM
batching and fewer admissions. Store queues are another possible transfer
mechanism, but require their own QACR/SQ ownership and timing proof.

## 4. A later asynchronous transport must service the PCM consumer too

The failed experiment queued storage production while PCM filling still
depended on opportunities supplied by the game's sound updater. The report
is consistent with late service; it does not isolate every source of delay.

The desired pipeline has distinct ownership:

1. RAW read completes into an owned main-RAM slot.
2. An assembler prepares an aligned immutable stereo chunk.
3. A bounded transfer owner writes only an admitted future ring span.
4. Both plane completions commit the fill and release staging.

An asynchronous transfer completion can advance already prepared work, with
a bounded foreground fallback. It cannot create fresh card data while the
CPU or card path is unavailable. A verified service/interrupt owner is still
required; adding a timer or recursively calling the native SDK is not a
proven solution.

Size buffers by measured time coverage. Four raw sectors provide only
53.333 ms, below the failed report's approximately 124 ms service gap. The
existing 32,768-frame AICA ring covers approximately 743.039 ms in total,
yet safe bank reuse has earlier observation/write deadlines. More total
capacity does not erase those deadlines or fix a sustained throughput deficit.
Do not simply double the ring: that would program `0xffff` as LEA, prohibited
by the reviewed AICA hardware contract. The fixed ring contract admits
32,768 frames; see the
[driver command evidence](cdda-toy-driver-command-static-2026-10-07.md).

## Instrumentation and release criteria

Before choosing a transport redesign, record the timing chain in a bounded
in-memory trace and expose it after playback/recovery. Include RAW submit,
start and completion; native DATA submit/completion; worker visits; cursor
proof acceptance; PCM preparation; transfer submit/completion; low reserve;
and recovery reason. Record dropped trace entries. Avoid live logging or
unbounded tracing in IRQ context.

Add cumulative/max cursor-proof, template, conversion, PCM-copy and
IRQ-masked time, alongside native SFX command latency and movie/data service
cadence. Current aggregate maxima alone cannot attribute the video holds.
Measure trace overhead with the same baseline.

Build/test order:

| Candidate | Change against its immediate control | Required decision |
|---|---|---|
| I | Bounded instrumentation only | Establish overhead and locate missed deadlines. |
| R | Private-state/cache-alias isolation, retain write-through | Preserve clean audio and all lifecycle behavior. |
| C | R with original game copy-back policy | Keep only if measured game/video behavior improves without audio/SFX regression. |
| S | Passive snapshot batching only | Lower observation/mask cost without losing passing timing cases. |
| D | Aligned assembler then isolated PIO/DMA comparison | Demonstrate transfer benefit and safe native ownership before asynchronous DMA. |
| A | Independently serviced PCM consumer plus asynchronous storage | Evaluate only after completion, cancellation and service ownership are proven. |

Each runtime must publish its exact source, profile flags and binary identity.
Use deterministic sample/destination/deadline checks, reproduce the documented
72-profile timing sweep, and add cache-invalidation injection and STOP/reset/pause/
driver-reload/recursive-hook cases where relevant. DMA tests must cover
delayed completion, cancellation between planes and timeout without buffer
reuse. Do not count only the total number of passing cases: losing an
established passing case is a regression to explain before console release.

On hardware, compare the same natural intro plus gameplay/SFX and lifecycle
controls. A useful result needs continuous CDDA and SFX, no new recovery
starts, and lower measured interference or better movie progression. One
successful intro does not prove all-title support. If audio regresses, keep
the clean baseline while using the trace to revise the specific factor.

## What PSX and Saturn research contributes

The public PSn00bSDK CD-stream example separates a main-RAM ring from SPU
playback storage, and releases transfer state on DMA completion. Its buffers
carry compressed VAG audio; copying its sizes into raw CDDA would be wrong.
Its STR player also defers CD work while conflicting MDEC output DMA is
active. These examples support explicit buffer lifetime and shared-resource
ownership, not a video-decoder transplant into Toy Commander.

SEGA's Cinepak guidance treats bus occupancy and task frequency together,
and recommends execution histories for supply, consumption and presentation.
That supports bounded transfers and the timing trace above. Neither platform
establishes the Dreamcast title's unknown DMA lease or proves that DMA alone
will fix its movie.

Public primary sources reviewed:

- [Renesas SH-4 Software Manual](https://www.renesas.com/en/document/mas/sh-4-software-manual), sections 3.3 and 4.3.8: P1/P2 access and software cache coherence.
- [KallistiOS G2 bus documentation](https://kos-docs.dreamcast.wiki/group__system__g2bus.html): G2 DMA, alignment and completion API.
- [PSn00bSDK CD streaming example](https://github.com/Lameguy64/PSn00bSDK/blob/master/examples/sound/cdstream/main.c), blob `2a9bbabddace6e88382289241bd79acc8e68796a`.
- [PSn00bSDK SPU streaming implementation](https://github.com/Lameguy64/PSn00bSDK/blob/master/examples/sound/cdstream/stream.c), blob `9dfc9ba00fe56c1e59849cf31eb2ee049f0abbeb`.
- [PSn00bSDK STR example](https://github.com/Lameguy64/PSn00bSDK/blob/master/examples/mdec/strvideo/main.c), blob `038ba7a938b7a10ddf2facddbec2d60ab9ff532b`.
- [SEGA Saturn Cinepak programming precautions](https://www.infochunk.com/saturn/segahtml_en/movi/cine/hon/p04_101.htm), original SEGA manual on a documentation mirror.

No SWAT/DreamShell implementation was inspected for this review. SWAT's
user-relayed advice motivates the questions; the proposed code remains
independently authored and constrained by public documentation and the
existing exact-title contracts.
