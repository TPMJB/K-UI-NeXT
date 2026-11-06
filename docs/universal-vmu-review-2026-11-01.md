# Shared VMU compatibility review — November 1, 2026

## Continuation baseline

- Physical-disc baseline: `bc3086754616`, branch `codex/physical-disc-vmu`.
- Accepted console result: Power Stone 2 boots from its physical disc and the virtual VMU works over SCI.
- Power Stone 1 (`T1201N`, executable CRC32 `c9e3107f`, 3,414,344 bytes) resolves its Maple driver but fails immutable buffer-layout discovery. Its log does not establish an allocator failure: that gate was never reached.
- Claude review: `claude/great-darwin-yjmgfi` through `b8e1d303`, including its early SCI family, early Maple family, exit pairs, HOTD2 arena, A1 selection, and report gesture. The local integration branch is `codex/universal-vmu`.

## What the earlier “1999 era” summary missed

The list spans different SDK versions and several independent admission gates. It is not one unimplemented driver family. Claude's later `docs/native-zr-family.md` already records much of the progress below and acknowledges the old Silver module.

| Game | Current evidence and remaining boundary |
|---|---|
| House of the Dead 2 | Claude test 7c: Background New with virtual A2 found the card, saved, and loaded after a power cycle. Extra lag and one first-boss freeze were reported with VMU On; the corresponding VMU Off run passed. Its reviewed 284 KiB arena currently belongs to the compact image-launch module. |
| Silver | Original SCI/Standard title-specific adapter already passed save, power-off, reboot and reload (`3c80d14a0529`, p0003). The adapter is retained. The direct allocator is now recognized, while shared executable extent, exit-consumer and lock-alias admission remains refused. |
| Sonic Adventure | Early SCI reader console play recorded. The direct syMalloc 1.35 allocator is now recognized; shared VMU still refuses its executable-loader/exit handling. |
| Time Stalkers | Early SCI reader console play recorded. The new direct-allocator candidate passes original-backed structural gates; shared VMU console boot/save/reload is the next test. |
| Elemental Gimmick Gear | Standalone New console play recorded after replacing a damaged rip. The candidate adds the direct allocator and a bounded literal-pool exit proof; shared VMU console boot/save/reload is the next test. |
| Sword of the Berserk | Later round-toward-zero family admitted; combined structural gates pass. Its save code targets A1, which Claude added as a per-game choice. Structural admission alone does not certify console persistence. |
| Crazy Taxi | Round-toward-zero SCI and Maple layouts admitted. A reviewed reservation route remains necessary. |
| Zombie Revenge | SCI family admitted; six-input Maple layout and reservation route remain. |
| Blue Stinger | Older table initializer remains outside the admitted early SCI family. |

Older failed tests do not override later successful console results. An original title-specific adapter's success also does not imply that the newer shared module admits that title.

## Review findings and changes

### Controller timing on the early driver

The split controller capture required IMASK 6/15 and a P2 lock pointer. The already admitted early driver permits other inherited interrupt levels and retains an exact U0/P1 lock pointer. Consequently, its controller-only traffic fell back to the larger submit path before physical DMA. That full path still has a later controller-only optimization; it is inaccurate to say every such poll performs a complete card overlay or an SD operation.

The shared split path now uses the same family-specific context and exact-pointer contract as the full worker. It does not accept arbitrary aliases. It keeps physical DMA intact, defers the same completion checks, and routes mixed/accessory/storage traffic through the existing full path. The report gesture is retained for early split completions so enabling the fast path does not silently disable Claude's diagnostics.

This removes a demonstrated source of unnecessary pre-DMA work. Console timing and the reported boss freeze remain unverified; SD contention or another card-path issue may still need investigation.

### Physical-disc integration

The two source branches merged without textual conflicts but needed semantic fixes:

1. Claude's shared installer now references the arena selector. The disc stage had no definition. It explicitly binds that selector to zero because this stage only admits allocator-backed leases.
2. The disc return binder assumed independent 12-byte exit veneers. Early SDK exits instead have an 8-byte head followed by a full 12-byte veneer. The binder now selects that full veneer, verifies both actual windows and both aliases, publishes with the production paired writer, and refreshes both snapshots before the owned retarget.

These changes share recognition and runtime improvements between image and disc launches without claiming that HOTD2's image-specific arena is automatically safe for the disc installer and its separate support reservation.

### Silver is a reference, not missing work to repeat

