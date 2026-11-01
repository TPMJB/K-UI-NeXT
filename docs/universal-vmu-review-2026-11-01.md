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
| Silver | Original SCI/Standard title-specific adapter already passed save, power-off, reboot and reload (`3c80d14a0529`, p0003). The adapter is retained. Shared admission still needs the older allocator, executable extent, exit-consumer and lock-alias proofs. |
| Sonic Adventure | Early SCI reader family admitted; standalone New console play recorded. Shared VMU needs syMalloc 1.35 and executable-loader/exit handling. |
| Time Stalkers | Early SCI reader console play recorded; early exit pair recognized. syMalloc 1.35 remains a shared VMU blocker and is a useful next allocator target. |
| Elemental Gimmick Gear | Standalone New console play recorded after replacing a damaged rip. Shared allocator and exit checks remain. |
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
