# Experimental VMU implementation status

This branch develops an original virtual memory card for K-UI. The public
2.0.0 release remains the stable runtime. The exact-Toy SCI-backed RAM-save
proof has delivered virtual responses and user-reported same-session save,
load and deletion. The separate Card tools physical-journal proof has now
committed generation `01` and reopened the same generation and image hash on the
console. The isolated Toy durable-save build `fc05c8e259e2` has passed host and
native audits. The user created profile `ABC` and reports finding it after
reboot; the supplied game screenshot lists that profile. Silver now also has
owner-confirmed imported-save loading and durable saving across a full power-off
and reboot using its separate p0003 card. Its practical timed-test stops have
been removed for routine play. General game support, automatic per-game card
selection and the normal-launcher integration remain unfinished.

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

The earlier R1 profile describes the base image only; journal placement cannot
be inferred from it. Durable SD saving requires a verified journal allocation
and a per-launch allocation/identity contract that survives legitimate saves.

A separate persistent-profile preparation tool now exports the base, journal
and identity allocations in a 4,096-byte P2 profile. Its raw FAT32/exFAT verifier
checks current ownership, full clusters including slack, metadata exclusions,
allocation bitmaps and incoming links. The exporter preserves source contents,
requires a zero journal marker, and repeats ownership and snapshot checks after
creating and reading back the output. Host sanitizer, adversarial filesystem
and real FatFs image tests pass. This preparation work does not enable retail
SD writes. See `vmu-persistent-profile-prep.md`.

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

Build `482122bf7a8a` then reached PROFILE / READY on SCI/exFAT. The uploaded
4,096-byte P2 passed the independent decoder and native C codec: canonical
p0001 identity/ownership, one image extent, one journal extent, exact useful
lengths and the original image SHA256. The final export scan had rechecked
ownership and source snapshots after creating the profile. This is successful
preparation evidence; it does not itself establish durable retail saving.

The separate physical-journal program uses a pinned persistent session:
current device identity and full semantic filesystem admission precede
physical-map journal recovery, then VMU validation and fresh image hashes/CRCs.
Prepared content
hashes are provenance rather than permanent expectations. Bound callbacks
authorize only useful image/journal sectors, with identity/health checks and
readback before commit success. The owned test updates the KUIPROOF short
description and package CRC16 within one block, leaving all other VMU blocks
and outer filesystem metadata unchanged. It retains the real redo record for
restart verification; the older synthetic A/X harness must not reopen this
binding. Strict sanitizer, independent failure-cut and real FAT32/exFAT image
tests pass. See `vmu-physical-journal-proof.md`.

The two supplied screenshots for build `1100516b4411` show the physical proof
passing on SCI/exFAT: **A** changes generation `00` to `01`, and the reopened
**X** run finds generation `01`. Both screens report the current image SHA256
prefix `75a42e93ded7d958`. The successful paths include fresh live allocation and
identity checks, the real physical journal binding, VMU filesystem/package
CRC16 validation, exact block readback and checked final SCI release. The
screens were supplied following the requested power-cycle sequence; the
photographs alone do not independently measure removal of power. This is
evidence for the owned single-block Card tools transaction and fresh reopen,
not a retail-game save or whole-save atomicity. Its valid redo record remains
bound to the same image/journal pair; the old synthetic A/X harness must not
reopen that binding.

The new isolated `READONLY_PROOF=4` Toy loader implements the next durable
retail experiment. Before game loading, its high stage decodes the embedded
P2, excludes all game/audio extents from all three VMU files' whole allocated
clusters, checks actual card identity, and performs fresh semantic filesystem
admission before real-map recovery. VMU validation, an explicit checkpoint,
fresh image SHA/CRCs and a final zero-marker check produce a sealed 1,272-byte
handoff. Preparation hashes remain provenance and do not reject legitimate
saved changes. The installer rechecks the handoff's seal, identity and maps
against the embedded P2 without replacing its current hashes.

The mode 4 worker retains the filtered physical-only Maple chain and guarded
MDSTAR restoration from the RAM proof. Its first storage transaction requires
the handoff's marker to remain exactly zero. Four write phases stage one block;
sync uses the existing real-map redo store under one complete SCI lease, with
ordered barriers and exact base readback. Stage and worker writes use PIO.
Current CRCs and the save acknowledgement become visible only after successful
lease release/restoration. Focused session, stage and backend sanitizer tests
pass. Build `fc05c8e259e2` also passed the exact-source native workflow: low
stack 1,224/1,232 bytes, combined worker stack 3,304/4,096 bytes, 9,536 bytes
spare inside the protected 64 KiB worker reservation, and both linked PIO
adapters passing all 16 argument/callback execution cases. The actual user P2
was personalized within its exact permitted span and the saved ZIP was checked
byte-for-byte. See `vmu-persistent-card-test.md`.

