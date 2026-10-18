# Experimental VMU implementation status

This branch develops an original virtual memory card for K-UI. The public
2.0.0 release remains the stable runtime. The isolated exact-Toy synthetic
read-only proof has delivered virtual VMU responses on the console. The next
separate proof reads a verified prepared image from the same SCI card used by
the game loader; hardware validation of that SD arbitration is pending.

## Implemented

- A standard memory-card-only Maple device: discovery, media geometry,
  512-byte reads, four 128-byte write phases, durable block sync, reset,
  retries and protocol error handling. LCD, clock and standalone VMU CPU
  execution are outside the initial device.
- A bounded software Maple schedule adapter that removes virtual requests,
  preserves physical packets and advertises the chosen subunit in a successful
  parent-controller response. Its caller must own both DMA submission and
  completion; the adapter itself does not install a hardware hook.
- Plain 128 KiB images backed by a separate two-sector redo journal. Success
  follows ordered durability barriers; recovery handles an interrupted image
  update. Each block is atomic under the documented storage fault model.
- A freestanding SCI backend with copied extent whitelists, partition bounds,
  game-data exclusions, pinned card identity and serialized bus ownership.
  The existing original SD command reader/writer serves actual I/O. Writes
  require a separate experimental compile flag and runtime permission.
- A resident service that composes the protocol, journal and SCI backend.
  It reserves storage before a transaction, reports initial contention as a
  retry, and requires healthy release before the final save acknowledgement.
  Its raw SD path is host tested and SH4 compiled; it is not installed in games.
- A blank-only formatter and bounded validator for canonical 200-block cards.
  Existing images are never reformatted automatically.
- A standalone KOS program that exercises the shared software layers through
  real microSD storage. Its synthetic FatFs sector map is not a retail-game
  extent handoff. See [the console test](vmu-harness-test.md).
- An independently selected Toy Commander observation loader. It validates the
  complete executable and exact hook instructions, then records real Maple
  schedules in a guarded title-owned allocation. It injects no VMU commands or
  responses. See [the observer test](vmu-maple-observer-test.md).
- Separate synthetic and SD-backed read-only A1 proofs. The SD variant checks
  a canonical per-card profile against every game allocation, verifies CID and
  all256 image sectors before handoff, then uses the resident GD ownership
  lock and SCI bus through a scoped typed lease. Its worker validates CID and
  block CRCs before publishing a virtual reply. Storage contention and faults
  stop this proof; no unproved retry or game-time write is enabled.
- A read-only SH4 executable analyzer and an exact match for the owner's
  Toy Commander executable. Static hook candidates do not authorize generic
  patches to other executables.

## Evidence and limits

`make test-vmu-prototype` runs strict-warning ASan/UBSan tests for the protocol,
Maple adapter, journal, default read-only and experimental SD backend, actual
SD command framing and cross-layer durable acknowledgements. Tests exercise
all 256 card blocks and thousands of torn writes, ordering failures, CRC
errors, card changes and malformed schedules.

`make -f Makefile.vmu host-test` and
`python3 tests/test_vmu_harness_images.py` execute the actual console sequence
through real FatFs on disposable FAT32/exFAT images. Fresh processes verify
persistence; refused invalid pairs leave the storage image unchanged. fsck
checks the outer filesystem. The harness creates a real allocated, CRC-valid
two-block VMS save through the protocol and verifies reset and remount behavior.
`python3 tests/test_vmu_image_compat.py` feeds that same image through the
production VMU Manager, extraction, VMS import and verified restore.
The pinned SH4/KOS workflow compiles and packages
an independently selectable `KUI/tools.kui` and retains source and native build
records.

Host fault injection does not substitute for a physical power-cycle test.
Successful SD busy/status completion relies on the card honoring its device
protocol; arbitrary defective or dishonest flash behavior is not covered.
The journal protects a block, not an entire game's multi-block save.

### Console evidence, 17 October 2026

The standalone harness from source `87140926efd1` passed on SCI/exFAT:
allocated `KUIPROOF` bytes and package CRC16, two used/198 free blocks,
virtual-device reset, fresh reopen after remount/reconnect, and a synced clean
checkpoint. The later Y export also verifies the latest persisted save, but
the user did not explicitly report whether a power cycle preceded it; the
checkpoint screen alone does not establish that timing.

The same source's Toy observer stopped during an accepted GD DMAREAD with
CD music off, before completing its four report pages. The terminal screen
reported function `2`, command `0x11`, rejection `0`, and caller PR
`0x8c08d55a`. This is an image-I/O failure, not a rejected request; its zero
parameter/stack-offset fields are expected for that terminal path. The screen
does not distinguish storage acquisition, physical reads, raw-sector parsing,
or stream-stop failure, and does not prove the Maple hook had run.