The original adapter and evidence remain in `docs/VMU-RESET-HANDOFF.md`, `src/loader/vmu_maple_observe_worker.c`, `include/kui/vmu_maple_observe_profile.h`, and `include/kui/vmu_silver_observe_witnesses.h`.

The available owner-supplied Silver startup was replayed in the existing SH model. Twenty original instructions change the Maple lock cell at `0x8c11cd24` from `0x8c11cd28` to `0xac11cd28`, with the store at `0x8c0dde66`. Thus an assumption that every early driver keeps the cached pointer is insufficient for future Silver support. Its startup-derived alias and coherence behavior need a specific proof.

Original syMalloc 1.35 routines from Silver and Time Stalkers were also replayed: initialization took 85 modeled instructions and 11 writes; a 192 KiB allocation took 53 instructions and returned `0x8cfd0000`; release took 73 instructions and restored the free block. These are interpreter instruction counts, not console cycle measurements.

The next allocator step is concrete: represent the direct 32-byte-tail metadata, validate live identity without a fabricated dispatch table, prove allocation/release and the reservation, and extend cache serialization. Time Stalkers has fewer additional blockers than Silver. Silver additionally reclaims part of its startup image and has extra firmware/exit consumers.

## Evidence and test boundaries

The original startup corpus contains 21 executables, including Silver, HOTD2 and the other early titles. It does not contain Power Stone 1, so this review does not invent a new layout for its photographed addresses or claim that it now launches with VMU.

Relevant automated checks cover the actual worker and installer code, early interrupt-route admission, original startup discovery, the unchanged physical-disc path, and generated SH-4 artifacts. Hardware results in this document come from the accepted owner tests; the changes made in this review need a fresh console run.

## Focused console follow-up

1. HOTD2 image: Background New, Virtual VMU On, A2 empty. Compare the lag and first boss to the accepted prior run; save and reload once if the timing is stable.
2. If a freeze persists, hold both triggers fully plus A+B+X+Y for about three seconds and capture the `0x4F` counter page. The gesture still works when early controller-only frames take the split path.
3. Power Stone 2 physical disc: Virtual VMU On over SCI; check the accepted save/reload baseline survives the shared-core integration.
4. Power Stone 1 requires its actual startup executable for the next layout proof; an unchanged refusal is not evidence of a new disc handoff failure.

## Follow-up after console build 26ed262974a8

The owner reports HOTD2 still works but feels laggier. Power Stone 2's physical-disc virtual VMU still works, with a long pause under `RELOCATING PASSIVE MAPLE WORKER`. These results supersede the preceding build's unverified console status; they do not validate the next candidate.

Two unnecessary work paths were identified in HOTD2's newly admitted split controller route:

- The early driver's port-A INFO/ALLINFO requests became eligible for controller-completion read-ahead. Previously they used the full metadata path and did not issue that cooperative reader tick. A tick may perform up to two sectors of PIO/CRC work. The candidate keeps split capture but restores the previous no-tick policy for those early-driver parent queries; ordinary condition polls and later drivers retain their existing policy.
- Split completion compared every live command word against its private snapshot, then immediately repeated the same comparison in `controller_complete`. There is no intervening game execution, DMA restart, storage operation or callback. The candidate retains the authoritative comparison before any response is followed and removes the duplicate scan. It keeps descriptor/layout checks and rejection of changed commands before response publication.

These are verified differences in work and scheduling, not console timing measurements or proof that every lag spike is fixed.

The disc's relocation caption previously remained through state initialization, support binding, worker arm, lifetime validation and hook publication. The candidate shows separate one-shot captions at those boundaries. It also uses guarded aligned 32-bit accesses in the relocator, retaining byte decoding for unaligned buffers. Hashes, complete relocation-table validation, alias/overlap checks and the no-output-write-on-refusal behavior remain. Generated SH-4 code must retain the aligned load; the source uses a small non-inlined leaf because GCC otherwise recreates byte loads at `-Os`.

The next console test should compare HOTD2 with the same New/X/A2 settings and identify the exact disc caption during the long pause. Power Stone 2 save/reload is the physical-disc regression check. No measured reduction in the entire startup pause is claimed.

### Physical-disc A1/A2 choice

The owner additionally requested A1/A2 selection for disc launches because Berserk only loads from A1. GD Play Options and the main VMU Tools menu now share a durable disc-slot preference. Missing and legacy records default to A2. The existing global On/Off preference and the new slot are preserved independently when either is changed. Image launches retain their existing per-game Background slot setting.