The user then created the Toy profile `ABC` and reports that it remained after
reboot. Photograph `image-1792298257262.jpg` shows `ABC` in the save selector.
This establishes user-reported named-profile restart persistence for this
exact experiment. It does not yet establish loading saved gameplay progress,
deliberate power-loss recovery during a write, or compatibility with other
titles and reader modes.

Mode 4 keeps the exact original Toy/SCI/Standard route, CDDA and RAM patches
off, the 64 KiB protected worker limit and 4 KiB private stack. Its stop bounds
remain 18,000 schedules after first card traffic and 36,000 overall,
approximately five and ten minutes at 60 schedules per second. Other titles, async/background
readers, WinCE, formatting and general virtual-card management remain separate
work; the public 2.0.0 runtime is unchanged.

The separate `PERSIST_IMPORT=1` Card tools test now captures a physical VMU
read only, validates its full 128 KiB image and copies it into a new unused
`p0002` or later pair. It requires fresh allocation admission of the working
`p0001` before creating files, skips all existing/partial folders, and publishes
the new identity last. The imported image and empty journal receive fresh raw
allocation/content checks before a new P2 is exported. A bounded BREAD adapter
checks the exact response length before payload access; project compiler
callgraphs reject physical VMU SDK calls other than the mutex pair.

Strict sanitizer tests include 2,078 clone callback failure cuts. Actual FatFs
FAT32/exFAT MBR2048 tests cover protected/opaque byte preservation, separate
p0002/p0003 imports, existing retained p0001 redo preservation, identity-last
publication, source framing failures, damaged allocation and stale-anchor
refusals, current P2 export and fsck. This is preparation evidence; an imported
retail save still needs its own profile-bound load/update/restart test. See
[vmu-import-prep-test.md](vmu-import-prep-test.md).

The first import console capture (`60a8caa629e3`, A1) refused with clone
I/O status -3 and image status 0 before creating any SD files. The screenshot
locates a read refusal before semantic VMU validation; its low-level cause is
not established. Review found the isolated reader used one frame trylock where
the pinned SDK waits. The new read revision uses a bounded 100 ms frame wait
and freezes exact block/pass, stage/result, frame state, response type/length
and function/location diagnostics on failure. Mocks now share send/receive
storage as the real KOS frame does.

The black official VMU later still produced different block-0 data in separate
accepted reads, while each individual raw-before/raw-after/copied-output CRC
agreed. The user reports that this card was also used for earlier copy/delete
tests; the causal connection remains unestablished. The white VMU instead
completed full stable reads but failed the old root geometry check. Build
`acef57cdf110` accepts the opaque root metadata without changing formatting,
FAT or allocation validation. Its white-card preview and completed import
both show 13 saves, 196 used blocks and the SHA prefix `0085e1c864b03bc3`.
The new pair is `/KUI/vmu-test/p0002`; the uploaded P2 has passed canonical
Python and actual C decode/re-encode, with a full image SHA matching the photo.
Physical source reads and separate SD-clone preparation are now demonstrated.
The copied card has four free blocks, so a new game save may not fit.

The later uploaded `card.vmd` exactly matches that import and all 256 profile
block CRCs. Production filesystem admission and an independent FAT walk agree
on 13 files, 196 used blocks and four free blocks. No filename or readable
payload description identifies Toy Commander. The game asks for at least eight
free blocks, so the imported-card Toy test is withdrawn for this full card.
Its contents must not be deleted to force that test. Existing p0001/ABC testing
and loading an imported save in its own supported game are separate checks.

This import did not change the retail loader's p0001 selection. A fresh mode-4
loader must inherit the corrected root validator and use the imported P2;
personalizing an older compiled loader changes its profile bytes only and
cannot update its validator. The profile alone does not contain the VMU's
save payloads or all names. Loading an imported save, updating it and reopening
it after power-off still require hardware confirmation. The exact-Toy hook
does not yet enable the imported Silver or Time Stalkers saves in those games.

Clean source `bf91dd1e994c` has now produced that fresh mode-4 loader. The
uploaded p0002 profile is applied only to its audited fixed read-only span;
the selected pair is displayed before launch and its guide names p0002.
Host stage coverage includes a 13-save, 196-used-block card with opaque root
metadata, a separate unchanged p0001, recovery and stale-map refusal. Native
stack, worker bounds, storage adapters and personalized module derivation
pass review. Imported-card gameplay and restart verification remain pending.