Later photographs bearing the same `87140926efd1` build reached the observer's
planned stop: 1,200 pre-submit schedules and 4,800 descriptors, with a maximum
of four descriptors per schedule. Each port A–D had 1,200 main-unit requests;
no subunit requests were recorded. The shown malformed/unsupported, missing-end,
receive-range and stack-guard error counters were zero. Captures cover pages
0, 2 and 3; page 2 was photographed twice and page 1 is missing, so the complete
command histogram has not been captured.

These counts include the final schedule, which the observer deliberately does
not start. They demonstrate a live pre-submit hook reaching its planned stop,
not DMA-completion interception or virtual VMU discovery, saving or loading.
The user also reported further progress with CD music On, but the exact loader
route for that attempt was not established. It is not evidence for the
observer's CDDA compatibility.

Review also found that the experimental low P2 bootstrap control shared a
cache line with mutable P1 pacing state. The observer now reserves complete
cache lines for that control, with linked-ELF checks to reject shared layouts.
This fixes a definite coherency hazard; its connection to the photographed
I/O failure remains unproven. Build `e17a57de4252` contains this isolation and
compact per-layer failure details, but the photographs above still show the
older `87140926efd1` build. Console validation of the newer build is pending;
the released 2.0.0 loader is unchanged.

Subsequent photographs explicitly bearing `e17a57de4252` show all four report
pages reaching the planned 1,200-schedule stop, with 4,800 descriptors and no
shown parse, unsupported-pattern, missing-end, receive-range or stack-guard
errors. Commands were 3,601 DEVINFO and 1,199 GETCOND requests; all addressed
main units and none addressed a VMU subunit. Trigger/enable/busy samples were
0/1/0, with no sampled deviations counted; the SDK manager base was `A05F6C00`.
Entry SR `60000161` and mask bitmap `40` agree on interrupt mask 6. This
establishes the pre-submit observer running on the console, without attributing
the earlier image-I/O failure to the cache-line fix or proving completion or
virtual saving. The next capture waits longer and retains bounded subunit
metadata so a physical VMU's actual memory-card requests can be examined. The
next experimental observer also samples response headers before the exact
normal SDK consumer, with caller, DMA, SDK-ready/lock and private-memory guards.
It changes no responses and does not mount a virtual card.

All four photographs of build `83bf9649d565` show its first successful passive
completion match: one submitted schedule, four RX headers sampled, no orphan,
overwrite, busy, mask or context rejection, and caller PR `8C0B5362`. The
retained main-A DEVINFO header `1C210005` advertises a subunit. The observer then
stopped before submitting schedule 2: five packet requests had been counted
(four main DEVINFO and one main GETCOND), with one unsupported control and one
missing-end count, but no malformed or RX-range error. No subunit packet had
yet been recorded. This demonstrates the normal completion hook and controller
subunit advertisement, not a physical-card read or virtual save.

The stopped build did not retain the rejected cursor/control, so that value
cannot be recovered from these photographs. Independent inspection of the exact
owner shows its SDK inserting one-word `00000700` padding between same-port
packets; the old observer rejected that legitimate form. The next passive
revision admits only that exact padding value, counts it separately, preserves
the packet/source/RX bounds, and displays failing cursor/control details on an
unsafe stop. Its padding-aware parser still requires console validation.

The four photographs of `d8788a9c8e86` supply that validation for this run.
It reached the planned memory-card window: first card request at schedule 28,
then 1,200 more attempted starts, ending at 1,228. All 1,227 submitted normal
schedules matched their completions, sampling 4,936 RX headers. The final four
main-unit packets were deliberately unsent. No parse, unsupported-control,
RX-range, stack, orphan, overwrite, busy, mask or completion-context error was
recorded. Of 4,940 packet requests, 4,912 targeted main units and 28 targeted A1:
one DEVINFO, one LCD update (`CMD12`, function `04000000`), one memory-info
request and 25 block reads. The first read targets root block 255. Last response
header `82010008` has the expected 130-word DATA shape for a 512-byte block plus
function/location fields. The payload was not sampled, so its contents and
checksums remain unverified. No memory-card write or synchronization was seen.
These are physical-card observations; no virtual reply was supplied.