A1 uses a separate physical-disc module compiled with the same endpoint exchange already used by the image A1 worker. The opposite Maple unit remains physical. Card/product/image/journal identity is independent of the endpoint, so switching the slot does not create or reassign a card. The package verifies matching immutable slot markers in both the disc stage and worker, plus the displayed slot name. The A1 and A2 module paths are distinct; an unavailable A1 module must fail instead of launching A2 silently. Berserk physical-disc boot, save and reload remain console tests.

## Follow-up after console build a25580bc70dd

The owner confirms a partial HOTD2 lag improvement and a working Berserk physical-disc launch with virtual A1. The longest disc-startup caption is now identified as `RESOLVING DISC MAPLE DRIVER AND ALLOCATOR`.

The disc stage had repeated the GUI launcher's full immutable discovery with its data cache disabled. The candidate builds the existing complete resolution proof during preflight and personalizes the loaded module's typed optional seed from that trusted per-launch result. Nothing is saved as a new filesystem cache. Post-bootstrap replay freshly hashes the exact program and checks the detector epoch, record digest, allocation/placement, release window, coherence, displaced windows and subsequent live installation. A missing or rejected seed retains complete fresh discovery. A1/A2 selection and card identity are unchanged.

For remaining HOTD2 spikes, a normal early-driver condition poll can still perform optional read-ahead PIO while interrupts are masked. The candidate caps those early ticks at one fully validated block; the previous two-block policy remains for other families. This retains prefetch and stream suppression rather than disabling the reader, but does not make an individual block's time bounded. Actual VMU reads and final durable commits remain separate possible sources of pauses. The existing PIO maxima and VMU counters can be requested with the held diagnostic gesture, without adding hot-loop timer sampling. New console comparison is required.

## Follow-up after console build dc9fc5e49302

The owner reports further partial improvement, no freezes in this run, and fixed disc-startup speed. The requested diagnostic photograph shows 23 successful VMU reads and 24 SD sector reads, no write phases/commits, and no SD faults. The 181 BUSY results are safe deferrals before a storage transaction is invoked. PIO peaks are 1123/1122 ticks (about 1.44 ms) with no soft-window overruns. The global AUX peak is 6541 ticks (about 8.37 ms), but combines demand delivery, optional ticks, quiesce and other events. It cannot establish that controller prefetch caused that peak.

The next candidate preserves the one-block policy and replaces repeated byte copies with alignment-checked integer word copies, retaining directional overlap handling and publication only after the complete block. The existing AUX maximum's update now also records its entry event/argument/IRQ context in the unused final word of the existing diagnostic row. No public state growth or extra timer samples are required. The held report swaps the redundant full-PIO maximum for that context word; the single-block maximum, overrun count and global AUX duration remain.

The owner also reports an immediate-after-insertion GD Play failure that otherwise requires backing out. Explicit launch readiness now handles known spin-up and media-change transitions with bounded, cancelable retries, while real read failures and the drive's poisoned state remain terminal. An A retry in the failed disc-card screen clears the prior identity/card choices and uses a new request generation. Existing capture/idle preparation, slot settings and preflight reuse are retained.

## Next compatibility candidate: direct syMalloc 1.35

Time Stalkers and Elemental Gimmick Gear are the next shared-VMU targets. The
new descriptor represents the original direct 32-byte-tail allocator as its
own family/layout, with both absent dispatch roles strictly zero. Its
allocation and release entries are initializer+0xAE and initializer+0x160;
complete reviewed code/literal coverage extends through initializer+0x268.
The 72-byte heap descriptor and fixed-eight prepared identity layout are
retained. Direct identity uses the actual allocator's three control literals
instead of inventing a dispatch table. First initialization, original
bottom-reservation calls, full free-list walks, intercepted releases,
reinitializer stops, placement and SCI-loan checks remain mandatory.

Original-code execution checks cover the actual Time Stalkers and EGG
startup-derived heap arguments and both module lease sizes. Independent
mutation tests cover all eight identity words, failed reads, malformed or
mixed descriptors, aliases, cursor/list changes, retained/protected frees and
reinitialization. Modeled instruction/read counts do not measure console
latency. Filling the existing identity plan directly avoids temporary arrays;
its runtime eight-read loop and ABI remain the same.