The separate read-only cadence probe at `bd9d4c02aff6` retains 32 interleaved
reads of blocks 0, 254 and 255 at each of three gaps (0, 20 and 100 ms).
It reports full-byte differences, cadence-local transitions, actual timing
and frozen reader diagnostics. A header displays the already captured FAT
entry for block 0, without extra I/O. It does not accept inconsistent data,
skip free blocks, normalize a clone or connect to SD. Fourteen sanitizer
scenarios and a clean native build pass; no new hardware result is implied.
See [vmu-read-probe-test.md](vmu-read-probe-test.md). The actual p0002
`card.vmd` would be needed to inspect all thirteen save names and payloads
offline; its preparation profile alone cannot provide them.

The black VMU has now completed the corrected `0438c2feb0c5` cadence probe in
A1: all 288 bounded-reader calls succeeded with no source-guard failures.
Block 0 differed from its first accepted baseline in 20/32, 26/32 and 24/32
samples at requested gaps of 0, 20 and 100 ms. Its cadence-local transitions
were 25/31, 26/31 and 24/31, so this is continuing change within each cadence,
not merely disagreement with an unstable first baseline. Slowing reads did
not stabilize the block. The aggregate byte/bit totals count repeated sample
comparisons, not distinct faulty locations.

FAT block 254 and root block 255 were each byte-identical across all 96
accepted samples. Their CRC32 values stayed `86e43a4c` and `0d4b0f69`;
FAT entry 0 was `fffc` (free). The visible frozen raw-before/raw-after/output
CRCs agree, including the changed first sample of the 100 ms cadence. This
probe has not tested any allocated save-data block and does not establish
save corruption or a cause in the earlier copy/delete tool. Exact raw import
continues to refuse inconsistent data; free blocks are not skipped or
normalized. A future read-only comparison should select allocated save data
from an admitted, stable filesystem map before deciding on logical file-copy
support separate from exact raw cloning. No new physical write test is needed
for this observation.

## Silver preparation and separate-card construction

The owner's supplied US Silver startup bundle identifies native product
`T15108N`, version `V1.001`, executable SHA256
`4460c60597d6c1f587c125c2dd4b2b491d0a568f31b52c91bce7d8c74f9d550d`.
An isolated passive profile now has its own exact manifest, heap initializer,
allocator metadata, Maple start/consumer sites, receive tables and exit guard.
It admits passive mode 0, synthetic read-only blank-card mode 1 and fixed
SD-backed read-only mode 2. Silver write modes 3 and 4 remain refused. The Toy
profile remains the default, with its existing admission and virtual-card modes.

Silver's completion wrapper inherits its caller's interrupt mask. The passive
worker counts lower-mask entries and leaves response headers unread; the
original SDK consumer still runs. It admits header observation only with mask
15 and its exact live context, including all 48 receive-slot pointers. The
first console test uses no physical VMUs, stops within 3,600 attempted starts
and needs all four report pages. See [the test guide](vmu-silver-observer-test.md).
The first passive console baseline has now completed; virtual save support
and durable compatibility remain unproved.

The first Silver owner test reports a freeze with `ENTERING OWNER BOOTSTRAP 2`
as the last visible line. That screen is not a phase boundary: the native base
relay previously left it visible through post-bootstrap CRC/SHA admission and
native startup. Static inspection found no concrete relocation, load-range or
owner-bootstrap collision. The next diagnostic adds Silver-only visible relay,
admission and heap-install checkpoints, plus 64 KiB SHA/reference-scan progress.
It retains the same exact guards and cache policy and does not claim to fix
the reported freeze. A photo of the last new checkpoint will locate the stop.

The owner subsequently corrected that report: the original `7c0cd589610e`
build did not freeze, but took a long time. All four passive report pages
show its planned 3,600-start hard stop, 14,400 descriptors, no malformed
traffic, stack faults or invalid RX buffers, and the expected two TX banks.
There are 3,599 completion calls: 3,540 matched schedules and 59 mask-skipped
schedules later overwritten. Header count 14,160 is exactly four per match.
The only sticky completion flag is `08` (interrupt mask); other context,
busy and orphan counters are zero. Start IMASK is 6; completion masks are 6
and 15. No subunit requests appear, consistent with the no-card baseline.
The earlier screen therefore does not describe a remaining boot freeze, and
rerunning the startup-checkpoint build solely for that issue is unnecessary.
The next bounded mode-1 test admits exactly masks 6 and 15 at both handoffs;
its worker still mutates replies only while the assembly interposer holds
mask 15. Exact owner witnesses pin the normal ready/TAS gates, which reject
resubmission or recursive consumption until the original consumer returns.
The separate gun producer is Holly-only and masked at level 6. Both timer
callback cells must remain zero, in addition to the existing caller, ready,
lock, bank, all-48-slot, software-trigger and idle checks. The completion also
rechecks exact source descriptors and the supported request words before any
reply is published. This is static admission for a new test, not console
evidence of virtual replies.