The next separate `READONLY_PROOF=1` target supplies a synthetic blank A1,
without SD access or persistence. It leaves the real DMA chain and SDK
bookkeeping intact. Before submission it snapshots supported A1 requests,
refuses writes/synchronization, and proves the exact fixed receive-bank layout,
capacity, source bounds, selectors and lock state. After unchanged hardware
completion it requires a valid physical controller with A1 absent and exact
no-response markers for A1. All virtual replies are staged privately and
validated before any external store; the controller advertisement is published
last, before the proved P2 SDK consumers. Whole-batch conflicts terminate the
proof without changing any RX byte. Strict host tests exercise all 256 blank
blocks and the integrated alternating-bank path, including every live RX-table
entry corrupted before and after DMA. Native layout/stack/integer audits and
console discovery/read validation are still required; durable saving remains
outside this proof. See `vmu-readonly-card-test.md` for the empty-slot test.

All four console pages for `aed8f76c54f5` show a clean synthetic-card run:
1,227 prepared/completed overlays, 17 virtual replies (one DEVINFO, one media
reply and 15 block reads), 8,008 generated bytes and zero commit calls. The last
read block is 241, the end of the standard directory area. The window stopped
as planned at attempt 1,228, with zero parse, unsupported-control, RX, stack,
completion-context, physical-conflict, refusal or bank-layout errors. The game
menu was not photographed; these pages establish the virtual protocol sequence
and successful guarded reply publication. They do not establish SD-backed data
or persistence. The next preparation tool must export the actual card identity,
clean image hashes and physical extent map; the earlier harness's synthetic
0..257 file callback map cannot be used as real card sectors.

Card tools now offers **Y** to verify/checkpoint that latest owned probe, map
the actual file allocation and export a unique 1,536-byte read-only profile.
Its canonical codec binds SCI transport, CID/capacity/addressing mode,
partition, image identity, at most 16 physical extents, all block CRCs and the
full-image SHA-256. After closing/unmounting/disconnecting, a fresh raw SCI
connection verifies every mapped sector and the same card identity before the
profile is written. Real FAT32/exFAT host tests cover superfloppies, partition
offsets, unique exports, source preservation and identity/read failure refusal.
The console's399d10269c83 Y result is PROFILE/READY forp0001, one physical
extent and image SHA prefix4edbef747fb15d17. Both independent Python and the
C codec admit the supplied1536-byte profile. This preparation result does not
by itself establish game-time SD arbitration.

The separate `READONLY_PROOF=2` target now copies the admitted profile into
worker-owned P2 data. The high stage verifies CID/capacity, all256 block CRCs
and image SHA against its existing initialized SCI card before game-file
reads, rejecting overlap with every game extent including audio tracks. The
completion worker borrows the same resident SCI bus under the same cached P1
GD lock, using a scoped descriptor copy and an aligned512-byte private buffer.
Each bounded read checks CID, CMD18/data/stop success and the image block CRC;
only successful lease release permits reply publication. Any post-acquisition
uncertainty or restoration failure poisons the resident card session. Initial
contention is a diagnostic stop, with no wire traffic or fabricated Maple
retry. The zero-write game-time policy remains in force.

ABI5 separates passive mode0, synthetic mode1 and SD-backed mode2. Native
packaging audits the linked gateway's lack of incoming resident calls before
excluding its frame from the unchanged low GD stack budget; all linked worker
and resident callback frames fit the worker's4 KiB stack. CI compiles only a
deterministic non-owner profile template. Local personalization validates the
clean native provenance and changes only the fixed1536-byte ROdata span in
stage/entry plus runtime envelope checksums, retaining original bytes and a
complete transformation audit. The owner's CID/map never enters Git history.
See `vmu-sd-readonly-card-test.md` for the next hardware test.

A page-2 request/reply display mismatch after later
controller-only polls is also corrected; that issue did not affect replies.

The owner's four pages for `34279291cb07` establish the first actual SD-backed
retail read proof:15virtual block reads and15CID checks, no SD busy/faults,
and matching expected/actual CRC `b2aa7578` for block241. All1,223 completed
schedules matched; the first34-word CMD0C memory-write request was refused
before hardware submission as designed. No sync or game save completed.

The next isolated `READONLY_PROOF=3` experiment keeps the SD base read-only
and adds at most16committed512-byte RAM blocks. Four128-byte write phases
and WRSYNC use the existing memory-card protocol; a successful sync commits
only to that RAM overlay. A private hardware chain contains only admitted main
controller packets, with every virtual A1 packet removed before MDST. The
original SDK transmit buffers and bookkeeping remain unchanged. Completion
validates and restores the original MDSTAR before publishing the virtual
batch to the SDK consumer. An all-virtual schedule or unsupported physical
traffic is a controlled stop. Native/host proofs must pass before delivery;
the owner's first RAM-save test is recorded below.
ABI6 retains64-byte exports and88-byte config, extending the snapshot to708
bytes for RAM and filtered-chain counters. See `vmu-volatile-card-test.md`.

