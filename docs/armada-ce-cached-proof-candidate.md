# Armada CE cached-code ownership-proof candidate

Current result: the subsequent cached MODE0 readonly test, build
`af18576dba18`, reached the existing loader's playback baseline according to
the user. It completed1,200 restored fences,1,199 initial/final proofs,
1,199 advertisements and one INFO reply without a recorded fault. This run
did not exercise MEDIA/READ/finalization or WRITE. Playback fine-tuning is now
separate from VMU development. See
`evidence/armada-af18576-cached-readonly-console.json` and
`armada-ce-vmu-write-integration.md`. The comparisons and next-test instructions
below are retained history; no further FMV comparison is requested.

The 9ed8 gate-only test improved playback but remained below baseline. The
matched proof-only test was a severe performance failure: video stopped and
repeated audio began early. It nevertheless completed 1,199 matching initial
and final proofs and restored all 1,200 DMA fence leases. A2 advertisements,
backend calls and guest RX stores remained zero. The terminal reason 6 / phase 2
capture was planned; playback failed before it. Full mode is held.

See `evidence/armada-9ed8-proof-only-console.json`. Matching safety counters do
not measure latency or establish a performance pass. The extra ownership-query
path is the principal remaining suspect; neither its masked duration nor audio
underrun has been measured.

## Costs outside the common read counter

The common sampler's displayed195 loads omit both RX ownership queries. For
this representative parent/controller fixture, those add660 budgeted guest
inspection loads and24 bulk snapshot calls per poll. The old helper also
executes all C instructions through uncached P2, repeatedly copies32-byte
mapping records for already captured metadata lines, and clears each mapping
through32 volatile byte stores. Fourteen resolves per query mean448 such clear
stores, or896 per initial/final pair. Stack, private copies and instruction
fetches are outside the guest read counter.

This candidate addresses those execution costs without dropping mapping,
allocation, descriptor, context, repeated-data or final-query witnesses.

## Linked execution and storage domains

| Domain | Contents |
| --- | --- |
| P2 executable island | Existing shim, terminal transfer, legacy array-read leaf, complete native snapshot and their literals |
| Cached P1 executable/read-only image | C functions, compiler copy helper and immutable constants |
| P2 mutable storage | BSS, shared snapshot job, staging, private stack, counters and reports |

The ELF has explicit VMA/LMA domains and one contiguous physical copy image.
The unchanged bridge enters the same P2 shim; its root-call literal names the
linked P1 C entry. All absolute C function pointers resolve to P1. Both cache
leaves remain P2, and every load between their first and last array operations
uses P2/private storage. The native leaf keeps the required eight instructions
before cached execution resumes. No generic callback-based cache-word sequence
is reachable in this native configuration.

The existing pre-NK publication sequence copies and compares the complete
physical image, then invalidates caches before owner execution. The new package
audit pins the final relay/trampoline publication contract. No runtime cache
maintenance, CCR update, SR-mask change or reservation expansion is introduced.
Initialized mutable data is rejected; all mutable addresses remain P2. Fresh
snapshot source selection accommodates ordinary operand-cache eviction between
snapshots. No tag or source witness survives a query/table-walk boundary.

RX repeated-word hits use a line already admitted during this same uninterrupted
query. New lines still undergo range, mapping and coherent snapshot admission;
all retained mappings and used words are checked again before success. Cached
page hits use two scalar fields instead of copying the whole mapping. The
native resolver clears the same eight fields through aligned volatile word
stores. Its ordinary non-native byte-clear implementation remains unchanged.

## Verification and next hardware comparison

Host sanitizer tests cover the real native RX query using the extracted
production C wrapper and a separate generic snapshot reference. The native
assembly has separate instruction/ABI/coherence-model tests. They check
refusals, output clearing, all retained final witnesses, budget accounting,
private bounds and the P1-caller/P2-leaf boundary. Linked auditors reject wrong
aliases, changed shim/MMIO/store/leaf instructions, initialized mutable data,
noncontiguous copy images, invalid workspace/stack bounds and altered cache
publication. The archive contains final audit results and a clean source build
record. These checks do not establish hardware timing or playback improvement.

Run only the new proof-only module. Back up and replace
`KUI/apps/games/ce-probe.kui`, cold boot with the same Armada image/settings and
physical A1, and compare the opening FMV. Skip the FMV if it becomes unusable.
Photograph the five automatic report pages. Expect MODE2, reason6 / phase2,
nonzero/equal FIRST and LAST, paired fence counts and zero A2 publication.
TRAFFIC READS should remain195 for the same four-record shape; this candidate
principally changes execution cost outside that counter.

No virtual card is advertised by proof-only. The full read-only mode from the
previous pack should remain held. WRITE staging/commit, durable virtual-save
storage, automatic adapter detection and resident physical GD-ROM boot remain
unfinished. No source or module is merged into the ordinary release branch.

## Hardware playback result and next comparison

Build2da67add6637 mode2 completed1,199 matched initial/final proofs and
restored all1,200 fence windows with no recorded fault or A2 publication.
The user reported improved playback, still below the existing loader baseline;
that baseline itself had imperfect FMV/audio compared with physical disc.
The relevant comparison is the same image/settings/transport loader without
the experimental VMU hook. Physical-disc parity is a separate performance goal.
No mask-duration, cycle or FPS measurement is claimed.

The next bounded comparison enables the existing read-only A2 publication
through MODE0, using the same cached helper implementation. No runtime code
change is needed; only the immutable four-byte selector differs. The full
backend/publication/store call graph is already included in the native audit.
The matched helper comparison and final package audit must pass before shipping.

Use the new cached read-only module, not the old all-P2 full module. Expected
terminal state is MODE0, phase2, with reason6 at1,200 visits or reason5 if an
A2 guard refuses. A2 advertisement and replies should become nonzero. The
known readonly WRITE-size guard is decimal96 (hex60); it stops before copying
a normal34-word write phase into12-byte request staging. Such a stop identifies
the known unfinished write path, not a completed writable VMU. Do not format
or save to the advertised virtual card in this comparison. There is no virtual
SD-save I/O. Compare playback, photograph all five automatic pages, then power
off. Existing physical A1 handling and all mapping/final-query checks remain.