The isolated Silver mode-1 loader presents only a synthetic empty A1 card.
It permits main-controller discovery/input and A1 device information, media
and block-read requests; other subunits and unsupported commands stop before
DMA submission. It never opens p0001, p0002 or an SD save backend, and does not
acknowledge writes or formatting. Both TX/RX banks, supported masks, unchanged
unrelated bytes, all receive-table corruptions and context/mutation refusals
are covered by sanitizer host fixtures using the actual read-only backend and
overlay. See
[the read-only test guide](vmu-silver-readonly-card-test.md).

The first mode-1 console report (`efa1f3885c5e`) shows 17 virtual replies:
one information reply, one media reply and 15 block reads. The 8,008 reply
bytes reconcile exactly to 116 + 32 + 15 * 524. All 732 submitted schedules
completed and matched, including inherited masks 6 and 15, with no reported
proof, bank, context, mask, busy, conflict, parse or stack fault. The report
stopped at start 733: first card request 133 plus the configured 600-start
window. The owner had not reached the loading screen. This is discovery/read
evidence for the synthetic empty card; it does not establish a load-menu result
or imported-save loading. The next mode-1 build extends only its hard limit
to 36,000 starts and post-card window to 18,000. Passive Silver and all Toy
limits retain their earlier values; saving and formatting remain disabled.

The longer `0cdd543d8e4f` test reached the main menu. After the owner pressed
Start accidentally, Silver issued one BLOCKWRITE (`0c`). The report shows
stop reason 6 and flag `08000000`, the strict request-shape guard. All 1,673
earlier batches completed and matched; the final A1 descriptor was counted
but stopped before request payload capture or DMA submission. The 17 earlier
read-only replies and 8,008 bytes are unchanged, with zero commits and no
context, bank, mask, conflict or stack faults. This is the expected read-only
refusal; the existing build can be reused to observe the Load Game screen.
The report does not identify the intended contents of that attempted write.

The next owner photograph shows Silver's Load screen with an empty list and
"NO SILVER DATA FOUND" at A1, the expected result for the synthetic blank
image. Together with the earlier reply reports, this completes the empty-card
discovery/read/menu check. The next test will read the unchanged full imported
p0002 image; it needs no additional free blocks because saving remains disabled.

The mode-2 test carries the complete Silver read-only contract from mode 1,
including exact masks 6/15, inactive timer callbacks, held ready/TAS state,
all 48 slot pointers and unchanged source metadata. It adds the existing
serialized resident SCI read lease, per-block CRC/identity checks and release
health checks before reply publication. No SD writer, sync, volatile-save or
durable-save path is enabled. Both Silver modes use the extended run limits.

The uploaded full-card P2 preparation profile and raw image match exactly.
A separate projection helper preserves its base extents, identity, CID, SHA
and all block CRCs in the canonical read-only R1 format. It does not change
the input pair or grant write ownership. The mode-2 high stage must still
reject game-track overlap and verify the actual card and all 256 mapped
sectors before handoff. The owner confirms both existing Silver saves loaded
and worked with build `e5f8f76e8923` and the unchanged p0002 image. No new
report pages accompanied that result. Saving, deletion and persistence after
a new Silver write remain untested. See
[the SD read-only guide](vmu-silver-sd-readonly-card-test.md).

The pure offline subset constructor retains explicitly selected directory
slots, full allocated payload blocks, original directory records and the
whole root. It validates the complete source first, clears removed files and
unused data in a separate output, and validates that output independently.
It does not infer save groups from prefixes or alter package/protection bytes.
The CLI refuses overwrites and emits byte hashes and the admitted inventory;
see [vmu-image-subset.md](vmu-image-subset.md).

Applied to the unchanged uploaded white-card image, exact slots 0, 1 and 2
retain `_SILVER_.SYS`, `_SILVER_.001` and `_SILVER_.002`: 36 used blocks and
164 free. The separate image SHA256 is
`36a1275ac94b9826778d09116864ba4740a4ad50dbc9f3ff49bff65236f7754b`.
This is an offline image, not an admitted persistent pair. Its own identity,
empty journal and fresh live-allocation profile remain necessary before
installation. Neither p0001/ABC nor the full p0002 backup is modified.
Automatic per-game assignment and on-console subset preparation remain work
to do; the constructor establishes the image operation beneath that workflow.