EGG's two apparent branches into its early exit veneers are low halfwords of
RAM-address literals. They belong to one 23-word pool with 32 MOV.L consumers,
immediately after RTS and its register-pop delay slot. A cold fallback proves
a maximal pointer pool of at most 128 bytes, a safe return/delay, a positive
full-word reference to every word, and no encoded branch, raw address, MOVA or
partial-word entry to the pool or delay slot. Pool sources remain active while
proving absence of entries. Pools must exclude every patch window; raw
pointers into hook interiors are always refused. This is a bounded extension
to the existing encoded-control guard, not a whole-program indirect-control
proof or a game-name exception.

The corpus comparison initially caught an unintended additional Silver
admission: recognizing its allocator made the generic suffix-reclamation
helper applicable. Reclamation is now explicitly limited to the previously
reviewed dispatch-table allocator, in both raw and retained descriptors.
Recognition of DIRECT32 does not silently extend that separate lifetime
proof. Silver's original allocator is positively exercised while its shared
launch remains refused; the startup P2 lock rewrite and additional lifetime
requirements need their own review. Its old SCI/Standard adapter remains the
working console reference. Time Stalkers and EGG retain their whole loaded
executables and need no such reclamation.

Detector epoch 7 invalidates prior discovery records. Seven tracked fixtures
were rebuilt and replayed from their actual originals, changing only epoch
and integrity bytes. Grandia II and Lodoss remain explicit stale epoch-6
fixtures because their originals are unavailable; they are refused rather
than relabeled. The complete mandatory eight-title bottom-heap corpus audit
therefore remains unavailable. New title admission and the supplied targeted
proofs do not establish that that complete audit passed, or certify console
save persistence.

The 21-original comparison admits 13 titles at the structural/model gates,
versus 11 at the preceding baseline. Only Time Stalkers and EGG are new;
every prior accepted row is unchanged, and its canonical record differs only
in detector epoch and derived digest. The release-catalog positive packaging
tests still refuse their old six-record evidence/Grandia II input, so no
prebuilt discovery catalog is included. These input-related refusals remain
visible in the delivered validation summary.

## Owner follow-up after 242169041894

EGG works and can save/load. A power-cycle reload was not explicitly reported.
Time Stalkers instead rebooted after bootstrap 2. HOTD2 is marginally smoother
but retains annoying lag; its new event-tag photograph records DELIVER(20),
entry IRQ 0, AUX 0x19E0=6624 ticks (8.47872ms), ONE 0x481=1153 ticks
(1.47584ms), PIO attempts 0x1F07=7943, zero overruns/SD faults, 23 card reads,
24 SD reads and five pre-transaction BUSY deferrals. The deliberate4F report
is not itself a new crash. This identifies the largest sampled reader visit,
not every gameplay pause or its complete cause.

### Time Stalkers pre-initializer clear

The previous allocator proof began too late. Original startup instructions at
0x8c010226..22e call memset 0x8c01c6e8 over 0x8c3b0000..0x8d000000 before
the initializer hook at 0x8c010236. Bounded original-instruction replay shows
that this erases the live high installer and its private stack. The old low
hook then attempts to call erased code. Allocator correctness alone did not
prove the lifetime of its temporary caller.

An exact-source cold contract now proves the clear/caller/helper/roles before
any literal is patched, including on cached metadata launches. The initial
clear preserves only the actual linked stage/code/BSS/private-installer-stack
interval and clears every other requested byte through the original helper.
Its unique startup-clear literal is then retired to the selected retained
low terminal, preventing a repeat of that known startup call from erasing the
live lease before the initializer's existing terminal guard. The one-shot
pending tuple alone retains the original memset target for immediate cleanup.
This uses the same immutable-original-caller assumption as the surrounding
loader contracts; arbitrary later self-modifying code is not monitored.

The successful installer uses an authored 48-byte temporary-stage tail. It
restores its own saved PR/SP before jumping to the exact original stackless
helper with P1stagebegin/zero/exact byte count. That helper may erase the dead
high code and stack, then RTS directly to the existing retained low
`mov r8,r15` continuation. The original post-initializer owner frame is restored
there. The helper touches no SP/PR/callee-saved registers. P1zero stores match
the original clear's cache semantics; no P2clear can leave dirty nonzero P1
tags behind. The no-byte-cost ABS resume offset is independently bound to the
unique actual JSR/NOP/MOV-SP/restore-PR seam. Neither low reader, its stack,
nor the VMU worker grows.