The owner's `906eb3e3ce83` run completed5,247filtered batches with no parser,
RX, stack, ownership, conflict, storage or private-chain faults. Its80write
phases and20sync commands yielded20RAM block commits across14distinct blocks;
38reads came from those committed RAM blocks and15from the immutable SD base.
The owner reports creating differently named saves, apparent same-session
loading, and deletion. This establishes the intended retail RAM-write/read
path in Toy Commander; it does not establish restart persistence. The captured
WHY5 is the owner terminal/reset-return reporting route, with no VMU fault.

Durable SD saving still requires a freshly verified journal allocation and a
per-launch allocation/identity contract that remains valid after legitimate
image changes. The current profile describes the base image only; journal
placement cannot be inferred from it.

A separate persistent-profile preparation tool now exports the base, journal
and identity allocations in a 4,096-byte P2 profile. Its raw FAT32/exFAT verifier
checks current ownership, full clusters including slack, metadata exclusions,
allocation bitmaps and incoming links. The exporter preserves source contents,
requires a zero journal marker, and repeats ownership and snapshot checks after
creating and reading back the output. Host sanitizer, adversarial filesystem
and real FatFs image tests pass. This preparation work does not enable retail
SD writes; a console export is still needed before the persistent runtime can
be bound to this card. See `vmu-persistent-profile-prep.md`.

The first P2 console run (`3b9617257eb1`) refused the exFAT boot-field gate
with status -4/detail 15 before creating an output. That combined message did
not identify the failing field. Review found and corrected two compatibility
bugs: exFAT PartitionOffset zero is unspecified rather than a mismatch, and
backup VolumeFlags are stale. Current main flags, nonzero offset mismatches,
volume length, checksums and allocation ownership remain guarded. Refusals now
display the exact captured boot fields and rejection bits on short rows, with
no extra media reads. The photograph alone does not establish which field
caused that first refusal.

A second `941ea939c1bb` capture showed the current dirty flag cleared and boot
admission passing. Its status -9/detail 110 came from the conservative raw FAT
incoming-link scan. A raw exFAT FAT word is not a live reference merely because
it contains a VMU cluster number: contiguous allocations do not interpret FAT
entries, and stale words may survive past allocations. The exFAT ownership
proof now traverses all live FAT-chained regular files with exact tails,
allocation-bit checks and target exclusions, alongside the existing directory,
system and contiguous-range checks. FAT32's incoming-link scan is retained.
The captured refusal did not identify which live or stale source triggered it.

## Work required before in-game saves

1. Prove exact game-side Maple ownership before submission and before the
   original completion consumer sees responses. Controller discovery, mixed
   physical/virtual schedules, no-hardware schedules and IRQ bookkeeping all
   need correct behavior. A write watchpoint that fires after MDST starts is
   too late to provide this contract.
2. Reserve memory that the game cannot overwrite. Toy Commander's admitted
   allocator leases 192 KiB, but existing live-use evidence protects only the
   first 64 KiB; direct SDK scratch may use the remainder. The new service
   streams blocks and does not assume a safe 128 KiB RAM mirror.
3. Prepare a per-game image and journal while the shell owns the filesystem,
   establish immutable physical extents and exclusive allocation, pin the SD
   card identity, sync, unmount, and hand off only resident-owned state.
   Imported images require validation; related discs need deliberate shared
   card identities.
4. Reserve storage transaction ownership before a commit. Contention after a
   journal transaction has started cannot be reported as a harmless retry.
   Any uncertain I/O must stop writes until controlled recovery.
5. Validate discovery, formatting, saving, loading and restart persistence in
   the admitted game on the owner's console. Start with synchronous native
   SCI loading; async/background CDDA and WinCE require separate ownership and
   scheduling work.

The chosen virtual address shadows a complete Maple subunit. Keeping a
physical VMU's LCD at that same address would require an additional function
router. Other physical subunits must continue through hardware unchanged.

No DreamShell loader implementation was consulted or incorporated. Protocol
and SH4 behavior were checked against primary documentation and the project's
pinned KallistiOS sources; the new project modules are independently authored.
See [protocol](vmu-protocol.md), [journal](vmu-store.md),
[Maple ownership](vmu-maple-interception.md), and
[runtime design](vmu-runtime-design.md) for the precise contracts. The
[service contract](vmu-service.md) describes the composed resident API.