The next preparation package stages a separate p0003 pair containing that
Silver-only image, a zeroed 1,024-byte journal and the existing tool's exact
number-3 identity. It is copied only as a new folder; p0001 and p0002 remain
in place. The unchanged dedicated preparation program can export an arbitrary
validated VMU image's new live allocation profile with Y. It does not require
the synthetic KUIPROOF save, replay a journal or change source files. The new
pair must pass those on-console checks before a Silver write test can be bound
to it. See [the preparation guide](vmu-silver-pair-preparation.md).

## Silver durable save and cold-boot confirmation

The canonical p0003 preparation profile matches the separately constructed
Silver card's identity, initial image SHA and all 256 block CRCs. Builds
f64204b63131 and f75dd7ff8520 enabled Silver mode4 using the existing durable
backend with exact Silver heap/exit, banks, timer and interrupt-mask guards.
The first report stopped at its planned 18,000-start card window after 20
durable block commits, without reported storage, context or chain faults.
The follow-up extended that window to allow reaching a save point.

On 2026-10-18 the owner explicitly reported that the save persisted after
powering off and rebooting. This closes the Silver save/restart/load hardware
check. Neither a screenshot nor a new image was supplied for independent
inspection, so the record attributes the result to the owner's confirmation.
The current package retains all durable admission, recovery and fault guards
and sets its default schedule bound to UINT32_MAX, removing the practical
five-minute/one-hour interruptions. Explicit short test windows remain valid.

The owner identified **CHECKING OWNER SHA** as Silver's long startup pause.
That SHA runs during the intentionally uncached post-bootstrap verification;
cache changes require a separate CPU-state/publication review. The hash check
is retained, and startup optimization is deferred behind virtual-card work.
See [Silver's card guide](vmu-silver-persistent-card-test.md).

## Normal launch and complete save snapshots (2026-10-19)

The runtime can now select a separate `KUI/apps/games/vmu-silver.kui` when the
exact admitted Silver disc is launched through Games. Installing that module
opts in; other products retain the ordinary loader. Requested special modes,
transformed images, patches, corrupt packages and mismatched full maps refuse.
A typed immutable package capability is checked against the actual linked
worker profile and persistent mode during native packaging. It is independent
of the personalized ownership profile. This removes loader swapping for the
Silver test; dynamic card assignment and general game hooks remain pending.

The isolated importer now uses a read-only canonical save snapshot before the
unchanged exact clone API. Every allocated file and all metadata stay exact;
only FAT-free user blocks become zero without physical reads. This addresses
the black VMU's changing free block0 without relaxing allocated-data checks.
The source is identity-pinned and metadata/allocated blocks are verified twice
on capture and against pinned hashes on later clone reads. No physical writes
or existing virtual-card overwrites are enabled. Both changes still require
the owner hardware tests in [the combined guide](vmu-normal-launch-import-test.md).

## Work required before durable retail release

1. Silver save persistence across a full reboot is owner confirmed. Repeat Toy
   create/load/update/delete cycles beyond the initial named-profile result.
2. Exercise controlled recovery after uncertain retail I/O. The journal remains
   atomic per block, not across an entire game save.
3. Both Silver saves loaded from the imported p0002, and the separate p0003
   Silver card passed owner-reported save/reboot persistence. Keep p0002 as the
   unchanged full backup. Direct physical-VMU import passed preparation.
   Migration from existing offline VMD backups remains separate; VMU Manager
   extraction currently stages backup bundles instead of a live virtual card.
4. Add selectable standard 200-block virtual cards and per-game assignments,
   broader title hooks, and deliberate shared identities for related discs.
   The selected user-facing model is automatic whole-card selection before
   launch, using the disc's stable game identity, with an explicit shared-card
   override for related games/discs. Original and converted image formats must
   resolve to the same assignment. Import creates new cards containing complete
   related save groups; it does not filter a full card's directory while
   pretending to provide extra free space. Keep a full unchanged source backup
   and retain unassigned files for review. Each destination needs its own
   image, identity, journal and fresh ownership profile before launch. This
   dynamic routing/import workflow is not implemented by the current title-specific loaders;
   the optional exact-Silver normal-launch route above is the first integration.
   A backup must become a new image/journal/identity pair with fresh allocation
   admission; replacing the active image alone could replay an unrelated old
   redo record. Keep the working `ABC` pair intact during migration work.
5. Validate other titles and reader modes independently. Formatting and general
   virtual-card management remain separate work; the public runtime is unchanged.

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