The finish checker requires the exact pending tuple/state, completed installer,
retired clear literal, masked context and exact low return PC. The tuple is
consumed before the tail target executes. Linked replay covers the real clear,
initializer, allocator/worker handoff and return to guest code, plus malformed
state and deliberate wrong-tail counterfactuals. The physical-disc gateway is
itself high code, so that path refuses this startup-clear contract at GUI and
fresh/cached cold preflight until a surviving continuation is separately proved.
It is not silently admitted using the earlier allocator-only disc test.

### Games and rip identity

A demonstrated duplicate path is an empty/non-image old rip folder beside its
new copy. A successful explicit saved-library scan omits proven empty remnants
while retaining raw browsing, nested/ambiguous folders and every real image.
Refresh also renews an owned warm filesystem session, respecting an already
invalidated external-job session. A failed/cancelled/changed-volume scan does
not publish an incomplete replacement. Catalogue v3 adds bounded checked IP
identity fields; v2 still loads without cold image I/O and upgrades on X Scan.
Existing rips therefore need a fresh scan, not a rename or another rip.

Disc ordinals come from IP device bytes 0x2b/0x2d with a checked GD-ROM/CD-ROM
n/t field. New output folders label DiscN explicitly, keeping copy suffixes
separate. Existing descriptors, raw-disc/checkpoint identity and old completed
manifest bytes remain the authority for resume/verify. Optional manifest
ordinal fields are strictly paired. Multi-disc grouping requires matching
product/version/region/media/container identity and distinct actual ordinals;
title text and rip/copy order never merge different releases. Real FatFs
FAT32/exFAT tests cover out-of-order four-disc captures, duplicate copies,
old-name resume and read-only verification, plus 56 catalogue/volume/session
cases. No actual Shenmue header was supplied locally, so console metadata
recognition remains a test rather than an asserted four-disc result.

### HOTD2 follow-up work

The retained map's complete pre-pass still proves every 64-bit endpoint,
partition bound, slot, ownership and coverage relation. The overlap pass now
hoists a proved 32-bit endpoint, skips its initially empty pair iteration, and
retains every unordered pair/shared-boundary check. No validated-map cache or
new runtime state is introduced. Frozen-reference differential and actual SH
execution cover endpoints through UINT32_MAX, refusal of 2^32, aliases,
private copies and fragmented maps. A single extent preserves 16 map reads
and changes 396 to 393 modeled instructions; a 63-extent reverse case changes
73215 to 48753 with fewer map loads. These are work counts, not console cycles
or evidence of HOTD2's actual fragment count.


## Latest main integration

This candidate merges the owner's `main` through
`a038b146e535a18a5e274e1ad05d12568d9f74bc`, including the console-tested
menu screenshots, Favorites and Recently played update, and the VMU
read/import recovery follow-up. The exact signed upstream commits and objects
were imported without rewriting their identities. The new menu code is combined
with catalogue v3, checked IP disc grouping, complete replacement scans and the
empty-rip-remnant fix. Activity filters operate on the complete saved catalogue;
raw discovery is still independent of the selected display filter.

The newer startup-clear bridge, direct allocator recognition, physical-disc
readiness/preflight guards, A1/A2 selection and read-ahead timing/integrity work
are retained from `d3f49fa7a6ae`. Native conflict resolution preserves the newer
reviewed code and pins rather than reverting them to older shared upstream
versions. Final source and artifact evidence is generated after this merge.


## Scan crash and latest main follow-up

The owner confirmed Time Stalkers image boot and improved HOTD2 gameplay on
`bbe142d4a278`, then reported a Games scan crash. The old linked GDI scan path
used 68,192 bytes before further helpers on a 65,536-byte worker stack. Explicit
scan/find/identity staging is now heap-owned; warmup/prefetch use one serialized
worker page; the generic named opener keeps CUE/single-image transactions on
the heap. Allocation failures preserve the last complete catalogue. The target
checker reproduces the historical overflow and binds current production SH
instructions, including cold entry paths and an explicit residual leaf allowance.

This follow-up also merges main through `34e0ddce89b35208e4c166ee461773d052d32375`.
X is now direct favorite and Y cycles tabs; scans and layout live in Library
options. New session request collection uses admitted numeric group membership
and refuses oversized, invalid or cancelled snapshots atomically. The optional
112 KiB/324 KiB SCI session profile is packaged separately from the unchanged
known-working compact modules. Native session protocol and linked checks do not
claim physical disc switching or return-to-K-UI acceptance.
